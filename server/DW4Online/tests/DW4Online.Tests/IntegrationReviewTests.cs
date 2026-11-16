using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// Round I1b's integration, the review's relay findings (2026-11-14) and W7e's race path: a GO's barrier into a floor without the
/// slots, one JOIN_REQ to a solo leader at a time, the answers of a split barrier and of a straggler's barrier under the instance and
/// visit their BARRIERs named (a member that resumed into the next visit after a cut and re-sent its old barrier never took the
/// JOIN's BARRIER_OK of instance 0), and the notice when a member plays apart from its stage group.
/// </summary>
public partial class HubLogicTests
{
    /// <summary>Two slots of <paramref name="floor"/> held by admitted members without a seat in its instance (as a removed joiner
    /// still catching up, or a removed member before its HOME fork, hold theirs).</summary>
    private static List<int> ReserveSeatless(Harness h, HubInstance floor, params string[] names)
    {
        var taken = new List<int>();
        var k = 0;
        foreach (var name in names) Admit(h, name, Player(500 + k++, name));
        for (var s = 0; s < Wire.MaxSlots && taken.Count < names.Length; ++s)
            if (floor.Slots[s] is null && floor.Reserved[s] is null)
            {
                floor.Reserved[s] = h.Mem(names[taken.Count]);
                taken.Add(s);
            }
        return taken;
    }

