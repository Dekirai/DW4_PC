using DW4Online.Domain.Story;

namespace DW4Online.Tests;

/// <summary>
/// The room story of one room session (SEAMLESS.md 8.3-8.6): rebase on the room's difficulty, the plausibility rules (the
/// sender's stage sets the flag or changes the counter, one phase step, counter limits, UI stages refused), the engine
/// lines, and the owner-away policies drop (the dropped set, values that count again only from a visit after the drop),
/// hold (merged when the owner returns) and apply.
/// </summary>
public class RoomStoryKeeperTests
{
    private static RoomStoryKeeper Keeper(StoryPolicy policy = StoryPolicy.Drop, bool ownerPresent = true, int difficulty = 0, RoomStoryData? story = null) =>
        new(story ?? new RoomStoryData(), difficulty, policy, ownerPresent);

    private static StoryDelta Flags(params int[] flags) => new() { Flags = [.. flags] };

    [Fact]
    public void A_room_opens_with_the_working_set_of_its_difficulty()
    {
        var owner = new RoomStoryData { Revision = 7 };
        owner.Flags[3] = 1;   // working phase 3 (normal play)
        owner.Flags[702] = 1; // its normal copy
        owner.Flags[8] = 1;   // hard copy: phase 2
        owner.Flags[519] = 1; // key 1 on normal
        owner.Flags[392] = 1; // a persistent lobby flag (no difficulty)
        var hard = Keeper(difficulty: 1, story: owner);
        Assert.Equal(2, StoryMerge.Phase(hard.Story));
        Assert.Equal(0, hard.Story.Folder[1]);
        Assert.Equal(1, hard.Story.Flags[392]);
        Assert.Equal(7u, hard.Story.Revision);
        var normal = Keeper(difficulty: 0, story: owner);
        Assert.Equal(3, StoryMerge.Phase(normal.Story));
        Assert.Equal(1, normal.Story.Folder[1]);
    }

    [Fact]
    public void Plausibility_refuses_flags_from_stages_that_never_set_them()
    {
        var k = Keeper();
        // 392 is set in stagML12 / stagML13, not in stag0101; 23 is a temporary; 19 only in the quest dungeons dngq2*.
        var t = k.Take(Flags(392, 23, 19), "stag0101", 0);
        Assert.False(t.Changed);
        Assert.Contains(new StoryRefused(StoryKeyKind.Flag, 392, StoryRefusal.Stage), t.Refused);
        Assert.Contains(new StoryRefused(StoryKeyKind.Flag, 23, StoryRefusal.NotStory), t.Refused);
        Assert.Contains(new StoryRefused(StoryKeyKind.Flag, 19, StoryRefusal.Stage), t.Refused);
        t = k.Take(Flags(392), "stagML12", 0);
        Assert.True(t.Changed);
        Assert.Empty(t.Refused);
        Assert.Equal(1, k.Story.Flags[392]);
        Assert.Equal(1u, k.Story.Revision);
        // The title and the other UI stages send nothing that counts.
        t = k.Take(Flags(393), "stagIS10", 0);
        Assert.Equal([new StoryRefused(StoryKeyKind.Flag, 393, StoryRefusal.NotMember)], t.Refused);
        t = k.Take(Flags(393), null, 0);
        Assert.Equal(StoryRefusal.NotMember, t.Refused.Single().Reason);
        // Keys come from item pickups anywhere.
        t = k.Take(new StoryDelta { Folder = [4] }, "dng101b1", 0);
        Assert.True(t.Changed && k.Story.Folder[4] == 1 && k.Story.Flags[518 + 4] == 1);
    }

