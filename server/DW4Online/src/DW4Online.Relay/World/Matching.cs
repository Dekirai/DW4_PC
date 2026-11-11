using DW4Online.Domain.Story;
using DW4Online.Domain.World;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;

namespace DW4Online.Relay.World;

/// <summary>A candidate's state for an arrival (SEAMLESS.md 19.7.2, 19.30.3).</summary>
public enum RowState
{
    /// <summary>The instance has a joinable stage group at the arrival stage: a JOIN there.</summary>
    Joinable,
    /// <summary>No stage group at the arrival stage, which is a gate stage the host's story opened or a visited stage: a guest's NEW.</summary>
    GuestNew,
    /// <summary>A stage group at the arrival stage that is not joinable yet (loading, a scene, a boss fight, arriving now): JOIN waits.</summary>
    Waiting,
    /// <summary>Not choosable: the host reconnecting, story not here yet, too long to catch up.</summary>
    Greyed,
}

/// <summary>One row of an arrival's candidates: the instance, its stage group at the arrival stage (null: none), the state, its players
/// at the arrival stage, the friend relation and the worst round trip of its members.</summary>
public sealed record CandidateRow(WorldInstance World, HubInstance? Group, RowState State, int Here, bool HostFriend, int FriendsInside, double PingMs,
    string Why = "")
{
    public bool Choosable => State != RowState.Greyed;
    public bool JoinableNow => State is RowState.Joinable or RowState.GuestNew;
}

/// <summary>The world's pure rules: Visible() (19.7.2), the gate rule (19.30.3) and the order of the candidates (19.7.3, decision 40).</summary>
public static class Matching
{
    /// <summary>Two accounts with a block in either direction (the caches of both sides; a blocked player never learns of it).</summary>
    public static bool Blocked(HubMember a, HubMember b) =>
        a.Blocks.Contains(b.AccountId) || a.BlockedBy.Contains(b.AccountId) || b.Blocks.Contains(a.AccountId) || b.BlockedBy.Contains(a.AccountId);

    /// <summary>
    /// Visible(i, m) (19.7.2): no block in either direction between m and any member of i; m not under a removal bar of i; i Public,
    /// or Friends and m a friend of the host (or in his party, round I4), or Private and m in the host's party (I4); staff see every
    /// instance whatever its visibility (the blocks and the bars still count for a staff member who simply plays, 19.14).
    /// </summary>
    public static bool Visible(WorldInstance i, HubMember m, long now)
    {
        if (i.Seats.ContainsKey(m)) return true;
        if (i.Bars.TryGetValue(m.AccountId, out var until) && now < until) return false;
        foreach (var x in i.Members)
            if (Blocked(m, x)) return false;
        return i.Visibility switch
        {
            Visibility.Public => true,
            Visibility.Friends => m.Staff || m.Friends.Contains(i.HostAccount),
            _ => m.Staff,
        };
    }

    /// <summary>The lobby gate offers this gate stage on the host's story (19.30.3; stagML11_m1__mate00.lua): stag0101 after flag
    /// 503 (the training), stag0201 from phase 4 (flag 4), stag0301 from phase 5, stag0401 from phase 6 (flag 6, or 86 after the
    /// last briefing). Phases are one-hot, so "from phase n" reads flags n..6 and 86.</summary>
    public static bool GateOpened(RoomStoryData story, string stage, StorySets? sets = null)
    {
        sets ??= StorySets.Default;
        var phase = StoryMerge.Phase(story, sets);
        return stage.ToLowerInvariant() switch
        {
            "stag0101" => story.Flags[sets.TrainingFlag] != 0,
            "stag0201" => phase >= 4,
            "stag0301" => phase >= 5,
            "stag0401" => phase >= 6,
            _ => false,
        };
    }

    /// <summary>May an arrival from home at <paramref name="stage"/> enter <paramref name="w"/> as a guest where it has no stage group
    /// (19.30.3): a gate stage of its area whose gate the host's story opened, or a stage the instance has visited.</summary>
    public static bool MayEnterAsGuest(WorldInstance w, string stage, AreaTable areas) =>
        w.Visited.Contains(stage) || (areas.IsGate(stage) && areas.AreaOf(stage) == w.Area && GateOpened(w.Keeper.Story, stage));

    /// <summary>
    /// The order of 19.7.3 with decision 40: rows that can be entered now first (the waiting and the greyed after every joinable one);
    /// among them (1) the party's (round I4), (2) friends: the host a friend, then the most friends inside, (3) rows with players at
    /// the arrival stage before rows with none, (4) the lowest worst round trip (within 10 ms equal), (5) more free seats, (6) more
    /// players at the arrival stage, (7) the same story point (round I2), (8) the older instance. Boss fights before greyed rows.
    /// A stage group at the arrival stage that is joinable in a moment (loading, a scene: 3.4's JOIN waits for it) ranks with the
    /// joinable rows in steps 1-3 (19.7.3 puts only "boss fight, host reconnecting, full, story not here yet" after them; round I1b's
    /// review: a friend's instance in a short scene fell behind a stranger's row, which took the cursor) and after the rows joinable
    /// now in step 4 ("joinable now, by ping").
    /// </summary>
    public static List<CandidateRow> Order(IEnumerable<CandidateRow> rows) =>
        rows.OrderBy(Rank)
            .ThenBy(r => r.HostFriend ? 0 : r.FriendsInside > 0 ? 1 : 2)
            .ThenByDescending(r => r.HostFriend ? 0 : r.FriendsInside)
            .ThenBy(r => r.Here > 0 ? 0 : 1)
            .ThenBy(r => r.JoinableNow ? 0 : 1)
            .ThenBy(r => Math.Floor(r.PingMs / 10.0))
            .ThenByDescending(r => r.World.FreeSeats)
            .ThenByDescending(r => r.Here)
            .ThenBy(r => r.World.CreatedAt)
            .ThenBy(r => r.World.Wid)
            .ToList();

    /// <summary>The order's first key: joinable now or in a moment 0, a boss fight 1 (choosing it waits for its end), greyed 2.</summary>
    private static int Rank(CandidateRow r) => r.State switch
    {
        RowState.Joinable or RowState.GuestNew => 0,
        RowState.Waiting => r.Group is { BossLock: true } ? 1 : 0,
        _ => 2,
    };

    /// <summary>The row I1a's automatic join takes (19.7.1, decision 40): the first by <see cref="Order"/> that is not greyed, also one
    /// with nobody at the arrival stage (I1b's prompt preselects *New instance* there instead).</summary>
    public static CandidateRow? AutoJoinRow(IReadOnlyList<CandidateRow> ordered) => ordered.FirstOrDefault(r => r.Choosable);

    /// <summary>The row I1b's prompt preselects (19.7.3): the first row when it is not greyed and is the party's or a friend's or has
    /// players at the arrival stage; otherwise none (*New instance*).</summary>
    public static CandidateRow? Preselected(IReadOnlyList<CandidateRow> ordered) =>
        ordered.Count > 0 && ordered[0].Choosable && (ordered[0].HostFriend || ordered[0].FriendsInside > 0 || ordered[0].Here > 0) ? ordered[0] : null;
}
