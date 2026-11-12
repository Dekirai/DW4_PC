using DW4Online.Domain.Rooms;
using DW4Online.Domain.Story;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;

namespace DW4Online.Tests;

/// <summary>
/// The stories in the world hub (SEAMLESS.md 19.8, round I1a's story lane): a keeper per (account, box) loaded at the admission;
/// at home a member's STORY with its own owner key goes to its own keeper and is saved as its own; in an instance every member's
/// STORY goes to the host's keeper with the host's key (another key: *owner*); a revision reaches exactly the members playing on
/// that story; the earned queue merges across owners; the DISSOLVE carries the own story rebased on the own difficulty.
/// </summary>
public partial class HubLogicTests
{
    private static StoryMsg StoryOf(Harness h, string name, StoryDelta delta, uint owner, byte box = 1)
    {
        var i = h.Mem(name).Instance!;
        return new StoryMsg(i.Id, i.Visit, 0, delta, owner, box);
    }

    [Fact]
    public void At_home_a_members_story_is_its_own_and_is_saved_for_its_account()
    {
        var h = new Harness(new InstanceOptions { StoryMinVisitSeconds = 1 });
        var saved = new List<(long Account, RoomStoryData Story)>();
        h.Hub.StorySaver = (a, s, _) => saved.Add((a, s));
        Admit(h, "a", Player(100, "a"));
        Admit(h, "b", Player(101, "b"));
        h.Barrier("a", "stagml12"); // a hub stage: home
        h.Advance(1500);
        h.Send("a", StoryOf(h, "a", new StoryDelta { Flags = [392] }, h.Mem("a").Id).Frame());
        var ack = h.P("a").Last(Msg.StoryAck, StoryAckMsg.Parse);
        Assert.Equal((1u, h.Mem("a").Id, (byte)1), (ack.Revision, ack.OwnerMember, ack.OwnerBox));
        Assert.Equal([100L], saved.Select(x => x.Account));
        Assert.Equal(1, h.Hub.StoryState(100)!.Flags.Count(f => f == 392));
        // b hears nothing of a's own story.
        Assert.DoesNotContain(h.P("b").All(Msg.StoryAck, StoryAckMsg.Parse), x => x.OwnerMember == h.Mem("a").Id);
        // A key that is not the member's current owner (b's id) is refused: owner.
        h.Send("a", StoryOf(h, "a", new StoryDelta { Flags = [393] }, h.Mem("b").Id).Frame());
        Assert.Equal(StoryRefusal.Owner, h.P("a").Last(Msg.StoryAck, StoryAckMsg.Parse).Refused.Single().Reason);
    }

    [Fact]
    public void In_an_instance_every_members_story_goes_to_the_hosts_keeper_and_reaches_the_instances_members_only()
    {
        var h = SealedPair(out _, instances: new InstanceOptions { StoryMinVisitSeconds = 1 }, stage: "stag0101"); // (505 / 506 are set there)
        h.Connect("c", 2);
        var saved = new List<long>();
        h.Hub.StorySaver = (a, _, _) => saved.Add(a);
        var host = h.Mem("a");
        h.Seal(1500);
        h.Send("b", StoryOf(h, "b", new StoryDelta { Flags = [505] }, host.Id).Frame());
        Assert.Equal((1u, host.Id), (h.P("b").Last(Msg.StoryAck, StoryAckMsg.Parse).Revision, h.P("b").Last(Msg.StoryAck, StoryAckMsg.Parse).OwnerMember));
        Assert.Equal(1u, h.P("a").Last(Msg.StoryAck, StoryAckMsg.Parse).Revision); // the host follows
        Assert.DoesNotContain(h.P("c").All(Msg.StoryAck, StoryAckMsg.Parse), x => x.OwnerMember == host.Id && x.Revision == 1);
        Assert.Equal([host.AccountId], saved);
        Assert.Empty(h.Hub.StoryState(h.Mem("b").AccountId)!.Flags); // the guest's own story untouched
        // A guest's own key while it plays on the host's story: owner.
        h.Send("b", StoryOf(h, "b", new StoryDelta { Flags = [506] }, h.Mem("b").Id).Frame());
        Assert.Equal(StoryRefusal.Owner, h.P("b").Last(Msg.StoryAck, StoryAckMsg.Parse).Refused.Single().Reason);
    }

    [Fact]
    public void A_guests_queued_value_merges_into_the_hosts_story_when_due()
    {
        var h = new Harness(new InstanceOptions { StoryMinVisitSeconds = 1, StoryFlagBurst = 1, StoryFlagSeconds = 30 });
        Admit(h, "a", Player(100, "a") with { Story = StoryAt(2) });
        Admit(h, "b", Player(101, "b"));
        // a hosts and walks on; b enters a's instance at stag0101 as a guest (visited), a stage group of its own.
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Barrier("a", "stag0102");
        h.Snapshot("a");
        h.Barrier("b", "stag0101");
        h.Snapshot("b");
        Assert.Equal(ArrivalKind.GuestNew, h.Decision("b").World.Arrival);
        var host = h.Mem("a");
        var keeper = h.Hub.KeeperOf(host.AccountId)!;
        h.Advance(1500);
        h.Send("b", StoryOf(h, "b", new StoryDelta { Flags = [505, 506] }, host.Id).Frame());
        var ack = h.P("b").Last(Msg.StoryAck, StoryAckMsg.Parse);
        Assert.Equal(StoryRefusal.Queued, ack.Refused.Single().Reason); // one flag at once for a guest, the other earned
        Assert.Equal(1, keeper.EarnedCount);
        h.Advance(31_000);
        h.Housekeep();
        Assert.Equal((1, 1, 0), (keeper.Story.Flags[505] + 0, keeper.Story.Flags[506] + 0, keeper.EarnedCount));
        Assert.Empty(h.Hub.StoryState(101)!.Flags);
    }
    [Fact]
    public void The_dissolve_carries_the_own_story_rebased_on_the_own_difficulty()
    {
        var h = new Harness();
        var own = new RoomStoryData { Revision = 3 };
        own.Flags[2] = 1; // phase 2 on Normal ...
        StoryMerge.DeriveCopies(own, 0);
        Admit(h, "a", Player(100, "a") with { Story = own });
        // a leaves home with Hard as its own difficulty (its BARRIER's settings), plays, and goes home.
        BarrierWith(h, "a", "stag0101", new RoomSettings(1, 0, 1, 0, false, 0, 0, false));
        h.Snapshot("a");
        BarrierWith(h, "a", "stagML11", new RoomSettings(1, 0, 1, 0, false, 0, 0, false));
        var ok = h.Decision("a");
        Assert.Equal((BarrierDecision.Dissolve, StoryMode.Merge), (ok.Decision, ok.World.StoryMode)); // the host's story is his own
        Assert.Equal(1, ok.World.Settings.Difficulty);
        Assert.Equal(0, StoryMerge.Phase(RoomStoryData.FromBlob(ok.Story, ok.StoryRevision))); // ... nothing on Hard
        Assert.Equal(1, h.Hub.KeeperOf(100)!.Difficulty);
    }
}
