using System.Buffers;
using System.Buffers.Binary;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using DW4Online.Relay.Protocol;
using Microsoft.Extensions.Logging;

namespace DW4Online.Relay.Hub;

/// <summary>
/// One member's DW4R2 connection after the handshake: an ordered send queue (bulk frames pass the state budget,
/// Relay:StateBytesPerSecond), and the reader loop with the abuse limits (frame size, inbound bytes and frames per second,
/// malformed frames).
/// <para>
/// The queue is written by one thread at a time, the holder of the write token. M4 (e), round I1a: a frame sent inside an
/// <see cref="Outbox"/> scope (the hub's hot paths: a received frame, a sealing pass, the housekeeping) is written by the
/// thread of that scope right after it released the hub's lock, with non-blocking sends of everything queued at once
/// (Relay:DirectSends); a presence frame waits up to Relay:PresenceFlushMs to go out with others (<see cref="SendSoon"/>);
/// only a full socket buffer, a state transfer or the end of the connection hands the queue to the asynchronous writer on
/// the thread pool. Without a scope (or with DirectSends off) every frame goes through the thread pool as before.
/// </para>
/// </summary>
internal sealed class Connection : IHubPeer
{
    /// <summary>Frames and bytes one non-blocking send takes at most.</summary>
    private const int GatherFrames = 64;
    private const int GatherBytes = 64 * 1024;

    [ThreadStatic] private static List<ArraySegment<byte>>? t_gather;

    private readonly TcpClient _client;
    private readonly Socket _socket;
    private readonly NetworkStream _stream;
    private readonly RelayOptions _options;
    private readonly ILogger _log;
    private readonly bool _direct;
    private readonly int _flushMs;
    private readonly Queue<(byte[] Frame, bool Bulk)> _pending = new();
    private readonly Lock _pendingLock = new();
    private readonly TokenBucket _stateBudget;
    private readonly CancellationTokenSource _closed = new();
    private readonly TaskCompletionSource _done = new(TaskCreationOptions.RunContinuationsAsynchronously);
    // The queue's length, readable without its lock (written under it).
    private int _count;
    private long _queued;
    private long _queuedBulk;
    private int _closing;
    // No more frames will come (CLOSE queued, or the peer is gone): the writer ends the connection once the queue is empty.
    private int _completed;
    // The writer may run (set when the hub's handshake is done; frames before it only queue).
    private int _started;
    // The write token: 1 while a thread or the asynchronous writer drains the queue.
    private int _writing;
    // The part of the queue's first frame a partial non-blocking send wrote already.
    private int _offset;
    // Queued with the presence flusher (a frame of SendSoon waits).
    private int _soon;

    public Connection(TcpClient client, RelayOptions options, ILogger log, IPAddress ip)
    {
        _client = client;
        _socket = client.Client;
        _stream = client.GetStream();
        _options = options;
        _log = log;
        _stateBudget = new TokenBucket(options.StateBytesPerSecond, options.StateBurstBytes, TimeProvider.System);
        _direct = options.DirectSends;
        _flushMs = _direct ? Math.Max(0, options.PresenceFlushMs) : 0;
        // (Overlapped reads and writes ignore the non-blocking mode; only the direct sends use it.)
        if (_direct) _socket.Blocking = false;
        Ip = ip;
    }

    public IPAddress Ip { get; }
    public CancellationToken Closed => _closed.Token;
    public long BytesIn { get; private set; }
    public long BytesOut { get; private set; }
    /// <summary>Frames written by a thread right after a hot path's lock or by the presence flusher (diagnostics).</summary>
    public static long DirectFrames => Interlocked.Read(ref s_directFrames);
    /// <summary>Frames written by the asynchronous writer.</summary>
    public static long PooledFrames => Interlocked.Read(ref s_pooledFrames);
    /// <summary>Non-blocking sends (one per gathered batch of frames).</summary>
    public static long DirectSends => Interlocked.Read(ref s_directSends);
    private static long s_directFrames;
    private static long s_pooledFrames;
    private static long s_directSends;

