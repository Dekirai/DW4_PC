using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using DW4Online.Relay.Protocol;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

namespace DW4Online.Relay.Hub;

/// <summary>What the handshake asks the room service: the ticket's room, account and nonce.</summary>
public sealed record HubAdmission(string Room, long AccountId, string Nonce);

/// <summary>The room service's answer: the member's room index, name, owner flag and the room's compatibility data.</summary>
public sealed record HubAdmitted(int MemberIndex, string Username, bool Owner, RoomInfo Room);

/// <summary>
/// The DW4R2 relay (SEAMLESS.md 6): one TCP connection per client and room, which stays across the member's instances.
/// The first line is "DW4R2 &lt;ticket&gt; &lt;protocol&gt; &lt;dataHash&gt; &lt;settingsHash&gt;\n", answered with
/// "OK &lt;member&gt; &lt;serverTimeMs&gt;\n" or "ERR &lt;code&gt;\n"; then frames (type u8, length u32 LE, payload). A
/// dedicated thread seals the ticks of every sealed instance at 59.94 Hz; a housekeeping loop pings and enforces the
/// holds and limits. The relay never simulates the game.
/// </summary>
public sealed class RelayHub : BackgroundService
{
    private readonly RelayOptions _options;
    private readonly SealingOptions _sealing;
    private readonly InstanceOptions _instances;
    private readonly RelayTickets _tickets;
    private readonly ILogger<RelayHub> _log;
    private readonly ILoggerFactory _loggers;
    private readonly ConcurrentDictionary<string, RoomHub> _rooms = new(StringComparer.Ordinal);
    // Per open room, beyond its hub's life (an empty hub is dropped while the room stays open): the story keeper and the
    // instance ids (SEAMLESS.md 8.7), and the owner's presence as the room service last reported it (a report that came
    // while no hub was there, or while the first connection's admission loaded the story, still reaches the keeper).
    private readonly ConcurrentDictionary<string, RoomSessionState> _sessions = new(StringComparer.Ordinal);
    private readonly ConcurrentDictionary<string, bool> _ownerPresence = new(StringComparer.Ordinal);
    private readonly ConcurrentDictionary<IPAddress, int> _perIp = new();
    private readonly TaskCompletionSource<int> _started = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly HighResolutionTimer _timer = new();
    private readonly JsonMembershipLog? _mlog;
    private volatile bool _stopping;
    private Thread? _sealer;
    private TcpListener? _listener; // bound in StartAsync

    public RelayHub(RelayOptions options, SealingOptions sealing, InstanceOptions instances, RelayTickets tickets, ILoggerFactory loggers)
    {
        _options = options;
        _sealing = sealing;
        _instances = instances;
        _tickets = tickets;
        _loggers = loggers;
        _log = loggers.CreateLogger<RelayHub>();
        if (options.DiagnosticsDirectory.Length > 0) _mlog = new JsonMembershipLog(options.DiagnosticsDirectory);
    }

    /// <summary>The bound port once listening (Port 0 in tests).</summary>
    public Task<int> Started => _started.Task;

    /// <summary>The room service's check of a redeemed ticket (null = refused). Without it every valid ticket gets index 0 (relay-only tests).</summary>
    public Func<HubAdmission, ValueTask<HubAdmitted?>>? Admission { get; set; }

    /// <summary>Called every few seconds with the accounts connected per room (the relay connection is the room heartbeat).</summary>
    public Func<string, IReadOnlyCollection<long>, ValueTask>? Heartbeat { get; set; }

    /// <summary>Saves a room story's new revision as its owner's online story (account, copy, whether only counters changed;
    /// called under the room lock: only queue).</summary>
    public Action<long, DW4Online.Domain.Story.RoomStoryData, bool>? StorySaver { get; set; }

