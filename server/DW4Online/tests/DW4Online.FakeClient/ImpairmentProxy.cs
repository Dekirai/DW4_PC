using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using DW4Online.Relay.Hub;

namespace DW4Online.FakeClient;

/// <summary>Per direction: one-way delay, uniform jitter on top, and a TCP loss model.</summary>
/// <param name="OneWayMs">Base one-way time.</param>
/// <param name="JitterMs">Each segment's delay is OneWayMs + U(0, JitterMs).</param>
/// <param name="Loss">Probability that a segment is lost (0.01 = 1 %).</param>
public sealed record Impairment(double OneWayMs, double JitterMs, double Loss)
{
    public static readonly Impairment None = new(0, 0, 0);
}

/// <summary>
/// A latency / jitter / loss proxy on loopback (SEAMLESS.md 6.4, 16) that models TCP's behaviour, since Windows loopback
/// has no netem: every read from the sender is one segment; its delivery is now + one-way + jitter, never before an
/// earlier segment of the same direction (TCP delivers in order, so jitter adds latency instead of reordering). A lost
/// segment arrives one retransmission later: fast retransmit after three duplicate ACKs, which at 60 segments per second
/// is the round trip plus three segment gaps (RTT + 50 ms); a lost retransmission adds the minimum RTO (300 ms on Windows,
/// doubling). Every segment behind a lost one waits for it (head-of-line blocking).
/// </summary>
public sealed class ImpairmentProxy : IAsyncDisposable
{
    public const double SegmentGapMs = 1000.0 / 59.94;
    public const double MinRtoMs = 300;

    private readonly IPEndPoint _target;
    private readonly Impairment _up;   // client -> server
    private readonly Impairment _down; // server -> client
    private readonly TcpListener _listener;
    private readonly CancellationTokenSource _stop = new();
    private readonly Random _random;
    private readonly List<Task> _tasks = [];
    private readonly Lock _gate = new();
    private readonly PriorityQueue<(NetworkStream To, byte[] Data, long Seq), (long Due, long Seq)> _queue = new();
    private readonly HighResolutionTimer _timer = new();
    private readonly Thread _sender;
    private long _seq;

    public ImpairmentProxy(IPEndPoint listen, IPEndPoint target, Impairment up, Impairment down, int seed)
    {
        _target = target;
        _up = up;
        _down = down;
        _random = new Random(seed);
        _listener = new TcpListener(listen);
        _listener.Start();
        _sender = new Thread(SendLoop) { IsBackground = true, Name = "impairment proxy", Priority = ThreadPriority.AboveNormal };
        _sender.Start();
        _tasks.Add(Task.Run(AcceptAsync));
    }

    public int Port => ((IPEndPoint)_listener.LocalEndpoint).Port;
    public long LostSegments { get; private set; }
    public long Segments { get; private set; }

    private async Task AcceptAsync()
    {
        try
        {
            while (!_stop.IsCancellationRequested)
            {
                var client = await _listener.AcceptTcpClientAsync(_stop.Token);
                client.NoDelay = true;
                var server = new TcpClient { NoDelay = true };
                await server.ConnectAsync(_target, _stop.Token);
                var a = client.GetStream();
                var b = server.GetStream();
                lock (_gate)
                {
                    _tasks.Add(Task.Run(() => PumpAsync(a, b, _up, client, server)));
                    _tasks.Add(Task.Run(() => PumpAsync(b, a, _down, client, server)));
                }
            }
        }
        catch (Exception e) when (e is OperationCanceledException or SocketException or ObjectDisposedException)
        {
        }
    }

    private async Task PumpAsync(NetworkStream from, NetworkStream to, Impairment imp, TcpClient c1, TcpClient c2)
    {
        var buffer = new byte[65536];
        long last = 0; // the delivery time of the latest segment of this direction
        try
        {
            while (true)
            {
                var n = await from.ReadAsync(buffer, _stop.Token);
                if (n == 0) break;
                var now = Stopwatch.GetTimestamp();
                double delay;
                bool lost;
                lock (_gate)
                {
                    delay = imp.OneWayMs + (imp.JitterMs > 0 ? _random.NextDouble() * imp.JitterMs : 0);
                    lost = imp.Loss > 0 && _random.NextDouble() < imp.Loss;
                    if (lost)
                    {
                        var rtt = _up.OneWayMs + _down.OneWayMs + (_up.JitterMs + _down.JitterMs) / 2;
                        delay += rtt + 3 * SegmentGapMs;
                        var rto = MinRtoMs;
                        while (_random.NextDouble() < imp.Loss)
                        {
                            delay += rto;
                            rto *= 2;
                        }
                        ++LostSegments;
                    }
                    ++Segments;
                }
                var due = Math.Max(now + (long)(delay * Stopwatch.Frequency / 1000.0), last);
                last = due;
                var data = buffer.AsSpan(0, n).ToArray();
                lock (_gate)
                {
                    var seq = ++_seq;
                    _queue.Enqueue((to, data, seq), (due, seq));
                }
                _timer.Wake();
            }
        }
        catch (Exception e) when (e is IOException or SocketException or OperationCanceledException or ObjectDisposedException)
        {
        }
        finally
        {
            // Let queued data drain, then close both sides.
            await Task.Delay(TimeSpan.FromMilliseconds(Math.Max(imp.OneWayMs + imp.JitterMs, 0) + 600));
            c1.Dispose();
            c2.Dispose();
        }
    }

    private void SendLoop()
    {
        while (!_stop.IsCancellationRequested)
        {
            long due = long.MaxValue;
            while (true)
            {
                (NetworkStream To, byte[] Data, long Seq) item;
                lock (_gate)
                {
                    if (!_queue.TryPeek(out item, out var key))
                    {
                        due = long.MaxValue;
                        break;
                    }
                    if (key.Due > Stopwatch.GetTimestamp())
                    {
                        due = key.Due;
                        break;
                    }
                    _queue.Dequeue();
                }
                try
                {
                    item.To.Write(item.Data);
                }
                catch (Exception e) when (e is IOException or SocketException or ObjectDisposedException)
                {
                }
            }
            _timer.WaitUntil(due == long.MaxValue ? Stopwatch.GetTimestamp() + Stopwatch.Frequency / 50 : due);
        }
    }

    public async ValueTask DisposeAsync()
    {
        _stop.Cancel();
        _listener.Stop();
        _timer.Wake();
        try
        {
            await Task.WhenAll(_tasks.ToArray()).WaitAsync(TimeSpan.FromSeconds(3));
        }
        catch (Exception e) when (e is OperationCanceledException or TimeoutException or SocketException or IOException or ObjectDisposedException)
        {
        }
        _sender.Join(TimeSpan.FromSeconds(1));
        _timer.Dispose();
    }
}
