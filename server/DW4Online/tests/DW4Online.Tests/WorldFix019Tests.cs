using DW4Online.Domain.Rooms;
using DW4Online.Domain.Story;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// Round fix-019 (the review of DW4Online 0.1.7): the story leaks and seat rules the review found, each with a test that fails
/// without its fix. A guest's STORY after its instance's END; an engine on another's story (a removed member, a stage group of
/// nobody's instance) writing the own story with its own key; a removed joiner's fallback, which got an own instance on its own
/// story by a merge while its engine held the former host's, or its seat back past the removal bar and the four seats; the
/// staff actions' run. (Findings 1 and 2, the automatic join and the staff visibility, are in the lifecycle and review tests.)
/// </summary>
public partial class HubLogicTests
{
    /// <summary>a hosts at stag0101 (joinable, past its starting window); the others are at home.</summary>
    private static Harness HostAtStag0101(params string[] others)
    {
        var h = new Harness(new InstanceOptions { StoryMinVisitSeconds = 1 });
        h.Connect("a", 0);
        for (var k = 0; k < others.Length; ++k) h.Connect(others[k], k + 1);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        h.Advance(11_000);
        h.Hub.Housekeep(h.Now);
        return h;
    }

    [Fact]
    public void A_guests_story_after_its_instance_ended_never_reaches_the_hosts_keeper()
    {
        foreach (var how in new[] { "staff", "host_left" })
        {
            var h = SealedPair(out _, instances: new InstanceOptions { StoryMinVisitSeconds = 1 }, stage: "stag0101"); // (505 / 506 are set there)
            var host = h.Mem("a");
            var keeper = h.Hub.KeeperOf(host.AccountId)!;
            var w = W(h, "a");
            h.Seal(1500);
            // While the instance runs a guest's flag counts for the host (19.8.5) ...
            h.Send("b", StoryOf(h, "b", new StoryDelta { Flags = [505] }, host.Id).Frame());
            Assert.Equal(1, keeper.Story.Flags[505]);
            if (how == "staff") h.Hub.StaffEnd(w.Wid, 999, h.Now);
            else h.Send("a", new IntentMsg(h.Inst("a"), h.I("a").Visit, IntentKind.Leave, 0).Frame());
            Assert.True(w.Ending);
            // ... after its END not (the guest's engine goes home and is replaced; its values are discarded with the instance): before
            // fix-019 the STORY with the host's key, still the guest's owner key while its seat waits for its DISSOLVE, merged.
            h.Send("b", StoryOf(h, "b", new StoryDelta { Flags = [506] }, host.Id).Frame());
            var ack = h.P("b").Last(Msg.StoryAck, StoryAckMsg.Parse);
            Assert.Equal(StoryRefusal.Owner, ack.Refused.Single().Reason);
            Assert.Empty(ack.Story);
            Assert.Equal((0, 0), (keeper.Story.Flags[506] + 0, keeper.EarnedCount));
            Assert.Empty(h.Hub.StoryState(h.Mem("b").AccountId)!.Flags);
            // The host's own values stay his: his keeper is his own story.
            h.Send("a", StoryOf(h, "a", new StoryDelta { Flags = [506] }, host.Id).Frame());
            Assert.Equal(1, keeper.Story.Flags[506]);
        }
    }