    [Fact]
    public void The_phase_rises_one_step_from_a_stage_that_sets_it()
    {
        var k = Keeper();
        Assert.Equal(StoryRefusal.Stage, k.Take(new StoryDelta { Phase = 1 }, "stag0101", 0).Refused.Single().Reason);
        Assert.True(k.Take(new StoryDelta { Phase = 1 }, "stagML02", 0).Changed);
        Assert.Equal(StoryRefusal.PhaseJump, k.Take(new StoryDelta { Phase = 3 }, "stagML02", 0).Refused.Single().Reason);
        Assert.True(k.Take(new StoryDelta { Phase = 2 }, "stagML02", 0).Changed);
        Assert.Equal(2, StoryMerge.Phase(k.Story));
        Assert.Equal(1, k.Story.Flags[701]); // the normal copy, one-hot
        Assert.Equal(0, k.Story.Flags[700]);
        Assert.Empty(k.Take(new StoryDelta { Phase = 1 }, "stagML02", 0).Refused); // below the room's: nothing
    }

    [Fact]
    public void Counters_take_each_engine_line_once_within_limits()
    {
        var k = Keeper();
        var t = k.Take(new StoryDelta { Counters = [new CounterGrowth(7, 5, 1, 2)] }, "stag0001", 0);
        Assert.True(t.Changed && k.Story.Counters[7] == 2);
        t = k.Take(new StoryDelta { Counters = [new CounterGrowth(7, 5, 1, 2)] }, "stag0001", 0); // the partner of the same instance
        Assert.False(t.Changed);
        t = k.Take(new StoryDelta { Counters = [new CounterGrowth(7, 5, 1, 3), new CounterGrowth(7, 6, 1, 1)] }, "stag0001", 0);
        Assert.True(t.Changed && k.Story.Counters[7] == 4);
        Assert.Equal(StoryRefusal.Stage, k.Take(new StoryDelta { Counters = [new CounterGrowth(1, 9, 1, 500)] }, "stag0001", 0).Refused.Single().Reason);
        Assert.True(k.Take(new StoryDelta { Counters = [new CounterGrowth(1, 9, 1, 500)] }, "stagML13", 0).Changed);
        Assert.Equal(500, k.Story.Counters[1]);
        Assert.Equal(StoryRefusal.Limit, k.Take(new StoryDelta { Counters = [new CounterGrowth(7, 8, 1, 100_001)] }, "stag0001", 0).Refused.Single().Reason);
        Assert.Equal(StoryRefusal.Limit, k.Take(new StoryDelta { Counters = [new CounterGrowth(7, 8, 1, -1)] }, "stag0001", 0).Refused.Single().Reason);
    }

    [Fact]
    public void Drop_refuses_records_and_takes_a_value_again_only_from_a_later_visit()
    {
        var k = Keeper(StoryPolicy.Drop, ownerPresent: false);
        var visit = k.Epoch; // the guest's lobby visit began before the drop
        var t = k.Take(new StoryDelta { Flags = [392], Folder = [2], Counters = [new CounterGrowth(1, 4, 1, 300)] }, "stagML13", visit);
        Assert.False(t.Changed);
        Assert.All(t.Refused, r => Assert.Equal(StoryRefusal.OwnerAway, r.Reason));
        Assert.Equal(3, t.Refused.Count);
        Assert.Equal([392], k.Dropped.Flags);
        Assert.Equal([2], k.Dropped.Folder);
        Assert.Equal([1], k.Dropped.Counters);
        Assert.Equal(0u, k.Story.Revision);
        // The owner returns: the old engine line still holds the values; they do not count (the barrier removes them).
        Assert.False(k.SetOwnerPresent(true));
        t = k.Take(new StoryDelta { Flags = [392], Counters = [new CounterGrowth(1, 4, 1, 300)] }, "stagML13", visit);
        Assert.False(t.Changed);
        Assert.All(t.Refused, r => Assert.Equal(StoryRefusal.OwnerAway, r.Reason));
        // A visit after the drop (the barrier cleaned the engine) that sets it anew: it counts.
        t = k.Take(new StoryDelta { Flags = [392], Counters = [new CounterGrowth(1, 7, 1, 100)] }, "stagML13", k.Epoch);
        Assert.True(t.Changed);
        Assert.Equal(1, k.Story.Flags[392]);
        Assert.Equal(100, k.Story.Counters[1]);
        // A phase made while away falls back too.
        Assert.False(k.SetOwnerPresent(false));
        k.Take(new StoryDelta { Phase = 1 }, "stagML02", k.Epoch);
        Assert.Contains(1, k.Dropped.Flags);
    }

