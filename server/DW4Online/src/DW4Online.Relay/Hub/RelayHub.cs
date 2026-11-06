using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Story;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

namespace DW4Online.Relay.Hub;

/// <summary>What the handshake asks the world's admission: the redeemed ticket's account, role and nonce.</summary>
public sealed record HubAdmission(long AccountId, string Role, string Nonce);

/// <summary>
/// The DW4R2 relay of the world (SEAMLESS.md 6, 19.17): one TCP connection per member, one hub for the whole server (the
/// generalized room hub, 19.17.4). The first line is "DW4R2 &lt;ticket&gt; 49 &lt;dataHash&gt; &lt;ignored&gt;\n", answered with
/// "OK &lt;memberId&gt; &lt;serverTimeMs&gt;\n" or "ERR protocol|data|ticket|full|busy\n"; then frames (type u8, length u32 LE,
/// payload). A dedicated thread seals the ticks of every sealed stage group at 59.94 Hz; a housekeeping loop pings and enforces
/// the holds, limits and the world's timers. The relay never simulates the game.
/// </summary>
public sealed class RelayHub : BackgroundService
{
    private readonly RelayOptions _options;
    private readonly SealingOptions _sealing;
    private readonly InstanceOptions _instances;
    private readonly WorldHubOptions _worldOptions;
    private readonly RelayTickets _tickets;
    private readonly ILogger<RelayHub> _log;
    private readonly ILoggerFactory _loggers;
    private readonly RoomHub _hub;
    private readonly ConcurrentDictionary<IPAddress, int> _perIp = new();
    private readonly TaskCompletionSource<int> _started = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly HighResolutionTimer _timer = new();
    private readonly JsonMembershipLog? _mlog;
    private volatile bool _stopping;
    private Thread? _sealer;
    private TcpListener? _listener; // bound in StartAsync

    public RelayHub(RelayOptions options, SealingOptions sealing, InstanceOptions instances, RelayTickets tickets, ILoggerFactory loggers,
        WorldHubOptions? world = null)
    {
        _options = options;
        _sealing = sealing;
        _instances = instances;
        _worldOptions = world ?? new WorldHubOptions();
        _tickets = tickets;
        _loggers = loggers;
        _log = loggers.CreateLogger<RelayHub>();
        if (options.DiagnosticsDirectory.Length > 0) _mlog = new JsonMembershipLog(options.DiagnosticsDirectory);
        _hub = new RoomHub(RoomInfo.World, _options, _sealing, _instances, _loggers.CreateLogger("DW4Online.Relay.World"), _mlog,
            job => _ = Task.Run(() => RunStateJobAsync(job)), _worldOptions)
        {
            StorySaver = (owner, story, countersOnly) => StorySaver?.Invoke(owner, story, countersOnly),
            SelectionRunner = f => Task.Run(f),
        };
        _hub.OnSealingStarted(_timer.Wake);
    }

    /// <summary>The bound port once listening (Port 0 in tests).</summary>
    public Task<int> Started => _started.Task;

    /// <summary>The world's hub (every public member of it runs under <c>lock (hub.Sync)</c>).</summary>
    public RoomHub World => _hub;

    public WorldHubOptions WorldOptions => _worldOptions;

    /// <summary>The world's admission of a redeemed ticket (null = refused, ERR ticket). Without it every valid ticket is a plain
    /// player named after its account (relay-only tests).</summary>
    public Func<HubAdmission, ValueTask<HubAdmit?>>? Admission { get; set; }

    /// <summary>Called every few seconds with the accounts connected (accounts.last_seen_at, 19.4).</summary>
    public Func<IReadOnlyCollection<long>, ValueTask>? Heartbeat { get; set; }

    /// <summary>Saves a keeper's new revision as its owner's online story (account, copy, whether only counters changed; called
    /// under the hub lock: only queue).</summary>
    public Action<long, RoomStoryData, bool>? StorySaver { get; set; }

    public bool HighResolutionTimer => _timer.HighResolution;

    /// <summary>Whether the sealing thread runs (diagnostics and tests: it must have ended when Dispose returns).</summary>
    public bool SealingThreadAlive => Volatile.Read(ref _sealer)?.IsAlive == true;

