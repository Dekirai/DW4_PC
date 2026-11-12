using DW4Online.Domain.Rooms;
using DW4Online.Domain.Story;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;

namespace DW4Online.Relay.World;

/// <summary>The relay's world keys (section "World", SEAMLESS.md 19.17.5). The web lane's WorldWebOptions binds the same section.</summary>
public sealed class WorldHubOptions
{
    /// <summary>Members in the world at a time (replaces the room limit; ERR full beyond).</summary>
    public int MaxMembers { get; set; } = 256;
    /// <summary>Instances at a time: a NEW beyond it plays as a stage group of an instance nobody is offered (no candidate).</summary>
    public int MaxInstances { get; set; } = 128;
    public int MaxSpectators { get; set; } = 2;
    /// <summary>Ghost figures per receiver in a hub stage (Q10).</summary>
    public int HubFigures { get; set; } = 30;
    /// <summary>The nearest figures that get every PRESENCE2; the farther ones every second one (19.13).</summary>
    public int NearFigures { get; set; } = 10;
    /// <summary>Presence bytes a receiver gets per second (its own budget beside the game frames).</summary>
    public int PresenceBytesPerSecond { get; set; } = 24 * 1024;
    public int HostGraceSeconds { get; set; } = 60;
    /// <summary>The end notice before HOME (three seconds).</summary>
    public int EndNoticeTicks { get; set; } = 180;
    public int RemovedBarMinutes { get; set; } = 10;
    /// <summary>A member whose connection is gone leaves the world after this (a host in his grace after the grace plus the margin).</summary>
    public int MemberTimeoutSeconds { get; set; } = 35;
    public int MemberTimeoutMarginSeconds { get; set; } = 10;
    /// <summary>The host's grace starts at this much silence (no INPUT, no PONG) while his slot fills (19.6.7).</summary>
    public int HostSilenceSeconds { get; set; } = 4;
    public int HostReturnSeconds { get; set; } = 60;
    /// <summary>The automatic co-arrival window of a new instance (19.7.6).</summary>
    public int StartingSeconds { get; set; } = 10;
    public bool StaffTagWhenPlaying { get; set; } = true;
    /// <summary>The game data the world admits (decimal data hashes; empty = any).</summary>
    public List<string> DataHashes { get; set; } = [];
    /// <summary>"Ranked" data hashes (the achievements' context).</summary>
    public List<string> RankedDataHashes { get; set; } = [];
    /// <summary>A host's RESUME window (60 s at 59.94 Hz plus 600 ticks, 19.6.7).</summary>
    public int HostMaxResumeTicks { get; set; } = 4200;
    /// <summary>How often the guests see "Kim is reconnecting... n s" during the grace.</summary>
    public int HostAwayNoticeSeconds { get; set; } = 5;
    /// <summary>A story keeper nobody plays on any more (its owner left the world, no instance runs on it) is dropped after this.</summary>
    public int KeeperIdleMinutes { get; set; } = 5;
    /// <summary>Test runs only (scenario W10d, never in appsettings): an arrival's JOIN target counts as not joinable for this long after
    /// the arrival's first JOIN decision (HoldKind.Join, BARRIER_WAIT not joinable), as a scene would hold it. 0: off.</summary>
    public int TestJoinHoldMs { get; set; }
    /// <summary>Round I1b (19.7): the arrival prompt. False: round I1a's automatic join of the first row (the load gate's fake
    /// clients, which answer no prompt).</summary>
    public bool ArrivalPrompt { get; set; } = true;
    /// <summary>OPTIONS goes again on a change at most this often (19.7.3: once a second).</summary>
    public int OptionsRefreshMs { get; set; } = 1000;
    /// <summary>A test hook (scenario W2b): the relay drops every PLAN unanswered, so the client's press goes out after
    /// World:PlanWaitMs and the barrier is decided as a loading-screen arrival.</summary>
    public bool TestDropPlans { get; set; }

    public bool AdmitsData(ulong dataHash) =>
        DataHashes.Count == 0 || DataHashes.Any(h => ulong.TryParse(h, out var v) && v == dataHash);

    public bool Ranked(ulong dataHash) => RankedDataHashes.Any(h => ulong.TryParse(h, out var v) && v == dataHash);
}

