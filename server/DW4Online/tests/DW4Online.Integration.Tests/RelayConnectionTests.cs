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
                    line = await HandshakeAndDropAsync(port, tickets.Issue("LEAK01", 7, RelayRoles.Member));
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
        var hello = string.Create(CultureInfo.InvariantCulture, $"{Wire.Magic} {ticket} 44 1 {0x1234UL:x16}\n");
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
}
