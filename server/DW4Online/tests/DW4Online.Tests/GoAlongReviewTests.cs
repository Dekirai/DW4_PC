using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// Round I1b's go along, the review's cases (2026-11-12): *B: Back to the lobby* from a held GO barrier of several members, a *Cancel*
/// that crosses the stamp, the client's report that its GO ended without a departure, the session context of a GO (3.7), and the
/// re-entry's 120 s counted from the member's own leave.
/// </summary>
public partial class HubLogicTests
{
    /// <summary>a (the host), b and c co-arrive in stag0201 (one sealed stage group); <paramref name="entrant"/> forks off alone and walks
    /// into dng204b1: the offer opens.</summary>
    private static Harness GoTrio(out GoOffer offer, string entrant = "c", WorldHubOptions? world = null)
    {
        var h = new Harness(world: world);
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Connect("c", 2);
        foreach (var n in new[] { "a", "b", "c" }) h.Send(n, new HeadingMsg(h.Inst(n), "stag0201", 0, 5).Frame());
        foreach (var n in new[] { "a", "b", "c" }) h.Barrier(n, "stag0201");
        h.Snapshot("a");
        foreach (var n in new[] { "b", "c" })
        {
            var ok = h.Decision(n);
            h.Send(n, new SnapHashMsg(ok.NewInst, ok.NewVisit, 0xAA).Frame());
        }
        Assert.True(h.I("a").Sealing);
        h.Joinable("a");
        h.Seal(500);
        Assert.Equal(3, h.I("a").Count);
        Assert.Same(W(h, "a").Host, h.Mem("a"));
        ForkInto(h, entrant, "dng204b1");
        offer = Assert.Single(W(h, "a").Offers);
        return h;
    }

    /// <summary><paramref name="name"/> forks off its sealed stage group alone (an exit) and parks at the barrier into <paramref name="stage"/>.</summary>
    private static void ForkInto(Harness h, string name, string stage, ulong context = 5)
    {
        var i = h.I(name);
        var bit = (byte)(1 << h.Mem(name).Slot);
        h.Send(name, new HeadingMsg(i.Id, stage, 0, context, 0).Frame());
        foreach (var x in i.Members.Select(x => x.Name).ToList()) h.Send(x, new SplitMsg(i.Id, i.Visit, i.NextSeal - 1, bit, SplitKind.Exit).Frame());
        h.Seal(200);
        h.Barrier(name, stage, context: context);
    }

    /// <summary>The members <paramref name="goers"/> of one sealed stage group answer the offer while its floor loads, are stamped at one
    /// tick (one departure of them all, one barrier), and the floor turns busy in the second of the departure: their barrier is held.</summary>
    private static void HeldGoBarrier(Harness h, GoOffer o, string floor, params string[] goers)
    {
        var s = h.I(goers[0]);
        h.Joinable(goers[0]);
        foreach (var x in goers) h.Send(x, new IntentMsg(s.Id, s.Visit, IntentKind.GoAlong, o.Id).Frame());
        Assert.All(goers, x => Assert.False(h.Mem(x).Go!.Stamped));
        h.Snapshot(floor);
        h.Joinable(floor);
        Assert.All(goers, x => Assert.True(h.Mem(x).Go!.Stamped));
        Assert.Single(goers.Select(x => h.Mem(x).Go!.StampTick).Distinct());
        h.Seal(300);
        h.Joinable(floor, joinable: false);
        foreach (var x in goers) h.Barrier(x, "dng204b1");
        Assert.All(goers, x => Assert.Same(h.Mem(goers[0]).Arrival, h.Mem(x).Arrival));
        Assert.Equal(goers.Length, h.Mem(goers[0]).Arrival!.Members.Count);
    }

    /// <summary>A stamped GO reaches its barrier (sealed: the GO's tick sealed; solo-led: JOIN_REQ kind go answered first).</summary>
    private static void GoBarrierOf(Harness h, string name, string stage)
    {
        var bi = h.I(name);
        if (bi.Mode != InstanceMode.Sealed)
        {
            var req = h.P(name).Last(Msg.JoinReq, JoinReqMsg.Parse);
            h.Log(name, 0, 300);
            h.Send(name, new JoinAtMsg(req.Inst, req.Visit, 300).Frame());
        }
        h.Seal(300);
        h.Barrier(name, stage);
    }