    [Fact]
    public void Hold_keeps_until_the_owner_returns_then_merges()
    {
        var k = Keeper(StoryPolicy.Hold, ownerPresent: false);
        var t = k.Take(new StoryDelta { Phase = 1, Flags = [392] }, "stagML02", 0);
        Assert.Equal(StoryRefusal.Stage, t.Refused.Single(r => r.Kind == StoryKeyKind.Flag).Reason); // 392: not from stagML02
        Assert.Equal(StoryRefusal.Held, t.Refused.Single(r => r.Kind == StoryKeyKind.Phase).Reason);
        t = k.Take(new StoryDelta { Phase = 2 }, "stagML02", 0); // a second step while away: plausible against the held phase
        Assert.Equal(StoryRefusal.Held, t.Refused.Single().Reason);
        k.Take(new StoryDelta { Flags = [392], Counters = [new CounterGrowth(1, 3, 1, 50)] }, "stagML12", 0); // 1 only in stagML13
        k.Take(new StoryDelta { Counters = [new CounterGrowth(1, 3, 2, 50)] }, "stagML13", 0);
        Assert.Equal(0u, k.Story.Revision);
        Assert.Empty(k.Dropped.Flags);
        Assert.True(k.SetOwnerPresent(true));
        Assert.Equal(2, StoryMerge.Phase(k.Story));
        Assert.Equal(1, k.Story.Flags[392]);
        Assert.Equal(50, k.Story.Counters[1]);
        Assert.Equal(0, k.HeldCount);
    }

    [Fact]
    public void Apply_merges_while_the_owner_is_away()
    {
        var k = Keeper(StoryPolicy.Apply, ownerPresent: false);
        Assert.True(k.Take(Flags(392), "stagML12", 0).Changed);
        Assert.Empty(k.Dropped.Flags);
    }

    [Fact]
    public void A_visit_gives_one_phase_step_and_one_key_item_once_it_is_old_enough()
    {
        // (The per-member intervals apart: A_patient_member_... checks those.)
        var k = new RoomStoryKeeper(new RoomStoryData(), 0, StoryPolicy.Drop, true) { PhaseStepSeconds = 0, KeySeconds = 0 };
        var young = new StoryVisit(1, 77, 3);
        // Younger than MinVisitSeconds: everything waits (the client sends it again).
        var t = k.Take(new StoryDelta { Phase = 1, Flags = [22], Folder = [0] }, "stagML02", 0, young);
        Assert.False(t.Changed);
        Assert.Equal(3, t.Refused.Count(r => r.Reason == StoryRefusal.Wait));
        // Old enough: the phase step, the flag and one key item.
        var old = young with { Seconds = 12 };
        t = k.Take(new StoryDelta { Phase = 1, Flags = [22], Folder = [0, 1, 2] }, "stagML02", 0, old);
        Assert.True(t.Changed);
        Assert.Equal(1, StoryMerge.Phase(k.Story));
        Assert.Equal(1, k.Story.Flags[22]);
        Assert.Equal([1, 0, 0], k.Story.Folder[..3]);
        Assert.Equal([1, 2], t.Refused.Where(r => r is { Kind: StoryKeyKind.Folder, Reason: StoryRefusal.Wait }).Select(r => r.Index));
        // A second step in the same visit waits, however long it lasts.
        t = k.Take(new StoryDelta { Phase = 2, Folder = [1] }, "stagML02", 0, old with { Seconds = 600 });
        Assert.Equal(2, t.Refused.Count(r => r.Reason == StoryRefusal.Wait));
        Assert.False(t.Changed);
        // The member's next visit takes the next step and the next key; another member's visit counts on its own.
        t = k.Take(new StoryDelta { Phase = 2, Folder = [1, 2] }, "stagML02", 0, new StoryVisit(1, 78, 11));
        Assert.Equal(2, StoryMerge.Phase(k.Story));
        Assert.Equal(new StoryRefused(StoryKeyKind.Folder, 2, StoryRefusal.Wait), t.Refused.Single());
        Assert.True(k.Take(new StoryDelta { Folder = [2] }, "dng101b1", 0, new StoryVisit(2, 78, 11)).Changed);
        Assert.Equal(1, k.Story.Folder[2]);
    }

