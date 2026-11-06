using DW4Online.Domain.World;

namespace DW4Online.Domain.Abstractions;

/// <summary>
/// The world relay as the domain sees it (SEAMLESS.md 19.17): admission of world tickets, the browser snapshot, the play
/// context for achievements and the staff actions. API, website and relay run in one process (ServerApp), so these are
/// in-process calls. The relay implements it (round I1's relay lane) and registers it before ServerApp's
/// <c>TryAddSingleton&lt;IWorldRelay&gt;(NullWorldRelay.Instance)</c>. Every member returns at once: the relay never holds
/// its hub lock longer than a TryEnter for them and queues its work.
/// </summary>
public interface IWorldRelay
{
    /// <summary>The relay runs (Relay:Enabled).</summary>
    bool Available { get; }

    /// <summary>The port the relay listens on once it started (Relay:Port 0 picks a free one in tests).</summary>
    int? BoundPort { get; }

    /// <summary>
    /// Set once by <see cref="Services.WorldService"/>: the relay calls it in its handshake after the ticket's signature,
    /// expiry and single use checked out, outside its hub lock and before the member is added; null refuses the ticket.
    /// </summary>
    void SetAdmission(Func<WorldAdmission, ValueTask<WorldAdmitted?>> check);

    /// <summary>A one-time ticket for the fixed world channel with the role <see cref="WorldRoles.Member"/> or <see cref="WorldRoles.Staff"/>.</summary>
    WorldTicket IssueTicket(long accountId, string role);

    /// <summary>The browser snapshot (19.17.2, rebuilt every housekeeping pass, immutable; <see cref="WorldSnapshot.Empty"/> before the first).</summary>
    WorldSnapshot Snapshot();

    /// <summary>The account's play context for achievements (19.8.9); null when it was not in the world within the last minute.</summary>
    WorldPlayContext? PlayContext(long accountId);

    /// <summary>POST /world/leave: closes the account's world connection (the relay sends CLOSE left); false when it has none.</summary>
    bool Leave(long accountId);

    /// <summary>Staff: ends the instance (END reason staff, 19.6.5); false when there is no such instance.</summary>
    bool EndInstance(uint wid, long staffAccountId);

    /// <summary>Staff: the member goes home (round I1b adds the removal bar); false when the account is not a member of it.</summary>
    bool RemoveMember(uint wid, long accountId, long staffAccountId);
}

/// <summary>A new world ticket and its nonce (the service remembers the newest nonce per account).</summary>
public sealed record WorldTicket(string Ticket, string Nonce, DateTimeOffset Expires);

/// <summary>What a redeemed world ticket says (account, ticket role, nonce).</summary>
public sealed record WorldAdmission(long AccountId, string Role, string Nonce);

/// <summary>
/// The service's answer to a world ticket: the account's name, its live role (re-read from the database: a demotion
/// counts at once), its bound box (1 until round D), its preferences, and its friends and blocks for Visible() (19.7.2;
/// loaded here, outside the hub's lock).
/// </summary>
public sealed record WorldAdmitted(long AccountId, string Username, string Role, int Box, OnlinePrefs Prefs)
{
    /// <summary>
    /// True when the ticket came from POST /world/enter (Play): the relay replaces a member the account still has (CLOSE
    /// replaced, a hosted instance ends, 19.17.3); false for /world/ticket (a reconnect).
    /// </summary>
    public bool NewSession { get; init; }
    public IReadOnlyList<long> Friends { get; init; } = [];
    /// <summary>Accounts this account blocks.</summary>
    public IReadOnlyList<long> Blocks { get; init; } = [];
    /// <summary>Accounts that block this account (never shown to him).</summary>
    public IReadOnlyList<long> BlockedBy { get; init; } = [];
}

/// <summary>
/// An account's play context (19.8.9). In an instance: its wid and history id, whether it hosts, the most players of its
/// instance in the last minute, the instance's randomizer, data hash and difficulty. At home (no instance): wid 0, no
/// history id, host (the own story), one player, its own settings and data hash.
/// </summary>
public sealed record WorldPlayContext(uint Wid, long? HistoryId, bool IsHost, int Players, bool Randomizer, ulong DataHash, int Difficulty);

/// <summary>
/// The browser snapshot: every instance with its members, and every connected member with where it is (the friends'
/// whereabouts, "last online", the counts). Read without the hub's lock; filtered per viewer by the caller.
/// </summary>
public sealed record WorldSnapshot(DateTime BuiltAtUtc, int MembersOnline, IReadOnlyList<WorldInstanceRow> Instances)
{
    public static readonly WorldSnapshot Empty = new(DateTime.MinValue, 0, []);

    /// <summary>Every connected member (players, staff, spectators): where it is.</summary>
    public IReadOnlyList<WorldOnlineRow> Online { get; init; } = [];
}

/// <summary>An instance in the snapshot. Members are players only, host first; spectators only for staff pages.</summary>
public sealed record WorldInstanceRow(
    uint Wid,
    long? HistoryId,
    long HostAccountId,
    string HostName,
    int HostBox,
    string Area,
    string HostStage,
    string Visibility,
    ulong DataHash,
    int Difficulty,
    string Settings,
    bool Randomizer,
    string StoryLabel,
    DateTime StartedAtUtc,
    bool Starting,
    bool Ending,
    bool HostAway,
    IReadOnlyList<WorldMemberRow> Members,
    IReadOnlyList<WorldMemberRow> Spectators);

/// <summary>A member of an instance: account, member id, name, role (host, guest, staff, spectator), stage, connection state.</summary>
public sealed record WorldMemberRow(long AccountId, uint MemberId, string Name, string Role, string Stage, bool Connected, bool Lagging);

/// <summary>
/// A connected member: its stage (lower-case folder, empty at the title) and its instance (0 at home); a spectator is
/// listed at home (its lobby) with <paramref name="Spectating"/> set, never in the watched instance (19.14).
/// </summary>
public sealed record WorldOnlineRow(long AccountId, uint MemberId, string Name, string Stage, uint Wid, bool Staff, bool Spectating = false);

/// <summary>
/// Optional on the relay's <see cref="IWorldRelay"/>: where the relay sends a host's visibility choice (19.6.2, Q9: the
/// host's last choice becomes his online_prefs.default_visibility). <see cref="Services.WorldService"/> sets it in its
/// constructor; the relay calls it off its lock (account, visibility) and never waits for it.
/// </summary>
public interface IWorldVisibilitySink
{
    Func<long, string, Task>? RememberVisibility { get; set; }
}

/// <summary>The world relay while no relay is wired in (the relay switched off, or the server lanes before the relay's): nobody is online.</summary>
public sealed class NullWorldRelay : IWorldRelay
{
    public static readonly NullWorldRelay Instance = new();

    public bool Available => false;
    public int? BoundPort => null;

    public void SetAdmission(Func<WorldAdmission, ValueTask<WorldAdmitted?>> check)
    {
    }

    public WorldTicket IssueTicket(long accountId, string role) => throw new InvalidOperationException("The relay is switched off.");
    public WorldSnapshot Snapshot() => WorldSnapshot.Empty;
    public WorldPlayContext? PlayContext(long accountId) => null;
    public bool Leave(long accountId) => false;
    public bool EndInstance(uint wid, long staffAccountId) => false;
    public bool RemoveMember(uint wid, long accountId, long staffAccountId) => false;
}