    /// <summary>The stage groups as the last housekeeping pass saw them, or now when the hub's lock is free within 50 ms.</summary>
    public IReadOnlyList<InstanceView> Views()
    {
        if (!Monitor.TryEnter(_hub.Sync, 50)) return _hub.CachedViews;
        try
        {
            return _hub.Views();
        }
        finally
        {
            Monitor.Exit(_hub.Sync);
        }
    }

    /// <summary>Runs <paramref name="f"/> under the hub lock with the relay clock.</summary>
    public T Locked<T>(Func<RoomHub, long, T> f)
    {
        lock (_hub.Sync) return f(_hub, Stopwatch.GetTimestamp());
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
        _log.LogInformation("Relay (DW4R2, the world, protocol {Protocol}) listening on {Address}:{Port}; sealing timer {Kind}", Wire.WorldProtocol,
            _options.ListenAddress, port, _timer.HighResolution ? "high resolution" : "sleep and spin");
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
            lock (_hub.Sync) _hub.CloseAll(CloseReason.ServerStopping, "The server is stopping.");
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
            var next = SealRoom(_hub);
            // Nothing sealed: wait for a wake (a stage group starts sealing) but look again every 50 ms.
            var limit = Stopwatch.GetTimestamp() + Stopwatch.Frequency / 20;
            _timer.WaitUntil(Math.Min(next, limit));
        }
    }

    /// <summary>The sealing thread's passes over the hub, and those that found its lock taken (SEAMLESS.md 19.17.4: with one hub
    /// for the world a miss is world-wide seal jitter). Diagnostics and the load gate.</summary>
    public long SealPasses => Interlocked.Read(ref _sealPasses);
    /// <summary>Frames the relay process wrote right after a hot path's lock (M4 (e), Relay:DirectSends) and through the
    /// thread pool's writer.</summary>
    public static (long Direct, long Pooled, long Sends) FramesWritten => (Connection.DirectFrames, Connection.PooledFrames, Connection.DirectSends);
    public long SealTryMisses => Interlocked.Read(ref _sealTryMisses);
    private long _sealPasses;
    private long _sealTryMisses;

    /// <summary>
    /// One step of the sealing thread: seals the due ticks and returns when it needs the next pass. A hub busy under its lock (a
    /// burst of frames, a state being sent) is not waited for: it seals its due ticks in the next pass a millisecond later (in
    /// one batch), counted in <see cref="SealTryMisses"/>. The pass's time is read once the lock is held.
    /// </summary>
    public long SealRoom(RoomHub hub)
    {
        Interlocked.Increment(ref _sealPasses);
        if (!Monitor.TryEnter(hub.Sync))
        {
            Interlocked.Increment(ref _sealTryMisses);
            return Stopwatch.GetTimestamp() + Stopwatch.Frequency / 1000;
        }
        // (The pass's SEALED frames are written by this thread once the lock is released, M4 (e).)
        Outbox.Enter();
        try
        {
            try
            {
                return hub.Seal(Stopwatch.GetTimestamp());
            }
            catch (Exception e)
            {
                hub.Fault(e, "sealing");
                return long.MaxValue;
            }
            finally
            {
                Monitor.Exit(hub.Sync);
            }
        }
        finally
        {
            Outbox.Exit();
        }
    }

    private async Task HousekeepAsync(CancellationToken stop)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(250));
        var lastBeat = Stopwatch.GetTimestamp();
        try
        {
            while (await timer.WaitForNextTickAsync(stop))
            {
                var now = Stopwatch.GetTimestamp();
                var beat = (now - lastBeat) >= 5 * Stopwatch.Frequency;
                if (beat) lastBeat = now;
                List<long> connected = [];
                Outbox.Enter();
                try
                {
                    lock (_hub.Sync)
                    {
                        try
                        {
                            _hub.Housekeep(now);
                        }
                        catch (Exception e)
                        {
                            _hub.Fault(e, "housekeeping");
                        }
                        foreach (var m in _hub.Members)
                            if (m.Peer is { } p && (now - m.LastHeard) > (long)_options.IdleTimeoutSeconds * Stopwatch.Frequency &&
                                !(m.World is { HostAway: true } w && w.Host == m))
                                p.Close(CloseReason.Timeout, "Nothing heard from this connection.");
                        if (beat) connected = _hub.Members.Where(m => m.Connected).Select(m => m.AccountId).ToList();
                    }
                }
                catch (Exception e)
                {
                    _log.LogError(e, "Relay housekeeping failed");
                }
                finally
                {
                    Outbox.Exit();
                }
                // The heartbeat goes to the account service on its own task: it writes to the database, and a slow or stalled
                // database must never hold up the pings, holds and timeouts.
                if (beat && connected.Count > 0 && Heartbeat is { } h)
                {
                    if (_heartbeats is { IsCompleted: false }) HeartbeatsSkipped++;
                    else
                        _heartbeats = Task.Run(async () =>
                        {
                            try
                            {
                                await h(connected);
                            }
                            catch (Exception e) when (e is not OperationCanceledException)
                            {
                                _log.LogDebug(e, "relay heartbeat failed");
                            }
                        }, CancellationToken.None);
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
            // The address has its connections (a household or a carrier-grade NAT): the client is told why instead of seeing a reset.
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
            // The protocol floor in code (19.17.3): a world relay speaks only round I1's protocol, whatever Play:MinimumProtocol says.
            if (parts.Length != 5 || parts[0] != Wire.Magic ||
                !int.TryParse(parts[2], NumberStyles.None, CultureInfo.InvariantCulture, out var protocol) ||
                !ulong.TryParse(parts[3], NumberStyles.None, CultureInfo.InvariantCulture, out var dataHash) ||
                !ulong.TryParse(parts[4], NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out _) || protocol != Wire.WorldProtocol)
            {
                await ReplyAsync(stream, "ERR protocol", stop);
                return;
            }
            var claims = _tickets.Redeem(parts[1]);
            if (claims is null || claims.Room != Wire.WorldChannel || claims.Role is not (RelayRoles.Member or RelayRoles.Staff))
            {
                await ReplyAsync(stream, "ERR ticket", stop);
                return;
            }
            if (!_worldOptions.AdmitsData(dataHash))
            {
                await ReplyAsync(stream, "ERR data", stop);
                return;
            }
            HubAdmit? admitted;
            if (Admission is { } admit) admitted = await admit(new HubAdmission(claims.Account, claims.Role, claims.Nonce));
            else admitted = new HubAdmit(claims.Account, "member" + claims.Account.ToString(CultureInfo.InvariantCulture)) { Staff = claims.Role == RelayRoles.Staff };
            if (admitted is null)
            {
                await ReplyAsync(stream, "ERR ticket", stop);
                return;
            }
            admitted = admitted with { DataHash = dataHash };
            conn = new Connection(client, _options, _log, ip);
            var full = false;
            lock (_hub.Sync)
            {
                if (_hub.MemberOf(claims.Account) is null && _hub.Members.Count >= _worldOptions.MaxMembers) full = true;
                else
                {
                    var c = conn;
                    member = _hub.Connect(admitted, conn, Stopwatch.GetTimestamp(), id =>
                    {
                        var serverMs = (long)(Stopwatch.GetTimestamp() * 1000.0 / Stopwatch.Frequency);
                        c.Send(Encoding.ASCII.GetBytes(FormattableString.Invariant($"OK {id} {serverMs}\n")));
                    });
                }
            }
            if (full)
            {
                conn = null;
                await ReplyAsync(stream, "ERR full", stop);
                return;
            }
            _log.LogInformation("Relay: account {Account} entered the world as member {Member}", claims.Account, member!.Id);
            var writer = Task.Run(conn.WriteLoopAsync, CancellationToken.None);
            var m = member;
            var graceful = await conn.ReadLoopAsync((type, payload) =>
            {
                // (The frames this one produces are written by this thread once the lock is released, M4 (e).)
                Outbox.Enter();
                try
                {
                    lock (_hub.Sync) _hub.Receive(m, type, payload.Span, Stopwatch.GetTimestamp());
                }
                finally
                {
                    Outbox.Exit();
                }
            });
            lock (_hub.Sync) _hub.Disconnect(member, conn, Stopwatch.GetTimestamp(), graceful);
            // The writer ends with its queue: BYE gets the goodbye; a connection that ended without one (a crash, a lost network)
            // gets nothing more, and the queue ends too, or the writer would wait for good.
            if (graceful) conn.Close(CloseReason.Left, "Goodbye.");
            else conn.Finish();
            await writer;
        }
        catch (ProtocolException e)
        {
            // A frame that broke a rule inside the hub logic (after the connection layer's own checks).
            _log.LogInformation("Relay connection from {Ip} closed: {Reason}", ip, e.Message);
            conn?.Close(CloseReason.Protocol, e.Message);
            if (member is not null && conn is not null)
                lock (_hub.Sync) _hub.Disconnect(member, conn, Stopwatch.GetTimestamp());
        }
        catch (Exception e) when (e is IOException or SocketException or OperationCanceledException or ObjectDisposedException)
        {
            _log.LogDebug(e, "relay connection from {Ip} ended", ip);
            if (member is not null && conn is not null)
                lock (_hub.Sync) _hub.Disconnect(member, conn, Stopwatch.GetTimestamp());
        }
        catch (Exception e)
        {
            // A fault in the hub logic: logged, the connection closes, the world goes on.
            _log.LogError(e, "Relay connection from {Ip} failed", ip);
            conn?.Close(CloseReason.Protocol, "Internal error.");
            if (member is not null && conn is not null)
                lock (_hub.Sync) _hub.Disconnect(member, conn, Stopwatch.GetTimestamp());
        }
        finally
        {
            if (conn is null) client.Dispose();
            else conn.Abort();
            Release(ip);
        }
    }

    private SemaphoreSlim? _stateBuilds;
    private SemaphoreSlim StateBuilds => LazyInitializer.EnsureInitialized(ref _stateBuilds, () => new SemaphoreSlim(Math.Max(1, _options.MaxConcurrentStateBuilds)));

    /// <summary>State transfers whose build was skipped because a newer one for the same member replaced them meanwhile.</summary>
    public long StateBuildsSkipped => Interlocked.Read(ref _stateBuildsSkipped);
    private long _stateBuildsSkipped;

    /// <summary>
    /// Builds a state transfer on the thread pool (every block decompressed, without the hub lock), then sends it. At most
    /// Relay:MaxConcurrentStateBuilds run at once in the whole relay, and a job a newer one replaced meanwhile is not built.
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
            _log.LogError(e, "Building a state transfer failed");
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
        // A host disposed without StopAsync (its start failed after the relay began, for example on Kestrel's port) finds the
        // sealing thread still running: stop it before its timer goes.
        _stopping = true;
        _timer.Wake();
        _listener?.Stop();
        base.Dispose(); // cancels ExecuteAsync
        var sealer = Volatile.Read(ref _sealer);
        if (sealer is null || sealer.Join(TimeSpan.FromSeconds(2))) _timer.Dispose(); // a thread that never ends keeps it
        _mlog?.Dispose();
    }
}

