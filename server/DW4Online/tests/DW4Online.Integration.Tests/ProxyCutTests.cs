using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using DW4Online.FakeClient;

namespace DW4Online.Integration.Tests;

/// <summary>
/// The impairment proxy's scheduled cuts (the world's host grace scenarios W7-W7g, SEAMLESS.md 19.24): a closing cut drops
/// the open connection and refuses new ones until it ends; a half-open cut closes nothing and holds every byte back until
/// it ends, then delivers it in order. No database: a loopback echo server behind the proxy.
/// </summary>
public class ProxyCutTests
{
    private static (TcpListener Listener, Task Echo) EchoServer()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var echo = Task.Run(async () =>
        {
            while (true)
            {
                TcpClient c;
                try
                {
                    c = await listener.AcceptTcpClientAsync();
                }
                catch (Exception e) when (e is SocketException or ObjectDisposedException)
                {
                    return;
                }
                _ = Task.Run(async () =>
                {
                    var s = c.GetStream();
                    var buffer = new byte[1024];
                    try
                    {
                        int n;
                        while ((n = await s.ReadAsync(buffer)) > 0) await s.WriteAsync(buffer.AsMemory(0, n));
                    }
                    catch (Exception e) when (e is IOException or SocketException or ObjectDisposedException)
                    {
                    }
                    c.Dispose();
                });
            }
        });
        return (listener, echo);
    }

    private static double Now => DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() / 1000.0;

    private static async Task<byte> RoundTripAsync(NetworkStream s, byte value, int timeoutMs = 5000)
    {
        await s.WriteAsync(new[] { value });
        var b = new byte[1];
        using var cts = new CancellationTokenSource(timeoutMs);
        return await s.ReadAsync(b, cts.Token) == 1 ? b[0] : throw new IOException("closed");
    }

    [Fact]
    public async Task A_half_open_cut_holds_the_bytes_and_closes_nothing()
    {
        var (server, _) = EchoServer();
        var target = (IPEndPoint)server.LocalEndpoint;
        await using var proxy = new ImpairmentProxy(new IPEndPoint(IPAddress.Loopback, 0), target, Impairment.None, Impairment.None, 1)
        {
            Cuts = [new LinkCut(Now + 0.5, 1.5, Half: true)],
        };
        using var client = new TcpClient();
        await client.ConnectAsync(IPAddress.Loopback, proxy.Port);
        var s = client.GetStream();
        Assert.Equal(1, await RoundTripAsync(s, 1));
        await Task.Delay(700);
        var held = Stopwatch.StartNew();
        Assert.Equal(2, await RoundTripAsync(s, 2)); // sent inside the cut: arrives after it, on the same connection
        Assert.True(held.Elapsed.TotalSeconds > 0.8, $"came back after {held.Elapsed.TotalSeconds:0.00} s");
        Assert.Equal(3, await RoundTripAsync(s, 3));
        server.Stop();
    }

    [Fact]
    public async Task A_closing_cut_drops_the_connection_and_refuses_reconnects_until_it_ends()
    {
        var (server, _) = EchoServer();
        var target = (IPEndPoint)server.LocalEndpoint;
        await using var proxy = new ImpairmentProxy(new IPEndPoint(IPAddress.Loopback, 0), target, Impairment.None, Impairment.None, 1)
        {
            Cuts = [LinkCut.Parse(string.Create(System.Globalization.CultureInfo.InvariantCulture, $"{Now + 0.5:0.000},1.5,close"))],
        };
        using (var client = new TcpClient())
        {
            await client.ConnectAsync(IPAddress.Loopback, proxy.Port);
            var s = client.GetStream();
            Assert.Equal(1, await RoundTripAsync(s, 1));
            await Task.Delay(900);
            await Assert.ThrowsAnyAsync<Exception>(() => RoundTripAsync(s, 2, 1000));
        }
        using (var during = new TcpClient())
        {
            await during.ConnectAsync(IPAddress.Loopback, proxy.Port); // accepted, then closed at once
            await Assert.ThrowsAnyAsync<Exception>(() => RoundTripAsync(during.GetStream(), 3, 1000));
        }
        await Task.Delay(1200);
        using var after = new TcpClient();
        await after.ConnectAsync(IPAddress.Loopback, proxy.Port);
        Assert.Equal(4, await RoundTripAsync(after.GetStream(), 4));
        server.Stop();
    }

    /// <summary>A link that keeps dropping (scenario W7g): several half-open cuts from one ';'-separated --cut, each holding the bytes
    /// sent inside it until its end, the window between them passing bytes at once, on the same connection.</summary>
    [Fact]
    public async Task Repeated_half_open_cuts_hold_each_ones_bytes_and_pass_the_window_between_them()
    {
        var (server, _) = EchoServer();
        var target = (IPEndPoint)server.LocalEndpoint;
        var t0 = Now + 0.5;
        var cuts = LinkCut.ParseAll(string.Create(System.Globalization.CultureInfo.InvariantCulture, $"{t0:0.000},1.5,half; {t0 + 2.5:0.000},1.5,half"));
        Assert.Equal([(1.5, true), (1.5, true)], cuts.Select(c => (c.Seconds, c.Half)));
        Assert.Empty(LinkCut.ParseAll(""));
        await using var proxy = new ImpairmentProxy(new IPEndPoint(IPAddress.Loopback, 0), target, Impairment.None, Impairment.None, 1) { Cuts = cuts };
        using var client = new TcpClient();
        await client.ConnectAsync(IPAddress.Loopback, proxy.Port);
        var s = client.GetStream();
        Assert.Equal(1, await RoundTripAsync(s, 1));
        await Task.Delay(1000); // inside the first cut
        var held = Stopwatch.StartNew();
        Assert.Equal(2, await RoundTripAsync(s, 2));
        Assert.True(held.Elapsed.TotalSeconds > 0.6, $"the first cut held it {held.Elapsed.TotalSeconds:0.00} s");
        held.Restart();
        Assert.Equal(3, await RoundTripAsync(s, 3)); // the window between the cuts
        Assert.True(held.Elapsed.TotalSeconds < 0.5, $"the window held it {held.Elapsed.TotalSeconds:0.00} s");
        await Task.Delay(1300); // inside the second cut
        held.Restart();
        Assert.Equal(4, await RoundTripAsync(s, 4));
        Assert.True(held.Elapsed.TotalSeconds > 0.6, $"the second cut held it {held.Elapsed.TotalSeconds:0.00} s");
        server.Stop();
    }
}
