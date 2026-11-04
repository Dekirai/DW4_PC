using DW4Online.Domain.Story;

namespace DW4Online.Tests;

/// <summary>The flag spacing of round 59 with the Hold policy (round 59's verify, minor).</summary>
public class HeldFlagSpacingTests
{
    private static StoryVisit V(int member, ulong key, double at, long account) => new(member, key, 100, account, at, false);

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
        Assert.Contains(b.Refused, r => r.Index == 394 && r.Reason == StoryRefusal.Wait);
        var c = k.Take(new StoryDelta { Flags = [.. eight, 394] }, "stagml12", 0, V(1, 7, 40, 5));
        Assert.All(c.Refused, r => Assert.Equal(StoryRefusal.Held, r.Reason));
        Assert.Equal(9, k.HeldCount);
    }
}