    [Fact]
    public void A_removed_members_engine_on_the_hosts_story_writes_no_story_whatever_its_key_and_its_next_barrier_goes_home()
    {
        // b is removed from a's sealed stage group: its seat and story owner go at once (19.6.6); its engine plays a's story until
        // its HOME. A modified client's STORY with b's own key reached b's own keeper before fix-019 (the owner key matched).
        var h = SealedPair(out _, instances: new InstanceOptions { StoryMinVisitSeconds = 1 }, stage: "stag0101");
        var b = h.Mem("b");
        h.Seal(1500);
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, b.Id).Frame());
        Assert.Null(b.World);
        h.Send("b", StoryOf(h, "b", new StoryDelta { Flags = [505] }, b.Id).Frame());
        Assert.Equal(StoryRefusal.Owner, h.P("b").Last(Msg.StoryAck, StoryAckMsg.Parse).Refused.Single().Reason);
        h.Send("b", StoryOf(h, "b", new StoryDelta { Flags = [505] }, h.Mem("a").Id).Frame());
        Assert.Equal(StoryRefusal.Owner, h.P("b").Last(Msg.StoryAck, StoryAckMsg.Parse).Refused.Single().Reason);
        Assert.Empty(h.Hub.StoryState(b.AccountId)!.Flags);
        Assert.Equal(0, h.Hub.KeeperOf(h.Mem("a").AccountId)!.Story.Flags[505]);

        // The host leaves the world: the instance goes with its last seat, its stage group (b alone in it now) is nobody's. A barrier
        // of b into a field (a client that ignored its HOME) was a fresh arrival from home before fix-019: an own instance whose
        // merge carried a's story into b's. Now it goes home with the replace.
        var w = W(h, "a");
        h.Hub.Leave(h.Mem("a").AccountId, h.Now);
        Assert.Null(h.Hub.WorldInstance(w.Wid));
        Assert.True(h.I("b").Detached);
        h.Send("b", StoryOf(h, "b", new StoryDelta { Flags = [506] }, b.Id).Frame());
        Assert.Equal(StoryRefusal.Owner, h.P("b").Last(Msg.StoryAck, StoryAckMsg.Parse).Refused.Single().Reason);
        Assert.Empty(h.Hub.StoryState(b.AccountId)!.Flags);
        var i = h.I("b");
        h.Send("b", new BarrierMsg(i.Id, i.Visit, i.SealStop ?? i.NextSeal, "stag0102", 0, i.PresentMask, i.PresentMask, 5, [(byte)'b']).Frame());
        h.Seal(100);
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.Dissolve, RoomHub.HomeStage, StoryMode.Replace, b.Id, 0u),
            (ok.Decision, ok.World.RedirectStage, ok.World.StoryMode, ok.World.OwnerMember, ok.World.Wid));
        Assert.DoesNotContain(h.Hub.Worlds, x => x.Host == b);
    }

    [Fact]
    public void A_removed_joiner_whose_instance_is_gone_goes_home_from_nobodys_instance_on_its_own_story()
    {
        var h = HostAtStag0101("b");
        var w = W(h, "a");
        var b = h.Mem("b");
        h.Barrier("b", "stag0101");
        Assert.Equal(BarrierDecision.Join, h.Decision("b").Decision);
        Assert.NotNull(b.Joining);
        // a removes b while it still loads (catches up); then a goes home by the gate disc: the instance ends and goes with its last
        // seat, b's target stage group ends, b falls back.
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, b.Id).Frame());
        Assert.False(w.Seats.ContainsKey(b));
        h.Barrier("a", "stagML11");
        Assert.Null(h.Hub.WorldInstance(w.Wid));
        // Before fix-019: a new instance of b's own (CreateWorld, arrival NewInstance, the merge of b's own story) while b's engine held
        // a's story from its JOIN. Now: a stage group of nobody's instance, no story in the answer, HOME at once.
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.New, 0u, ArrivalKind.None, 0), (ok.Decision, ok.World.Wid, ok.World.Arrival, ok.Story.Length));
        Assert.Equal(h.Mem("a").Id, ok.World.OwnerMember); // (the key its engine's story has: the former host's)
        Assert.Null(b.World);
        Assert.DoesNotContain(h.Hub.Worlds, x => x.Host == b);
        var g = h.I("b");
        Assert.True(g.Detached);
        var home = h.P("b").Last(Msg.JoinReq, JoinReqMsg.Parse);
        Assert.Equal((JoinReqKind.Home, g.Id), (home.Kind, home.Inst));
        // Its STORY (either key) reaches no keeper.
        h.Snapshot("b");
        h.Advance(1500);
        h.Send("b", StoryOf(h, "b", new StoryDelta { Flags = [505] }, b.Id).Frame());
        h.Send("b", StoryOf(h, "b", new StoryDelta { Flags = [506] }, ok.World.OwnerMember).Frame());
        Assert.Empty(h.Hub.StoryState(b.AccountId)!.Flags);
        Assert.Equal((0, 0), (h.Hub.KeeperOf(h.Mem("a").AccountId)!.Story.Flags[505] + 0, h.Hub.KeeperOf(h.Mem("a").AccountId)!.Story.Flags[506] + 0));
        // Unanswered, the HOME is asked again by the housekeeping (as an instance's group's).
        h.Advance(5_100);
        h.Hub.Housekeep(h.Now);
        h.Hub.Housekeep(h.Now); // (the timeout files it again, the next pass asks)
        Assert.Equal(2, h.P("b").All(Msg.JoinReq, JoinReqMsg.Parse).Count(r => r.Kind == JoinReqKind.Home));
        // b answers: HOME sealed at J; its way home is a DISSOLVE with the replace of its own story.
        h.Log("b", 0, 30);
        h.Send("b", new JoinAtMsg(g.Id, g.Visit, 40).Frame());
        Assert.Contains(g.PendingEvents.Values.SelectMany(l => l), e => e.Type == Ev.Home);
        h.Seal(300);
        h.Send("b", new BarrierMsg(g.Id, g.Visit, g.NextSeal, "stagML11", 3, g.PresentMask, g.PresentMask, 5, [(byte)'b']).Frame());
        h.Seal(100);
        var back = h.Decision("b");
        Assert.Equal((BarrierDecision.Dissolve, StoryMode.Replace, b.Id), (back.Decision, back.World.StoryMode, back.World.OwnerMember));
    }

    [Fact]
    public void A_removed_joiners_fallback_never_takes_a_seat_back_past_the_bar_or_the_four_seats()
    {
        var h = HostAtStag0101("b", "c", "d", "e");
        var w = W(h, "a");
        var b = h.Mem("b");
        h.Barrier("b", "stag0101");
        Assert.NotNull(b.Joining);
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, b.Id).Frame());
        Assert.True(w.Bars.ContainsKey(b.AccountId));
        // c and d join a's stage group (its last slots, b's still reserved), e enters as a guest at the stage (the group is full):
        // four seats.
        h.Barrier("c", "stag0101");
        h.Barrier("d", "stag0101");
        h.Barrier("e", "stag0101");
        Assert.Equal(4, w.Seats.Count);
        Assert.Same(w, W(h, "e"));
        // a walks on to stag0102: its stage group's joiners fall back. c and d keep their seats (guests' NEWs in a's instance); b,
        // barred and without a seat, took a fifth seat before fix-019.
        h.Barrier("a", "stag0102");
        Assert.Equal(4, w.Seats.Count);
        Assert.False(w.Seats.ContainsKey(b));
        Assert.Same(w, W(h, "c"));
        Assert.Same(w, W(h, "d"));
        Assert.True(h.I("b").Detached);
        Assert.Equal(0u, h.Decision("b").World.Wid);
        Assert.DoesNotContain(h.I("b"), w.Groups);
        // b's next barrier goes home, never into a's instance.
        var i = h.I("b");
        h.Snapshot("b");
        h.Send("b", new BarrierMsg(i.Id, i.Visit, 10, "stag0103", 0, i.PresentMask, i.PresentMask, 5, [(byte)'b']).Frame());
        Assert.Equal((BarrierDecision.Dissolve, RoomHub.HomeStage, StoryMode.Replace), (h.Decision("b").Decision, h.Decision("b").World.RedirectStage,
            h.Decision("b").World.StoryMode));
        Assert.False(w.Seats.ContainsKey(b));
    }

    [Fact]
    public void The_staff_actions_name_the_relays_run_and_another_runs_wid_is_refused()
    {
        var h = SealedPair(out _);
        var w = W(h, "a");
        h.Hub.Housekeep(h.Now);
        Assert.Equal(h.Hub.RunId, h.Hub.CachedSnapshot.Run);
        Assert.Matches("^[0-9a-f]{16}$", h.Hub.RunId);
        // A staff page of an earlier run (a server restart: wids count from 1 again) names the same wid: refused, nothing ends.
        Assert.False(h.Hub.StaffRemove(w.Wid, h.Mem("b").AccountId, 999, h.Now, "00000000deadbeef"));
        Assert.False(h.Hub.StaffEnd(w.Wid, 999, h.Now, "00000000deadbeef"));
        Assert.False(w.Ending);
        Assert.Same(w, W(h, "b"));
        Assert.NotEqual(h.Hub.RunId, new Harness().Hub.RunId);
        Assert.True(h.Hub.StaffEnd(w.Wid, 999, h.Now, h.Hub.RunId));
        Assert.Equal(EndReason.Staff, w.EndReason);
    }
}