    [Fact]
    public void A_room_takes_only_the_clear_flag_of_its_own_difficulty_and_counters_within_the_wallet()
    {
        var owner = new RoomStoryData();
        owner.Flags[12] = 1; // the hard copy of phase 6 (the last briefing)
        var hard = Keeper(difficulty: 1, story: owner);
        var t = hard.Take(Flags(406, 407, 408), "stagBS06", 0);
        Assert.True(t.Changed);
        Assert.Equal(1, hard.Story.Flags[407]);
        Assert.Equal([406, 408], t.Refused.Where(r => r.Reason == StoryRefusal.Stage).Select(r => r.Index).Order());
        // COUNTER 1 counts donated bits (wallet and bank hold at most 9,999,999 each).
        Assert.Equal(StoryRefusal.Limit, hard.Take(new StoryDelta { Counters = [new CounterGrowth(1, 3, 1, 20_000_001)] }, "stagML13", 0).Refused.Single().Reason);
        Assert.True(hard.Take(new StoryDelta { Counters = [new CounterGrowth(1, 3, 1, 19_999_998)] }, "stagML13", 0).Changed);
    }

    /// <summary>
    /// The clear flag comes from the last boss, after the last briefing (phase 6): the round 59 review's guest BARRIERed into
    /// stagBS06 of a younger story and gave the owner a clear (and the next difficulty's rooms) in ten seconds. Under hold
    /// the held phase counts, as for the phase limit.
    /// </summary>
    [Fact]
    public void The_clear_flag_needs_the_last_briefing()
    {
        var five = new RoomStoryData();
        five.Flags[5] = 1;   // working phase 5
        five.Flags[704] = 1; // its normal copy (the room rebases on it)
        var k = Keeper(story: five);
        Assert.Equal(5, StoryMerge.Phase(k.Story));
        Assert.Equal(new StoryRefused(StoryKeyKind.Flag, 406, StoryRefusal.PhaseJump), k.Take(Flags(406), "stagBS06", 0).Refused.Single());
        Assert.Equal(0, k.Story.Flags[406]);
        // Phase 6 and then the clear (with phase 7 in the same set, as the last boss gives both).
        Assert.True(k.Take(new StoryDelta { Phase = 6 }, "stagML02", 0).Changed);
        var t = k.Take(new StoryDelta { Phase = 7, Flags = [406] }, "stagBS06", 0);
        Assert.True(t.Changed);
        Assert.Empty(t.Refused);
        Assert.Equal(1, k.Story.Flags[406]);
        Assert.Equal(7, StoryMerge.Phase(k.Story));
        // Hold: the held phase 6 lets the clear be held too; both merge when the owner returns.
        var hold = Keeper(StoryPolicy.Hold, ownerPresent: false, story: five);
        Assert.Equal(StoryRefusal.Held, hold.Take(new StoryDelta { Phase = 6 }, "stagML02", 0).Refused.Single().Reason);
        Assert.Equal(StoryRefusal.Held, hold.Take(Flags(406), "stagBS06", 0).Refused.Single().Reason);
        Assert.True(hold.SetOwnerPresent(true));
        Assert.Equal(1, hold.Story.Flags[406]);
        Assert.Equal(6, StoryMerge.Phase(hold.Story));
    }