    [Fact]
    public void A_go_barrier_waits_while_its_floor_lacks_the_slots_and_joins_once_they_are_free()
    {
        var h = GoTrio(out var o, entrant: "a");
        HeldGoBarrier(h, o, "a", "b", "c");
        var floor = h.I("a");
        var held = h.Mem("b").Arrival!;
        Assert.Equal((HoldKind.Join, long.MaxValue), (held.Hold, held.HoldUntil));
        var reserved = ReserveSeatless(h, floor, "x", "y");
        Assert.False(W(h, "a").Seats.ContainsKey(h.Mem("x")));
        Assert.Equal(1, floor.FreeSlots);
        var answers = h.P("b").All(Msg.BarrierOk, BarrierOkMsg.Parse).Count;
        // The floor turns joinable with one free slot for a GO of two: before, the GO branch joined it all the same and Join threw
        // (JoinSlot -1) under the hub's lock. It waits now, as for a boss fight.
        var e = Record.Exception(() => h.Joinable("a"));
        Assert.Null(e);
        Assert.Same(held, h.Mem("b").Arrival);
        Assert.Same(held, h.Mem("c").Arrival);
        Assert.Equal(HoldKind.Join, held.Hold);
        Assert.Equal(answers, h.P("b").All(Msg.BarrierOk, BarrierOkMsg.Parse).Count);
        h.Advance(1500);
        h.Housekeep();
        Assert.Equal(HoldKind.Join, held.Hold);
        Assert.Equal(WaitReason.NotJoinable, h.P("b").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason);
        // The slots free (the joiner's READY or fallback, the HOME fork): the GO joins the offer's stage group.
        foreach (var s in reserved) floor.Reserved[s] = null;
        h.Joinable("a");
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.Join, floor.Id), (ok.Decision, ok.NewInst));
        Assert.Equal((BarrierDecision.Join, floor.Id), (h.Decision("c").Decision, h.Decision("c").NewInst));
        Assert.False(h.Hub.Counters.ContainsKey("join_without_slots"));
    }

    /// <summary>The same floor without a GO (b and c walk into dng204b1 by the mat): the ordinary path never joins a stage group
    /// without the slots (NEW with the notice Full), as before.</summary>
    [Fact]
    public void An_arrival_without_a_go_never_joins_a_floor_without_the_slots()
    {
        var h = GoTrio(out _, entrant: "a");
        var floor = h.I("a");
        h.Snapshot("a");
        h.Joinable("a");
        ReserveSeatless(h, floor, "x", "y");
        var s0 = h.I("b");
        h.Barrier("b", "dng204b1", tick: s0.NextSeal - 1);
        h.Barrier("c", "dng204b1", tick: s0.NextSeal - 1);
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.New, BarrierNotice.Full), (ok.Decision, ok.Notice));
    }

    /// <summary>
    /// b, solo-led in stag0201, asks to go along: JOIN_REQ kind go is out. c arrives and joins b's stage group (READY): its JOIN_REQ
    /// waits, since JOIN_AT names no kind (before, both went out at once and b's answer to the go's sealed c's JOIN, the GO not at all;
    /// the backstop asked again after 5 s). b's JOIN_AT seals the GO.
    /// </summary>
    [Fact]
    public void A_solo_leader_has_one_join_req_out_at_a_time_so_the_gos_answer_seals_the_go()
    {
        var h = GoPair(out var o);
        h.Snapshot("a");
        h.Joinable("a");
        var bi = h.I("b");
        Assert.Equal(InstanceMode.SoloLed, bi.Mode);
        h.Joinable("b");
        h.Log("b", 0, 300);
        h.Send("b", new IntentMsg(bi.Id, bi.Visit, IntentKind.GoAlong, o.Id).Frame());
        Assert.True(h.Mem("b").Go!.Stamped);
        Assert.NotNull(bi.AskedGo);
        var goReq = h.P("b").Last(Msg.JoinReq, JoinReqMsg.Parse);
        Assert.Equal(JoinReqKind.Go, goReq.Kind);
        // c from home: I1a's automatic join (the prompt is off in GoPair) of b's stage group (b plays at stag0201: Here 1).
        Admit(h, "c", Player(300, "c"));
        h.Barrier("c", "stag0201");
        var okC = h.Decision("c");
        Assert.Equal((BarrierDecision.Join, bi.Id), (okC.Decision, okC.NewInst));
        h.Send("c", new ReadyMsg(okC.NewInst, okC.NewVisit, 300, 0, 5000, [7]).Frame());
        h.Advance(200);
        h.Housekeep();
        Assert.Equal([JoinReqKind.Go], h.P("b").All(Msg.JoinReq, JoinReqMsg.Parse).Select(r => r.Kind));
        Assert.True(h.Mem("c").Joining is { Ready: true, Stamped: false });
        h.Send("b", new JoinAtMsg(goReq.Inst, goReq.Visit, 310).Frame());
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("go_stamped_solo"));
        Assert.Null(bi.AskedGo);
        Assert.Null(bi.SoloJoin);
        Assert.True(h.Mem("c").Joining is { Ready: true, Stamped: false }); // (its JOIN waits for its own JOIN_AT)
    }

    /// <summary>b's JOIN_REQ kind go is out when the host removes b: the HOME's JOIN_REQ waits for the go's answer (before, it went at
    /// once, b's answer to the go's took it, and the HOME's JOIN_AT came while the group was sealed and was dropped: the removed
    /// member's HOME waited for the 5 s re-ask).</summary>
    [Fact]
    public void A_removal_while_a_gos_join_req_is_out_asks_the_home_after_the_gos_answer()
    {
        var h = GoPair(out var o);
        h.Snapshot("a");
        h.Joinable("a");
        var bi = h.I("b");
        Assert.Equal(InstanceMode.SoloLed, bi.Mode);
        h.Joinable("b");
        h.Log("b", 0, 300);
        h.Send("b", new IntentMsg(bi.Id, bi.Visit, IntentKind.GoAlong, o.Id).Frame());
        var goReq = h.P("b").Last(Msg.JoinReq, JoinReqMsg.Parse);
        Assert.Equal(JoinReqKind.Go, goReq.Kind);
        var start = h.Now;
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, h.Mem("b").Id).Frame());
        h.Housekeep();
        Assert.DoesNotContain(h.P("b").All(Msg.JoinReq, JoinReqMsg.Parse), r => r.Kind == JoinReqKind.Home);
        h.Send("b", new JoinAtMsg(goReq.Inst, goReq.Visit, 310).Frame());
        Assert.Null(bi.AskedGo);
        h.Log("b", 300, 10); // (the leader's LOG still covers the ticks before J)
        // b leads alone again after the go's tick: the HOME's JOIN_REQ goes out, b answers it, the HOME is stamped.
        JoinReqMsg? homeReq = null;
        for (var k = 0; k < 40 && homeReq is null; ++k)
        {
            h.Seal(100);
            if (bi.Mode == InstanceMode.SoloLed) h.Log("b", bi.Log.Count, 6);
            h.Housekeep();
            homeReq = h.P("b").All(Msg.JoinReq, JoinReqMsg.Parse).LastOrDefault(r => r.Kind == JoinReqKind.Home);
        }
        Assert.NotNull(homeReq);
        h.Send("b", new JoinAtMsg(homeReq!.Inst, homeReq.Visit, bi.Log.Count + 5).Frame());
        Assert.NotEqual(0, bi.HomeStamped);
        Assert.True((h.Now - start) * 1000.0 / System.Diagnostics.Stopwatch.Frequency < 5000, "the HOME within the 5 s re-ask");
    }

    [Fact]
    public void A_split_barriers_answer_names_the_instance_and_visit_the_reporters_barrier_named()
    {
        var h = SealedPair(out var inst, new RelayOptions { LagLeaveSeconds = 600 });
        var i = h.I("a");
        h.Seal(500);
        var visit = i.Visit;
        h.Barrier("a", "stag0102", tick: i.NextSeal - 2);
        h.Seal(2000);
        h.Advance(3100);
        h.Housekeep();
        var ok = h.Decision("a");
        Assert.Equal(1, h.Hub.Counters["barrier_split"]);
        Assert.Equal((BarrierDecision.New, "stag0102"), (ok.Decision, ok.Stage));
        // The parked client matches BARRIER_OK's old inst and visit with its own (before: instance 0, never heard).
        Assert.Equal((inst, visit), (ok.Inst, ok.Visit));
    }

    [Fact]
    public void A_stragglers_barrier_is_answered_under_the_instance_and_visit_it_named()
    {
        var h = SealedPair(out var inst);
        var i = h.I("a");
        h.Seal(300);
        // A BARRIER of a visit the relay no longer has the member in (its group was decided without it).
        h.Send("a", new BarrierMsg(i.Id, i.Visit + 5, 40, "stag0202", 0, 0b01, 0b01, 5, [(byte)'a']).Frame());
        var ok = h.Decision("a");
        Assert.Equal((inst, i.Visit + 5), (ok.Inst, ok.Visit));
    }

    /// <summary>
    /// W7e's race path (round I1b's integration, runs r4, rr): a and b park together at stag0201's exit, the sealed NEW of both is
    /// decided, and a's link is cut before its BARRIER_OK reaches it. b loads (SNAPSHOT_ASK makes it the uploader) and plays on. a
    /// resumes into the new visit, its engine still at the old barrier, and sends that barrier again under the new visit; b never
    /// reports it, so it is split off and decided alone: a JOIN of b's stage group. Its BARRIER_OK names the instance and visit a's
    /// client is in; with instance 0 the client dropped it, then the state, never sent READY and ended in an instance of its own after
    /// join_ready_timeout. (Here the old barrier's tick lies inside what the new visit sealed: the split after
    /// Instances:BarrierReportSeconds; past it, the next test, the barrier is decided apart at once.)
    /// </summary>
    [Fact]
    public void A_member_that_resumed_into_the_next_visit_and_re_sent_its_old_barrier_gets_the_join_under_its_visit()
    {
        var h = SealedPair(out _, stage: "stag0201");
        h.Joinable("a");
        h.Seal(500);
        var first = h.I("a");
        var tick = first.NextSeal - 1;
        h.Barrier("a", "stag0202", tick: tick);
        h.Barrier("b", "stag0202", tick: tick);
        var okB = h.Decision("b");
        Assert.Equal((BarrierDecision.New, InstanceMode.Sealed), (okB.Decision, okB.Mode));
        var next = h.I("b");
        Assert.Same(next, h.Mem("a").Instance);
        Assert.Same(h.Mem("a"), next.Snapshot.Uploader);
        // a's link is cut right after its BARRIER (its BARRIER_OK lost with it).
        h.Hub.Disconnect(h.Mem("a"), h.P("a"), h.Now);
        // b loads; after SnapshotAskGraceMs the relay asks b for the snapshot, b uploads it and plays.
        h.Advance(2100);
        h.Housekeep();
        Assert.True(h.P("b").Has(Msg.SnapshotAsk));
        h.Snapshot("b");
        h.Joinable("b");
        h.Seal(1000);
        Assert.True(next.Snapshot.Complete);
        // a is back within its reconnect window: resumable in the new visit (WELCOME names it).
        var pa = h.Connect("a", 0);
        var welcome = pa.Last(Msg.Welcome, WelcomeMsg.Parse);
        Assert.Equal((next.Id, next.Visit), (welcome.Inst, welcome.Visit));
        // Its engine still stands at stag0201's barrier: it sends that barrier as one of the visit it was told.
        h.Barrier("a", "stag0202", tick: tick + 1);
        Assert.NotNull(next.Barrier);
        Assert.False(pa.Has(Msg.BarrierOk));
        // b never reports it: the barrier is split off after Instances:BarrierReportSeconds and decided alone, a JOIN of b's stage group.
        h.Seal(2000);
        h.Advance(3100);
        h.Housekeep();
        Assert.Equal(1, h.Hub.Counters["barrier_split"]);
        var ok = h.Decision("a");
        Assert.Equal((BarrierDecision.Join, next.Id, next.Visit), (ok.Decision, ok.NewInst, ok.NewVisit));
        Assert.Equal((next.Id, next.Visit), (ok.Inst, ok.Visit)); // (the client's own visit: it takes the answer, then the state)
        Assert.Same(next, h.Mem("a").Joining!.Target);
    }

    /// <summary>
    /// W7e's race as the real runs had it: the old visit ran far longer than the new one has when a comes back, so a's re-sent barrier
    /// lies past every tick b's stage group sealed: another timeline's barrier, decided apart at once (a LEAVE, b told "a plays apart",
    /// a JOIN of b's group under a's visit), without the barrier's stall for b and without a desync's split.
    /// </summary>
    [Fact]
    public void A_resumed_members_old_barrier_past_the_sealed_ticks_is_decided_apart_at_once_without_stalling_the_others()
    {
        var h = SealedPair(out _, stage: "stag0201");
        h.Joinable("a");
        h.Seal(6000);
        var first = h.I("a");
        var tick = first.NextSeal - 1;
        h.Barrier("a", "stag0202", tick: tick);
        h.Barrier("b", "stag0202", tick: tick);
        var next = h.I("b");
        Assert.Same(next, h.Mem("a").Instance);
        h.Hub.Disconnect(h.Mem("a"), h.P("a"), h.Now);
        h.Advance(2100);
        h.Housekeep();
        h.Snapshot("b");
        h.Joinable("b");
        h.Seal(1000);
        Assert.True(next.Sealing);
        Assert.True(tick > next.NextSeal + RoomHub.StaleBarrierTicks);
        var pa = h.Connect("a", 0);
        Assert.Equal((next.Id, next.Visit), (pa.Last(Msg.Welcome, WelcomeMsg.Parse).Inst, pa.Last(Msg.Welcome, WelcomeMsg.Parse).Visit));
        h.Barrier("a", "stag0202", tick: tick + 1);
        // At once: no barrier collected, b's group seals on; a leaves it, b is told; a's JOIN of b's group under a's visit.
        Assert.Null(next.Barrier);
        Assert.True(next.Sealing);
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("barrier_apart"));
        Assert.False(h.Hub.Counters.ContainsKey("barrier_split"));
        var ok = h.Decision("a");
        Assert.Equal((BarrierDecision.Join, next.Id, next.Visit), (ok.Decision, ok.NewInst, ok.NewVisit));
        Assert.Equal((next.Id, next.Visit), (ok.Inst, ok.Visit));
        Assert.Contains(h.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse), n => n.Kind == WorldNoticeKind.Apart && n.Member == h.Mem("a").Id);
        h.Seal(300);
        Assert.Contains(h.P("b").Ticks(), t => t.Rec.Events.Any(e => e.Type == Ev.Leave && e.Slot == 0));
    }

    /// <summary>A member taken out of its stage group while it stays in the instance (lag, 6.9) leaves no figure without a word: the
    /// others get WORLD_NOTICE apart (before round I1b's integration, the stage group's LEAVE toast said "X left").</summary>
    [Fact]
    public void A_member_lagged_out_of_its_stage_group_is_told_to_the_others_as_playing_apart()
    {
        var h = SealedPair(out var inst, new RelayOptions { LagLeaveSeconds = 2 });
        h.Joinable("a");
        var i = h.I("a");
        // a plays on, b sends nothing: b's slot fills, it lags, and after Relay:LagLeaveSeconds its LEAVE is stamped.
        for (var k = 0; k < 400 && h.Mem("b").Instance == i; ++k)
        {
            h.Send("a", new InputMsg(inst, i.Visit, i.NextSeal + 3, [new Pad(1, 0, 0, 0, 0), new Pad(1, 0, 0, 0, 0)]).Frame());
            h.Seal(17);
            if (k % 10 == 0) h.Housekeep();
        }
        Assert.Null(h.Mem("b").Instance);
        Assert.Same(W(h, "a"), h.Mem("b").World); // (still a member of the instance)
        var apart = h.P("a").All(Msg.WorldNotice, WorldNoticeMsg.Parse).Where(n => n.Kind == WorldNoticeKind.Apart).ToList();
        Assert.Equal([h.Mem("b").Id], apart.Select(n => n.Member));
        Assert.DoesNotContain(h.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse), n => n.Kind == WorldNoticeKind.Apart);
    }
}