    public void Send(byte[] frame, bool bulk = false)
    {
        if (!Queue(frame, bulk)) return;
        if (_direct && !bulk && Outbox.Mark(this)) return;
        Kick(inline: false);
    }

    /// <summary>A presence frame: it may wait up to Relay:PresenceFlushMs to go out with the connection's next frames.</summary>
    public void SendSoon(byte[] frame)
    {
        if (_flushMs == 0)
        {
            Send(frame);
            return;
        }
        if (!Queue(frame, false)) return;
        if (Interlocked.Exchange(ref _soon, 1) == 0) PresenceFlusher.Add(this, _flushMs);
    }

    private bool Queue(byte[] frame, bool bulk)
    {
        if (Volatile.Read(ref _closing) != 0) return false;
        // Game frames and state transfers have their own limits: a member that does not read is closed before the relay
        // holds much for it (16 connections of one address could park 512 MiB here).
        var over = bulk
            ? Interlocked.Add(ref _queuedBulk, frame.Length) > _options.MaxQueuedStateBytes
            : Interlocked.Add(ref _queued, frame.Length) > _options.MaxQueuedBytes;
        if (over)
        {
            _log.LogWarning("Relay connection from {Ip} does not read its frames; closing it", Ip);
            Close(CloseReason.Timeout, "Too many unsent frames.");
            return false;
        }
        Enqueue(frame, bulk);
        return true;
    }

    private void Enqueue(byte[] frame, bool bulk)
    {
        lock (_pendingLock)
        {
            _pending.Enqueue((frame, bulk));
            _count = _pending.Count;
        }
    }

    private bool Empty => Volatile.Read(ref _count) == 0;

    private bool TryPeek(out (byte[] Frame, bool Bulk) item)
    {
        lock (_pendingLock) return _pending.TryPeek(out item);
    }

    private void Dequeue(int frames = 1)
    {
        lock (_pendingLock)
        {
            for (var k = 0; k < frames; ++k) _pending.Dequeue();
            _count = _pending.Count;
        }
    }

    public void Close(CloseReason reason, string text)
    {
        if (Interlocked.Exchange(ref _closing, 1) != 0) return;
        Enqueue(new CloseMsg(reason, text).Frame(), false);
        Volatile.Write(ref _completed, 1);
        Kick(inline: false);
        // The writer flushes CLOSE and closes the socket; a stuck writer is cut after two seconds.
        _ = Task.Delay(2000).ContinueWith(_ => Abort(), TaskScheduler.Default);
    }

    /// <summary>No more frames (the peer is gone): the writer sends what is queued and ends; a stuck one is cut after two seconds.</summary>
    public void Finish()
    {
        Interlocked.Exchange(ref _closing, 1);
        Volatile.Write(ref _completed, 1);
        Kick(inline: false);
        _ = Task.Delay(2000).ContinueWith(_ => Abort(), TaskScheduler.Default);
    }

    public void Abort()
    {
        Interlocked.Exchange(ref _closing, 1);
        Volatile.Write(ref _completed, 1);
        try
        {
            _closed.Cancel();
        }
        catch (ObjectDisposedException)
        {
        }
        _client.Dispose();
        _done.TrySetResult();
    }

    /// <summary>Starts writing (the handshake is done); completes when the connection's writing ended.</summary>
    public Task WriteLoopAsync()
    {
        Volatile.Write(ref _started, 1);
        Kick(inline: false);
        return _done.Task;
    }

    /// <summary>The presence flusher's turn: writes what waits (the frames of <see cref="SendSoon"/> and any behind them).</summary>
    internal void FlushSoon()
    {
        Interlocked.Exchange(ref _soon, 0);
        Kick(inline: true);
    }

    /// <summary>
    /// Takes the write token if it is free and there is something to do, and drains the queue on this thread
    /// (<paramref name="inline"/>: outside the hub's lock only) or on the thread pool.
    /// </summary>
    internal void Kick(bool inline)
    {
        if (Volatile.Read(ref _started) == 0) return;
        if (Empty && Volatile.Read(ref _completed) == 0) return;
        if (Interlocked.CompareExchange(ref _writing, 1, 0) != 0) return;
        if (inline && _direct) DrainInline();
        else Pool();
    }

