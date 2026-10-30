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
}