/// <summary>What the world's admission found about an account (loaded outside the hub lock, SEAMLESS.md 19.17.2).</summary>
public sealed record HubAdmit(long AccountId, string Name)
{
    public bool Staff { get; init; }
    public int Box { get; init; } = 1;
    public ulong DataHash { get; init; }
    public RoomStoryData Story { get; init; } = new();
    public Visibility DefaultVisibility { get; init; } = Visibility.Public;
    public IReadOnlyCollection<long> Friends { get; init; } = [];
    public IReadOnlyCollection<long> Blocks { get; init; } = [];
    public IReadOnlyCollection<long> BlockedBy { get; init; } = [];
    /// <summary>POST /world/enter (Play): a known member of the account is replaced (19.17.3).</summary>
    public bool NewSession { get; init; }
    /// <summary>The relay's handle to run the same admission again (the live re-check, 19.14).</summary>
    public object? Handle { get; init; }
}

/// <summary>A place in an instance (19.3): taken while the member plays in it; held during the host's grace.</summary>
public sealed class Seat(HubMember member, long since, DateTime sinceUtc, string role)
{
    public HubMember Member { get; } = member;
    public long Since { get; } = since;
    public DateTime SinceUtc { get; } = sinceUtc;
    /// <summary>host | guest | staff (instance_history_members.role).</summary>
    public string Role { get; } = role;
}

/// <summary>
/// A user's instance (SEAMLESS.md 19.3, 19.17.2): a host, his story and settings, a visibility, up to four seats, its area and the
/// stages it has visited, and its stage groups (<see cref="HubInstance"/>). From its first NEW to its end.
/// </summary>
public sealed class WorldInstance(uint wid)
{
    public uint Wid { get; } = wid;
    /// <summary>The instance_history id once the queued row is written; never on the wire (19.17.2).</summary>
    public long? HistoryId
    {
        get => Interlocked.Read(ref _historyId) is var v && v != 0 ? v : null;
        set => Interlocked.Exchange(ref _historyId, value ?? 0);
    }
    private long _historyId;
    /// <summary>Made beyond World:MaxInstances: a stage group nobody is offered (no candidate, no browser row).</summary>
    public bool Unlisted { get; set; }
    public required HubMember Host { get; init; }
    public long HostAccount => Host.AccountId;
    public required StoryKeeper Keeper { get; init; }
    public required ulong DataHash { get; init; }
    public Visibility Visibility { get; set; }
    /// <summary>The visibility the audit log last got, and a change that waits to settle before its audit row and the host's
    /// remembered default are written (a burst of changes writes once, 19.16): who made it, when, and the host's choice if his.</summary>
    public Visibility LoggedVisibility { get; set; }
    public (HubMember By, long At)? VisibilityPending { get; set; }
    public Visibility? HostChoicePending { get; set; }
    public required RoomSettings Settings { get; init; }
    public ulong SettingsHash => Settings.Hash(Wire.WorldProtocol, DataHash);
    public bool Randomizer => Settings.RandomizerActive;
    /// <summary>The area of the stage the host entered from home (19.30.2), fixed for the instance's life; null: a stage of no area.</summary>
    public string? Area { get; init; }
    public required string FirstStage { get; init; }
    /// <summary>The session context of the instance's first barrier (3.7): fresh arrivals match it (a quest needs the same quest).</summary>
    public ulong Context { get; init; }
    /// <summary>The stages where the instance has had a stage group (19.30.3).</summary>
    public HashSet<string> Visited { get; } = new(StringComparer.OrdinalIgnoreCase);
    public Dictionary<HubMember, Seat> Seats { get; } = [];
    public HashSet<HubInstance> Groups { get; } = [];
    public long CreatedAt { get; init; }
    public DateTime StartedUtc { get; init; }
    /// <summary>Until then (or until its first stage group is joinable) the instance is *starting* (19.7.6).</summary>
    public long StartingUntil { get; set; }
    public bool Ending { get; set; }
    public EndReason? EndReason { get; set; }
    public long EndedAt { get; set; }
    /// <summary>The host's grace (0: none): when it began and when it runs out.</summary>
    public long HostAwaySince { get; set; }
    public long GraceUntil { get; set; }
    public long LastAwayNotice { get; set; }
    /// <summary>Removal bars: account until (relay clock).</summary>
    public Dictionary<long, long> Bars { get; } = [];
    /// <summary>Every account that held a seat in the instance (a report names the instance both shared, round I1b).</summary>
    public HashSet<long> EverSeated { get; } = [];
    public int PeakPlayers { get; set; }
    /// <summary>INSTANCE_INFO is sent in the next housekeeping pass.</summary>
    public bool InfoDirty { get; set; }
    /// <summary>The players of the last minute (achievements' play context): when the housekeeping last saw n seats taken.</summary>
    public long[] SeenWithPlayers { get; } = new long[Wire.MaxSlots + 1];

    public bool HostAway => HostAwaySince != 0;
    public int Players => Seats.Count;
    public int FreeSeats => Math.Max(0, Wire.MaxSlots - Seats.Count);
    public IEnumerable<HubMember> Members => Seats.Keys;
}
