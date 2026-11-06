using DW4Online.Domain.Story;
using DW4Online.Relay.Hub;

namespace DW4Online.Tests;

/// <summary>
/// Round G-core (SEAMLESS.md 8.8, 19.25 step 1) on the server: flag 503 ("has been to the training field") is story, set
/// only from stag0001; a key item counts only from the stages it comes from (K2 / K3 from stagML02's scripts, the dropped
/// keys from their dungeon floors, measured in the r25c campaign); the rebase at a room's open sets 503 for a story that
/// shows the training was played; earned values are answered Queued (protocol 47) instead of Wait.
/// </summary>
public class GCoreKeeperTests
{
    private static RoomStoryKeeper Keeper(RoomStoryData? story = null, int difficulty = 0) =>
        new(story ?? new RoomStoryData(), difficulty, StoryPolicy.Drop, ownerPresent: true);

    [Fact]
    public void The_sets_hold_503_and_the_key_stages()
    {
        var s = StorySets.Default;
        Assert.True(s.IsPersistent(503));
        Assert.Equal(["stag0001"], s.FlagSetters[503]);
        Assert.Equal(503, s.TrainingFlag);
        Assert.True(s.GivesKey("stagML02", 2) && s.GivesKey("stagml02", 3));
        Assert.False(s.GivesKey("stagML11", 3));
        Assert.True(s.GivesKey("dng101b2", 0) && !s.GivesKey("stag0101", 0));
        Assert.True(s.GivesKey("dng412b4", 10) && !s.GivesKey("dng411b3", 10));
        for (var k = 0; k < s.Keys; ++k) Assert.NotEmpty(s.KeySetters[k]); // every key has a measured stage
        Assert.True(s.ProvesTraining(505));  // stag0101's hint panel: beyond the training
        Assert.False(s.ProvesTraining(501)); // the lobby's intro
        Assert.False(s.ProvesTraining(22));  // a briefing flag (stagML02)
        Assert.False(s.ProvesTraining(503)); // the training itself
    }

    [Fact]
    public void Flag_503_counts_only_from_the_training_field()
    {
        var k = Keeper();
        var t = k.Take(new StoryDelta { Flags = [503] }, "stag0101", 0);
        Assert.Equal(new StoryRefused(StoryKeyKind.Flag, 503, StoryRefusal.Stage), t.Refused.Single());
        Assert.True(k.Take(new StoryDelta { Flags = [503] }, "stag0001", 0).Changed);
        Assert.Equal(1, k.Story.Flags[503]);
    }

    [Fact]
    public void A_key_counts_only_from_its_stages()
    {
        var k = Keeper();
        var t = k.Take(new StoryDelta { Folder = [3] }, "stagML11", 0);
        Assert.Equal(new StoryRefused(StoryKeyKind.Folder, 3, StoryRefusal.Stage), t.Refused.Single());
        Assert.False(t.Changed);
        Assert.True(k.Take(new StoryDelta { Folder = [3] }, "stagML02", 0).Changed);
        // A guest's own key brought into the lobby (the call shop's way before round G) never reaches the owner's story.
        t = k.Take(new StoryDelta { Folder = [9, 10] }, "stagml11", 0);
        Assert.All(t.Refused, r => Assert.Equal(StoryRefusal.Stage, r.Reason));
        Assert.Equal(0, k.Story.Folder[9] + k.Story.Folder[10]);
        Assert.True(k.Take(new StoryDelta { Folder = [9] }, "dng411b3", 0).Changed);
    }

    [Fact]
    public void A_guests_second_key_is_queued_and_merged_after_the_spacing_in_another_stage()
    {
        var k = Keeper();
        StoryVisit Guest(ulong key, double at) => new(1, key, 15, 101, at);
        Assert.True(k.Take(new StoryDelta { Folder = [0] }, "dng101b2", 0, Guest(1, 1000)).Changed);
        var t = k.Take(new StoryDelta { Folder = [1] }, "dng102b3", 0, Guest(2, 1030));
        Assert.Equal(new StoryRefused(StoryKeyKind.Folder, 1, StoryRefusal.Queued), t.Refused.Single());
        Assert.Equal(1, k.EarnedCount);
        // The guest is in the lobby now, which gives no key: the queued key is merged when due all the same.
        Assert.False(k.MergeEarned(Guest(3, 1059), "stagml11").Changed);
        Assert.True(k.MergeEarned(Guest(3, 1061), "stagml11").Changed);
        Assert.Equal(1, k.Story.Folder[1]);
        Assert.Equal(0, k.EarnedCount);
    }

    [Fact]
    public void Visit_age_waits_stay_wait()
    {
        var k = Keeper();
        // A visit younger than the minimum: not earned (a plausibility rule), so the client sends it again.
        Assert.Equal(StoryRefusal.Wait, k.Take(new StoryDelta { Folder = [2] }, "stagML02", 0, new StoryVisit(1, 1, 3, 101, 10)).Refused.Single().Reason);
    }

    [Fact]
    public void A_room_opens_with_503_when_the_owners_story_shows_the_training()
    {
        // Phase 3 in the normal copies (the stored story of a phase-3 owner): 503 = 1 in the room story.
        var three = new RoomStoryData();
        three.Flags[3] = 1;
        three.Flags[702] = 1;
        Assert.Equal(1, Keeper(three).Story.Flags[503]);
        // A phase-2 owner who never left the lobby (the briefing's 22, the lobby's intro 501): no training, 503 = 0.
        var lobby = new RoomStoryData();
        foreach (var f in new[] { 2, 701, 22, 501, 502 }) lobby.Flags[f] = 1;
        Assert.Equal(0, Keeper(lobby).Story.Flags[503]);
        // One training attempt (COUNTER 7) is enough; so is a key item.
        lobby.Counters[7] = 1;
        Assert.Equal(1, Keeper(lobby).Story.Flags[503]);
        // A Hard room of an owner who cleared Normal: the Hard copies hold phase 1 only, 503 stays (no copies of 503).
        var cleared = new RoomStoryData();
        foreach (var f in new[] { 86, 713, 7, 518, 519, 520 }) cleared.Flags[f] = 1;
        var hard = Keeper(cleared, difficulty: 1);
        Assert.Equal(1, StoryMerge.Phase(hard.Story));
        Assert.Equal(1, hard.Story.Flags[503]);
        // The rebase reads the stored story before it rebases: the Normal phase copy 702 proves it in a Very Hard room too.
        Assert.Equal(1, Keeper(three, difficulty: 2).Story.Flags[503]);
        Assert.Equal(0, StoryMerge.Phase(Keeper(three, difficulty: 2).Story));
    }
}
