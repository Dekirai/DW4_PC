using DW4Online.Domain.Story;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// The refix of round fix-019 (the review of fix-019): a JOIN took a removed member's seat back (the hold's end of a fresh arrival
/// removed while it waited, a member removed at its barrier); a detached group's former host had his own STORY refused; the matching's
/// "players at the arrival stage" counted players on their way elsewhere; an engine on another's story without a seat there read as
/// at home on its own story in the achievements' play context. Each test fails without its fix.
/// </summary>
public partial class HubLogicTests
{
    [Fact]
    public void A_fresh_arrival_removed_during_its_join_hold_goes_home_and_never_takes_its_seat_back()
    {
        // a hosts at stag0101 in an entry scene (not joinable yet); b, c, d, e come from home and wait for their automatic JOIN. (b's
        // default is Private: the instance it gets after its removal is no starting instance the others would co-arrive in.)
        var h = new Harness(new InstanceOptions { StoryMinVisitSeconds = 1 });
        var history = new List<WorldHistoryEvent>();
        h.Hub.History = history.Add;
        Admit(h, "a", Player(100, "a"));
        Admit(h, "b", Player(101, "b") with { DefaultVisibility = Visibility.Private });
        foreach (var (name, k) in new[] { ("c", 102), ("d", 103), ("e", 104) }) Admit(h, name, Player(k, name));
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a", joinable: false);
        h.Advance(11_000);
        h.Hub.Housekeep(h.Now);
        var w = W(h, "a");
        var b = h.Mem("b");
        h.Barrier("b", "stag0101");
        Assert.True(w.Seats.ContainsKey(b));
        Assert.False(h.P("b").Has(Msg.BarrierOk));
        // a removes b while it waits: b goes home from its barrier at once (round I1b, SendRemovedHome: a DISSOLVE into the lobby on its
        // own story; fix-019's refix had it decide again alone, an instance of its own) and owes no HOME.
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, b.Id).Frame());
        Assert.False(w.Seats.ContainsKey(b));
        var own = h.Decision("b");
        Assert.Equal((BarrierDecision.Dissolve, b.Id, "stagml11"), (own.Decision, own.World.OwnerMember, own.World.RedirectStage.ToLowerInvariant()));
        Assert.Null(b.World);
        Assert.Null(b.HomeWish);
        Assert.Null(b.Arrival);
        // c, d, e take the three free seats while a's scene runs; then a's stage group turns joinable: the JOINs never make a fifth
        // seat and never seat b (before the refix: b's held JOIN took its seat back, "b joined" went to everyone again, a second
        // history row, and e was pushed into an instance of its own).
        h.Barrier("c", "stag0101");
        h.Barrier("d", "stag0101");
        h.Barrier("e", "stag0101");
        Assert.Equal(4, w.Seats.Count);
        h.Joinable("a");
        h.Hub.Housekeep(h.Now);
        Assert.Equal(4, w.Seats.Count);
        Assert.False(w.Seats.ContainsKey(b));
        foreach (var x in new[] { "c", "d", "e" })
        {
            Assert.Same(w, W(h, x));
            Assert.Equal((BarrierDecision.Join, w.Wid), (h.Decision(x).Decision, h.Decision(x).World.Wid));
        }
        Assert.Single(history.OfType<HistoryJoined>(), j => j.World == w && j.Account == b.AccountId);
        Assert.Single(h.P("a").All(Msg.WorldNotice, WorldNoticeMsg.Parse), n => n.Kind == WorldNoticeKind.Joined && n.Member == b.Id);
        Assert.Single(h.P("b").All(Msg.BarrierOk, BarrierOkMsg.Parse));
    }

    [Fact]
    public void A_member_removed_at_its_barrier_goes_home_from_it_and_never_joins_without_a_seat()
    {
        // a hosts and moves on to stag0102; his friend b enters his instance at stag0101 as a guest (a stage group of its own).
        var h = new Harness(new InstanceOptions { StoryMinVisitSeconds = 1 });
        var history = new List<WorldHistoryEvent>();
        h.Hub.History = history.Add;
        Admit(h, "a", Player(100, "a"));
        Admit(h, "b", Player(101, "b") with { Friends = [100] });
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        h.Advance(11_000);
        h.Hub.Housekeep(h.Now);
        var w = W(h, "a");
        h.Barrier("a", "stag0102");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a", joinable: false); // (an entry scene)
        var b = h.Mem("b");
        h.Barrier("b", "stag0101");
        Assert.Equal((BarrierDecision.New, ArrivalKind.GuestNew, w.Wid), (h.Decision("b").Decision, h.Decision("b").World.Arrival, h.Decision("b").World.Wid));
        h.Snapshot("b");
        h.Log("b", 0, 60);
        h.Joinable("b");
        // b follows a to stag0102: a JOIN of a's stage group, held for its scene. a removes b meanwhile.
        h.Barrier("b", "stag0102");
        Assert.Equal(WaitReason.NotJoinable, h.P("b").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason);
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, b.Id).Frame());
        Assert.False(w.Seats.ContainsKey(b));
        // At once (round I1b, SendRemovedHome): b goes home from that barrier, a DISSOLVE into the lobby on its own story (fix-019's
        // refix let its JOIN go on without a seat until its owed HOME; before the refix the JOIN took the seat back past the bar).
        var home = h.Decision("b");
        Assert.Equal((BarrierDecision.Dissolve, StoryMode.Replace, b.Id, "stagml11"),
            (home.Decision, home.World.StoryMode, home.World.OwnerMember, home.World.RedirectStage.ToLowerInvariant()));
        h.Joinable("a");
        h.Hub.Housekeep(h.Now);
        Assert.Single(h.P("b").All(Msg.BarrierOk, BarrierOkMsg.Parse), o => o.Decision == BarrierDecision.Dissolve);
        Assert.DoesNotContain(h.P("b").All(Msg.BarrierOk, BarrierOkMsg.Parse), o => o.Decision == BarrierDecision.Join);
        Assert.False(w.Seats.ContainsKey(b));
        Assert.Null(b.World);
        Assert.Single(history.OfType<HistoryJoined>(), j => j.World == w && j.Account == b.AccountId);
        Assert.Single(h.P("a").All(Msg.WorldNotice, WorldNoticeMsg.Parse), n => n.Kind == WorldNoticeKind.Joined && n.Member == b.Id);
        Assert.True(w.Bars.ContainsKey(b.AccountId));
        Assert.Null(b.HomeWish);
        // Its own story and home context from then on: the achievements read it at home.
        var ctx = h.Hub.PlayContext(b.AccountId, h.Now)!;
        Assert.Equal((0u, true), (ctx.Wid, ctx.IsHost));
    }

    [Fact]
    public void A_detached_groups_former_host_keeps_his_own_story_and_his_home_context()
    {
        // a's instance ends (staff); his solo stage group never answers the END's HOME and is detached: his engine still plays his own
        // story, so his STORY goes to his own keeper (19.8.5: the host's own values stay his). The refix's first version refused it.
        var h = new Harness(new InstanceOptions { StoryMinVisitSeconds = 1 });
        h.Connect("a", 0);
        var a = h.Mem("a");
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        var w = W(h, "a");
        Assert.True(h.Hub.StaffEnd(w.Wid, 999, h.Now));
        for (var k = 0; k < 8 && !h.I("a").Detached; ++k)
        {
            h.Advance(5_100);
            h.Hub.Housekeep(h.Now);
            h.Hub.Housekeep(h.Now);
        }
        Assert.True(h.I("a").Detached);
        Assert.Same(w, h.I("a").DetachedFrom);
        Assert.Null(h.Hub.WorldInstance(w.Wid));
        h.Advance(1500);
        h.Send("a", StoryOf(h, "a", new StoryDelta { Flags = [505] }, a.Id).Frame());
        Assert.Empty(h.P("a").Last(Msg.StoryAck, StoryAckMsg.Parse).Refused);
        Assert.Equal(1, h.Hub.KeeperOf(a.AccountId)!.Story.Flags[505]);
        var ctx = h.Hub.PlayContext(a.AccountId, h.Now)!;
        Assert.Equal((0u, true), (ctx.Wid, ctx.IsHost));
    }

    [Fact]
    public void Players_on_their_way_elsewhere_are_not_here_and_a_stranger_gets_an_instance_of_his_own()
    {
        // a and b play a's Public instance at stag0101; a reaches the exit to stag0102 (the group waits at its barrier, b not yet).
        var h = SealedPair(out _, stage: "stag0101");
        Admit(h, "c", Player(500, "c"));
        h.Seal(500);
        h.Joinable("a");
        h.Hub.Housekeep(h.Now);
        var w = W(h, "a");
        h.Barrier("a", "stag0102", tick: h.I("a").NextSeal + 5);
        Assert.NotNull(h.I("a").Barrier);
        // c, a stranger, comes to stag0101: nobody of a's instance stays there, so the I1a interim rule makes him no guest of theirs
        // (before the refix a and b counted as "here" and c entered a's instance alone in a new stage group at stag0101).
        // (Round I1b: c's loading-screen prompt lists a's row with Here 0 and the cursor on New instance, decision 40; c takes the cursor.)
        h.P("c").AutoAnswer = false;
        h.Barrier("c", "stag0101");
        var o = h.P("c").Last(Msg.Options, OptionsMsg.Parse);
        Assert.Equal(((byte)0, OptionsMsg.NewInstance), (Assert.Single(o.Rows, r => r.Wid == w.Wid).Here, o.Preselected));
        h.Send("c", new ChoiceMsg(0, o.Inst, o.Visit, ChoiceKind.New, 0, o.Visibility, 0, ChoiceMsg.DefaultBackPoint).Frame());
        var ok = h.Decision("c");
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (ok.Decision, ok.World.Arrival));
        Assert.NotSame(w, W(h, "c"));
        Assert.False(w.Seats.ContainsKey(h.Mem("c")));
    }

    [Fact]
    public void A_fallback_from_nobodys_instance_plays_as_the_former_instances_guest_until_its_way_home()
    {
        // b joins a's instance and is removed while it catches up; a goes home and his instance goes; b falls back from nobody's
        // instance (wid 0 in its BARRIER_OK, its engine on a's story and settings until its way home).
        var h = HostAtStag0101("b");
        var w = W(h, "a");
        var b = h.Mem("b");
        h.Barrier("b", "stag0101");
        Assert.NotNull(b.Joining);
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, b.Id).Frame());
        var joining = h.Hub.PlayContext(b.AccountId, h.Now)!;
        Assert.Equal((w.Wid, false), (joining.Wid, joining.IsHost));
        h.Barrier("a", "stagML11");
        Assert.Null(h.Hub.WorldInstance(w.Wid));
        Assert.Equal(0u, h.Decision("b").World.Wid);
        Assert.Same(w, h.I("b").DetachedFrom);
        // Before the refix: Wid 0, IsHost true and b's own settings (story challenges and the ruleset as on the own story).
        var ctx = h.Hub.PlayContext(b.AccountId, h.Now)!;
        Assert.Equal((w.Wid, false, w.Settings.RandomizerActive, w.Settings.Difficulty), (ctx.Wid, ctx.IsHost, ctx.Randomizer, ctx.Difficulty));
        // Its way home (HOME answered, the barrier home): the DISSOLVE's replace gives it its own story, and the context is home again.
        var g = h.I("b");
        h.Snapshot("b");
        h.Log("b", 0, 30);
        h.Send("b", new JoinAtMsg(g.Id, g.Visit, 40).Frame());
        h.Seal(300);
        h.Send("b", new BarrierMsg(g.Id, g.Visit, g.NextSeal, "stagML11", 3, g.PresentMask, g.PresentMask, 5, [(byte)'b']).Frame());
        h.Seal(100);
        Assert.Equal((BarrierDecision.Dissolve, StoryMode.Replace), (h.Decision("b").Decision, h.Decision("b").World.StoryMode));
        var home = h.Hub.PlayContext(b.AccountId, h.Now)!;
        Assert.Equal((0u, true), (home.Wid, home.IsHost));
    }
}
