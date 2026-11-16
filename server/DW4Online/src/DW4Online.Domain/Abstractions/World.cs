using DW4Online.Domain.World;

namespace DW4Online.Domain.Abstractions;

/// <summary>The start of an instance as instance_history records it (19.17.1).</summary>
public sealed record InstanceHistoryStart(long HostAccountId, int HostBox, string FirstStage, string Visibility, int Difficulty, int StoryPhase,
    string Settings, DateTime StartedAt);

/// <summary>
/// instance_history and instance_history_members, the write side (19.17.1): the relay's history queue calls it off its
/// lock (round I1's relay lane); the service closes rows a previous process left open at its start and prunes old ones.
/// </summary>
public interface IInstanceHistoryRepository
{
    /// <summary>Writes a new instance row; returns its history id.</summary>
    Task<long> StartAsync(InstanceHistoryStart start, CancellationToken ct = default);

    /// <summary>Ends an instance (<see cref="InstanceEndReasons"/>) with its peak player count (1..4); open member rows end with it ('ended').</summary>
    Task EndAsync(long id, string endReason, int peakPlayers, DateTime endedAt, CancellationToken ct = default);

    /// <summary>A member entered (<see cref="InstanceRoles"/>); a member that comes back gets a new row.</summary>
    Task MemberJoinedAsync(long id, long accountId, int box, string role, DateTime joinedAt, CancellationToken ct = default);

    /// <summary>
    /// The member row of that join ends (<see cref="InstanceLeaveReasons"/>): the account's newest open row in the instance
    /// that started no later than a second after <paramref name="joinedAt"/> (the relay's seat time; it need not equal the
    /// stored milliseconds of <see cref="MemberJoinedAsync"/>).
    /// </summary>
    Task MemberLeftAsync(long id, long accountId, DateTime joinedAt, string leaveReason, DateTime leftAt, bool sharedStory, CancellationToken ct = default);

    /// <summary>At the service start: instances a previous process left open end with 'server_stop' (their members 'ended'); returns how many.</summary>
    Task<int> CloseOpenAsync(DateTime now, CancellationToken ct = default);
}

/// <summary>An instance of an account's history as the website shows it (players only; spectator rows for staff pages).</summary>
public sealed class InstanceHistoryEntry
{
    public long Id { get; init; }
    public string HostName { get; init; } = "";
    public string FirstStage { get; init; } = "";
    public string Visibility { get; init; } = "";
    public int Difficulty { get; init; }
    public DateTime StartedAt { get; init; }
    public DateTime? EndedAt { get; init; }
    public string? EndReason { get; init; }
    public int PeakPlayers { get; init; }
    /// <summary>The account's role in it (host, guest, staff, spectator).</summary>
    public string Role { get; init; } = "";
    public DateTime JoinedAt { get; init; }
    public DateTime? LeftAt { get; init; }
    public string? LeaveReason { get; init; }
}

/// <summary>A player the account shared an instance with ("Recently played with", 19.26): the newest time they did.</summary>
public sealed class PlayedWithRow
{
    public long AccountId { get; init; }
    public string Username { get; init; } = "";
    public DateTime LastPlayedAt { get; init; }
    public int Times { get; init; }
}

/// <summary>The world's account-side store: online_prefs, accounts.last_seen_at and the history queries of the website.</summary>
public interface IWorldRepository
{
    /// <summary>Deletes ended instances started before <paramref name="cut"/> with their members; returns how many (the
    /// 90-day prune is web's housekeeping, not part of the relay's history interface above).</summary>
    Task<int> PruneHistoryAsync(DateTime cut, CancellationToken ct = default);

    /// <summary>The account's preferences; <see cref="OnlinePrefs.Default"/> when it has no row.</summary>
    Task<OnlinePrefs> GetPrefsAsync(long accountId, CancellationToken ct = default);

    /// <summary>Writes the preferences (an insert or an update); false when the account is deleted.</summary>
    Task<bool> SavePrefsAsync(long accountId, OnlinePrefs prefs, DateTime now, CancellationToken ct = default);

    /// <summary>Sets only default_visibility (the host's last choice, Q9); false when the account is deleted.</summary>
    Task<bool> SaveVisibilityAsync(long accountId, string visibility, DateTime now, CancellationToken ct = default);