/// <summary>
/// The world's membership log under Relay:DiagnosticsDirectory (the online test stack, i1a-contract.md "web 12:50"): one file
/// world_&lt;run&gt;.jsonl per relay process (&lt;run&gt; = the UTC start yyyyMMddTHHmmss + "_" + the pid), every membership event with
/// its server time and its wid (0 when none).
/// </summary>
internal sealed class JsonMembershipLog(string directory) : IMembershipLog, IDisposable
{
    private readonly Lock _gate = new();
    private readonly long _start = Stopwatch.GetTimestamp();
    private readonly string _run = DateTime.UtcNow.ToString("yyyyMMdd'T'HHmmss", CultureInfo.InvariantCulture) + "_" + Environment.ProcessId;
    private StreamWriter? _file;

    public void Write(string room, IReadOnlyDictionary<string, object?> entry)
    {
        var d = new Dictionary<string, object?>(entry) { ["ms"] = Math.Round((Stopwatch.GetTimestamp() - _start) * 1000.0 / Stopwatch.Frequency, 1) };
        d.TryAdd("wid", 0u);
        var line = JsonSerializer.Serialize(d);
        lock (_gate)
        {
            if (_file is null)
            {
                Directory.CreateDirectory(directory);
                _file = new StreamWriter(Path.Combine(directory, "world_" + _run + ".jsonl"), append: true) { AutoFlush = true };
            }
            _file.WriteLine(line);
        }
    }

    public void Dispose()
    {
        lock (_gate)
        {
            _file?.Dispose();
            _file = null;
        }
    }
}