    private static int OkCount(Harness h, string name) => h.P(name).All(Msg.BarrierOk, BarrierOkMsg.Parse).Count;

    [Fact]
    public void Back_to_the_lobby_from_a_held_go_barrier_takes_only_the_member_that_pressed_it_home()
    {
        // a (the host) and b answer c's offer: one barrier with both. b's B is b's own *Leave instance* (19.30.7 step 4).
        var h = GoTrio(out var o);
        var w = W(h, "a");
        HeldGoBarrier(h, o, "c", "a", "b");
        var aOks = OkCount(h, "a");
        var bi = h.Mem("b").Instance!;
        h.Send("b", new IntentMsg(bi.Id, bi.Visit, IntentKind.Leave, 0).Frame());
        Assert.Equal((BarrierDecision.Dissolve, "stagML11"), (h.Decision("b").Decision, h.Decision("b").World.RedirectStage));
        Assert.Null(h.Mem("b").World);
        Assert.Equal(aOks, OkCount(h, "a")); // the host still waits for c's floor
        Assert.False(w.Ending);
        Assert.Equal([h.Mem("a")], h.Mem("a").Arrival!.Members.Select(x => x.Member));
        h.Joinable("c");
        Assert.Equal((BarrierDecision.Join, h.I("c").Id), (h.Decision("a").Decision, h.Decision("a").NewInst));

        // The host's B (after "End the instance for everyone?") ends the instance: everyone at that barrier goes home with it.
        var e = GoTrio(out var o2);
        var w2 = W(e, "a");
        HeldGoBarrier(e, o2, "c", "a", "b");
        var ai = e.Mem("a").Instance!;
        e.Send("a", new IntentMsg(ai.Id, ai.Visit, IntentKind.Leave, 0).Frame());
        Assert.True(w2.Ending);
        Assert.Equal((BarrierDecision.Dissolve, BarrierDecision.Dissolve), (e.Decision("a").Decision, e.Decision("b").Decision));

        // Two guests stamped together (the host is the entrant): b's B takes b home, c waits on, the instance runs.
        var g = GoTrio(out var o3, entrant: "a");
        var w3 = W(g, "a");
        HeldGoBarrier(g, o3, "a", "b", "c");
        var cOks = OkCount(g, "c");
        var gi = g.Mem("b").Instance!;
        g.Send("b", new IntentMsg(gi.Id, gi.Visit, IntentKind.Leave, 0).Frame());
        Assert.Equal(BarrierDecision.Dissolve, g.Decision("b").Decision);
        Assert.Equal(cOks, OkCount(g, "c"));
        Assert.False(w3.Ending);
        Assert.NotNull(g.Mem("c").Arrival);
    }

    [Fact]
    public void Back_to_the_lobby_of_a_guest_leaves_a_defeated_host_the_side_rule_took_along_waiting()
    {
        // a (the host) is defeated beside b; b answers c's offer alone, and the side rule takes a along (b is the last living member,
        // W31d's pattern): one barrier of both, a not alive in it.
        var h = GoTrio(out var o);
        var w = W(h, "a");
        var s = h.I("b");
        h.Joinable("b");
        h.Send("b", new IntentMsg(s.Id, s.Visit, IntentKind.GoAlong, o.Id).Frame());
        h.Snapshot("c");
        h.Joinable("c");
        Assert.True(h.Mem("b").Go!.Stamped);
        h.Seal(300);
        h.Joinable("c", joinable: false);
        var alive = (byte)(1 << h.Mem("b").Slot);
        foreach (var x in new[] { "a", "b" })
            h.Send(x, new BarrierMsg(s.Id, s.Visit, 0, "dng204b1", 0, alive, s.PresentMask, 5, [(byte)x[0]]).Frame());
        Assert.Equal(2, h.Mem("b").Arrival!.Members.Count);
        Assert.False(w.Ending); // (no M3: a living member is in the barrier)
        var aOks = OkCount(h, "a");
        h.Send("b", new IntentMsg(s.Id, s.Visit, IntentKind.Leave, 0).Frame());
        Assert.Equal(BarrierDecision.Dissolve, h.Decision("b").Decision);
        Assert.Equal(aOks, OkCount(h, "a"));
        Assert.False(w.Ending);
        Assert.NotNull(h.Mem("a").Arrival);
    }