    private void Pool() => ThreadPool.UnsafeQueueUserWorkItem(static c => _ = c.DrainAsync(), this, preferLocal: false);

    /// <summary>Releases the write token; true when this thread took it again (frames came meanwhile, or the end).</summary>
    private bool Release()
    {
        // (A full fence: a producer that queued after the drain's last look sees the token free, or this sees its frame.)
        Interlocked.Exchange(ref _writing, 0);
        if (Empty && Volatile.Read(ref _completed) == 0) return false;
        return Interlocked.CompareExchange(ref _writing, 1, 0) == 0;
    }

    /// <summary>
    /// Holding the token: non-blocking sends of the queued game frames, as many as are queued in one send; a state transfer, a
    /// full socket buffer and the end go to the asynchronous writer.
    /// </summary>
    private void DrainInline()
    {
        var gather = t_gather ??= new List<ArraySegment<byte>>(GatherFrames);
        try
        {
            do
            {
                while (true)
                {
                    gather.Clear();
                    var bytes = 0;
                    var bulkFirst = false;
                    lock (_pendingLock)
                    {
                        foreach (var item in _pending)
                        {
                            if (item.Bulk)
                            {
                                bulkFirst = gather.Count == 0;
                                break;
                            }
                            if (gather.Count == GatherFrames || (gather.Count > 0 && bytes + item.Frame.Length > GatherBytes)) break;
                            gather.Add(item.Frame);
                            bytes += item.Frame.Length;
                        }
                    }
                    if (bulkFirst || _offset != 0)
                    {
                        Pool(); // (a state transfer waits for its budget; a partial frame's rest: the asynchronous writer)
                        return;
                    }
                    if (gather.Count == 0) break;
                    SocketError error;
                    var sent = gather.Count == 1
                        ? _socket.Send(gather[0].Array!, 0, gather[0].Count, SocketFlags.None, out error)
                        : _socket.Send(gather, SocketFlags.None, out error);
                    if (error is not (SocketError.Success or SocketError.WouldBlock))
                    {
                        Abort();
                        return;
                    }
                    Interlocked.Increment(ref s_directSends);
                    var left = error == SocketError.Success ? sent : 0;
                    var done = 0;
                    while (done < gather.Count && left >= gather[done].Count)
                    {
                        left -= gather[done].Count;
                        BytesOut += gather[done].Count;
                        Interlocked.Add(ref _queued, -gather[done].Count);
                        ++done;
                    }
                    if (done > 0) Dequeue(done);
                    Interlocked.Add(ref s_directFrames, done);
                    if (done < gather.Count)
                    {
                        // The socket's buffer is full (a slow reader): the asynchronous writer waits for it from here.
                        _offset = left;
                        BytesOut += left;
                        Pool();
                        return;
                    }
                }
                if (Volatile.Read(ref _completed) != 0)
                {
                    Pool(); // (the asynchronous writer ends the connection)
                    return;
                }
            } while (Release());
        }
        catch (ObjectDisposedException)
        {
            Abort();
        }
        finally
        {
            gather.Clear();
        }
    }

    /// <summary>Holding the token: writes the queue asynchronously (state transfers through the state budget); at the end of
    /// the connection (completed and empty) closes it.</summary>
    private async Task DrainAsync()
    {
        try
        {
            do
            {
                while (TryPeek(out var item))
                {
                    if (item.Bulk) await _stateBudget.TakeAsync(item.Frame.Length, _closed.Token);
                    var from = _offset;
                    await _stream.WriteAsync(item.Frame.AsMemory(from), _closed.Token);
                    _offset = 0;
                    Dequeue();
                    BytesOut += item.Frame.Length - from;
                    if (item.Bulk) Interlocked.Add(ref _queuedBulk, -item.Frame.Length);
                    else Interlocked.Add(ref _queued, -item.Frame.Length);
                    Interlocked.Increment(ref s_pooledFrames);
                }
                if (Volatile.Read(ref _completed) != 0)
                {
                    await _stream.FlushAsync(_closed.Token);
                    Abort();
                    return;
                }
            } while (Release());
        }
        catch (Exception e) when (e is IOException or SocketException or OperationCanceledException or ObjectDisposedException)
        {
            Abort();
        }
    }

