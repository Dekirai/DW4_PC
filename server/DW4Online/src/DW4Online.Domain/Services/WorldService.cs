using System.Collections.Concurrent;
using System.Text.Json;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.World;

namespace DW4Online.Domain.Services;

/// <summary>
/// What POST /world/enter and /world/ticket hand out (19.17.3): the one-time relay ticket, the ticket's role (member or
/// staff), the account's role (player, moderator, admin), the bound box, the preferences and the world's protocol.
/// </summary>
public sealed record WorldEntry(string Ticket, string Role, int Box, OnlinePrefs Prefs, int Protocol, string AccountRole = Roles.Player);

/// <summary>The world's counts for the home page, GET /world and the admin index (no names).</summary>
public sealed record WorldCounts(int PlayersOnline, int Instances, IReadOnlyList<AreaCount> Areas);

public sealed record AreaCount(string Area, string Name, int Instances, int Players);

/// <summary>An instance as GET /instances and the website's /instances list it for one viewer.</summary>
public sealed record InstanceListing(uint Wid, string Host, string Area, string AreaName, string Stage, string StageName, int Players, int MaxPlayers,
    string? Visibility, int Difficulty, bool Randomizer, string StoryLabel, int AgeSeconds, bool Yours, IReadOnlyList<string> Names);

/// <summary>
/// Where a friend is, as his whereabouts preference lets the viewer see it (19.20): online in the lobby, playing in an
/// area (a Private instance only "Playing"), or offline with the time he was last online. Null: he shows nothing.
/// </summary>
public sealed record FriendPresence(bool Online, string Status, string? Area, string? Stage, DateTime? LastSeenAt);

/// <summary>
/// The world's service side (SEAMLESS.md 19.17.2, round I1a): the world session's admission (POST /world/enter and
/// /world/ticket hand out one-time relay tickets; the relay asks <see cref="AdmitAsync"/> before it accepts one), the
/// counts and the instance lists from the relay's browser snapshot, the play context for achievements, the online
/// preferences, the staff actions on instances (End, Remove, audited), and what the account and story services must
/// tell the relay (<see cref="IWorldNotifier.AccountChanged"/>, <see cref="IWorldNotifier.StoryReplaced"/>). It takes
/// over RoomService's remaining jobs; the relay's in-memory state stays the relay's.
/// </summary>
public sealed class WorldService
{
    private readonly IWorldRelay _relay;
    private readonly IWorldNotifier _notifier;
    private readonly IWorldRepository _world;
    private readonly IInstanceHistoryRepository _history;
    private readonly IAccountRepository _accounts;
    private readonly ISocialRepository _social;
    private readonly IAuditLog _audit;
    private readonly VersionGate _versions;
    private readonly WorldWebOptions _options;
    private readonly ServiceOptions _service;
    private readonly TimeProvider _time;
    private readonly Story.OnlineStoryService? _stories;
    private readonly HashSet<ulong> _dataHashes;
    // The newest ticket's nonce per account (one world session per account, 19.17.3): an older ticket is refused. NewSession:
    // the ticket came from /world/enter (Play), not from /world/ticket (a reconnect).
    private readonly ConcurrentDictionary<long, (string Nonce, bool NewSession)> _nonces = new();

    public WorldService(IWorldRelay relay, IWorldNotifier notifier, IWorldRepository world, IInstanceHistoryRepository history, IAccountRepository accounts,
        ISocialRepository social, IAuditLog audit, VersionGate versions, WorldWebOptions options, ServiceOptions service, TimeProvider time,
        Story.OnlineStoryService? stories = null)
    {
        _relay = relay;
        _notifier = notifier;
        _world = world;
        _history = history;
        _accounts = accounts;
        _social = social;
        _audit = audit;
        _versions = versions;
        _options = options;
        _service = service;
        _time = time;
        _stories = stories;
        _dataHashes = options.DataHashes.Select(h => WorldWebOptions.ParseHash(h, "World:DataHashes")).ToHashSet();
        _relay.SetAdmission(AdmitAsync);
        if (_relay is IWorldVisibilitySink sink) sink.RememberVisibility = (account, visibility) => RememberVisibilityAsync(account, visibility);
    }

    /// <summary>The story box a world session plays (one box until round D's boxes; 19.17.3).</summary>
    public const int BoundBox = 1;

    /// <summary>How long the relay's ticket check waits for the database before it refuses (the client asks for a new ticket).</summary>
    public TimeSpan AdmissionTimeout { get; init; } = TimeSpan.FromSeconds(5);

