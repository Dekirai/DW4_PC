using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using Microsoft.Extensions.Logging.Abstractions;

namespace DW4Online.Integration.Tests;

/// <summary>The relay's connection handling over loopback, without the database (round S1 review).</summary>
public class RelayConnectionTests
{
    /// <summary>
    /// A connection that ends without BYE (a crash, a lost network) releases its address's slot: the writer used to wait
    /// for good on a queue nobody completed, so after Relay:MaxConnectionsPerIp such ends the address could not connect
    /// again until a restart (households and carrier-grade NAT share one).
    /// </summary>
    [Fact]
    public async Task Connections_that_end_without_bye_release_their_address_slot()
    {
        var key = RandomNumberGenerator.GetBytes(32);
        var tickets = new RelayTickets(key, TimeSpan.FromSeconds(60), TimeProvider.System);
        var relay = new RelayOptions { Enabled = true, ListenAddress = "127.0.0.1", Port = TestPorts.Next(), MaxConnectionsPerIp = 3 };
        var hub = new RelayHub(relay, new SealingOptions(), new InstanceOptions(), tickets, NullLoggerFactory.Instance);
        using var stop = new CancellationTokenSource(TimeSpan.FromSeconds(60));
        await hub.StartAsync(stop.Token);
        try
        {
            var port = await hub.Started.WaitAsync(TimeSpan.FromSeconds(10));
            for (var k = 0; k < 10; ++k)
            {
                // Each one connects, gets OK and its WELCOME, then drops without BYE (a reset, no FIN handshake).
                var line = "";
                for (var attempt = 0; attempt < 30 && !line.StartsWith("OK ", StringComparison.Ordinal); ++attempt)
                {
                    if (attempt > 0) await Task.Delay(100);
                    line = await HandshakeAndDropAsync(port, tickets.Issue(Wire.WorldChannel, 7, RelayRoles.Member));
                }
                Assert.True(line.StartsWith("OK ", StringComparison.Ordinal), $"connection {k + 1} was refused ({line})");
            }
        }
        finally
        {
            await stop.CancelAsync();
            await hub.StopAsync(CancellationToken.None);
            hub.Dispose();
        }
    }

    private static async Task<string> HandshakeAndDropAsync(int port, string ticket)
    {
        using var client = new TcpClient();
        await client.ConnectAsync(IPAddress.Loopback, port);
        var stream = client.GetStream();
        var hello = string.Create(CultureInfo.InvariantCulture, $"{Wire.Magic} {ticket} 49 1 {0x1234UL:x16}\n");
        await stream.WriteAsync(Encoding.ASCII.GetBytes(hello));
        var line = new StringBuilder();
        var one = new byte[1];
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        try
        {
            while (await stream.ReadAsync(one, timeout.Token) == 1 && one[0] != '\n') line.Append((char)one[0]);
            if (line.ToString().StartsWith("OK ", StringComparison.Ordinal))
            {
                // The first frame (WELCOME): the server took the member.
                var header = new byte[Wire.HeaderBytes];
                await stream.ReadExactlyAsync(header, timeout.Token);
            }
        }
        catch (Exception e) when (e is IOException or OperationCanceledException or SocketException)
        {
            return "closed: " + line;
        }
        client.Client.LingerState = new LingerOption(true, 0);
        client.Client.Close();
        return line.ToString();
    }

    /// <summary>
    /// The room heartbeat goes to the database: a stalled one must not hold up the relay's housekeeping (a suspended
    /// database stopped every room's pings, holds and timeouts for as long as it hung). The heartbeat here never returns;
    /// the member's PING keeps coming once a second.
    /// </summary>
    [Fact]
    public async Task A_stalled_heartbeat_does_not_stop_the_pings()
    {
        var key = RandomNumberGenerator.GetBytes(32);
        var tickets = new RelayTickets(key, TimeSpan.FromSeconds(60), TimeProvider.System);
        var relay = new RelayOptions { Enabled = true, ListenAddress = "127.0.0.1", Port = TestPorts.Next() };
        var hub = new RelayHub(relay, new SealingOptions(), new InstanceOptions(), tickets, NullLoggerFactory.Instance);
        var stalled = new TaskCompletionSource();
        var beats = 0;
        hub.Heartbeat = async _ =>
        {
            Interlocked.Increment(ref beats);
            await stalled.Task;
        };
        using var stop = new CancellationTokenSource(TimeSpan.FromSeconds(60));
        await hub.StartAsync(stop.Token);
        try
        {
            var port = await hub.Started.WaitAsync(TimeSpan.FromSeconds(10));
            using var client = new TcpClient();
            await client.ConnectAsync(IPAddress.Loopback, port);
            var stream = client.GetStream();
            var hello = string.Create(CultureInfo.InvariantCulture, $"{Wire.Magic} {tickets.Issue(Wire.WorldChannel, 7, RelayRoles.Member)} 49 1 {0x1234UL:x16}\n");
            await stream.WriteAsync(Encoding.ASCII.GetBytes(hello));
            var one = new byte[1];
            while (await stream.ReadAsync(one) == 1 && one[0] != '\n')
            {
            }
            var pings = new List<DateTime>();
            var header = new byte[Wire.HeaderBytes];
            var until = DateTime.UtcNow.AddSeconds(9);
            using var read = new CancellationTokenSource(TimeSpan.FromSeconds(12));
            while (DateTime.UtcNow < until)
            {
                await stream.ReadExactlyAsync(header, read.Token);
                var payload = new byte[BitConverter.ToUInt32(header, 1)];
                await stream.ReadExactlyAsync(payload, read.Token);
                if (header[0] == Msg.Ping) pings.Add(DateTime.UtcNow);
                if (header[0] == Msg.Ping)
                    await stream.WriteAsync(new PongMsg(PingMsg.Parse(payload).Nonce, 0).Frame(), read.Token); // (the idle limit)
            }
            Assert.True(beats >= 1, "the heartbeat ran");
            Assert.True(pings.Count >= 7, $"{pings.Count} pings in 9 s");
            Assert.True(pings.Last() > until.AddSeconds(-2.5), "pings after the stalled heartbeat");
            Assert.True(hub.HeartbeatsSkipped >= 0);
        }
        finally
        {
            stalled.TrySetResult();
            await stop.CancelAsync();
            await hub.StopAsync(CancellationToken.None);
            hub.Dispose();
        }
    }

