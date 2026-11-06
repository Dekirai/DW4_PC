using DW4Online.Domain.Story;

namespace DW4Online.Tests;

/// <summary>
/// The world's story keeper per (account, box) (SEAMLESS.md 19.8): the owner's own values are not spaced, a guest's follow round
/// 59's rules with the earned queue; the host's grace holds values (merged at his return, discarded at the end); a story replaced
/// from outside refuses merges until its reload; the difficulty is the owner's and the DISSOLVE's payload is the own story
/// rebased on the member's own difficulty; round 59's owner-away machinery is gone (nothing is ever dropped or "not started").
/// </summary>
public class StoryKeeperTests
{
    private const long Host = 100;
    private const long Guest = 200;

    private static StoryKeeper Keeper(RoomStoryData? story = null, int difficulty = 0) => new(Host, 1, story ?? new RoomStoryData(), difficulty);

    private static StoryVisit Owner(ulong key, double at, double seconds = 15) => new(1, key, seconds, Host, at, Owner: true, OwnerSeen: true);
    private static StoryVisit Visitor(ulong key, double at, double seconds = 15) => new(2, key, seconds, Guest, at, Owner: false, OwnerSeen: true);

    [Fact]
    public void A_new_accounts_story_starts_at_zero_and_takes_values_at_once()
    {
        var k = Keeper();
        var t = k.Take(new StoryDelta { Flags = [505] }, "stag0101", Owner(1, 100));
        Assert.True(t.Changed);
        Assert.Empty(t.Refused);
        Assert.Equal((1u, (byte)1), (k.Story.Revision, k.Story.Flags[505]));
    }

    [Fact]
    public void The_owners_key_items_are_not_spaced_a_guests_are_queued_and_merged_when_due()
    {
        var k = Keeper();
        Assert.True(k.Take(new StoryDelta { Folder = [0] }, "dng101b2", Owner(1, 100)).Changed);
        Assert.True(k.Take(new StoryDelta { Folder = [1] }, "dng102b3", Owner(2, 110)).Changed); // the owner: another visit, 10 s later
        var g = Keeper();
        Assert.True(g.Take(new StoryDelta { Folder = [0] }, "dng101b2", Visitor(1, 100)).Changed);
        var queued = g.Take(new StoryDelta { Folder = [1] }, "dng102b3", Visitor(2, 110));
        Assert.Equal(StoryRefusal.Queued, queued.Refused.Single().Reason);
        Assert.Equal(1, g.EarnedCount);
        Assert.Contains(Guest, g.EarnedAccounts);
        Assert.False(g.MergeEarned(Visitor(3, 150), "stagml11").Changed); // 60 s apart not yet
        Assert.True(g.MergeEarned(Visitor(3, 161), "stagml11").Changed);  // across stages: the queue merges in a later visit
        Assert.Equal(1, g.Story.Folder[1]);
        Assert.Equal(0, g.EarnedCount);
    }

    [Fact]
    public void The_hosts_grace_holds_values_his_return_merges_them_and_the_end_discards_them()
    {
        var k = Keeper();
        k.Hold();
        var held = k.Take(new StoryDelta { Flags = [505] }, "stag0101", Visitor(1, 100));
        Assert.Equal(StoryRefusal.Held, held.Refused.Single().Reason);
        Assert.False(held.Changed);
        Assert.Equal(1, k.HeldCount);
        Assert.True(k.Release(merge: true));
        Assert.Equal(1, k.Story.Flags[505]);
        Assert.False(k.Held);
        var gone = Keeper();
        gone.Hold();
        gone.Take(new StoryDelta { Flags = [506] }, "stag0101", Visitor(1, 100));
        Assert.False(gone.Release(merge: false));
        Assert.Equal((0, 0), (gone.HeldCount, (int)gone.Story.Flags[506]));
    }

    [Fact]
    public void A_story_replaced_from_outside_refuses_merges_until_it_is_reloaded()
    {
        var k = Keeper();
        k.Take(new StoryDelta { Folder = [0] }, "dng101b2", Visitor(1, 100));
        k.Take(new StoryDelta { Folder = [1] }, "dng102b3", Visitor(2, 110)); // queued
        k.Replacing = true;
        Assert.Equal(StoryRefusal.Wait, k.Take(new StoryDelta { Flags = [505] }, "stag0101", Owner(3, 120)).Refused.Single().Reason);
        Assert.False(k.MergeEarned(Visitor(4, 500), "stagml11").Changed);
        var fresh = new RoomStoryData { Revision = 9 };
        k.Replace(fresh);
        Assert.False(k.Replacing);
        Assert.Equal((9u, 0, (byte)0), (k.Story.Revision, k.EarnedCount, k.Story.Folder[0]));
    }

    [Fact]
    public void The_difficulty_is_the_owners_and_the_dissolve_payload_is_rebased_without_touching_the_keeper()
    {
        var s = new RoomStoryData();
        s.Flags[4] = 1; // phase 4 on Normal
        StoryMerge.DeriveCopies(s, 0);
        var k = Keeper(s);
        Assert.Equal(4, StoryMerge.Phase(k.Story));
        var hard = k.RebasedCopy(1);
        Assert.Equal(0, StoryMerge.Phase(hard)); // nothing on Hard yet
        Assert.Equal(4, StoryMerge.Phase(k.Story));
        k.SetDifficulty(1);
        Assert.Equal((1, 0), (k.Difficulty, StoryMerge.Phase(k.Story)));
        k.SetDifficulty(0);
        Assert.Equal(4, StoryMerge.Phase(k.Story));
    }

    [Fact]
    public void Only_the_storys_own_difficultys_clear_counts_and_only_after_the_last_briefing()
    {
        var k = Keeper(difficulty: 1);
        var other = k.Take(new StoryDelta { Flags = [406] }, "stagbs06", Owner(1, 100));
        Assert.Equal(StoryRefusal.Stage, other.Refused.Single().Reason);
        var early = k.Take(new StoryDelta { Flags = [407] }, "stagbs06", Owner(2, 200));
        Assert.Equal(StoryRefusal.PhaseJump, early.Refused.Single().Reason);
    }

    [Fact]
    public void Nothing_is_dropped_any_more_a_value_the_story_has_is_no_change()
    {
        var k = Keeper();
        Assert.True(k.Take(new StoryDelta { Flags = [505] }, "stag0101", Visitor(1, 100)).Changed);
        var again = k.Take(new StoryDelta { Flags = [505] }, "stag0101", Visitor(2, 200));
        Assert.False(again.Changed);
        Assert.Empty(again.Refused);
        Assert.DoesNotContain(again.Refused, r => r.Reason is StoryRefusal.OwnerAway or StoryRefusal.NotStarted);
    }
}
