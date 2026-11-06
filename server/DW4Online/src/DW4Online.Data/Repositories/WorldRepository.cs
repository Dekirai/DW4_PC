using Dapper;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.World;

namespace DW4Online.Data.Repositories;

/// <summary>
/// The world's tables (migration 0013, SEAMLESS.md 19.17.1): online_prefs, accounts.last_seen_at, instance_history and
/// instance_history_members. Rows are never written for a deleted account (the scrub keeps the anonymous account row).
/// </summary>
public sealed class WorldRepository(Database db) : IWorldRepository, IInstanceHistoryRepository
{
    // --- online_prefs ----------------------------------------------------------------------------------------------------

    private sealed class PrefsRow
    {
        public string DefaultVisibility { get; init; } = Visibilities.Public;
        public string Invites { get; init; } = OnlinePrefs.InvitesAnyone;
        public string Whereabouts { get; init; } = OnlinePrefs.WhereaboutsFriends;
        public bool FriendToasts { get; init; } = true;
        public int CameraMode { get; init; }
        public int LightMode { get; init; } = 1;
        public bool FastLoading { get; init; }
        public long? RandomizerSeed { get; init; }
        public long RandomizerModes { get; init; }
        public int LastBox { get; init; } = 1;
    }

    public async Task<OnlinePrefs> GetPrefsAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        var row = await c.QuerySingleOrDefaultAsync<PrefsRow>(new CommandDefinition(
            "SELECT default_visibility, invites, whereabouts, friend_toasts, camera_mode, light_mode, fast_loading, " +
            "CAST(randomizer_seed AS SIGNED) AS randomizer_seed, CAST(randomizer_modes AS SIGNED) AS randomizer_modes, last_box " +
            "FROM online_prefs WHERE account_id = @accountId", new { accountId }, cancellationToken: ct));
        return row is null
            ? OnlinePrefs.Default
            : new OnlinePrefs(row.DefaultVisibility, row.Invites, row.Whereabouts, row.FriendToasts, row.CameraMode, row.LightMode, row.FastLoading,
                row.RandomizerSeed is { } seed ? (uint)seed : null, (uint)row.RandomizerModes, row.LastBox);
    }

    public async Task<bool> SavePrefsAsync(long accountId, OnlinePrefs prefs, DateTime now, CancellationToken ct = default)
    {
        if (!prefs.IsValid) throw new ArgumentException("Preferences out of range.", nameof(prefs));
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteAsync(new CommandDefinition(
            "INSERT INTO online_prefs (account_id, default_visibility, invites, whereabouts, friend_toasts, camera_mode, light_mode, fast_loading, " +
            "randomizer_seed, randomizer_modes, last_box, updated_at) " +
            "SELECT @accountId, @vis, @invites, @where, @toasts, @camera, @light, @fast, @seed, @modes, @box, @now FROM accounts " +
            "WHERE id = @accountId AND status <> 'deleted' " +
            "ON DUPLICATE KEY UPDATE default_visibility = VALUES(default_visibility), invites = VALUES(invites), whereabouts = VALUES(whereabouts), " +
            "friend_toasts = VALUES(friend_toasts), camera_mode = VALUES(camera_mode), light_mode = VALUES(light_mode), " +
            "fast_loading = VALUES(fast_loading), randomizer_seed = VALUES(randomizer_seed), randomizer_modes = VALUES(randomizer_modes), " +
            "last_box = VALUES(last_box), updated_at = VALUES(updated_at)",
            new
            {
                accountId,
                vis = prefs.DefaultVisibility,
                invites = prefs.Invites,
                where = prefs.Whereabouts,
                toasts = prefs.FriendToasts,
                camera = prefs.CameraMode,
                light = prefs.LightMode,
                fast = prefs.FastLoading,
                seed = prefs.RandomizerSeed is { } s ? (long?)s : null,
                modes = (long)prefs.RandomizerModes,
                box = prefs.LastBox,
                now,
            }, cancellationToken: ct)) > 0;
    }

    public async Task<bool> SaveVisibilityAsync(long accountId, string visibility, DateTime now, CancellationToken ct = default)
    {
        if (!Visibilities.IsValid(visibility)) throw new ArgumentException("Unknown visibility.", nameof(visibility));
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteAsync(new CommandDefinition(
            "INSERT INTO online_prefs (account_id, default_visibility, updated_at) SELECT @accountId, @visibility, @now FROM accounts " +
            "WHERE id = @accountId AND status <> 'deleted' " +
            "ON DUPLICATE KEY UPDATE default_visibility = VALUES(default_visibility), updated_at = VALUES(updated_at)",
            new { accountId, visibility, now }, cancellationToken: ct)) > 0;
    }

    // --- last seen, blocks --------------------------------------------------------------------------------------------------

    public async Task TouchLastSeenAsync(IReadOnlyCollection<long> accountIds, DateTime at, CancellationToken ct = default)
    {
        if (accountIds.Count == 0) return;
        await using var c = await db.OpenAsync(ct);
        foreach (var chunk in accountIds.Distinct().Chunk(500))
            await c.ExecuteAsync(new CommandDefinition(
                "UPDATE accounts SET last_seen_at = @at WHERE id IN @ids AND status <> 'deleted' AND (last_seen_at IS NULL OR last_seen_at < @at)",
                new { ids = chunk, at }, cancellationToken: ct));
    }

    public async Task<DateTime?> GetLastSeenAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.QuerySingleOrDefaultAsync<DateTime?>(new CommandDefinition(
            "SELECT last_seen_at FROM accounts WHERE id = @accountId", new { accountId }, cancellationToken: ct));
    }

    public async Task<IReadOnlyList<StaffAuditRow>> ListStaffAuditAsync(long? actorId, string? auditEvent, int limit, CancellationToken ct = default)
    {
        var events = auditEvent is null ? StaffAuditRow.Events : StaffAuditRow.Events.Where(e => e == auditEvent).ToArray();
        if (events.Length == 0) return [];
        await using var c = await db.OpenAsync(ct);
        return (await c.QueryAsync<StaffAuditRow>(new CommandDefinition(
            "SELECT CAST(l.id AS SIGNED) AS id, l.at, l.event, s.username AS staff, p.username AS player, l.detail FROM audit_log l " +
            "LEFT JOIN accounts s ON s.id = l.actor_id LEFT JOIN accounts p ON p.id = l.account_id " +
            "WHERE l.event IN @events AND (@actor IS NULL OR l.actor_id = @actor) ORDER BY l.id DESC LIMIT @limit",
            new { events, actor = actorId, limit = Math.Clamp(limit, 1, 1000) }, cancellationToken: ct))).ToList();
    }

    public async Task<IReadOnlyList<PresencePrefsRow>> ListPresencePrefsAsync(IReadOnlyCollection<long> accountIds, CancellationToken ct = default)
    {
        if (accountIds.Count == 0) return [];
        await using var c = await db.OpenAsync(ct);
        var rows = new List<PresencePrefsRow>();
        foreach (var chunk in accountIds.Distinct().Chunk(500))
            rows.AddRange(await c.QueryAsync<PresencePrefsRow>(new CommandDefinition(
                "SELECT CAST(a.id AS SIGNED) AS account_id, COALESCE(p.whereabouts, 'friends') AS whereabouts, a.last_seen_at " +
                "FROM accounts a LEFT JOIN online_prefs p ON p.account_id = a.id WHERE a.id IN @ids",
                new { ids = chunk }, cancellationToken: ct)));
        return rows;
    }

    public async Task<IReadOnlyList<long>> ListBlockedByAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return (await c.QueryAsync<long>(new CommandDefinition(
            "SELECT CAST(account_id AS SIGNED) FROM account_blocks WHERE blocked_account_id = @accountId", new { accountId }, cancellationToken: ct))).ToList();
    }

    // --- history: queries ---------------------------------------------------------------------------------------------------

    public async Task<IReadOnlyList<InstanceHistoryEntry>> ListHistoryAsync(long accountId, int limit, bool withSpectating, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return (await c.QueryAsync<InstanceHistoryEntry>(new CommandDefinition(
            "SELECT CAST(h.id AS SIGNED) AS id, u.username AS host_name, h.first_stage, h.visibility, h.difficulty, h.started_at, h.ended_at, " +
            "h.end_reason, h.peak_players, m.role, m.joined_at, m.left_at, m.leave_reason " +
            "FROM instance_history_members m JOIN instance_history h ON h.id = m.instance_id JOIN accounts u ON u.id = h.host_account_id " +
            "WHERE m.account_id = @accountId AND (@spectating OR m.role <> 'spectator') ORDER BY m.joined_at DESC LIMIT @limit",
            new { accountId, limit = Math.Clamp(limit, 1, 500), spectating = withSpectating }, cancellationToken: ct))).ToList();
    }

    public async Task<IReadOnlyList<PlayedWithRow>> ListPlayedWithAsync(long accountId, DateTime since, int limit, CancellationToken ct = default)
    {
        // Two member rows of one instance whose times overlap; spectators never count on either side (19.14).
        await using var c = await db.OpenAsync(ct);
        return (await c.QueryAsync<PlayedWithRow>(new CommandDefinition(
            "SELECT CAST(o.account_id AS SIGNED) AS account_id, a.username, " +
            "MAX(LEAST(COALESCE(o.left_at, UTC_TIMESTAMP(3)), COALESCE(m.left_at, UTC_TIMESTAMP(3)))) AS last_played_at, " +
            "COUNT(DISTINCT o.instance_id) AS times " +
            "FROM instance_history_members m JOIN instance_history_members o ON o.instance_id = m.instance_id AND o.account_id <> m.account_id " +
            "JOIN accounts a ON a.id = o.account_id " +
            "WHERE m.account_id = @accountId AND m.role <> 'spectator' AND o.role <> 'spectator' AND a.status <> 'deleted' " +
            "AND o.joined_at <= COALESCE(m.left_at, UTC_TIMESTAMP(3)) AND m.joined_at <= COALESCE(o.left_at, UTC_TIMESTAMP(3)) " +
            "AND COALESCE(m.left_at, UTC_TIMESTAMP(3)) >= @since " +
            "GROUP BY o.account_id, a.username ORDER BY last_played_at DESC LIMIT @limit",
            new { accountId, since, limit = Math.Clamp(limit, 1, 200) }, cancellationToken: ct))).ToList();
    }

    // --- history: the relay's queue ----------------------------------------------------------------------------------------

    public async Task<long> StartAsync(InstanceHistoryStart s, CancellationToken ct = default)
    {
        if (!Visibilities.IsValid(s.Visibility)) throw new ArgumentException("Unknown visibility.", nameof(s));
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteScalarAsync<long>(new CommandDefinition(
            "INSERT INTO instance_history (host_account_id, host_box, first_stage, visibility, difficulty, story_phase, settings, started_at) " +
            "VALUES (@host, @box, @stage, @visibility, @difficulty, @phase, @settings, @at); SELECT CAST(LAST_INSERT_ID() AS SIGNED)",
            new
            {
                host = s.HostAccountId,
                box = Math.Clamp(s.HostBox, 1, 4),
                stage = Truncate(s.FirstStage.ToLowerInvariant(), 16),
                visibility = s.Visibility,
                difficulty = Math.Clamp(s.Difficulty, 0, 2),
                phase = Math.Clamp(s.StoryPhase, 0, 255),
                settings = Truncate(s.Settings, 200),
                at = s.StartedAt,
            }, cancellationToken: ct));
    }

    public async Task EndAsync(long id, string endReason, int peakPlayers, DateTime endedAt, CancellationToken ct = default)
    {
        if (!InstanceEndReasons.All.Contains(endReason)) throw new ArgumentException("Unknown end reason.", nameof(endReason));
        await using var c = await db.OpenAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE instance_history SET ended_at = @endedAt, end_reason = @endReason, peak_players = GREATEST(peak_players, @peak) " +
            "WHERE id = @id AND ended_at IS NULL",
            new { id, endedAt, endReason, peak = Math.Clamp(peakPlayers, 1, 4) }, tx, cancellationToken: ct));
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE instance_history_members SET left_at = @endedAt, leave_reason = 'ended' WHERE instance_id = @id AND left_at IS NULL",
            new { id, endedAt }, tx, cancellationToken: ct));
        await tx.CommitAsync(ct);
    }

    public async Task MemberJoinedAsync(long id, long accountId, int box, string role, DateTime joinedAt, CancellationToken ct = default)
    {
        if (!InstanceRoles.All.Contains(role)) throw new ArgumentException("Unknown role.", nameof(role));
        await using var c = await db.OpenAsync(ct);
        // (A deleted account writes nothing; a second join at the same millisecond is the same row.)
        await c.ExecuteAsync(new CommandDefinition(
            "INSERT IGNORE INTO instance_history_members (instance_id, account_id, box, role, joined_at) " +
            "SELECT @id, @accountId, @box, @role, @joinedAt FROM accounts WHERE id = @accountId AND status <> 'deleted'",
            new { id, accountId, box = Math.Clamp(box, 1, 4), role, joinedAt }, cancellationToken: ct));
    }

    public async Task MemberLeftAsync(long id, long accountId, DateTime joinedAt, string leaveReason, DateTime leftAt, bool sharedStory,
        CancellationToken ct = default)
    {
        if (!InstanceLeaveReasons.All.Contains(leaveReason)) throw new ArgumentException("Unknown leave reason.", nameof(leaveReason));
        await using var c = await db.OpenAsync(ct);
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE instance_history_members SET left_at = @leftAt, leave_reason = @leaveReason, shared_story = shared_story OR @sharedStory " +
            "WHERE instance_id = @id AND account_id = @accountId AND joined_at = @joinedAt AND left_at IS NULL",
            new { id, accountId, joinedAt, leaveReason, leftAt, sharedStory }, cancellationToken: ct));
    }

    public async Task<int> CloseOpenAsync(DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE instance_history_members m JOIN instance_history h ON h.id = m.instance_id SET m.left_at = @now, m.leave_reason = 'ended' " +
            "WHERE h.ended_at IS NULL AND m.left_at IS NULL", new { now }, tx, cancellationToken: ct));
        var closed = await c.ExecuteAsync(new CommandDefinition(
            "UPDATE instance_history SET ended_at = @now, end_reason = 'server_stop' WHERE ended_at IS NULL", new { now }, tx, cancellationToken: ct));
        await tx.CommitAsync(ct);
        return closed;
    }

    public async Task<int> PruneAsync(DateTime cut, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteAsync(new CommandDefinition(
            "DELETE FROM instance_history WHERE started_at < @cut AND ended_at IS NOT NULL", new { cut }, cancellationToken: ct));
    }

    private static string Truncate(string s, int max) => s.Length <= max ? s : s[..max];
}