    [Fact]
    public void A_cancel_before_the_gos_tick_is_sealed_withdraws_it_one_after_it_keeps_the_go_and_its_barrier_rules()
    {
        // The GO's tick is not sealed yet: it leaves the pending events, nothing reaches a stream.
        var h = GoTrio(out var o);
        var s = h.I("b");
        h.Joinable("b");
        h.Send("b", new IntentMsg(s.Id, s.Visit, IntentKind.GoAlong, o.Id).Frame());
        h.Snapshot("c");
        h.Joinable("c");
        Assert.True(h.Mem("b").Go!.Stamped);
        h.Send("b", new IntentMsg(s.Id, s.Visit, IntentKind.GoAlong, 0).Frame());
        Assert.Null(h.Mem("b").Go);
        h.Seal(300);
        Assert.DoesNotContain(h.P("b").Ticks().SelectMany(t => t.Rec.Events), e => e.Type == Ev.Go);
        Assert.Equal(0, Offers(h, "b").Last().Flags & (OfferMsg.Stamped | OfferMsg.Waiting));

        // The GO is sealed (the *Cancel* crossed the stamp): it runs on every engine, so the request stays and the row says
        // "Going along..."; its barrier keeps the GO's rules: c's floor busy, no new stage group next to c's after 40 s.
        var k = GoTrio(out var o2);
        var ks = k.I("b");
        k.Joinable("b");
        k.Send("b", new IntentMsg(ks.Id, ks.Visit, IntentKind.GoAlong, o2.Id).Frame());
        k.Snapshot("c");
        k.Joinable("c");
        var tick = k.Mem("b").Go!.StampTick;
        k.Seal(300);
        Assert.Contains(k.P("b").Ticks(), t => t.Tick == tick && t.Rec.Events.Any(e => e.Type == Ev.Go));
        k.Send("b", new IntentMsg(ks.Id, ks.Visit, IntentKind.GoAlong, 0).Frame());
        Assert.True(k.Mem("b").Go is { Stamped: true, WithdrawLate: true });
        Assert.Equal(1, k.Hub.Counters["go_withdraw_late"]);
        Assert.Equal(OfferMsg.Stamped, Offers(k, "b").Last().Flags & OfferMsg.Stamped);
        k.Joinable("c", joinable: false);
        ForkInto(k, "b", "dng204b1");
        k.Advance(40_000);
        k.Hub.Housekeep(k.Now);
        Assert.NotNull(k.Mem("b").Arrival);
        Assert.Single(W(k, "a").Groups, x => !x.Ended && x.Stage == "dng204b1");
        k.Joinable("c");
        Assert.Equal((BarrierDecision.Join, k.I("c").Id), (k.Decision("b").Decision, k.Decision("b").NewInst));

        // The client's report that its stamped GO did nothing (defeated at its step) or expired (INTENT 4, -1) ends the request.
        var d = GoTrio(out var o3);
        var ds = d.I("b");
        d.Joinable("b");
        d.Send("b", new IntentMsg(ds.Id, ds.Visit, IntentKind.GoAlong, o3.Id).Frame());
        d.Snapshot("c");
        d.Joinable("c");
        d.Seal(300);
        d.Send("b", new IntentMsg(ds.Id, ds.Visit, IntentKind.GoAlong, RoomHub.GoEndedArgument).Frame());
        Assert.Null(d.Mem("b").Go);
        Assert.Equal(0, Offers(d, "b").Last().Flags & OfferMsg.Stamped);
    }