    /// <summary>Called when a background write failed (what, error): keeping a session start, a visibility, last seen.</summary>
    public Action<string, Exception>? WriteFailed { get; set; }

    public bool RelayAvailable => _relay.Available;

    // --- The world session ---------------------------------------------------------------------------------------------------

    /// <summary>
    /// POST /world/enter: the version gate (the world's protocol floor), maintenance, the relay, the game data
    /// (World:DataHashes), then a one-time relay ticket with the role from the account's database role, the preferences and
    /// the bound box. The story the session starts from is kept in the story history (reason session), as a room's was.
    /// </summary>
    public async Task<Result<WorldEntry>> EnterAsync(AuthenticatedSession session, int? protocol, ulong? dataHash, string? clientVersion,
        CancellationToken ct = default)
    {
        var entry = await IssueAsync(session, protocol, dataHash, clientVersion, newSession: true, ct);
        if (!entry.Ok || _stories is null) return entry;
        try
        {
            await _stories.KeepSessionStartAsync(session.AccountId, ct);
        }
        catch (Exception e) when (e is not OperationCanceledException)
        {
            WriteFailed?.Invoke($"keeping the session start of account {session.AccountId}", e);
        }
        return entry;
    }

    /// <summary>POST /world/ticket: the same checks and a new one-time ticket (a reconnect); the old ticket stops counting.</summary>
    public Task<Result<WorldEntry>> TicketAsync(AuthenticatedSession session, int? protocol, ulong? dataHash, string? clientVersion,
        CancellationToken ct = default) => IssueAsync(session, protocol, dataHash, clientVersion, newSession: false, ct);

    /// <summary>
    /// The checks of /world/enter and /world/ticket. The version gate applies when the client names its protocol or version
    /// (as the login does; the relay's handshake refuses any protocol but the world's anyway); the game data when a hash is given.
    /// </summary>
    private async Task<Result<WorldEntry>> IssueAsync(AuthenticatedSession session, int? protocol, ulong? dataHash, string? clientVersion,
        bool newSession, CancellationToken ct)
    {
        if ((protocol is not null || clientVersion is not null) && _versions.Check(protocol, clientVersion) is { } outdated) return outdated;
        if (_service.Maintenance) return new DomainError(ErrorKind.Unavailable, "maintenance", "The server is in maintenance: the world is closed for now.");
        if (!_relay.Available) return new DomainError(ErrorKind.Unavailable, "relay_unavailable", "The relay is switched off on this server.");
        if (_dataHashes.Count > 0 && dataHash is { } hash && !_dataHashes.Contains(hash))
            return DomainError.Conflict("unsupported_data", "Your game data is not supported on this server.");
        var account = await _accounts.GetAsync(session.AccountId, ct);
        if (account is null || account.Status != AccountStatus.Active)
            return DomainError.Forbidden("account_inactive", "This account was locked, banned or deleted.");
        var prefs = await _world.GetPrefsAsync(session.AccountId, ct);
        var role = WorldRoles.Of(account.Role);
        WorldTicket ticket;
        try
        {
            ticket = _relay.IssueTicket(session.AccountId, role);
        }
        catch (Exception e) when (e is ArgumentException or InvalidOperationException)
        {
            return new DomainError(ErrorKind.Unavailable, "relay_ticket_failed", "The relay could not issue a ticket. Try again.");
        }
        _nonces[session.AccountId] = (ticket.Nonce, newSession);
        return new WorldEntry(ticket.Ticket, role, BoundBox, prefs, _versions.MinimumProtocol, account.Role);
    }

    /// <summary>POST /world/leave: ends the account's world session (the relay closes its connection); its tickets stop counting.</summary>
    public async Task LeaveAsync(long accountId, CancellationToken ct = default)
    {
        _nonces.TryRemove(accountId, out _);
        var left = _relay.Leave(accountId);
        if (left) await TouchLastSeenSafeAsync([accountId], ct);
    }

