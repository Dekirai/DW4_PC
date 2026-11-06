using System.Buffers;
using System.Collections.Concurrent;
using System.Buffers.Binary;
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
/// thread of that scope right after it released the hub's lock, with a non-blocking send (Relay:DirectSends); only a full
/// socket buffer, a state transfer or the end of the connection hands the queue to the asynchronous writer on the thread
/// pool. Without a scope (or with DirectSends off) every frame goes through the thread pool as before.
/// </para>
/// </summary>
internal sealed class Connection : IHubPeer
{
    private readonly TcpClient _client;
    private readonly Socket _socket;
    private readonly NetworkStream _stream;
    private readonly RelayOptions _options;
    private readonly ILogger _log;
    private readonly bool _direct;
    private readonly ConcurrentQueue<(byte[] Frame, bool Bulk)> _pending = new();
    private readonly TokenBucket _stateBudget;
    private readonly CancellationTokenSource _closed = new();
    private readonly TaskCompletionSource _done = new(TaskCreationOptions.RunContinuationsAsynchronously);
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

    public Connection(TcpClient client, RelayOptions options, ILogger log, IPAddress ip)
    {
        _client = client;
        _socket = client.Client;
        _stream = client.GetStream();
        _options = options;
        _log = log;
        _stateBudget = new TokenBucket(options.StateBytesPerSecond, options.StateBurstBytes, TimeProvider.System);
        _direct = options.DirectSends;
        // (Overlapped reads and writes ignore the non-blocking mode; only the direct sends use it.)
        if (_direct) _socket.Blocking = false;
        Ip = ip;
    }

    public IPAddress Ip { get; }
    public CancellationToken Closed => _closed.Token;
    public long BytesIn { get; private set; }
    public long BytesOut { get; private set; }
    /// <summary>Frames written by the thread of an outbox scope (diagnostics: the share of direct sends).</summary>
    public static long DirectFrames => Interlocked.Read(ref s_directFrames);
    /// <summary>Frames written by the asynchronous writer.</summary>
    public static long PooledFrames => Interlocked.Read(ref s_pooledFrames);
    private static long s_directFrames;
    private static long s_pooledFrames;

    public void Send(byte[] frame, bool bulk = false)
    {
        if (Volatile.Read(ref _closing) != 0) return;
        // Game frames and state transfers have their own limits: a member that does not read is closed before the relay
        // holds much for it (16 connections of one address could park 512 MiB here).
        var over = bulk
            ? Interlocked.Add(ref _queuedBulk, frame.Length) > _options.MaxQueuedStateBytes
            : Interlocked.Add(ref _queued, frame.Length) > _options.MaxQueuedBytes;
        if (over)
        {
            _log.LogWarning("Relay connection from {Ip} does not read its frames; closing it", Ip);
            Close(CloseReason.Timeout, "Too many unsent frames.");
            return;
        }
        _pending.Enqueue((frame, bulk));
        if (_direct && !bulk && Outbox.Mark(this)) return;
        Kick(inline: false);
    }

    public void Close(CloseReason reason, string text)
    {
        if (Interlocked.Exchange(ref _closing, 1) != 0) return;
        _pending.Enqueue((new CloseMsg(reason, text).Frame(), false));
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

    /// <summary>
    /// Takes the write token if it is free and there is something to do, and drains the queue on this thread
    /// (<paramref name="inline"/>: outside the hub's lock only) or on the thread pool.
    /// </summary>
    internal void Kick(bool inline)
    {
        if (Volatile.Read(ref _started) == 0) return;
        if (_pending.IsEmpty && Volatile.Read(ref _completed) == 0) return;
        if (Interlocked.CompareExchange(ref _writing, 1, 0) != 0) return;
        if (inline && _direct) DrainInline();
        else Pool();
    }

    private void Pool() => ThreadPool.UnsafeQueueUserWorkItem(static c => _ = c.DrainAsync(), this, preferLocal: false);

    /// <summary>Releases the write token; true when this thread took it again (frames came meanwhile, or the end).</summary>
    private bool Release()
    {
        Volatile.Write(ref _writing, 0);
        if (_pending.IsEmpty && Volatile.Read(ref _completed) == 0) return false;
        return Interlocked.CompareExchange(ref _writing, 1, 0) == 0;
    }

    /// <summary>Holding the token: non-blocking sends of the queued game frames; the rest goes to the asynchronous writer.</summary>
    private void DrainInline()
    {
        try
        {
            do
            {
                while (_pending.TryPeek(out var item))
                {
                    if (item.Bulk || _offset != 0)
                    {
                        Pool(); // (a state transfer waits for its budget; a partial frame's rest: the asynchronous writer)
                        return;
                    }
                    var sent = _socket.Send(item.Frame, 0, item.Frame.Length, SocketFlags.None, out var error);
                    if (error == SocketError.Success && sent == item.Frame.Length)
                    {
                        _pending.TryDequeue(out _);
                        Written(item);
                        Interlocked.Increment(ref s_directFrames);
                        continue;
                    }
                    if (error is SocketError.Success or SocketError.WouldBlock)
                    {
                        // The socket's buffer is full (a slow reader): the asynchronous writer waits for it from here.
                        _offset = error == SocketError.Success ? sent : 0;
                        Pool();
                        return;
                    }
                    Abort();
                    return;
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
    }

    /// <summary>Holding the token: writes the queue asynchronously (state transfers through the state budget); at the end of
    /// the connection (completed and empty) closes it.</summary>
    private async Task DrainAsync()
    {
        try
        {
            do
            {
                while (_pending.TryPeek(out var item))
                {
                    if (item.Bulk) await _stateBudget.TakeAsync(item.Frame.Length, _closed.Token);
                    await _stream.WriteAsync(item.Frame.AsMemory(_offset), _closed.Token);
                    _offset = 0;
                    _pending.TryDequeue(out _);
                    Written(item);
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

    private void Written((byte[] Frame, bool Bulk) item)
    {
        BytesOut += item.Frame.Length;
        if (item.Bulk) Interlocked.Add(ref _queuedBulk, -item.Frame.Length);
        else Interlocked.Add(ref _queued, -item.Frame.Length);
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