    /// <summary>Moves accounts.last_seen_at forward to <paramref name="at"/> for the given accounts.</summary>
    Task TouchLastSeenAsync(IReadOnlyCollection<long> accountIds, DateTime at, CancellationToken ct = default);

    Task<DateTime?> GetLastSeenAsync(long accountId, CancellationToken ct = default);

    /// <summary>Accounts that block <paramref name="accountId"/> (the relay's social cache; never shown to him).</summary>
    Task<IReadOnlyList<long>> ListBlockedByAsync(long accountId, CancellationToken ct = default);

    /// <summary>The account's newest instances (as host, guest, staff or spectator), newest first.</summary>
    Task<IReadOnlyList<InstanceHistoryEntry>> ListHistoryAsync(long accountId, int limit, bool withSpectating, CancellationToken ct = default);

    /// <summary>
    /// Players who shared an instance with the account as players (spectators on either side never count) since
    /// <paramref name="since"/>, newest first; deleted accounts are left out.
    /// </summary>
    Task<IReadOnlyList<PlayedWithRow>> ListPlayedWithAsync(long accountId, DateTime since, int limit, CancellationToken ct = default);

    /// <summary>The whereabouts preference (the default when no row) and last_seen_at of the given accounts.</summary>
    Task<IReadOnlyList<PresencePrefsRow>> ListPresencePrefsAsync(IReadOnlyCollection<long> accountIds, CancellationToken ct = default);

    /// <summary>The staff rows of the audit log, newest first: of one staff member (<paramref name="actorId"/>) or of all; one event or all.</summary>
    Task<IReadOnlyList<StaffAuditRow>> ListStaffAuditAsync(long? actorId, string? auditEvent, int limit, CancellationToken ct = default);
}

/// <summary>Whether and when friends may see an account's whereabouts (online_prefs.whereabouts, accounts.last_seen_at).</summary>
public sealed class PresencePrefsRow
{
    public long AccountId { get; init; }
    public string Whereabouts { get; init; } = OnlinePrefs.WhereaboutsFriends;
    public DateTime? LastSeenAt { get; init; }
}

/// <summary>A staff row of the audit log (19.14, 19.19's GET /admin/audit): who did what to whom, with both names.</summary>
public sealed class StaffAuditRow
{
    public long Id { get; init; }
    public DateTime At { get; init; }
    public string Event { get; init; } = "";
    public string? Staff { get; init; }
    public string? Player { get; init; }
    public string? Detail { get; init; }
    /// <summary>
    /// The current names of the accounts the detail names by id (hostId, memberIds: staff_end_instance, staff_remove), as
    /// "host Kim; members Kim, Lee"; null when it names none. The detail itself holds no names (an account's deletion
    /// renames it, so a deleted player shows under his anonymous name here).
    /// </summary>
    public string? Accounts { get; init; }

    /// <summary>The account ids a detail names: (hostId, memberIds); empty for other details or text that is no JSON.</summary>
    public static (long? Host, IReadOnlyList<long> Members) DetailAccounts(string? detail)
    {
        if (string.IsNullOrEmpty(detail)) return (null, []);
        try
        {
            using var doc = System.Text.Json.JsonDocument.Parse(detail);
            if (doc.RootElement.ValueKind != System.Text.Json.JsonValueKind.Object) return (null, []);
            long? host = doc.RootElement.TryGetProperty("hostId", out var h) && h.TryGetInt64(out var hv) ? hv : null;
            var members = doc.RootElement.TryGetProperty("memberIds", out var m) && m.ValueKind == System.Text.Json.JsonValueKind.Array
                ? m.EnumerateArray().Where(x => x.TryGetInt64(out _)).Select(x => x.GetInt64()).ToList()
                : [];
            return (host, members);
        }
        catch (System.Text.Json.JsonException)
        {
            return (null, []);
        }
    }

    /// <summary>The audit events of staff actions and looks (the view v_staff_audit, migration 0013, holds the same list except
    /// report_reopened: migration 0014 could not replace the view without the right DROP).</summary>
    public static readonly string[] Events =
    [
        "staff_join", "staff_join_override", "staff_spectate_start", "staff_spectate_end", "staff_end_instance", "staff_remove",
        "instance_visibility", "instance_remove", "report_handled", "report_reopened", "room_close_admin", "admin_role", "admin_status",
        "admin_password_reset", "admin_unlock_revoke", "character_restore", "story_restore",
    ];
}