    /// <summary>
    /// The relay's check of a redeemed world ticket (signature, expiry and single use already checked by the relay): the
    /// newest ticket issued to the account in this process, the account active, its role re-read (a demotion counts at once,
    /// 19.14), its preferences, friends and blocks loaded here, outside the hub's lock. The same (account, nonce) answers
    /// the same way again while it is the account's newest: the relay calls it again to re-check a member after
    /// <see cref="IWorldNotifier.AccountChanged"/> and the other notifier calls. Null refuses it (also a hanging database,
    /// after <see cref="AdmissionTimeout"/>).
    /// </summary>
    public async ValueTask<WorldAdmitted?> AdmitAsync(WorldAdmission ticket)
    {
        if (!_nonces.TryGetValue(ticket.AccountId, out var issued) || !string.Equals(issued.Nonce, ticket.Nonce, StringComparison.Ordinal)) return null;
        using var timeout = new CancellationTokenSource(AdmissionTimeout);
        try
        {
            var account = await _accounts.GetAsync(ticket.AccountId, timeout.Token);
            if (account is null || account.Status != AccountStatus.Active) return null;
            var prefs = await _world.GetPrefsAsync(account.Id, timeout.Token);
            var friends = await _social.ListFriendsAsync(account.Id, timeout.Token);
            var blocks = await _social.ListBlocksAsync(account.Id, timeout.Token);
            var blockedBy = await _world.ListBlockedByAsync(account.Id, timeout.Token);
            await TouchLastSeenSafeAsync([account.Id], timeout.Token);
            return new WorldAdmitted(account.Id, account.Username, WorldRoles.Of(account.Role), BoundBox, prefs)
            {
                NewSession = issued.NewSession,
                Friends = friends.Select(f => f.AccountId).ToList(),
                Blocks = blocks.Select(b => b.AccountId).ToList(),
                BlockedBy = blockedBy,
            };
        }
        catch (OperationCanceledException)
        {
            return null;
        }
    }

    // --- What the website and the API show --------------------------------------------------------------------------------

    public WorldSnapshot Snapshot() => _relay.Snapshot();

    /// <summary>Players online and instances per area (every instance counts; no names). The home page, GET /world, the admin index.</summary>
    public WorldCounts Counts()
    {
        var snapshot = _relay.Snapshot();
        var areas = AreaTable.Areas.Select(a =>
        {
            var rows = snapshot.Instances.Where(i => i.Area == a.Key).ToList();
            return new AreaCount(a.Key, a.Name, rows.Count, rows.Sum(i => i.Members.Count));
        }).ToList();
        return new WorldCounts(Math.Max(snapshot.MembersOnline, snapshot.Online.Count(o => !o.Spectating)), snapshot.Instances.Count, areas);
    }

    /// <summary>The viewer's friends and blocks (both directions) for Visible().</summary>
    public async Task<WorldViewer> ViewerAsync(long accountId, string role, CancellationToken ct = default)
    {
        var friends = (await _social.ListFriendsAsync(accountId, ct)).Select(f => f.AccountId).ToHashSet();
        var blocks = (await _social.ListBlocksAsync(accountId, ct)).Select(b => b.AccountId).ToHashSet();
        blocks.UnionWith(await _world.ListBlockedByAsync(accountId, ct));
        return new WorldViewer(accountId, role is Roles.Moderator or Roles.Admin, friends, blocks);
    }

    /// <summary>
    /// The instances the viewer may see (Visible(), 19.7.2), optionally of one area, newest first: GET /instances and the
    /// website's /instances. The visibility is only shown to staff.
    /// </summary>
    public IReadOnlyList<InstanceListing> Instances(WorldViewer viewer, string? area = null)
    {
        var snapshot = _relay.Snapshot();
        var now = Clock.Now(_time);
        return snapshot.Instances
            .Where(i => (string.IsNullOrEmpty(area) || i.Area == area) && WorldVisibility.Visible(i, viewer))
            .OrderByDescending(i => i.StartedAtUtc).ThenBy(i => i.Wid)
            .Select(i => new InstanceListing(i.Wid, i.HostName, i.Area, AreaTable.AreaName(i.Area), i.HostStage, AreaTable.Default.DisplayName(i.HostStage),
                i.Members.Count, 4, viewer.Staff ? i.Visibility : null, i.Difficulty, i.Randomizer, i.StoryLabel,
                (int)Math.Max(0, (now - i.StartedAtUtc).TotalSeconds), i.Members.Any(m => m.AccountId == viewer.AccountId),
                i.Members.Select(m => m.Name).ToList()))
            .ToList();
    }