    [Fact]
    public void A_cancel_while_the_join_req_go_is_out_withdraws_it_and_the_join_at_seals_the_lead_alone()
    {
        var h = GoPair(out var o);
        h.Snapshot("a");
        h.Joinable("a");
        var bi = h.I("b");
        Assert.Equal(InstanceMode.SoloLed, bi.Mode);
        h.Joinable("b");
        h.Send("b", new IntentMsg(bi.Id, bi.Visit, IntentKind.GoAlong, o.Id).Frame());
        var req = h.P("b").Last(Msg.JoinReq, JoinReqMsg.Parse);
        Assert.Equal(JoinReqKind.Go, req.Kind);
        h.Send("b", new IntentMsg(bi.Id, bi.Visit, IntentKind.GoAlong, 0).Frame());
        Assert.Null(h.Mem("b").Go);
        // (The client answered the JOIN_REQ before it heard of the withdraw's effect.)
        h.Log("b", 0, 300);
        h.Send("b", new JoinAtMsg(req.Inst, req.Visit, 300).Frame());
        h.Seal(200);
        var events = h.P("b").Ticks().SelectMany(t => t.Rec.Events).ToList();
        Assert.DoesNotContain(events, e => e.Type == Ev.Go);
        Assert.Contains(events, e => e.Type == Ev.LeadFrom);
        Assert.Equal(1, h.Hub.Counters["go_join_at_withdrawn"]);
    }

    [Fact]
    public void A_go_never_joins_a_floor_whose_play_changed_the_context_but_a_floors_own_load_is_no_change()
    {
        // a's dng204b1 loads with the entry context 5 and a quest step there makes it 6: b's request (context 5) ends, not stamped.
        var h = GoPair(out var o);
        h.Snapshot("a");
        h.Joinable("a", context: 5);
        h.Joinable("a", context: 6);
        var bi = h.I("b");
        h.Joinable("b");
        h.Send("b", new IntentMsg(bi.Id, bi.Visit, IntentKind.GoAlong, o.Id).Frame());
        Assert.Null(h.Mem("b").Go);
        var refused = h.P("b").Last(Msg.WorldNotice, WorldNoticeMsg.Parse);
        Assert.Equal((WorldNoticeKind.GoRefused, (uint)GoRefusal.NotOffered), (refused.Kind, refused.Argument));
        Assert.Equal(1, h.Hub.Counters["go_context_changed"]);

        // B1's own load changed a context value (COUNTER 2) and a went on to B2: a GO from the field (context 5) still goes to B2.
        var d = GoPair(out var od);
        d.Snapshot("a");
        d.Joinable("a", context: 7);
        d.Barrier("a", "dng204b2", context: 7);
        d.Snapshot("a");
        d.Joinable("a", context: 7);
        Assert.Equal("dng204b2", od.Stage);
        var di = d.I("b");
        d.Joinable("b");
        d.Send("b", new IntentMsg(di.Id, di.Visit, IntentKind.GoAlong, od.Id).Frame());
        Assert.True(d.Mem("b").Go!.Stamped);
        GoBarrierOf(d, "b", "dng204b2");
        Assert.Equal((BarrierDecision.Join, d.I("a").Id), (d.Decision("b").Decision, d.Decision("b").NewInst));

        // A quest step on B1 (after its load) before a went on to B2: the entrants carry the change, the GO is refused.
        var q = GoPair(out var oq);
        q.Snapshot("a");
        q.Joinable("a", context: 7);
        q.Joinable("a", context: 8);
        q.Barrier("a", "dng204b2", context: 8);
        Assert.True(oq.ContextChanged);
        q.Snapshot("a");
        q.Joinable("a", context: 8);
        var qi = q.I("b");
        q.Joinable("b");
        q.Send("b", new IntentMsg(qi.Id, qi.Visit, IntentKind.GoAlong, oq.Id).Frame());
        Assert.Null(q.Mem("b").Go);

        // The context changes after the stamp (in the second of the departure): the GO's barrier does not join (provisional: a stage
        // group of its own there with the notice OtherContext, as any arrival in another context; SEAMLESS.md 19.32 asks the user).
        var c = GoPair(out var oc);
        c.Snapshot("a");
        c.Joinable("a", context: 5);
        var ci = c.I("b");
        c.Joinable("b");
        c.Send("b", new IntentMsg(ci.Id, ci.Visit, IntentKind.GoAlong, oc.Id).Frame());
        Assert.True(c.Mem("b").Go!.Stamped);
        c.Joinable("a", context: 6);
        GoBarrierOf(c, "b", "dng204b1");
        Assert.Equal((BarrierDecision.New, BarrierNotice.OtherContext), (c.Decision("b").Decision, c.Decision("b").Notice));
        Assert.Equal(1, c.Hub.Counters["go_context_changed"]);
    }

