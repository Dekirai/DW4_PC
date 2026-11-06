using System.Buffers.Binary;
using System.Net;
using System.Net.Sockets;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using Microsoft.Extensions.Logging.Abstractions;

namespace DW4Online.Integration.Tests;

/// <summary>
/// The relay connection's writer and reader (M4 (e), round I1a): frames sent inside an outbox scope are written by that thread
/// after the hub's lock with non-blocking sends; a full socket buffer (a reader that does not read) and state transfers hand
/// the queue to the asynchronous writer. Whatever path a frame takes, the peer gets every frame whole and in the order it was
/// sent. The reader reads into a buffer and takes frames cut anywhere, run together, or larger than its buffer.
/// </summary>
public class ConnectionWriteTests
{
    private const byte GameType = 0x42;

    private static byte[] Frame(int k, int size)
    {
        var f = new byte[Wire.HeaderBytes + size];
        f[0] = GameType;
        BinaryPrimitives.WriteUInt32LittleEndian(f.AsSpan(1), (uint)size);
        BinaryPrimitives.WriteInt32LittleEndian(f.AsSpan(Wire.HeaderBytes), k);
        f[^1] = (byte)k;
        return f;
    }

    private static async Task<(TcpClient Reader, TcpClient Server)> PairAsync()
    {
        var listener = new TcpListener(IPAddress.Loopback, TestPorts.Next());
        listener.Start();
        try
        {
            var reader = new TcpClient { ReceiveBufferSize = 8192 };
            var accept = listener.AcceptTcpClientAsync();
            await reader.ConnectAsync(IPAddress.Loopback, ((IPEndPoint)listener.LocalEndpoint).Port);
            var server = await accept;
            server.SendBufferSize = 8192;
            return (reader, server);
        }
        finally
        {
            listener.Stop();
        }
    }

    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public async Task Every_frame_arrives_whole_and_in_order_through_a_full_socket_buffer_and_state_transfers(bool direct)
    {
        var (reader, server) = await PairAsync();
        using var r = reader;
        var options = new RelayOptions { DirectSends = direct, MaxQueuedBytes = 64 << 20, MaxQueuedStateBytes = 64 << 20 };
        var conn = new Connection(server, options, NullLogger.Instance, IPAddress.Loopback);
        var writer = conn.WriteLoopAsync();
        // 3,000 frames of 1 KB while nobody reads (far more than the socket buffers hold), from four threads in turn as the
        // hub's hot paths send them (a scope each, under one lock), every 50th a state transfer (bulk).
        const int count = 3000;
        var gate = new object();
        var next = 0;
        var threads = Enumerable.Range(0, 4).Select(_ => new Thread(() =>
        {
            while (true)
            {
                Outbox.Enter();
                try
                {
                    lock (gate)
                    {
                        if (next == count) return;
                        var k = next++;
                        conn.Send(Frame(k, 1024), bulk: k % 50 == 49);
                    }
                }
                finally
                {
                    Outbox.Exit();
                }
            }
        })).ToList();
        threads.ForEach(t => t.Start());
        threads.ForEach(t => t.Join());
        conn.Close(CloseReason.Left, "Goodbye.");
        // Now the reader reads: every frame whole and in order, then CLOSE, then the end of the stream.
        var stream = reader.GetStream();
        var header = new byte[Wire.HeaderBytes];
        var payload = new byte[1024];
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(20));
        for (var k = 0; k < count; ++k)
        {
            await stream.ReadExactlyAsync(header, timeout.Token);
            Assert.Equal((GameType, 1024u), (header[0], BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(1))));
            await stream.ReadExactlyAsync(payload, timeout.Token);
            Assert.Equal((k, (byte)k), (BinaryPrimitives.ReadInt32LittleEndian(payload), payload[^1]));
        }
        await stream.ReadExactlyAsync(header, timeout.Token);
        Assert.Equal(Msg.Close, header[0]);
        var close = new byte[BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(1))];
        await stream.ReadExactlyAsync(close, timeout.Token);
        Assert.Equal(CloseReason.Left, CloseMsg.Parse(close).Reason);
        Assert.Equal(0, await stream.ReadAsync(new byte[1], timeout.Token));
        await writer.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal((long)count * (Wire.HeaderBytes + 1024) + Wire.HeaderBytes + close.Length, conn.BytesOut);
    }

    [Fact]
    public async Task The_reader_takes_frames_split_or_run_together_and_frames_above_its_buffer()
    {
        var (client, server) = await PairAsync();
        var conn = new Connection(server, new RelayOptions(), NullLogger.Instance, IPAddress.Loopback);
        var got = new List<(byte Type, int K, int Last, int Len)>();
        var reading = conn.ReadLoopAsync((type, payload) =>
            got.Add((type, BinaryPrimitives.ReadInt32LittleEndian(payload.Span), payload.Span[^1], payload.Length)));
        int[] sizes = [5, 1024, 40_000, 8, 200_000, 16 * 1024 - 5, 16 * 1024, 12, 5, 70_000, 6]; // (at least 5: k and its low byte apart)
        var all = sizes.SelectMany((n, k) => Frame(k, n)).ToArray();
        var stream = client.GetStream();
        // In pieces that cut headers and payloads anywhere, some with a pause (separate reads), some back to back.
        var at = 0;
        var piece = 1;
        while (at < all.Length)
        {
            var n = Math.Min(piece, all.Length - at);
            await stream.WriteAsync(all.AsMemory(at, n));
            at += n;
            piece = piece * 7 % 9001 + 1;
            if (piece % 3 == 0) await Task.Delay(2);
        }
        client.Dispose();
        Assert.False(await reading.WaitAsync(TimeSpan.FromSeconds(10))); // (closed without BYE)
        Assert.Equal(sizes.Select((n, k) => (GameType, k, k & 0xFF, n)), got.Select(x => (x.Type, x.K, x.Last, x.Len)));
        Assert.Equal(all.Length, conn.BytesIn);
        conn.Abort();
    }

    [Fact]
    public async Task A_peer_that_is_gone_ends_the_writer_and_frames_after_it_are_dropped()
    {
        var (reader, server) = await PairAsync();
        var conn = new Connection(server, new RelayOptions(), NullLogger.Instance, IPAddress.Loopback);
        var writer = conn.WriteLoopAsync();
        Outbox.Enter();
        conn.Send(Frame(1, 16));
        Outbox.Exit();
        reader.Dispose();
        conn.Finish();
        await writer.WaitAsync(TimeSpan.FromSeconds(5));
        Outbox.Enter();
        conn.Send(Frame(2, 16)); // (after the end: nothing, no throw)
        Outbox.Exit();
    }
}