    /// <summary>
    /// An owner without an online story starts at zero (the user's decision of 31.10, SEAMLESS.md 8.6): until the owner's
    /// game enters a stage every value is dropped with the reason NotStarted, before the per-visit rules (a value that only
    /// waited could slip in after the start) and whatever the policy; afterwards an old visit's value stays refused, a
    /// later visit's counts.
    /// </summary>
    [Fact]
    public void An_owner_without_a_story_starts_at_zero()
    {
        var k = new RoomStoryKeeper(new RoomStoryData(), 0, StoryPolicy.Apply, true, started: false);
        var visit = k.Epoch;
        var t = k.Take(new StoryDelta { Phase = 1, Flags = [22], Folder = [0], Counters = [new CounterGrowth(1, 4, 1, 300)] }, "stagML02", visit,
            new StoryVisit(1, 7, 2)); // (a young visit: dropped, not left waiting)
        Assert.False(t.Changed);
        Assert.Equal(3, t.Refused.Count(r => r.Reason == StoryRefusal.NotStarted));
        Assert.Contains(t.Refused, r => r.Kind == StoryKeyKind.Counter && r.Reason == StoryRefusal.Stage); // (1 only in stagML13)
        Assert.Equal([1, 22], k.Dropped.Flags);
        Assert.Equal([0], k.Dropped.Folder);
        Assert.Equal(0u, k.Story.Revision);
        Assert.True(k.OwnerStarted());
        Assert.False(k.OwnerStarted());
        // The old visit's engine still holds them: refused (its next barrier removes them).
        t = k.Take(new StoryDelta { Flags = [22] }, "stagML02", visit, new StoryVisit(1, 7, 20));
        Assert.Equal(new StoryRefused(StoryKeyKind.Flag, 22, StoryRefusal.NotStarted), t.Refused.Single());
        // A visit that began after the drop sets it anew: it counts.
        t = k.Take(new StoryDelta { Phase = 1, Flags = [22] }, "stagML02", k.Epoch, new StoryVisit(1, 8, 20));
        Assert.True(t.Changed);
        Assert.Equal(1, StoryMerge.Phase(k.Story));
        Assert.Equal(1u, k.Story.Revision);
    }

    /// <summary>A member's phase steps count five minutes apart and its key items a minute apart; another member counts on its own.</summary>
    [Fact]
    public void A_patient_member_gets_its_phase_steps_and_key_items_spaced_out()
    {
        var k = Keeper();
        StoryVisit V(int member, ulong key, double at) => new(member, key, 15, 100 + member, at);
        Assert.True(k.Take(new StoryDelta { Phase = 1, Folder = [0] }, "stagML02", 0, V(1, 1, 1000)).Changed);
        var t = k.Take(new StoryDelta { Phase = 2, Folder = [1] }, "stagML02", 0, V(1, 2, 1030));
        Assert.Equal(2, t.Refused.Count(r => r.Reason == StoryRefusal.Wait));
        t = k.Take(new StoryDelta { Phase = 2, Folder = [1] }, "stagML02", 0, V(1, 3, 1061));
        Assert.Equal(new StoryRefused(StoryKeyKind.Phase, 2, StoryRefusal.Wait), t.Refused.Single());
        Assert.Equal(1, k.Story.Folder[1]);
        Assert.True(k.Take(new StoryDelta { Phase = 2 }, "stagML02", 0, V(2, 4, 1062)).Changed);
        Assert.Equal(StoryRefusal.Wait, k.Take(new StoryDelta { Phase = 3 }, "stagML02", 0, V(1, 5, 1299)).Refused.Single().Reason);
        Assert.True(k.Take(new StoryDelta { Phase = 3 }, "stagML02", 0, V(1, 6, 1300)).Changed);
        // The same account under another member index (it left and joined again) is the same member.
        Assert.Equal(StoryRefusal.Wait, k.Take(new StoryDelta { Phase = 4 }, "stagML02", 0, new StoryVisit(5, 9, 15, 101, 1301)).Refused.Single().Reason);
    }