    /// <summary>
    /// Where friends are (GET /friends' presence, the friends page, profiles), as each one's whereabouts preference allows
    /// (19.20): "nobody" shows nothing (null); online: "In the lobby" (at home, at the title, or watching as staff, 19.14),
    /// "Playing" in a Private instance, else "Playing in &lt;area&gt;" with the stage; offline: the last time online.
    /// The caller has checked that the viewer is their friend.
    /// </summary>
    public async Task<IReadOnlyDictionary<long, FriendPresence?>> PresenceAsync(IReadOnlyCollection<long> friendIds, CancellationToken ct = default)
    {
        var result = new Dictionary<long, FriendPresence?>();
        if (friendIds.Count == 0) return result;
        var prefs = (await _world.ListPresencePrefsAsync(friendIds, ct)).ToDictionary(p => p.AccountId);
        var snapshot = _relay.Snapshot();
        var online = snapshot.Online.GroupBy(o => o.AccountId).ToDictionary(g => g.Key, g => g.First());
        var instances = snapshot.Instances.ToDictionary(i => i.Wid);
        foreach (var id in friendIds)
        {
            if (!prefs.TryGetValue(id, out var p) || p.Whereabouts == OnlinePrefs.WhereaboutsNobody)
            {
                result[id] = null;
                continue;
            }
            if (!online.TryGetValue(id, out var here))
            {
                result[id] = new FriendPresence(false, "Offline", null, null, p.LastSeenAt);
                continue;
            }
            if (here.Spectating || here.Wid == 0 || !instances.TryGetValue(here.Wid, out var instance))
            {
                result[id] = new FriendPresence(true, AreaTable.Default.IsHub(here.Stage) ? "In the lobby" : "Online", null, null, null);
                continue;
            }
            result[id] = instance.Visibility == Visibilities.Private
                ? new FriendPresence(true, "Playing", null, null, null)
                : new FriendPresence(true, $"Playing in {AreaTable.AreaName(instance.Area)}", instance.Area, AreaTable.Default.DisplayName(here.Stage), null);
        }
        return result;
    }

    /// <summary>The play context for achievements (19.8.9): null when the account was not in the world within the last minute.</summary>
    public WorldPlayContext? PlayContext(long accountId) => _relay.PlayContext(accountId);

    // --- Preferences --------------------------------------------------------------------------------------------------------

    public Task<OnlinePrefs> GetPrefsAsync(long accountId, CancellationToken ct = default) => _world.GetPrefsAsync(accountId, ct);

    /// <summary>PUT /me/online-prefs: every field optional; the relay hears of it (<see cref="IWorldNotifier.PrefsChanged"/>).</summary>
    public async Task<Result<OnlinePrefs>> UpdatePrefsAsync(long accountId, JsonElement body, CancellationToken ct = default)
    {
        var current = await _world.GetPrefsAsync(accountId, ct);
        var updated = current.Apply(body);
        if (!updated.Ok) return updated.Error!;
        return await SavePrefsAsync(accountId, updated.Value!, ct);
    }

    public async Task<Result<OnlinePrefs>> SavePrefsAsync(long accountId, OnlinePrefs prefs, CancellationToken ct = default)
    {
        if (!prefs.IsValid) return DomainError.Invalid("invalid_prefs", "A preference is out of range.");
        if (!await _world.SavePrefsAsync(accountId, prefs, Clock.Now(_time), ct))
            return DomainError.Forbidden("account_inactive", "This account was deleted.");
        _notifier.PrefsChanged(accountId);
        return prefs;
    }

    /// <summary>
    /// The host's visibility choice becomes his default for the next instance (Q9, 19.6.2): the relay calls it off its
    /// lock when a host changes his instance's visibility. Never throws (a failure is reported to <see cref="WriteFailed"/>).
    /// </summary>
    public async Task RememberVisibilityAsync(long accountId, string visibility, CancellationToken ct = default)
    {
        if (!Visibilities.IsValid(visibility)) return;
        try
        {
            await _world.SaveVisibilityAsync(accountId, visibility, Clock.Now(_time), ct);
        }
        catch (Exception e) when (e is not OperationCanceledException)
        {
            WriteFailed?.Invoke($"remembering the visibility of account {accountId}", e);
        }
    }

    // --- Account and story changes ----------------------------------------------------------------------------------------

    /// <summary>
    /// The account's role or status changed, or it was deleted (AccountEndpoints, AdminEndpoints, the website's account and
    /// admin pages): its tickets stop counting and the relay re-checks it at once (a ban or a deletion closes its
    /// connection and, for a host, ends his instance; 19.14).
    /// </summary>
    public void AccountChanged(long accountId) => _notifier.AccountChanged(accountId);

    /// <summary>The account's online story of a box was reset or restored (19.8.1): the relay's keeper reloads it, a hosted instance ends.</summary>
    public void StoryReplaced(long accountId, int box = 1) => _notifier.StoryReplaced(accountId, box);

    // --- Staff --------------------------------------------------------------------------------------------------------------