    [Fact]
    public void A_re_entry_is_silent_only_within_two_minutes_of_the_members_own_leave_and_never_with_a_member_never_named()
    {
        // c is in dng204b1 (the offer's first entrant); b goes into the same dungeon too (its B2: no join needed here), leaves it
        // while c stays in, and comes back: within 120 s of its leave silently, after them with the notice for a.
        var h = GoTrio(out var o);
        h.Snapshot("c");
        h.Joinable("c");
        ForkInto(h, "b", "dng204b2");
        Assert.Equal(OfferMsg.Notice, Offers(h, "a").Last().Flags & OfferMsg.Notice); // (b went in: news)
        h.Snapshot("b");
        h.Barrier("b", "stag0202");
        h.Snapshot("b");
        h.Advance(30_000);
        h.Hub.Housekeep(h.Now);
        var before = Offers(h, "a").Count;
        h.Barrier("b", "dng204b2");
        Assert.All(Offers(h, "a").Skip(before), x => Assert.Equal(0, x.Flags & OfferMsg.Notice));
        h.Snapshot("b");
        h.Barrier("b", "stag0202");
        h.Snapshot("b");
        h.Advance(121_000);
        h.Hub.Housekeep(h.Now);
        before = Offers(h, "a").Count;
        h.Barrier("b", "dng204b2");
        Assert.Contains(Offers(h, "a").Skip(before), x => (x.Flags & OfferMsg.Notice) != 0 && x.Id == o.Id);

        // Once the offer closed, the 120 s count from its close (19.30.5): b left 150 s ago, c (the last one) 50 s ago.
        var r = GoTrio(out var or);
        r.Snapshot("c");
        r.Joinable("c");
        ForkInto(r, "b", "dng204b2");
        r.Snapshot("b");
        r.Barrier("b", "stag0202");
        r.Snapshot("b");
        r.Advance(100_000);
        r.Barrier("c", "stag0202");
        Assert.False(or.Open);
        r.Advance(50_000);
        r.Hub.Housekeep(r.Now);
        before = Offers(r, "a").Count;
        r.Barrier("b", "dng204b2");
        Assert.True(or.Open);
        var reopened = Offers(r, "a").Skip(before).ToList();
        Assert.Contains(reopened, x => x.Id == or.Id && (x.Flags & OfferMsg.Reopened) != 0);
        Assert.All(reopened, x => Assert.Equal(0, x.Flags & OfferMsg.Notice));

        // c's offer closed when c came out; c comes back within 120 s together with b, whom it never named: news, with the notice.
        var k = GoTrio(out var ok);
        k.Snapshot("c");
        k.Joinable("c");
        // c and b arrive in stag0202 together (b forks off a's group; c's barrier holds for b's announced one): one stage group.
        k.Send("b", new HeadingMsg(k.I("b").Id, "stag0202", 0, 5, 0).Frame());
        k.Send("c", new HeadingMsg(k.I("c").Id, "stag0202", 0, 5, 0).Frame());
        k.Barrier("c", "stag0202");
        Assert.False(ok.Open);
        ForkInto(k, "b", "stag0202");
        Assert.Same(k.Mem("c").Instance, k.Mem("b").Instance);
        var kc = k.Decision("c");
        k.Snapshot("c");
        k.Send("b", new SnapHashMsg(kc.NewInst, kc.NewVisit, 0xAA).Frame());
        k.Advance(20_000);
        before = Offers(k, "a").Count;
        foreach (var x in new[] { "c", "b" }) k.Barrier(x, "dng204b1");
        var news = Offers(k, "a").Skip(before).ToList();
        Assert.Contains(news, x => x.Op == OfferOp.Open && (x.Flags & OfferMsg.Notice) != 0);
        Assert.DoesNotContain(news, x => (x.Flags & OfferMsg.Reopened) != 0);
    }
}
