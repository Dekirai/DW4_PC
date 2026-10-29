using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

namespace DW4Online.Relay;

/// <summary>
/// The TCP relay (proven in the groundwork spike, hardened here). Every connection starts with one ASCII line:
/// <list type="bullet">
/// <item>host control: "DW4R1 HOST &lt;ticket&gt;\n" -> "OK &lt;hostKey&gt;\n"; then the relay announces each guest as
/// "GUEST &lt;n&gt; &lt;account&gt;\n". Closing this socket closes the room (no host migration).</item>
/// <item>guest: "DW4R1 GUEST &lt;ticket&gt;\n" -> "OK\n" once the host's data socket arrived, then raw bytes.</item>
/// <item>host data: "DW4R1 DATA &lt;room&gt; &lt;n&gt; &lt;hostKey&gt;\n" -> spliced with guest n byte for byte.</item>
/// </list>
/// Failures answer "ERR &lt;reason&gt;\n" and close. The relay does not parse the game protocol.
/// </summary>
public sealed class RelayServer(RelayOptions options, RelayTickets tickets, TimeProvider time, ILogger<RelayServer> log) : BackgroundService
{
    private readonly ConcurrentDictionary<string, HostEntry> _rooms = new(StringComparer.Ordinal);
    private readonly ConcurrentDictionary<IPAddress, int> _perIp = new();
    private readonly TaskCompletionSource<int> _started = new(TaskCreationOptions.RunContinuationsAsynchronously);

    /// <summary>The bound port once listening (useful with Port 0 in tests).</summary>
    public Task<int> Started => _started.Task;

    /// <summary>Raised when a room's host control socket closes; the room service then closes the room.</summary>
    public event Action<string>? HostDisconnected;

    public int RoomCount => _rooms.Count;

    /// <summary>Drops a room's host control socket and every spliced pair of the room.</summary>
    public void CloseRoom(string room)
    {
        if (_rooms.TryRemove(room, out var entry)) entry.Close();
    }

    protected override async Task ExecuteAsync(CancellationToken stop)
    {
        var listener = new TcpListener(IPAddress.Parse(options.ListenAddress), options.Port);
        try
        {
            listener.Start();
        }
        catch (Exception e)
        {
            _started.TrySetException(e);
            throw;
        }
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        log.LogInformation("Relay listening on {Address}:{Port}", options.ListenAddress, port);
        _started.TrySetResult(port);
        try
        {
            while (!stop.IsCancellationRequested)
            {
                TcpClient client;
                try
                {
                    client = await listener.AcceptTcpClientAsync(stop);
                }
                catch (OperationCanceledException)
                {
                    break;
                }
                catch (SocketException e)
                {
                    log.LogDebug(e, "accept failed");
                    continue;
                }
                _ = Task.Run(() => HandleAsync(client, stop), CancellationToken.None);
            }
        }
        finally
        {
            listener.Stop();
            foreach (var room in _rooms.Keys) CloseRoom(room);
        }
    }

    private async Task HandleAsync(TcpClient client, CancellationToken stop)
    {
        var ip = (client.Client.RemoteEndPoint as IPEndPoint)?.Address ?? IPAddress.None;
        if (ip.IsIPv4MappedToIPv6) ip = ip.MapToIPv4();
        if (_perIp.AddOrUpdate(ip, 1, (_, n) => n + 1) > options.MaxConnectionsPerIp)
        {
            Release(ip);
            client.Dispose();
            return;
        }
        var keep = false;
        try
        {
            client.NoDelay = true;
            var stream = client.GetStream();
            string line;
            using (var handshake = CancellationTokenSource.CreateLinkedTokenSource(stop))
            {
                handshake.CancelAfter(TimeSpan.FromSeconds(options.HandshakeTimeoutSeconds));
                line = await ReadLineAsync(stream, handshake.Token);
            }
            var parts = line.Split(' ');
            if (parts.Length < 3 || parts[0] != "DW4R1")
            {
                await ReplyAsync(stream, "ERR protocol", stop);
                return;
            }
            switch (parts[1])
            {
                case "HOST" when parts.Length == 3:
                    keep = await HostAsync(client, stream, ip, parts[2], stop);
                    break;
                case "GUEST" when parts.Length == 3:
                    await GuestAsync(client, stream, parts[2], stop);
                    break;
                case "DATA" when parts.Length == 5:
                    keep = await DataAsync(client, stream, parts[2], parts[3], parts[4], stop);
                    break;
                default:
                    await ReplyAsync(stream, "ERR protocol", stop);
                    break;
            }
        }
        catch (Exception e) when (e is IOException or SocketException or OperationCanceledException or ObjectDisposedException)
        {
            log.LogDebug(e, "relay connection from {Ip} ended", ip);
        }
        finally
        {
            if (!keep)
            {
                client.Dispose();
                Release(ip);
            }
        }
    }