    /// <summary>Staff: every instance, Private ones included, with members and spectators (/admin/instances).</summary>
    public IReadOnlyList<WorldInstanceRow> AdminInstances() =>
        _relay.Snapshot().Instances.OrderByDescending(i => i.StartedAtUtc).ThenBy(i => i.Wid).ToList();

    /// <summary>Staff ends an instance (END reason staff): audit row staff_end_instance with the instance, host, visibility and members.</summary>
    public async Task<Result<bool>> EndInstanceAsync(long actorId, uint wid, byte[]? ip, CancellationToken ct = default)
    {
        var row = _relay.Snapshot().Instances.FirstOrDefault(i => i.Wid == wid);
        if (row is null || !_relay.EndInstance(wid, actorId)) return InstanceNotFound();
        await _audit.WriteAsync("staff_end_instance", row.HostAccountId, actorId, ip, AuditDetail(row), Clock.Now(_time), ct);
        return true;
    }

    /// <summary>Staff removes a member (it goes home; round I1b adds the 10-minute bar): audit row staff_remove.</summary>
    public async Task<Result<string>> RemoveMemberAsync(long actorId, uint wid, string? username, byte[]? ip, CancellationToken ct = default)
    {
        var row = _relay.Snapshot().Instances.FirstOrDefault(i => i.Wid == wid);
        if (row is null) return InstanceNotFound();
        var member = row.Members.Concat(row.Spectators).FirstOrDefault(m => string.Equals(m.Name, username?.Trim(), StringComparison.OrdinalIgnoreCase));
        if (member is null || !_relay.RemoveMember(wid, member.AccountId, actorId))
            return DomainError.NotFound("member_not_found", "No such player in this instance.");
        await _audit.WriteAsync("staff_remove", member.AccountId, actorId, ip,
            new { wid, historyId = row.HistoryId, host = row.HostName, member = member.Name, role = member.Role }, Clock.Now(_time), ct);
        return member.Name;
    }

    private static object AuditDetail(WorldInstanceRow row) => new
    {
        wid = row.Wid,
        historyId = row.HistoryId,
        host = row.HostName,
        visibility = row.Visibility,
        area = row.Area,
        stage = row.HostStage,
        members = row.Members.Select(m => m.Name).ToList(),
    };

    private static DomainError InstanceNotFound() => DomainError.NotFound("instance_not_found", "No such instance (it may have ended).");

    // --- History and housekeeping -----------------------------------------------------------------------------------------

    /// <summary>The account's instances, newest first (the admin account page with spectating rows, the player's own without).</summary>
    public Task<IReadOnlyList<InstanceHistoryEntry>> HistoryAsync(long accountId, int limit, bool withSpectating, CancellationToken ct = default) =>
        _world.ListHistoryAsync(accountId, limit, withSpectating, ct);

    /// <summary>"Recently played with" (19.26): players only, the last World:RecentlyPlayedDays days.</summary>
    public Task<IReadOnlyList<PlayedWithRow>> PlayedWithAsync(long accountId, int limit, CancellationToken ct = default) =>
        _world.ListPlayedWithAsync(accountId, Clock.Now(_time).AddDays(-Math.Clamp(_options.RecentlyPlayedDays, 1, Math.Max(1, _options.HistoryDays))), limit, ct);

    public Task<DateTime?> LastSeenAsync(long accountId, CancellationToken ct = default) => _world.GetLastSeenAsync(accountId, ct);

    /// <summary>At the service start: instances a previous process left open end with server_stop.</summary>
    public Task<int> CloseLeftoversAsync(CancellationToken ct = default) => _history.CloseOpenAsync(Clock.Now(_time), ct);

    /// <summary>Every member in the world counts as seen now (the housekeeping, once a minute).</summary>
    public async Task<int> TouchOnlineAsync(CancellationToken ct = default)
    {
        var online = _relay.Snapshot().Online.Select(o => o.AccountId).Distinct().ToList();
        await TouchLastSeenSafeAsync(online, ct);
        return online.Count;
    }

    /// <summary>Deletes ended instances older than World:HistoryDays with their members.</summary>
    public Task<int> PruneHistoryAsync(CancellationToken ct = default) =>
        _history.PruneAsync(Clock.Now(_time).AddDays(-Math.Max(1, _options.HistoryDays)), ct);

    private async Task TouchLastSeenSafeAsync(IReadOnlyCollection<long> accounts, CancellationToken ct)
    {
        try
        {
            await _world.TouchLastSeenAsync(accounts, Clock.Now(_time), ct);
        }
        catch (Exception e) when (e is not OperationCanceledException)
        {
            WriteFailed?.Invoke("updating last seen", e);
        }
    }
}