    /// <summary>
    /// The round 61 design's finding on round 59: every new game makes phase 1 at the lobby's first load and phase 2 at the
    /// first briefing a minute or two later. The owner's phase 2 waited for the five minutes, was refused for the stage once
    /// the owner left stagML02, and the room story stayed at phase 1. The owner's own steps are not spaced (one per visit
    /// still); a step that waits for that rule or a guest's spacing is earned and merged when due, in any stage.
    /// </summary>
    [Fact]
    public void The_owners_steps_are_not_spaced_and_a_waiting_step_is_merged_when_due()
    {
        var k = Keeper();
        StoryVisit Owner(ulong key, double at, double seconds = 15) => new(0, key, seconds, 100, at, Owner: true);
        StoryVisit Guest(ulong key, double at, double seconds = 15) => new(1, key, seconds, 101, at);
        Assert.True(k.Take(new StoryDelta { Phase = 1 }, "stagML11", 0, Owner(1, 1000)).Changed);
        Assert.True(k.Take(new StoryDelta { Phase = 2, Folder = [0] }, "stagML02", 0, Owner(2, 1060)).Changed);
        Assert.Equal(2, StoryMerge.Phase(k.Story));
        // One phase step and one key item per visit hold for the owner too: the second ones are earned ...
        var t = k.Take(new StoryDelta { Phase = 3, Folder = [1] }, "stagML02", 0, Owner(2, 1070));
        Assert.Equal(2, t.Refused.Count(r => r.Reason == StoryRefusal.Wait));
        Assert.Equal(2, k.EarnedCount);
        // ... and merged in a later visit ten seconds old, whatever its stage (stag0001 sets neither).
        Assert.False(k.MergeEarned(Owner(2, 1080), "stagML02").Changed);
        Assert.False(k.MergeEarned(Owner(3, 1081, 5), "stag0001").Changed);
        Assert.False(k.MergeEarned(Owner(4, 1100, 15), "stagIS12").Changed); // (not from the save room or the title)
        Assert.True(k.MergeEarned(Owner(5, 1101, 12), "stag0001").Changed);
        Assert.Equal(3, StoryMerge.Phase(k.Story));
        Assert.Equal(1, k.Story.Folder[1]);
        Assert.Equal(0, k.EarnedCount);
        Assert.False(k.HasEarned(0, 100));

        // A guest's steps stay spaced: its first counts, the next within five minutes (key items: one minute) is earned and
        // merged once the time has passed, in a later visit.
        Assert.True(k.Take(new StoryDelta { Phase = 4, Folder = [2] }, "stagML02", 0, Guest(10, 1200)).Changed);
        t = k.Take(new StoryDelta { Phase = 5, Folder = [3] }, "stagML02", 0, Guest(11, 1230));
        Assert.Equal(2, t.Refused.Count(r => r.Reason == StoryRefusal.Wait));
        Assert.False(k.MergeEarned(Guest(12, 1259), "stag0101").Changed);
        Assert.True(k.MergeEarned(Guest(12, 1261), "stag0101").Changed); // the key item, a minute after the last
        Assert.Equal(1, k.Story.Folder[3]);
        Assert.Equal(4, StoryMerge.Phase(k.Story));
        Assert.False(k.MergeEarned(Guest(13, 1499), "stag0101").Changed);
        Assert.True(k.MergeEarned(Guest(13, 1500), "stag0101").Changed);
        Assert.Equal(5, StoryMerge.Phase(k.Story));
        Assert.Equal(0, k.EarnedCount);
        // What the room story got meanwhile (another member's step) is no longer waited for.
        Assert.True(k.Take(new StoryDelta { Phase = 6, Folder = [4] }, "stagML02", 0, Guest(14, 1800)).Changed);
        Assert.Equal(StoryRefusal.Wait, k.Take(new StoryDelta { Folder = [5] }, "stag0101", 0, Guest(15, 1810)).Refused.Single().Reason);
        Assert.Equal(1, k.EarnedCount);
        Assert.True(k.Take(new StoryDelta { Folder = [5] }, "stag0101", 0, Owner(16, 1811)).Changed);
        Assert.False(k.MergeEarned(Guest(17, 1900), "stag0101").Changed);
        Assert.Equal(0, k.EarnedCount);
    }