    /// <summary>
    /// Reads frames and hands each to <paramref name="onFrame"/> until the peer closes, sends BYE (returns true) or breaks
    /// a limit (a <see cref="ProtocolException"/> closes with reason Protocol or Abuse). The socket is read into a buffer
    /// (M4 (e): one read for whatever arrived, not one for each header and one for each payload); a frame's payload is valid
    /// during <paramref name="onFrame"/> only.
    /// </summary>
    public async Task<bool> ReadLoopAsync(Action<byte, ReadOnlyMemory<byte>> onFrame)
    {
        var bytes = new TokenBucket(_options.BytesPerSecond, _options.BurstBytes, TimeProvider.System);
        var frames = new FrameBudget(_options.FramesPerSecond, _options.FrameBurst);
        var buffer = ArrayPool<byte>.Shared.Rent(ReadBufferBytes);
        int start = 0, end = 0;
        // Reads until the buffer holds <paramref name="need"/> bytes from start (false: the peer closed).
        async ValueTask<bool> Fill(int need)
        {
            if (need > buffer.Length - start)
            {
                // Room at the front, or a bigger buffer for a big frame (a snapshot chunk, a long LOG).
                var to = need > buffer.Length ? ArrayPool<byte>.Shared.Rent(need) : buffer;
                Buffer.BlockCopy(buffer, start, to, 0, end - start);
                if (to != buffer) ArrayPool<byte>.Shared.Return(buffer);
                buffer = to;
                end -= start;
                start = 0;
            }
            while (end - start < need)
            {
                var n = await _stream.ReadAsync(buffer.AsMemory(end), _closed.Token);
                if (n == 0) return false;
                end += n;
            }
            return true;
        }
        try
        {
            while (true)
            {
                if (end - start < Wire.HeaderBytes && !await Fill(Wire.HeaderBytes)) return false;
                var type = buffer[start];
                var len = BinaryPrimitives.ReadUInt32LittleEndian(buffer.AsSpan(start + 1));
                if (len > Wire.MaxFrameBytes) throw new ProtocolException("frame above 1 MiB");
                if (!frames.Take()) throw new AbuseException("too many frames per second");
                var total = (int)len + Wire.HeaderBytes;
                await bytes.TakeAsync(total, _closed.Token);
                if (end - start < total && !await Fill(total)) return false;
                BytesIn += total;
                var payload = buffer.AsMemory(start + Wire.HeaderBytes, (int)len);
                start += total;
                if (start == end) start = end = 0;
                if (type == Msg.Bye)
                {
                    ByeMsg.Parse(payload.Span);
                    return true;
                }
                onFrame(type, payload);
            }
        }
        catch (AbuseException e)
        {
            _log.LogWarning("Relay connection from {Ip} closed: {Reason}", Ip, e.Message);
            Close(CloseReason.Abuse, e.Message);
            return false;
        }
        catch (ProtocolException e)
        {
            _log.LogInformation("Relay connection from {Ip} closed: malformed frame ({Reason})", Ip, e.Message);
            Close(CloseReason.Protocol, e.Message);
            return false;
        }
        catch (Exception e) when (e is IOException or SocketException or OperationCanceledException or ObjectDisposedException)
        {
            return false;
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(buffer);
        }
    }

    /// <summary>The read buffer of a connection (a frame above it gets a bigger one while it is read).</summary>
    private const int ReadBufferBytes = 16 * 1024;

    private sealed class AbuseException(string message) : ProtocolException(message);