    // Returns true when the socket was handed over (it is disposed elsewhere).
    private async Task<bool> HostAsync(TcpClient client, NetworkStream stream, IPAddress ip, string ticket, CancellationToken stop)
    {
        var claims = tickets.Redeem(ticket);
        if (claims is null || claims.Role != RelayRoles.Host)
        {
            await ReplyAsync(stream, "ERR ticket", stop);
            return false;
        }
        if (_rooms.Count >= options.MaxRooms)
        {
            await ReplyAsync(stream, "ERR full", stop);
            return false;
        }
        var entry = new HostEntry(claims.Room, claims.Account, client, ip, Convert.ToHexString(RandomNumberGenerator.GetBytes(16)));
        if (!_rooms.TryAdd(claims.Room, entry))
        {
            await ReplyAsync(stream, "ERR room_taken", stop);
            return false;
        }
        await entry.SendAsync("OK " + entry.HostKey, stop);
        log.LogInformation("Relay room {Room} hosted by account {Account}", claims.Room, claims.Account);
        _ = Task.Run(() => WatchHostAsync(entry, stream, stop), CancellationToken.None);
        return true;
    }

    // The control socket only carries keep-alive lines from the host; its end closes the room.
    private async Task WatchHostAsync(HostEntry entry, NetworkStream stream, CancellationToken stop)
    {
        var buffer = new byte[256];
        try
        {
            using var linked = CancellationTokenSource.CreateLinkedTokenSource(stop, entry.Closed.Token);
            while (await stream.ReadAsync(buffer, linked.Token) > 0) { }
        }
        catch (Exception e) when (e is IOException or SocketException or OperationCanceledException or ObjectDisposedException)
        {
        }
        finally
        {
            if (_rooms.TryGetValue(entry.Room, out var current) && ReferenceEquals(current, entry)) _rooms.TryRemove(entry.Room, out _);
            entry.Close();
            Release(entry.Ip);
            log.LogInformation("Relay room {Room} closed (host control ended)", entry.Room);
            HostDisconnected?.Invoke(entry.Room);
        }
    }

    private async Task GuestAsync(TcpClient client, NetworkStream stream, string ticket, CancellationToken stop)
    {
        var claims = tickets.Redeem(ticket);
        if (claims is null || claims.Role != RelayRoles.Guest)
        {
            await ReplyAsync(stream, "ERR ticket", stop);
            return;
        }
        if (!_rooms.TryGetValue(claims.Room, out var entry))
        {
            await ReplyAsync(stream, "ERR no_host", stop);
            return;
        }
        var n = Interlocked.Increment(ref entry.NextGuest);
        var pending = new TaskCompletionSource<TcpClient>(TaskCreationOptions.RunContinuationsAsynchronously);
        entry.Pending[n] = pending;
        TcpClient hostData;
        try
        {
            await entry.SendAsync(FormattableString.Invariant($"GUEST {n} {claims.Account}"), stop);
            using var wait = CancellationTokenSource.CreateLinkedTokenSource(stop, entry.Closed.Token);
            wait.CancelAfter(TimeSpan.FromSeconds(options.HandshakeTimeoutSeconds));
            hostData = await pending.Task.WaitAsync(wait.Token);
        }
        catch (OperationCanceledException)
        {
            // A DATA connection that won the race against the timeout is closed here.
            if (!entry.Pending.TryRemove(n, out _) && pending.Task.IsCompletedSuccessfully) DisposeData(pending.Task.Result);
            await ReplyAsync(stream, "ERR host_timeout", stop);
            return;
        }
        try
        {
            await ReplyAsync(stream, "OK", stop);
            log.LogInformation("Relay room {Room}: guest {N} (account {Account}) spliced", entry.Room, n, claims.Account);
            using var pair = CancellationTokenSource.CreateLinkedTokenSource(stop, entry.Closed.Token);
            await SpliceAsync(stream, hostData.GetStream(), pair);
        }
        finally
        {
            DisposeData(hostData);
        }
    }

    private void DisposeData(TcpClient hostData)
    {
        IPAddress? ip = null;
        try
        {
            ip = (hostData.Client.RemoteEndPoint as IPEndPoint)?.Address;
        }
        catch (ObjectDisposedException)
        {
        }
        hostData.Dispose();
        if (ip is not null) Release(ip.IsIPv4MappedToIPv6 ? ip.MapToIPv4() : ip);
    }

    private async Task<bool> DataAsync(TcpClient client, NetworkStream stream, string room, string guest, string hostKey, CancellationToken stop)
    {
        if (!_rooms.TryGetValue(room, out var entry) || !int.TryParse(guest, out var n) ||
            !CryptographicOperations.FixedTimeEquals(Encoding.ASCII.GetBytes(hostKey), Encoding.ASCII.GetBytes(entry.HostKey)) ||
            !entry.Pending.TryRemove(n, out var pending))
        {
            await ReplyAsync(stream, "ERR data", stop);
            return false;
        }
        return pending.TrySetResult(client); // the guest's task owns and disposes it now
    }