    /// <summary>
    /// An earned value takes the owner-away policy of the moment it is due (dropped, held, merged); one that was dropped
    /// after the visit that earned it began is forgotten (the barrier removes it from that engine; it counts again when set
    /// anew). A room that has not started earns nothing (everything is dropped before the per-visit rules).
    /// </summary>
    [Fact]
    public void Earned_values_follow_the_owner_away_policy_of_the_moment_they_are_due()
    {
        StoryVisit Guest(ulong key, double at) => new(1, key, 15, 101, at);
        var drop = Keeper();
        Assert.True(drop.Take(new StoryDelta { Folder = [0] }, "stag0101", 0, Guest(1, 100)).Changed);
        drop.Take(new StoryDelta { Folder = [1] }, "stag0101", 0, Guest(2, 110));
        drop.SetOwnerPresent(false);
        var t = drop.MergeEarned(Guest(3, 200), "stag0101");
        Assert.False(t.Changed);
        Assert.Equal(new StoryRefused(StoryKeyKind.Folder, 1, StoryRefusal.OwnerAway), t.Refused.Single());
        Assert.Contains(1, drop.Dropped.Folder);
        Assert.Equal(0, drop.EarnedCount);

        var hold = Keeper(StoryPolicy.Hold);
        Assert.True(hold.Take(new StoryDelta { Folder = [0] }, "stag0101", 0, Guest(1, 100)).Changed);
        hold.Take(new StoryDelta { Folder = [1] }, "stag0101", 0, Guest(2, 110));
        hold.SetOwnerPresent(false);
        Assert.Equal(StoryRefusal.Held, hold.MergeEarned(Guest(3, 200), "stag0101").Refused.Single().Reason);
        Assert.True(hold.SetOwnerPresent(true));
        Assert.Equal(1, hold.Story.Folder[1]);

        // Earned in a visit of epoch 0, dropped afterwards from another member (epoch 1): forgotten.
        var stale = Keeper();
        Assert.True(stale.Take(new StoryDelta { Folder = [0] }, "stag0101", 0, Guest(1, 100)).Changed);
        stale.Take(new StoryDelta { Folder = [1] }, "stag0101", 0, Guest(2, 110));
        stale.SetOwnerPresent(false);
        stale.Take(new StoryDelta { Folder = [1] }, "stag0101", 0, new StoryVisit(2, 9, 15, 102, 120));
        stale.SetOwnerPresent(true);
        Assert.Empty(stale.MergeEarned(Guest(3, 200), "stag0101").Refused);
        Assert.Equal(0, stale.Story.Folder[1]);
        Assert.Equal(0, stale.EarnedCount);

        var notStarted = new RoomStoryKeeper(new RoomStoryData(), 0, StoryPolicy.Apply, ownerPresent: true, started: false);
        notStarted.Take(new StoryDelta { Folder = [0] }, "stag0101", 0, Guest(1, 100));
        notStarted.Take(new StoryDelta { Folder = [1] }, "stag0101", 0, Guest(1, 100));
        Assert.Equal(0, notStarted.EarnedCount);
    }

    /// <summary>
    /// Lines nobody can report any more are forgotten; a held line's growth beyond what was taken stays held and is merged
    /// when the owner returns.
    /// </summary>
    [Fact]
    public void Forgotten_lines_keep_their_held_growth()
    {
        var k = Keeper(StoryPolicy.Hold);
        Assert.True(k.Take(new StoryDelta { Counters = [new CounterGrowth(7, 5, 1, 4)] }, "stag0001", 0).Changed);
        Assert.False(k.SetOwnerPresent(false));
        k.Take(new StoryDelta { Counters = [new CounterGrowth(7, 5, 1, 10), new CounterGrowth(7, 6, 1, 3)] }, "stag0001", 0);
        Assert.Equal(3, k.LineCount); // (5,1) taken and held, (6,1) held
        Assert.Equal(3, k.PruneLines(_ => false)); // (5,1) taken and held, (6,1) held
        Assert.Equal(0, k.LineCount);
        Assert.Equal(1, k.HeldCount); // the growth of counter 7
        Assert.True(k.SetOwnerPresent(true));
        Assert.Equal(4 + 6 + 3, k.Story.Counters[7]); // (taken 4, held 10 of which 4 were taken, held 3)
        Assert.Equal(0, k.HeldCount);
        Assert.Equal(0, k.LineCount);
    }
}