    /// <summary>Frames per second with a burst; refuses instead of waiting (a flood closes the connection).</summary>
    private sealed class FrameBudget(int perSecond, int burst)
    {
        private double _tokens = burst;
        private long _stamp = Stopwatch.GetTimestamp();

        public bool Take()
        {
            var now = Stopwatch.GetTimestamp();
            _tokens = Math.Min(burst, _tokens + (now - _stamp) * (double)perSecond / Stopwatch.Frequency);
            _stamp = now;
            if (_tokens < 1) return false;
            _tokens -= 1;
            return true;
        }
    }
}

/// <summary>
/// M4 (e): a thread's scope around a hot path under the hub's lock. Frames sent inside it only queue and mark their
/// connection; <see cref="Exit"/>, after the lock is released, writes them on this thread (<see cref="Connection.Kick"/>).
/// </summary>
internal static class Outbox
{
    [ThreadStatic] private static List<Connection>? t_marked;
    [ThreadStatic] private static int t_depth;

    public static void Enter()
    {
        if (t_depth++ == 0) t_marked ??= [];
    }

    /// <summary>Inside a scope: notes the connection for the scope's end (true); outside: false.</summary>
    public static bool Mark(Connection c)
    {
        if (t_depth == 0) return false;
        var marked = t_marked!;
        if (marked.Count == 0 || marked[^1] != c) marked.Add(c);
        return true;
    }

    /// <summary>Ends the scope; the outermost end writes the marked connections' queues (call it after the lock's release).</summary>
    public static void Exit()
    {
        if (--t_depth > 0) return;
        var marked = t_marked!;
        for (var i = 0; i < marked.Count; ++i) marked[i].Kick(inline: true);
        marked.Clear();
    }
}

/// <summary>
/// M4 (e): the presence flusher, one thread for the process. A connection with a presence frame waiting
/// (<see cref="Connection.SendSoon"/>) is written at the flusher's next turn, every Relay:PresenceFlushMs, together with
/// whatever else waits for it (one non-blocking send for the lot); a game frame before that takes the waiting ones along.
/// </summary>
internal static class PresenceFlusher
{
    private static readonly ConcurrentQueue<Connection> Due = new();
    private static int s_started;

    public static void Add(Connection c, int intervalMs)
    {
        Due.Enqueue(c);
        if (Volatile.Read(ref s_started) == 0 && Interlocked.Exchange(ref s_started, 1) == 0)
            new Thread(() => Run(intervalMs)) { IsBackground = true, Name = "DW4R2 presence flush" }.Start();
    }

    private static void Run(int intervalMs)
    {
        using var timer = new HighResolutionTimer();
        var period = (long)(intervalMs * Stopwatch.Frequency / 1000.0);
        var next = Stopwatch.GetTimestamp() + period;
        while (true)
        {
            timer.WaitUntil(next);
            var now = Stopwatch.GetTimestamp();
            next = Math.Max(next + period, now + period / 2);
            for (var n = Due.Count; n > 0 && Due.TryDequeue(out var c); --n)
            {
                try
                {
                    c.FlushSoon();
                }
                catch (Exception)
                {
                    // (A connection's fault is its own: the writer aborts it; the flusher goes on.)
                }
            }
        }
    }
}

/// <summary>A byte budget: a burst allowance refilled at a sustained rate (waits when empty).</summary>
internal sealed class TokenBucket(int perSecond, int burst, TimeProvider time)
{
    private double _tokens = burst;
    private long _stamp = time.GetTimestamp();

    public async Task TakeAsync(int bytes, CancellationToken ct)
    {
        while (true)
        {
            var now = time.GetTimestamp();
            _tokens = Math.Min(burst, _tokens + time.GetElapsedTime(_stamp, now).TotalSeconds * perSecond);
            _stamp = now;
            if (_tokens >= bytes || _tokens >= burst)
            {
                _tokens -= bytes;
                return;
            }
            var wait = TimeSpan.FromSeconds(Math.Min(1.0, (bytes - _tokens) / perSecond));
            await Task.Delay(wait, time, ct);
        }
    }
}