    /// <summary>The most players the account's instance had in the last minute (null: no such member on the relay).</summary>
    public int? RecentInstancePlayers(string code, long accountId)
    {
        if (!_rooms.TryGetValue(code, out var hub)) return null;
        lock (hub.Sync) return hub.RecentInstancePlayers(accountId, Stopwatch.GetTimestamp());
    }

    /// <summary>The room story as the room API shows it (null: no hub for that room).</summary>
    public DW4Online.Domain.Rooms.RoomStoryState? Story(string code)
    {
        if (!_rooms.TryGetValue(code, out var hub)) return null;
        if (!Monitor.TryEnter(hub.Sync, 50)) return null;
        try
        {
            return hub.StoryState();
        }
        finally
        {
            Monitor.Exit(hub.Sync);
        }
    }

    public bool HighResolutionTimer => _timer.HighResolution;

    /// <summary>Whether the sealing thread runs (diagnostics and tests: it must have ended when Dispose returns).</summary>
    public bool SealingThreadAlive => Volatile.Read(ref _sealer)?.IsAlive == true;

    public IReadOnlyCollection<string> Rooms => _rooms.Keys.ToList();
    public RoomHub? Room(string code) => _rooms.GetValueOrDefault(code);

    public IReadOnlyList<InstanceView> Views(string code)
    {
        if (!_rooms.TryGetValue(code, out var hub)) return [];
        // A busy room (a burst of frames, a long state) answers with the last housekeeping pass's views (at most a
        // quarter second old) instead of holding up the room list and the admission.
        if (!Monitor.TryEnter(hub.Sync, 50)) return hub.CachedViews;
        try
        {
            return hub.Views();
        }
        finally
        {
            Monitor.Exit(hub.Sync);
        }
    }

    /// <summary>A member left the room: its connection closes and its slot leaves at once.</summary>
    public void MemberLeft(string code, long accountId, CloseReason reason, string text)
    {
        if (!_rooms.TryGetValue(code, out var hub)) return;
        lock (hub.Sync) hub.RemoveMember(accountId, reason, text, Stopwatch.GetTimestamp());
    }

    public void RoomClosed(string code, string text)
    {
        _sessions.TryRemove(code, out _);
        _ownerPresence.TryRemove(code, out _);
        if (!_rooms.TryRemove(code, out var hub)) return;
        lock (hub.Sync)
        {
            hub.Removed = true;
            hub.CloseAll(CloseReason.RoomClosed, text);
        }
    }

    public void OwnerPresence(string code, bool present)
    {
        _ownerPresence[code] = present;
        if (!_rooms.TryGetValue(code, out var hub)) return;
        lock (hub.Sync) hub.OwnerPresence(present);
    }

    /// <summary>
    /// Binds the listener before the hub runs, so a port in use fails the host's start with that error (the service
    /// log's FAIL line names it). .NET 10 runs all of <see cref="ExecuteAsync"/> on a thread of its own: a bind there
    /// only stopped the host, and the start then failed with an OperationCanceledException of the next hosted service.
    /// </summary>
    public override Task StartAsync(CancellationToken cancellationToken)
    {
        var listener = new TcpListener(IPAddress.Parse(_options.ListenAddress), _options.Port);
        try
        {
            listener.Start();
        }
        catch (SocketException e)
        {
            // Kestrel's wording for its own endpoints; the socket's text is localized, so it follows as the inner exception.
            var error = new IOException($"Failed to bind the relay to {_options.ListenAddress}:{_options.Port} (Relay:ListenAddress, Relay:Port): " +
                                        (e.SocketErrorCode == SocketError.AddressAlreadyInUse ? "address already in use." : $"{e.SocketErrorCode}."), e);
            _started.TrySetException(error);
            throw error;
        }
        _listener = listener;
        return base.StartAsync(cancellationToken);
    }

