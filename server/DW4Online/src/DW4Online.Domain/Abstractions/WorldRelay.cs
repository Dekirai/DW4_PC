using DW4Online.Domain.World;

namespace DW4Online.Domain.Abstractions;

// STUB COPY in the relay lane's branch (feat-i1a-relay): the web lane owns this file (i1a-contract.md, "web 12:50: IWorldRelay").
// The integration takes the web lane's version; the relay lane's additions it asked for there are NewSession and IWorldVisibilitySink.

/// <summary>The domain's view of the world relay (SEAMLESS.md 19.17.2); the relay implements it (WorldGateway).</summary>
public interface IWorldRelay
{
    bool Available { get; }
    int? BoundPort { get; }
    void SetAdmission(Func<WorldAdmission, ValueTask<WorldAdmitted?>> check);
    WorldTicket IssueTicket(long accountId, string role);
    WorldSnapshot Snapshot();
    WorldPlayContext? PlayContext(long accountId);
    bool Leave(long accountId);
    bool EndInstance(uint wid, long staffAccountId);
    bool RemoveMember(uint wid, long accountId, long staffAccountId);
}

/// <summary>Where the relay tells the domain the host's last visibility choice (Q9: remembered as online_prefs.default_visibility).</summary>
public interface IWorldVisibilitySink
{
    Func<long, string, Task>? RememberVisibility { get; set; }
}

public sealed record WorldTicket(string Ticket, string Nonce, DateTimeOffset Expires);

public sealed record WorldAdmission(long AccountId, string Role, string Nonce);

public sealed record WorldAdmitted(long AccountId, string Username, string Role, int Box, OnlinePrefs Prefs)
{
    public IReadOnlyList<long> Friends { get; init; } = [];
    public IReadOnlyList<long> Blocks { get; init; } = [];
    public IReadOnlyList<long> BlockedBy { get; init; } = [];
    /// <summary>The ticket's nonce came from POST /world/enter (Play): a known member of the account is replaced (19.17.3).</summary>
    public bool NewSession { get; init; }
}

public sealed record WorldPlayContext(uint Wid, long? HistoryId, bool IsHost, int Players, bool Randomizer, ulong DataHash, int Difficulty);

public sealed record WorldSnapshot(DateTime BuiltAtUtc, int MembersOnline, IReadOnlyList<WorldInstanceRow> Instances)
{
    public static readonly WorldSnapshot Empty = new(DateTime.MinValue, 0, []);
    /// <summary>Every connected member (web 14:10): counts, friends' whereabouts, accounts.last_seen_at.</summary>
    public IReadOnlyList<WorldOnlineRow> Online { get; init; } = [];
}

public sealed record WorldOnlineRow(long AccountId, uint MemberId, string Name, string Stage, uint Wid, bool Staff, bool Spectating = false);

public sealed record WorldInstanceRow(uint Wid, long? HistoryId, long HostAccountId, string HostName, int HostBox,
    string Area, string HostStage, string Visibility,
    ulong DataHash, int Difficulty, string Settings, bool Randomizer, string StoryLabel,
    DateTime StartedAtUtc, bool Starting, bool Ending, bool HostAway,
    IReadOnlyList<WorldMemberRow> Members, IReadOnlyList<WorldMemberRow> Spectators);

public sealed record WorldMemberRow(long AccountId, uint MemberId, string Name, string Role, string Stage, bool Connected, bool Lagging);

/// <summary>No relay (Relay:Enabled off).</summary>
public sealed class NullWorldRelay : IWorldRelay
{
    public static readonly NullWorldRelay Instance = new();

    public bool Available => false;
    public int? BoundPort => null;

    public void SetAdmission(Func<WorldAdmission, ValueTask<WorldAdmitted?>> check)
    {
    }

    public WorldTicket IssueTicket(long accountId, string role) => throw new InvalidOperationException("The relay is off.");
    public WorldSnapshot Snapshot() => WorldSnapshot.Empty;
    public WorldPlayContext? PlayContext(long accountId) => null;
    public bool Leave(long accountId) => false;
    public bool EndInstance(uint wid, long staffAccountId) => false;
    public bool RemoveMember(uint wid, long accountId, long staffAccountId) => false;
}
