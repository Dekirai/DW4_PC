using DW4Online.Domain.Story;

namespace DW4Online.Tests;

/// <summary>Round 59's spacing and caps at their edges (minors of round 59's verify).</summary>
public class HeldFlagSpacingTests
{
    private static StoryVisit V(int member, ulong key, double at, long account, bool owner = false) => new(member, key, 100, account, at, owner);

    // Hold, owner away: the held flags a client resends every story interval take no flag tokens, so a new flag gets the
    // next token (before, the resend took it and the new flag waited on).
    [Fact]
    public void Held_flags_resent_take_no_flag_tokens()
    {
        var k = new RoomStoryKeeper(new RoomStoryData(), 0, StoryPolicy.Hold, ownerPresent: false) { MinVisitSeconds = 0 };
        int[] eight = [324, 329, 330, 331, 332, 335, 392, 393];
        var a = k.Take(new StoryDelta { Flags = [.. eight] }, "stagml12", 0, V(1, 7, 0, 5));
        Assert.All(a.Refused, r => Assert.Equal(StoryRefusal.Held, r.Reason));
        Assert.Equal(8, k.HeldCount);
        var b = k.Take(new StoryDelta { Flags = [.. eight, 394] }, "stagml12", 0, V(1, 7, 10, 5));
        Assert.Equal(8, b.Refused.Count(r => r.Reason == StoryRefusal.Held));
        Assert.Contains(b.Refused, r => r.Index == 394 && r.Reason == StoryRefusal.Queued);
        var c = k.Take(new StoryDelta { Flags = [.. eight, 394] }, "stagml12", 0, V(1, 7, 40, 5));
        Assert.All(c.Refused, r => Assert.Equal(StoryRefusal.Held, r.Reason));
        Assert.Equal(9, k.HeldCount);
    }

    // The session growth cap is for guests: the owner's own counter 7 grows past 1,000 in one room session.
    [Fact]
    public void The_owners_own_counters_are_not_capped_per_session()
    {
        var k = new RoomStoryKeeper(new RoomStoryData(), 0, StoryPolicy.Drop, ownerPresent: true) { MinVisitSeconds = 0 };
        var a = k.Take(new StoryDelta { Counters = [new CounterGrowth(7, 3, 1, 1_500)] }, "stag0001", 0, V(0, 3UL << 32 | 1, 0, 9, owner: true));
        Assert.Equal(1_500, k.Story.Counters[7]);
        Assert.Empty(a.Refused);
        var b = k.Take(new StoryDelta { Counters = [new CounterGrowth(7, 4, 1, 5)] }, "stag0001", 0, V(0, 4UL << 32 | 1, 0, 9, owner: true));
        Assert.Equal(1_505, k.Story.Counters[7]);
        Assert.Empty(b.Refused);
    }
}
