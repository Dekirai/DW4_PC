using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using DW4Online.Relay;
using Microsoft.Extensions.Logging.Abstractions;

namespace DW4Online.Tests;

public sealed class ManualTime(DateTimeOffset start) : TimeProvider
{
    private DateTimeOffset _now = start;
    public override DateTimeOffset GetUtcNow() => _now;
    public void Advance(TimeSpan by) => _now += by;
}

public class RelayTicketTests
{
    private static readonly byte[] Key = RandomNumberGenerator.GetBytes(32);

    [Fact]
    public void Tickets_redeem_once()
    {
        var tickets = new RelayTickets(Key, TimeSpan.FromSeconds(60), TimeProvider.System);
        var t = tickets.Issue("AB12CD", 42, RelayRoles.Host);
        var claims = tickets.Redeem(t);
        Assert.NotNull(claims);
        Assert.Equal("AB12CD", claims.Room);
        Assert.Equal(42, claims.Account);
        Assert.Equal(RelayRoles.Host, claims.Role);
        Assert.Null(tickets.Redeem(t)); // single use
    }

    [Fact]
    public void Tampered_foreign_and_expired_tickets_fail()
    {
        var time = new ManualTime(DateTimeOffset.UtcNow);
        var tickets = new RelayTickets(Key, TimeSpan.FromSeconds(60), time);
        var t = tickets.Issue("AB12CD", 42, RelayRoles.Guest);
        var body = t[..t.IndexOf('.')];
        var forged = Convert.ToBase64String(Encoding.ASCII.GetBytes("AB12CD|43|host|99999999999|00")).TrimEnd('=') + t[t.IndexOf('.')..];
        Assert.Null(tickets.Redeem(forged));
        Assert.Null(tickets.Redeem(body + ".AAAA"));
        Assert.Null(tickets.Redeem("garbage"));
        Assert.Null(new RelayTickets(RandomNumberGenerator.GetBytes(32), TimeSpan.FromSeconds(60), time).Redeem(t));
        time.Advance(TimeSpan.FromSeconds(61));
        Assert.Null(tickets.Redeem(t));
    }

    [Fact]
    public void Bad_input_is_refused()
    {
        Assert.Throws<ArgumentException>(() => new RelayTickets(new byte[8], TimeSpan.FromSeconds(60), TimeProvider.System));
        var tickets = new RelayTickets(Key, TimeSpan.FromSeconds(60), TimeProvider.System);
        Assert.Throws<ArgumentException>(() => tickets.Issue("ab|cd", 1, RelayRoles.Host));
        Assert.Throws<ArgumentException>(() => tickets.Issue("ABCD", 1, "admin"));
    }
}

/// <summary>The relay end to end on loopback: host control, guest, host data, bytes both ways, room close.</summary>
public class RelayServerTests
{
    private static async Task<(RelayServer Server, RelayTickets Tickets, int Port, CancellationTokenSource Stop)> StartAsync()
    {
        var tickets = new RelayTickets(RandomNumberGenerator.GetBytes(32), TimeSpan.FromSeconds(60), TimeProvider.System);
        var options = new RelayOptions { Enabled = true, ListenAddress = "127.0.0.1", Port = 0, HandshakeTimeoutSeconds = 5, IdleTimeoutSeconds = 30 };
        var server = new RelayServer(options, tickets, TimeProvider.System, NullLogger<RelayServer>.Instance);
        var stop = new CancellationTokenSource(TimeSpan.FromSeconds(60));
        await server.StartAsync(stop.Token);
        var port = await server.Started.WaitAsync(TimeSpan.FromSeconds(10));
        return (server, tickets, port, stop);
    }

    private static async Task<(TcpClient Client, NetworkStream Stream)> ConnectAsync(int port, string line)
    {
        var client = new TcpClient { NoDelay = true };
        await client.ConnectAsync("127.0.0.1", port);
        var stream = client.GetStream();
        await stream.WriteAsync(Encoding.ASCII.GetBytes(line + "\n"));
        return (client, stream);
    }

    private static async Task<string> ReadLineAsync(NetworkStream stream)
    {
        var sb = new StringBuilder();
        var one = new byte[1];
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        while (await stream.ReadAsync(one, timeout.Token) == 1 && one[0] != '\n') sb.Append((char)one[0]);
        return sb.ToString();
    }

    private static async Task<byte[]> ReadExactlyAsync(NetworkStream stream, int count)
    {
        var buffer = new byte[count];
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        await stream.ReadExactlyAsync(buffer, timeout.Token);
        return buffer;
    }