    protected override async Task ExecuteAsync(CancellationToken stop)
    {
        var listener = _listener ?? throw new InvalidOperationException("RelayHub.StartAsync binds the listener first.");
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        _log.LogInformation("Relay (DW4R2) listening on {Address}:{Port}; sealing timer {Kind}", _options.ListenAddress, port,
            _timer.HighResolution ? "high resolution" : "sleep and spin");
        _started.TrySetResult(port);
        var sealer = new Thread(SealLoop) { IsBackground = true, Name = "DW4R2 sealing", Priority = ThreadPriority.AboveNormal };
        sealer.Start();
        Volatile.Write(ref _sealer, sealer); // only a started thread: Dispose joins it
        var housekeeping = Task.Run(() => HousekeepAsync(stop), CancellationToken.None);
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
                    _log.LogDebug(e, "accept failed");
                    continue;
                }
                _ = Task.Run(() => HandleAsync(client, stop), CancellationToken.None);
            }
        }
        finally
        {
            _stopping = true;
            _timer.Wake();
            listener.Stop();
            foreach (var code in _rooms.Keys.ToList()) RoomClosed(code, "The server is stopping.");
            try
            {
                await housekeeping;
            }
            catch (OperationCanceledException)
            {
            }
            sealer.Join(TimeSpan.FromSeconds(2));
        }
    }

    private void SealLoop()
    {
        while (!_stopping)
        {
            var now = Stopwatch.GetTimestamp();
            var next = long.MaxValue;
            foreach (var hub in _rooms.Values)
            {
                // A room busy under its lock (a burst of frames, a state being sent) is not waited for: the others seal
                // on time, it seals its due ticks in the next pass a millisecond later (in one batch).
                if (!Monitor.TryEnter(hub.Sync))
                {
                    next = Math.Min(next, now + Stopwatch.Frequency / 1000);
                    continue;
                }
                try
                {
                    next = Math.Min(next, hub.Seal(now));
                }
                catch (Exception e)
                {
                    hub.Fault(e, "sealing");
                }
                finally
                {
                    Monitor.Exit(hub.Sync);
                }
            }
            // Nothing sealed: wait for a wake (an instance starts sealing) but look again every 50 ms.
            var limit = Stopwatch.GetTimestamp() + Stopwatch.Frequency / 20;
            _timer.WaitUntil(Math.Min(next, limit));
        }
    }

    private async Task HousekeepAsync(CancellationToken stop)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(250));
        var lastBeat = Stopwatch.GetTimestamp();
        var beats = new List<(string Code, List<long> Members)>();
        try
        {
            while (await timer.WaitForNextTickAsync(stop))
            {
                var now = Stopwatch.GetTimestamp();
                var beat = (now - lastBeat) >= 5 * Stopwatch.Frequency;
                if (beat) lastBeat = now;
                foreach (var (code, hub) in _rooms.ToList())
                {
                    List<long> connected = [];
                    // One room's fault never ends the housekeeping of the others (a task that died silently stopped
                    // every room's pings, timeouts and heartbeats).
                    try
                    {
                        lock (hub.Sync)
                        {
                            try
                            {
                                hub.Housekeep(now);
                            }
                            catch (Exception e)
                            {
                                hub.Fault(e, "housekeeping");
                            }
                            foreach (var m in hub.Members)
                                if (m.Peer is { } p && (now - m.LastHeard) > (long)_options.IdleTimeoutSeconds * Stopwatch.Frequency)
                                    p.Close(CloseReason.Timeout, "Nothing heard from this connection.");
                            connected = hub.Members.Where(m => m.Connected).Select(m => m.AccountId).ToList();
                            if (hub.Empty)
                            {
                                // (Under the lock: a handshake that found this hub sees Removed and takes a new one.)
                                hub.Removed = true;
                                _rooms.TryRemove(new KeyValuePair<string, RoomHub>(code, hub));
                            }
                        }
                    }
                    catch (Exception e)
                    {
                        _log.LogError(e, "Relay housekeeping failed for room {Room}", code);
                    }
                    if (beat && connected.Count > 0) beats.Add((code, connected));
                }
                // The heartbeats go to the room service on their own task: it writes to the database, and a slow or
                // stalled database must never hold up the pings, holds and timeouts of every room (a suspended
                // database stopped every room's housekeeping for as long as it hung).
                if (beats.Count > 0 && Heartbeat is { } h)
                {
                    if (_heartbeats is { IsCompleted: false }) HeartbeatsSkipped++;
                    else
                    {
                        var batch = beats.ToList();
                        _heartbeats = Task.Run(async () =>
                        {
                            foreach (var (code, members) in batch)
                            {
                                try
                                {
                                    await h(code, members);
                                }
                                catch (Exception e) when (e is not OperationCanceledException)
                                {
                                    _log.LogDebug(e, "relay heartbeat for room {Room} failed", code);
                                }
                            }
                        }, CancellationToken.None);
                    }
                    beats.Clear();
                }
            }
        }
        catch (OperationCanceledException)
        {
        }
    }

    private Task? _heartbeats;

    /// <summary>Heartbeat rounds skipped because the previous one had not finished (a slow database).</summary>
    public long HeartbeatsSkipped { get; private set; }

    private async Task HandleAsync(TcpClient client, CancellationToken stop)
    {
        var ip = (client.Client.RemoteEndPoint as IPEndPoint)?.Address ?? IPAddress.None;
        if (ip.IsIPv4MappedToIPv6) ip = ip.MapToIPv4();
        if (_perIp.AddOrUpdate(ip, 1, (_, n) => n + 1) > _options.MaxConnectionsPerIp)
        {
            // The address has its connections (a household or a carrier-grade NAT across rooms): the client is told
            // why instead of seeing a reset.
            try
            {
                using var busy = CancellationTokenSource.CreateLinkedTokenSource(stop);
                busy.CancelAfter(TimeSpan.FromSeconds(2));
                await ReplyAsync(client.GetStream(), "ERR busy", busy.Token);
            }
            catch (Exception e) when (e is IOException or SocketException or OperationCanceledException or ObjectDisposedException or InvalidOperationException)
            {
            }
            Release(ip);
            client.Dispose();
            return;
        }
        Connection? conn = null;
        RoomHub? hub = null;
        HubMember? member = null;
        try
        {
            client.NoDelay = true;
            var stream = client.GetStream();
            string line;
            using (var handshake = CancellationTokenSource.CreateLinkedTokenSource(stop))
            {
                handshake.CancelAfter(TimeSpan.FromSeconds(_options.HandshakeTimeoutSeconds));
                line = await ReadLineAsync(stream, handshake.Token);
            }
            var parts = line.Split(' ');
            if (parts.Length != 5 || parts[0] != Wire.Magic ||
                !int.TryParse(parts[2], NumberStyles.None, CultureInfo.InvariantCulture, out var protocol) ||
                !ulong.TryParse(parts[3], NumberStyles.None, CultureInfo.InvariantCulture, out var dataHash) ||
                !ulong.TryParse(parts[4], NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var settingsHash))
            {
                await ReplyAsync(stream, "ERR protocol", stop);
                return;
            }
            var claims = _tickets.Redeem(parts[1]);
            if (claims is null || claims.Role != RelayRoles.Member)
            {
                await ReplyAsync(stream, "ERR ticket", stop);
                return;
            }
            HubAdmitted? admitted;
            if (Admission is { } admit) admitted = await admit(new HubAdmission(claims.Room, claims.Account, claims.Nonce));
            else admitted = new HubAdmitted(0, "member" + claims.Account.ToString(CultureInfo.InvariantCulture), true,
                new RoomInfo(claims.Room, protocol, dataHash, settingsHash, 10));
            if (admitted is null)
            {
                await ReplyAsync(stream, "ERR ticket", stop);
                return;
            }
            var room = admitted.Room;
            var error = room.Protocol != protocol ? "protocol" : room.DataHash != dataHash ? "data" : room.SettingsHash != settingsHash ? "settings" : null;
            if (error is not null)
            {
                await ReplyAsync(stream, "ERR " + error, stop);
                return;
            }
            if (!_rooms.ContainsKey(room.Code) && _rooms.Count >= _options.MaxRooms)
            {
                await ReplyAsync(stream, "ERR full", stop);
                return;
            }
            conn = new Connection(client, _options, _log, ip);
            while (member is null)
            {
                hub = _rooms.GetOrAdd(room.Code, _ => NewRoom(room));
                lock (hub.Sync)
                {
                    // An empty hub the housekeeping removed meanwhile is not joined (nobody would seal or housekeep it).
                    if (!hub.Removed)
                    {
                        hub.Info = room;
                        var serverMs = (long)(Stopwatch.GetTimestamp() * 1000.0 / Stopwatch.Frequency);
                        conn.Send(Encoding.ASCII.GetBytes(FormattableString.Invariant($"OK {admitted.MemberIndex} {serverMs}\n")));
                        member = hub.Connect(admitted.MemberIndex, claims.Account, admitted.Username, admitted.Owner, conn, Stopwatch.GetTimestamp());
                        // The owner's presence as the room service reported it last: the admission read it before the
                        // story's load, and a change meanwhile found no hub to tell (SEAMLESS.md 8.6).
                        if (_ownerPresence.TryGetValue(room.Code, out var present) && hub.Story.OwnerPresent != present) hub.OwnerPresence(present);
                    }
                }
                if (member is null) _rooms.TryRemove(new KeyValuePair<string, RoomHub>(room.Code, hub));
            }
            _log.LogInformation("Relay: account {Account} connected to room {Room} as member {Member}", claims.Account, room.Code, admitted.MemberIndex);
            var writer = Task.Run(conn.WriteLoopAsync, CancellationToken.None);
            var h = hub!;
            var m = member;
            var graceful = await conn.ReadLoopAsync((type, payload) =>
            {
                lock (h.Sync) h.Receive(m, type, payload.Span, Stopwatch.GetTimestamp());
            });
            lock (hub!.Sync) hub.Disconnect(member, conn, Stopwatch.GetTimestamp(), graceful);
            // The writer ends with its queue: BYE gets the goodbye; a connection that ended without one (a crash, a
            // lost network) gets nothing more, and the queue ends too, or the writer would wait for good and the socket,
            // the connection and the address's slot would never be released.
            if (graceful) conn.Close(CloseReason.Left, "Goodbye.");
            else conn.Finish();
            await writer;
        }
        catch (ProtocolException e)
        {
            // A frame that broke a rule inside the room logic (after the connection layer's own checks).
            _log.LogInformation("Relay connection from {Ip} closed: {Reason}", ip, e.Message);
            conn?.Close(CloseReason.Protocol, e.Message);
            if (hub is not null && member is not null && conn is not null)
                lock (hub.Sync) hub.Disconnect(member, conn, Stopwatch.GetTimestamp());
        }
        catch (Exception e) when (e is IOException or SocketException or OperationCanceledException or ObjectDisposedException)
        {
            _log.LogDebug(e, "relay connection from {Ip} ended", ip);
            if (hub is not null && member is not null && conn is not null)
                lock (hub.Sync) hub.Disconnect(member, conn, Stopwatch.GetTimestamp());
        }
        catch (Exception e)
        {
            // A fault in the room logic: logged, the connection closes, the room goes on.
            _log.LogError(e, "Relay connection from {Ip} failed", ip);
            conn?.Close(CloseReason.Protocol, "Internal error.");
            if (hub is not null && member is not null && conn is not null)
                lock (hub.Sync) hub.Disconnect(member, conn, Stopwatch.GetTimestamp());
        }
        finally
        {
            if (conn is null) client.Dispose();
            else conn.Abort();
            Release(ip);
        }
    }

    private RoomHub NewRoom(RoomInfo room)
    {
        var hub = new RoomHub(room, _options, _sealing, _instances, _loggers.CreateLogger("DW4Online.Relay.Room." + room.Code), _mlog,
            job => _ = Task.Run(() => RunStateJobAsync(job)), _sessions.GetOrAdd(room.Code, _ => new RoomSessionState()))
        {
            StorySaver = (owner, story, countersOnly) => StorySaver?.Invoke(owner, story, countersOnly),
        };
        hub.OnSealingStarted(_timer.Wake);
        return hub;
    }

    private SemaphoreSlim? _stateBuilds;
    private SemaphoreSlim StateBuilds => LazyInitializer.EnsureInitialized(ref _stateBuilds, () => new SemaphoreSlim(Math.Max(1, _options.MaxConcurrentStateBuilds)));

    /// <summary>State transfers whose build was skipped because a newer one for the same member replaced them meanwhile.</summary>
    public long StateBuildsSkipped => Interlocked.Read(ref _stateBuildsSkipped);
    private long _stateBuildsSkipped;

    /// <summary>
    /// Builds a state transfer on the thread pool (every block decompressed, without the room lock), then sends it. At
    /// most Relay:MaxConcurrentStateBuilds run at once in the whole relay, and a job a newer one replaced meanwhile is not
    /// built at all (one member's flood of arrivals kept 2.5 cores busy and froze sealed pairs in other rooms).
    /// </summary>
    private async Task RunStateJobAsync(StateJob job)
    {
        var gate = StateBuilds;
        await gate.WaitAsync();
        try
        {
            if (job.Superseded) Interlocked.Increment(ref _stateBuildsSkipped);
            else job.Build();
        }
        catch (Exception e)
        {
            _log.LogError(e, "Building a state transfer failed in room {Room}", job.Hub.Code);
        }
        finally
        {
            gate.Release();
        }
        try
        {
            lock (job.Hub.Sync) job.Hub.FinishState(job);
        }
        catch (Exception e)
        {
            lock (job.Hub.Sync) job.Hub.Fault(e, "a state transfer");
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

    public override void Dispose()
    {
        // A host disposed without StopAsync (its start failed after the relay began, for example on Kestrel's port)
        // finds the sealing thread still running: stop it before its timer goes, or it waits on a disposed handle and
        // takes the process down (an unhandled ObjectDisposedException on that thread).
        _stopping = true;
        _timer.Wake();
        _listener?.Stop();
        base.Dispose(); // cancels ExecuteAsync
        var sealer = Volatile.Read(ref _sealer);
        if (sealer is null || sealer.Join(TimeSpan.FromSeconds(2))) _timer.Dispose(); // a thread that never ends keeps it
        _mlog?.Dispose();
    }
}

/// <summary>One JSON-lines file per room under Relay:DiagnosticsDirectory: every membership event with its server time.</summary>
internal sealed class JsonMembershipLog(string directory) : IMembershipLog, IDisposable
{
    private readonly Dictionary<string, StreamWriter> _files = new(StringComparer.Ordinal);
    private readonly Lock _gate = new();
    private readonly long _start = Stopwatch.GetTimestamp();

    public void Write(string room, IReadOnlyDictionary<string, object?> entry)
    {
        var d = new Dictionary<string, object?>(entry) { ["ms"] = Math.Round((Stopwatch.GetTimestamp() - _start) * 1000.0 / Stopwatch.Frequency, 1) };
        var line = JsonSerializer.Serialize(d);
        lock (_gate)
        {
            if (!_files.TryGetValue(room, out var w))
            {
                Directory.CreateDirectory(directory);
                w = new StreamWriter(Path.Combine(directory, "room_" + room + ".jsonl"), append: true) { AutoFlush = true };
                _files[room] = w;
            }
            w.WriteLine(line);
        }
    }

    public void Dispose()
    {
        lock (_gate)
        {
            foreach (var w in _files.Values) w.Dispose();
            _files.Clear();
        }
    }
}