    /// <summary>An address past Relay:MaxConnectionsPerIp is told why ("ERR busy") instead of seeing a reset.</summary>
    [Fact]
    public async Task An_address_with_all_its_connections_gets_err_busy()
    {
        var key = RandomNumberGenerator.GetBytes(32);
        var tickets = new RelayTickets(key, TimeSpan.FromSeconds(60), TimeProvider.System);
        var relay = new RelayOptions { Enabled = true, ListenAddress = "127.0.0.1", Port = TestPorts.Next(), MaxConnectionsPerIp = 2 };
        var hub = new RelayHub(relay, new SealingOptions(), new InstanceOptions(), tickets, NullLoggerFactory.Instance);
        using var stop = new CancellationTokenSource(TimeSpan.FromSeconds(60));
        await hub.StartAsync(stop.Token);
        var held = new List<TcpClient>();
        try
        {
            var port = await hub.Started.WaitAsync(TimeSpan.FromSeconds(10));
            for (var k = 0; k < 2; ++k)
            {
                var c = new TcpClient();
                await c.ConnectAsync(IPAddress.Loopback, port);
                held.Add(c);
            }
            await Task.Delay(300); // (both accepted and counted)
            using var third = new TcpClient();
            await third.ConnectAsync(IPAddress.Loopback, port);
            var line = new StringBuilder();
            var one = new byte[1];
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            var stream = third.GetStream();
            while (await stream.ReadAsync(one, timeout.Token) == 1 && one[0] != '\n') line.Append((char)one[0]);
            Assert.Equal("ERR busy", line.ToString());
        }
        finally
        {
            foreach (var c in held) c.Dispose();
            await stop.CancelAsync();
            await hub.StopAsync(CancellationToken.None);
            hub.Dispose();
        }
    }

    /// <summary>
    /// The relay binds in StartAsync: a port in use fails the start itself with an error that names the port (the
    /// service's FAIL line), instead of a background failure that only stopped the host.
    /// </summary>
    [Fact]
    public async Task A_relay_port_in_use_fails_the_start_with_the_port()
    {
        using var holder = new TcpListener(IPAddress.Loopback, TestPorts.Next());
        holder.Start();
        var port = ((IPEndPoint)holder.LocalEndpoint).Port;
        var tickets = new RelayTickets(RandomNumberGenerator.GetBytes(32), TimeSpan.FromSeconds(60), TimeProvider.System);
        using var hub = new RelayHub(new RelayOptions { Enabled = true, ListenAddress = "127.0.0.1", Port = port }, new SealingOptions(),
            new InstanceOptions(), tickets, NullLoggerFactory.Instance);
        var error = await Assert.ThrowsAsync<IOException>(() => hub.StartAsync(CancellationToken.None));
        Assert.Equal($"Failed to bind the relay to 127.0.0.1:{port} (Relay:ListenAddress, Relay:Port): address already in use.", error.Message);
        Assert.Equal(SocketError.AddressAlreadyInUse, Assert.IsType<SocketException>(error.InnerException).SocketErrorCode);
        Assert.Same(error, (await Assert.ThrowsAsync<IOException>(() => hub.Started)));
    }

    /// <summary>
    /// A hub disposed without StopAsync (the host's start failed after the relay began, for example on Kestrel's port)
    /// ends its sealing thread before it disposes that thread's timer. Before, Dispose disposed the timer at once while
    /// the thread still waited on it (it only noticed the stop a moment later); when it woke up first, its
    /// ObjectDisposedException took the whole process down (a crashed integration run on 31.10).
    /// </summary>
    [Fact]
    public async Task A_hub_disposed_without_a_stop_ends_its_sealing_thread_first()
    {
        var tickets = new RelayTickets(RandomNumberGenerator.GetBytes(32), TimeSpan.FromSeconds(60), TimeProvider.System);
        for (var i = 0; i < 10; i++)
        {
            var hub = new RelayHub(new RelayOptions { Enabled = true, ListenAddress = "127.0.0.1", Port = TestPorts.Next() },
                new SealingOptions(), new InstanceOptions(), tickets, NullLoggerFactory.Instance);
            await hub.StartAsync(CancellationToken.None);
            await hub.Started.WaitAsync(TimeSpan.FromSeconds(10));
            for (var w = 0; w < 200 && !hub.SealingThreadAlive; w++) await Task.Delay(5);
            Assert.True(hub.SealingThreadAlive);
            await Task.Delay(30); // the sealing thread waits on its timer (50 ms at most per pass)
            hub.Dispose();
            Assert.False(hub.SealingThreadAlive); // ended before its timer was disposed
        }
    }
}