    [Fact]
    public async Task Guest_and_host_are_spliced_both_ways()
    {
        var (server, tickets, port, stop) = await StartAsync();
        try
        {
            var (hostControl, control) = await ConnectAsync(port, "DW4R1 HOST " + tickets.Issue("ROOM01", 1, RelayRoles.Host));
            var ok = await ReadLineAsync(control);
            Assert.StartsWith("OK ", ok, StringComparison.Ordinal);
            var hostKey = ok[3..];

            var (guestClient, guest) = await ConnectAsync(port, "DW4R1 GUEST " + tickets.Issue("ROOM01", 2, RelayRoles.Guest));
            var notice = (await ReadLineAsync(control)).Split(' ');
            Assert.Equal("GUEST", notice[0]);
            Assert.Equal("2", notice[2]);
            var (dataClient, data) = await ConnectAsync(port, $"DW4R1 DATA ROOM01 {notice[1]} {hostKey}");
            Assert.Equal("OK", await ReadLineAsync(guest));

            // A netplay-style frame each way, and a 90 KB burst like the START message's card data.
            var frame = new byte[] { 3, 30, 0, 0, 0 }.Concat(Enumerable.Range(0, 30).Select(i => (byte)i)).ToArray();
            await guest.WriteAsync(frame);
            Assert.Equal(frame, await ReadExactlyAsync(data, frame.Length));
            var start = RandomNumberGenerator.GetBytes(90_176);
            await data.WriteAsync(start);
            Assert.Equal(start, await ReadExactlyAsync(guest, start.Length));

            // The host leaving closes the room and the spliced pair.
            var closed = new TaskCompletionSource<string>();
            server.HostDisconnected += room => closed.TrySetResult(room);
            hostControl.Dispose();
            Assert.Equal("ROOM01", await closed.Task.WaitAsync(TimeSpan.FromSeconds(10)));
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
            var buffer = new byte[16];
            int read;
            try
            {
                read = await guest.ReadAsync(buffer, timeout.Token);
            }
            catch (IOException)
            {
                read = 0;
            }
            Assert.Equal(0, read);
            Assert.Equal(0, server.RoomCount);
            guestClient.Dispose();
            dataClient.Dispose();
        }
        finally
        {
            await server.StopAsync(CancellationToken.None);
            stop.Dispose();
        }
    }

    [Fact]
    public async Task Instance_channels_and_dropping_one_guest()
    {
        var (server, tickets, port, stop) = await StartAsync();
        try
        {
            Assert.True(RelayTickets.IsChannel("ABC234-12"));
            Assert.True(RelayTickets.IsChannel("ABC234-10000")); // past 9999 (the review's blocker)
            Assert.True(RelayTickets.IsChannel("ABC234-999999999"));
            Assert.False(RelayTickets.IsChannel("ABC234-1000000000"));
            Assert.False(RelayTickets.IsChannel("ABC234-"));
            Assert.False(RelayTickets.IsChannel("abc234-1"));
            var (hostControl, control) = await ConnectAsync(port, "DW4R1 HOST " + tickets.Issue("ABC234-1", 1, RelayRoles.Host));
            var hostKey = (await ReadLineAsync(control))[3..];

            async Task<(TcpClient, NetworkStream, TcpClient, NetworkStream)> GuestAsync(long account)
            {
                var (g, gs) = await ConnectAsync(port, "DW4R1 GUEST " + tickets.Issue("ABC234-1", account, RelayRoles.Guest));
                var n = (await ReadLineAsync(control)).Split(' ')[1];
                var (d, ds) = await ConnectAsync(port, $"DW4R1 DATA ABC234-1 {n} {hostKey}");
                Assert.Equal("OK", await ReadLineAsync(gs));
                return (g, gs, d, ds);
            }

            var (g2, s2, d2, ds2) = await GuestAsync(2);
            var (g3, s3, d3, ds3) = await GuestAsync(3);
            server.DropGuest("ABC234-1", 2);

            // Guest 2's pair is gone; guest 3 still talks to the host.
            using (var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10)))
            {
                var buffer = new byte[4];
                int read;
                try
                {
                    read = await ds2.ReadAsync(buffer, timeout.Token);
                }
                catch (IOException)
                {
                    read = 0;
                }
                Assert.Equal(0, read);
            }
            await s3.WriteAsync(new byte[] { 9, 8, 7 });
            Assert.Equal(new byte[] { 9, 8, 7 }, await ReadExactlyAsync(ds3, 3));
            Assert.Equal(1, server.RoomCount);

            server.CloseRoom("ABC234-1");
            Assert.Equal(0, server.RoomCount);
            foreach (var c in new[] { hostControl, g2, d2, g3, d3 }) c.Dispose();
        }
        finally
        {
            await server.StopAsync(CancellationToken.None);
            stop.Dispose();
        }
    }

    [Fact]
    public async Task Bad_handshakes_are_refused()
    {
        var (server, tickets, port, stop) = await StartAsync();
        try
        {
            var guestTicket = tickets.Issue("ROOM02", 5, RelayRoles.Guest);
            var (c1, s1) = await ConnectAsync(port, "DW4R1 HOST " + guestTicket); // a guest ticket cannot host
            Assert.Equal("ERR ticket", await ReadLineAsync(s1));
            var (c2, s2) = await ConnectAsync(port, "DW4R1 GUEST " + tickets.Issue("ROOM02", 5, RelayRoles.Guest));
            Assert.Equal("ERR no_host", await ReadLineAsync(s2));
            var (c3, s3) = await ConnectAsync(port, "HELLO");
            Assert.Equal("ERR protocol", await ReadLineAsync(s3));
            var hostTicket = tickets.Issue("ROOM02", 6, RelayRoles.Host);
            var (c4, s4) = await ConnectAsync(port, "DW4R1 HOST " + hostTicket);
            Assert.StartsWith("OK ", await ReadLineAsync(s4), StringComparison.Ordinal);
            var (c5, s5) = await ConnectAsync(port, "DW4R1 HOST " + hostTicket); // replayed ticket
            Assert.Equal("ERR ticket", await ReadLineAsync(s5));
            var (c6, s6) = await ConnectAsync(port, "DW4R1 DATA ROOM02 1 00000000000000000000000000000000");
            Assert.Equal("ERR data", await ReadLineAsync(s6));
            foreach (var c in new[] { c1, c2, c3, c4, c5, c6 }) c.Dispose();
        }
        finally
        {
            await server.StopAsync(CancellationToken.None);
            stop.Dispose();
        }
    }
}
