using System.Buffers;
using System.Buffers.Binary;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Threading.Channels;
using DW4Online.Relay.Protocol;
using Microsoft.Extensions.Logging;

namespace DW4Online.Relay.Hub;

/// <summary>
/// One member's DW4R2 connection after the handshake: an ordered send queue drained by a writer task (bulk frames pass
/// the state budget, Relay:StateBytesPerSecond), and the reader loop with the abuse limits (frame size, inbound bytes and
/// frames per second, malformed frames).
/// </summary>
internal sealed class Connection : IHubPeer
{
    private readonly TcpClient _client;
    private readonly NetworkStream _stream;
    private readonly RelayOptions _options;
    private readonly ILogger _log;
    private readonly Channel<(byte[] Frame, bool Bulk)> _queue = Channel.CreateUnbounded<(byte[], bool)>(new UnboundedChannelOptions { SingleReader = true });
    private readonly CancellationTokenSource _closed = new();
    private long _queued;
    private int _closing;

    public Connection(TcpClient client, RelayOptions options, ILogger log, IPAddress ip)
    {
        _client = client;
        _stream = client.GetStream();
        _options = options;
        _log = log;
        Ip = ip;
    }

    public IPAddress Ip { get; }
    public CancellationToken Closed => _closed.Token;
    public long BytesIn { get; private set; }
    public long BytesOut { get; private set; }

    public void Send(byte[] frame, bool bulk = false)
    {
        if (Volatile.Read(ref _closing) != 0) return;
        if (Interlocked.Add(ref _queued, frame.Length) > _options.MaxQueuedBytes)
        {
            _log.LogWarning("Relay connection from {Ip} does not read its frames; closing it", Ip);
            Close(CloseReason.Timeout, "Too many unsent frames.");
            return;
        }
        _queue.Writer.TryWrite((frame, bulk));
    }

    public void Close(CloseReason reason, string text)
    {
        if (Interlocked.Exchange(ref _closing, 1) != 0) return;
        _queue.Writer.TryWrite((new CloseMsg(reason, text).Frame(), false));
        _queue.Writer.TryComplete();
        // The writer flushes CLOSE and closes the socket; a stuck writer is cut after two seconds.
        _ = Task.Delay(2000).ContinueWith(_ => Abort(), TaskScheduler.Default);
    }

    /// <summary>No more frames (the peer is gone): the writer sends what is queued and ends; a stuck one is cut after two seconds.</summary>
    public void Finish()
    {
        Interlocked.Exchange(ref _closing, 1);
        _queue.Writer.TryComplete();
        _ = Task.Delay(2000).ContinueWith(_ => Abort(), TaskScheduler.Default);
    }

    public void Abort()
    {
        Interlocked.Exchange(ref _closing, 1);
        _queue.Writer.TryComplete();
        try
        {
            _closed.Cancel();
        }
        catch (ObjectDisposedException)
        {
        }
        _client.Dispose();
    }

    /// <summary>Writes queued frames until the queue completes or the socket fails.</summary>
    public async Task WriteLoopAsync()
    {
        var state = new TokenBucket(_options.StateBytesPerSecond, _options.StateBurstBytes, TimeProvider.System);
        try
        {
            while (await _queue.Reader.WaitToReadAsync(_closed.Token))
            {
                while (_queue.Reader.TryRead(out var item))
                {
                    if (item.Bulk) await state.TakeAsync(item.Frame.Length, _closed.Token);
                    await _stream.WriteAsync(item.Frame, _closed.Token);
                    BytesOut += item.Frame.Length;
                    Interlocked.Add(ref _queued, -item.Frame.Length);
                }
            }
            await _stream.FlushAsync(_closed.Token);
        }
        catch (Exception e) when (e is IOException or SocketException or OperationCanceledException or ObjectDisposedException)
        {
        }
        finally
        {
            Abort();
        }
    }

    /// <summary>
    /// Reads frames and hands each to <paramref name="onFrame"/> until the peer closes, sends BYE (returns true) or breaks
    /// a limit (a <see cref="ProtocolException"/> closes with reason Protocol or Abuse).
    /// </summary>
    public async Task<bool> ReadLoopAsync(Action<byte, ReadOnlyMemory<byte>> onFrame)
    {
        var header = new byte[Wire.HeaderBytes];
        var bytes = new TokenBucket(_options.BytesPerSecond, _options.BurstBytes, TimeProvider.System);
        var frames = new FrameBudget(_options.FramesPerSecond, _options.FrameBurst);
        try
        {
            while (true)
            {
                if (!await ReadExactlyAsync(header)) return false;
                var type = header[0];
                var len = BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(1));
                if (len > Wire.MaxFrameBytes) throw new ProtocolException("frame above 1 MiB");
                if (!frames.Take()) throw new AbuseException("too many frames per second");
                await bytes.TakeAsync((int)len + Wire.HeaderBytes, _closed.Token);
                var payload = ArrayPool<byte>.Shared.Rent((int)Math.Max(len, 1));
                try
                {
                    if (len > 0 && !await ReadExactlyAsync(payload.AsMemory(0, (int)len))) return false;
                    BytesIn += len + Wire.HeaderBytes;
                    if (type == Msg.Bye)
                    {
                        ByeMsg.Parse(payload.AsSpan(0, (int)len));
                        return true;
                    }
                    onFrame(type, payload.AsMemory(0, (int)len));
                }
                finally
                {
                    ArrayPool<byte>.Shared.Return(payload);
                }
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
    }

    private async Task<bool> ReadExactlyAsync(Memory<byte> buffer)
    {
        var got = 0;
        while (got < buffer.Length)
        {
            var n = await _stream.ReadAsync(buffer[got..], _closed.Token);
            if (n == 0) return false;
            got += n;
        }
        return true;
    }

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