    /// <summary>Copies both directions until one side closes, the room closes, or both stay idle too long.</summary>
    private async Task SpliceAsync(NetworkStream a, NetworkStream b, CancellationTokenSource pair)
    {
        var last = time.GetTimestamp();
        void Touch() => Interlocked.Exchange(ref last, time.GetTimestamp());
        var idle = TimeSpan.FromSeconds(options.IdleTimeoutSeconds);
        var watchdog = Task.Run(async () =>
        {
            while (!pair.IsCancellationRequested)
            {
                await Task.Delay(TimeSpan.FromMilliseconds(Math.Min(1000, idle.TotalMilliseconds / 4)), time, pair.Token);
                if (time.GetElapsedTime(Interlocked.Read(ref last)) > idle) pair.Cancel();
            }
        });
        var one = PumpAsync(a, b, Touch, pair.Token);
        var two = PumpAsync(b, a, Touch, pair.Token);
        await Task.WhenAny(one, two);
        await pair.CancelAsync();
        try
        {
            await Task.WhenAll(one, two, watchdog);
        }
        catch (Exception e) when (e is IOException or SocketException or OperationCanceledException or ObjectDisposedException)
        {
        }
    }

    private async Task PumpAsync(NetworkStream from, NetworkStream to, Action touch, CancellationToken ct)
    {
        var buffer = new byte[16384];
        var bucket = new TokenBucket(options.BytesPerSecond, options.BurstBytes, time);
        try
        {
            int read;
            while ((read = await from.ReadAsync(buffer, ct)) > 0)
            {
                await bucket.TakeAsync(read, ct);
                await to.WriteAsync(buffer.AsMemory(0, read), ct);
                touch();
            }
        }
        catch (Exception e) when (e is IOException or SocketException or OperationCanceledException or ObjectDisposedException)
        {
        }
    }

    private void Release(IPAddress ip)
    {
        while (_perIp.TryGetValue(ip, out var n))
        {
            if (n <= 1 ? _perIp.TryRemove(new KeyValuePair<IPAddress, int>(ip, n)) : _perIp.TryUpdate(ip, n - 1, n)) return;
        }
    }

    /// <summary>Reads one handshake line byte by byte, so nothing after the newline is consumed.</summary>
    private static async Task<string> ReadLineAsync(NetworkStream stream, CancellationToken ct)
    {
        var sb = new StringBuilder();
        var one = new byte[1];
        while (sb.Length < 600)
        {
            if (await stream.ReadAsync(one, ct) == 0) throw new IOException("closed during the handshake");
            if (one[0] == '\n') return sb.ToString().TrimEnd('\r');
            if (one[0] is < 0x20 or > 0x7E && one[0] != '\r') throw new IOException("binary data in the handshake");
            sb.Append((char)one[0]);
        }
        throw new IOException("handshake line too long");
    }

    private static async Task ReplyAsync(NetworkStream stream, string line, CancellationToken ct) =>
        await stream.WriteAsync(Encoding.ASCII.GetBytes(line + "\n"), ct);

    private sealed class HostEntry(string room, long account, TcpClient control, IPAddress ip, string hostKey)
    {
        private readonly SemaphoreSlim _write = new(1, 1);
        public string Room { get; } = room;
        public long Account { get; } = account;
        public TcpClient Control { get; } = control;
        public IPAddress Ip { get; } = ip;
        public string HostKey { get; } = hostKey;
        public ConcurrentDictionary<int, TaskCompletionSource<TcpClient>> Pending { get; } = new();
        public CancellationTokenSource Closed { get; } = new();
        public int NextGuest;

        public async Task SendAsync(string line, CancellationToken ct)
        {
            await _write.WaitAsync(ct);
            try
            {
                await Control.GetStream().WriteAsync(Encoding.ASCII.GetBytes(line + "\n"), ct);
            }
            finally
            {
                _write.Release();
            }
        }

        public void Close()
        {
            try
            {
                Closed.Cancel();
            }
            catch (ObjectDisposedException)
            {
            }
            foreach (var (_, pending) in Pending) pending.TrySetCanceled();
            Control.Dispose();
        }
    }
}

/// <summary>A per-direction byte budget: a burst allowance refilled at a sustained rate.</summary>
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
            if (_tokens >= bytes)
            {
                _tokens -= bytes;
                return;
            }
            var wait = TimeSpan.FromSeconds(Math.Min(1.0, (bytes - _tokens) / perSecond));
            await Task.Delay(wait, time, ct);
        }
    }
}
