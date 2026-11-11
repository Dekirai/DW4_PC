using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// Round I1b's go along on the hub without sockets (SEAMLESS.md 19.30.5-19.30.7): the offer an entry into a dungeon makes (OFFER with
/// the notice to the members in a field, none for the ones the exit port asked), its following, close and silent reopen, INTENT 4's
/// wait for a joinable target with a living member and the sender's sealing, membership-safe stage group, GO stamped sealed (one tick
/// for a stage group's requests) or through JOIN_REQ kind go, the 60 s and the boss fight, the withdraw, the GO's barrier joining the
/// offer's stage group with a hold that never runs out, the refusals.
/// </summary>
public partial class HubLogicTests
{
    /// <summary>a and b co-arrive in stag0201 (a hosts); a forks off and walks into dng204b1 alone: the offer opens.</summary>
    private static Harness GoPair(out GoOffer offer, byte asked = 0, WorldHubOptions? world = null, bool again = false)
    {
        var h = SealedPair(out _, stage: "stag0201", world: world);
        h.Joinable("a");
        h.Seal(500);
        var i = h.I("a");
        // a's exit port asked b on the mat (asked = b's slot) or nobody; a goes alone.
        h.Send("a", new HeadingMsg(i.Id, "dng204b1", 0, 5, asked).Frame());
        h.Send("a", new SplitMsg(i.Id, i.Visit, i.NextSeal - 1, 0b01, SplitKind.Exit).Frame());
        h.Send("b", new SplitMsg(i.Id, i.Visit, i.NextSeal - 1, 0b01, SplitKind.Exit).Frame());
        h.Seal(200);
        // (again: the engine announces the departure once more at the solo copy's NEXTSTAGE, with no asked slots: run c1 of W31b.)
        if (again) h.Send("a", new HeadingMsg(h.I("a").Id, "dng204b1", 0, 5, 0).Frame());
        h.Barrier("a", "dng204b1");
        var w = W(h, "a");
        offer = Assert.Single(w.Offers);
        return h;
    }

    private static List<OfferMsg> Offers(Harness h, string name) => h.P(name).All(Msg.Offer, OfferMsg.Parse);

    [Fact]
    public void An_entry_into_a_dungeon_offers_go_along_to_the_members_in_a_field_with_the_notice()
    {
        var h = GoPair(out var o);
        Assert.Equal(("dng204", "dng204b1", true), (o.Dungeon, o.Stage, o.Open));
        Assert.Equal([h.Mem("a")], o.Entrants);
        var m = Assert.Single(Offers(h, "b"));
        Assert.Equal((OfferOp.Open, OfferMsg.Notice, "dng204b1", (byte)20), (m.Op, (byte)(m.Flags & OfferMsg.Notice), m.Stage, m.NoticeSeconds));
        Assert.Equal([h.Mem("a").Id], m.Entrants);
        Assert.Empty(Offers(h, "a")); // (the entrant gets none)
        // The member the exit port asked on the mat gets the row without a notice.
        var g = GoPair(out _, asked: 0b10);
        var asked = Assert.Single(Offers(g, "b"));
        Assert.Equal((OfferOp.Open, 0), (asked.Op, asked.Flags & OfferMsg.Notice));
        // ... also when the same departure's HEADING comes again without them.
        var again = GoPair(out _, asked: 0b10, again: true);
        Assert.Equal((OfferOp.Open, 0), (Assert.Single(Offers(again, "b")).Op, Assert.Single(Offers(again, "b")).Flags & OfferMsg.Notice));
    }

    [Fact]
    public void The_offer_follows_its_entrants_closes_when_they_left_and_a_reentry_within_two_minutes_reopens_it_silently()
    {
        var h = GoPair(out var o);
        h.Snapshot("a");
        h.Joinable("a");
        // Deeper: no notice, the row's stage moves.
        h.Barrier("a", "dng204b2");
        var moved = Offers(h, "b").Last();
        Assert.Equal((OfferOp.Changed, 0, "dng204b2"), (moved.Op, moved.Flags & OfferMsg.Notice, moved.Stage));
        h.Snapshot("a");
        // Out into a field (nobody there): closed (left).
        h.Barrier("a", "stag0202");
        var closed = Offers(h, "b").Last();
        Assert.Equal((OfferOp.Closed, OfferClose.Left), (closed.Op, closed.Close));
        Assert.False(o.Open);
        // In again within 120 s: the same offer opens silently.
        h.Snapshot("a");
        h.Advance(30_000);
        h.Barrier("a", "dng204b1");
        var again = Offers(h, "b").Last();
        Assert.Equal((OfferOp.Open, o.Id, 0, OfferMsg.Reopened), (again.Op, again.Id, again.Flags & OfferMsg.Notice, again.Flags & OfferMsg.Reopened));
        Assert.True(o.Open);
        // After 120 s without a re-entry the next entry is new (a notice again).
        h.Snapshot("a");
        h.Barrier("a", "stag0202");
        h.Snapshot("a");
        h.Advance(121_000);
        h.Hub.Housekeep(h.Now);
        h.Barrier("a", "dng204b1");
        var fresh = Offers(h, "b").Last();
        Assert.Equal((OfferOp.Open, OfferMsg.Notice), (fresh.Op, fresh.Flags & OfferMsg.Notice));
        Assert.NotEqual(o.Id, fresh.Id);
    }

    [Fact]
    public void A_request_waits_for_a_joinable_target_with_a_living_member_then_go_is_stamped_and_its_barrier_joins_the_entrants()
    {
        var h = GoPair(out var o);
        var bi = h.I("b");
        h.Send("b", new IntentMsg(bi.Id, bi.Visit, IntentKind.GoAlong, o.Id).Frame());
        Assert.NotNull(h.Mem("b").Go);
        Assert.False(h.Mem("b").Go!.Stamped); // a's floor loads
        Assert.Equal(OfferMsg.Waiting, Offers(h, "b").Last().Flags & OfferMsg.Waiting);
        // a's floor: loaded and joinable, but nobody alive there: no rescue join.
        h.Snapshot("a");
        var ai = h.I("a");
        h.Send("a", new StateMsg(ai.Id, ai.Visit, 0, StateMsg.Joinable | StateMsg.MembershipSafe | StateMsg.LoadEnded | StateMsg.NoLivingMember, 5,
            StateReason.None).Frame());
        h.Joinable("b");
        h.Hub.Housekeep(h.Now);
        Assert.False(h.Mem("b").Go!.Stamped);
        // a lives: stamped in b's stage group (sealed: a GO event; solo-led: JOIN_REQ kind go).
        h.Joinable("a");
        var r = h.Mem("b").Go!;
        Assert.True(r.Stamped);
        Assert.Equal(("dng204b1", (byte)0), (r.Stage, r.Point));
        bi = h.I("b");
        if (bi.Mode == InstanceMode.Sealed)
        {
            h.Seal(300);
            Assert.Contains(h.P("b").Ticks().SelectMany(t => t.Rec.Events), e => e.Type == Ev.Go && e.Slot == h.Mem("b").Slot && e.GoTarget == ("dng204b1", (byte)0));
        }
        else
        {
            var req = h.P("b").Last(Msg.JoinReq, JoinReqMsg.Parse);
            Assert.Equal((JoinReqKind.Go, "dng204b1", (byte)0), (req.Kind, req.GoStage, req.GoPoint));
            h.Log("b", 0, 300);
            h.Send("b", new JoinAtMsg(req.Inst, req.Visit, 300).Frame());
            h.Seal(200);
            var ticks = h.P("b").Ticks();
            Assert.Contains(ticks, t => t.Rec.Events.Any(e => e.Type == Ev.Go && e.GoTarget == ("dng204b1", (byte)0)));
            Assert.Contains(ticks, t => t.Rec.Events.Any(e => e.Type == Ev.LeadFrom));
        }
        Assert.Equal(OfferMsg.Stamped, Offers(h, "b").Last().Flags & OfferMsg.Stamped);
        // b's GO barrier: a JOIN of a's stage group at its entrance; b joins the entrants silently (no new notice for anyone).
        h.Barrier("b", "dng204b1");
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.Join, h.I("a").Id), (ok.Decision, ok.NewInst));
        Assert.Null(h.Mem("b").Go);
        Assert.Contains(h.Mem("b"), o.Entrants);
    }

    [Fact]
    public void A_gos_barrier_waits_for_a_busy_floor_without_the_join_hold_and_back_to_the_lobby_takes_the_member_home()
    {
        var h = GoPair(out var o);
        h.Snapshot("a");
        h.Joinable("a");
        var bi = h.I("b");
        h.Joinable("b");
        h.Send("b", new IntentMsg(bi.Id, bi.Visit, IntentKind.GoAlong, o.Id).Frame());
        Assert.True(h.Mem("b").Go!.Stamped);
        // a's floor becomes busy in the second of the departure (a scene): b's barrier waits, past Instances:JoinHoldSeconds.
        h.Joinable("a", joinable: false);
        h.Barrier("b", "dng204b1");
        Assert.Equal(WaitReason.NotJoinable, h.P("b").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason);
        h.Advance(40_000);
        h.Hub.Housekeep(h.Now);
        Assert.NotNull(h.Mem("b").Arrival); // still held: no new stage group next to a's
        Assert.Single(W(h, "a").Groups, x => !x.Ended && x.Stage == "dng204b1");
        // *B: Back to the lobby*: the member's leave from its barrier: DISSOLVE home with the redirect.
        h.Send("b", new IntentMsg(bi.Id, bi.Visit, IntentKind.Leave, 0).Frame());
        var back = h.Decision("b");
        Assert.Equal((BarrierDecision.Dissolve, "stagML11"), (back.Decision, back.World.RedirectStage));
        Assert.Null(h.Mem("b").World);
    }

    [Fact]
    public void A_request_ends_after_sixty_seconds_but_not_in_a_boss_fight_and_a_withdraw_or_a_closed_offer_ends_it()
    {
        var h = GoPair(out var o);
        var bi = h.I("b");
        h.Joinable("b");
        h.Send("b", new IntentMsg(bi.Id, bi.Visit, IntentKind.GoAlong, o.Id).Frame());
        h.Advance(61_000);
        h.Hub.Housekeep(h.Now);
        Assert.Null(h.Mem("b").Go);
        var busy = h.P("b").Last(Msg.WorldNotice, WorldNoticeMsg.Parse);
        Assert.Equal((WorldNoticeKind.GoRefused, (uint)GoRefusal.Busy, h.Mem("a").Id), (busy.Kind, busy.Argument, busy.Member));
        Assert.True(o.Open); // (the offer stays open)
        // A boss fight on the target: no time limit, the row says so.
        h.Snapshot("a");
        var ai = h.I("a");
        h.Send("a", new StateMsg(ai.Id, ai.Visit, 0, StateMsg.MembershipSafe | StateMsg.LoadEnded | StateMsg.BossLock, 5, StateReason.Boss).Frame());
        h.Send("b", new IntentMsg(bi.Id, bi.Visit, IntentKind.GoAlong, o.Id).Frame());
        Assert.Equal(OfferMsg.Boss, Offers(h, "b").Last().Flags & OfferMsg.Boss);
        h.Advance(120_000);
        h.Hub.Housekeep(h.Now);
        Assert.NotNull(h.Mem("b").Go);
        // The withdraw (INTENT 4, 0): no notice.
        var notices = h.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse).Count;
        h.Send("b", new IntentMsg(bi.Id, bi.Visit, IntentKind.GoAlong, 0).Frame());
        Assert.Null(h.Mem("b").Go);
        Assert.Equal(notices, h.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse).Count);
        // A request whose offer closes ends: go refused, closed.
        h.Send("b", new IntentMsg(bi.Id, bi.Visit, IntentKind.GoAlong, o.Id).Frame());
        h.Barrier("a", "stag0202");
        Assert.Null(h.Mem("b").Go);
        var refusals = () => h.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse).Where(n => n.Kind == WorldNoticeKind.GoRefused).ToList();
        Assert.Equal(((uint)GoRefusal.Closed, "dng204b1"), (refusals().Last().Argument, refusals().Last().Text));
        Assert.Equal(OfferClose.Left, Offers(h, "b").Last().Close);
        // A late A on the closed offer is refused (closed).
        var before = refusals().Count;
        h.Send("b", new IntentMsg(bi.Id, bi.Visit, IntentKind.GoAlong, o.Id).Frame());
        Assert.Equal((before + 1, (uint)GoRefusal.Closed), (refusals().Count, refusals().Last().Argument));
    }

    /// <summary>b's stamped GO, sealed or through JOIN_REQ kind go and its JOIN_AT: the sealed tick records b's stage group got.</summary>
    private static List<TickEvent> StampedGoEvents(Harness h, GoOffer o)
    {
        h.Snapshot("a");
        h.Joinable("a");
        var bi = h.I("b");
        h.Joinable("b");
        h.Send("b", new IntentMsg(bi.Id, bi.Visit, IntentKind.GoAlong, o.Id).Frame());
        Assert.True(h.Mem("b").Go!.Stamped);
        bi = h.I("b");
        if (bi.Mode != InstanceMode.Sealed)
        {
            var req = h.P("b").Last(Msg.JoinReq, JoinReqMsg.Parse);
            h.Log("b", 0, 300);
            h.Send("b", new JoinAtMsg(req.Inst, req.Visit, 300).Frame());
        }
        h.Seal(300);
        return h.P("b").Ticks().SelectMany(t => t.Rec.Events).ToList();
    }

    [Fact]
    public void The_test_go_hook_stamps_a_defeat_before_the_go_at_its_tick_for_the_first_stamps_only_and_is_off_by_default()
    {
        // Off by default: no TEST event anywhere.
        var plain = GoPair(out var o0);
        Assert.DoesNotContain(StampedGoEvents(plain, o0), e => e.Type == Ev.Test);
        // "defeat": TEST(defeat, the going slot) right before the GO in the same tick record.
        var h = GoPair(out var o, world: new WorldHubOptions { TestGoHook = "defeat" });
        var events = StampedGoEvents(h, o);
        var go = events.FindIndex(e => e.Type == Ev.Go);
        Assert.True(go > 0);
        var test = events[go - 1];
        Assert.Equal((Ev.Test, h.Mem("b").Slot, (byte)TestHook.Defeat), (test.Type, test.Slot, test.Payload[1]));
        // "busy_sender:600" with a count of 1: the first stamp only.
        var b = GoPair(out var ob, world: new WorldHubOptions { TestGoHook = "busy_sender:600", TestGoHookCount = 1 });
        var busy = StampedGoEvents(b, ob).Single(e => e.Type == Ev.Test);
        Assert.Equal(((byte)TestHook.Busy, 600), (busy.Payload[1], System.Buffers.Binary.BinaryPrimitives.ReadInt32LittleEndian(busy.Payload.AsSpan(2))));
    }

    [Fact]
    public void A_test_intent_stamps_a_test_event_in_a_sealed_stage_group_only_with_test_hooks_on()
    {
        foreach (var on in new[] { false, true })
        {
            var h = SealedPair(out _, world: new WorldHubOptions { TestHooks = on });
            h.Joinable("a");
            h.Seal(100);
            var i = h.I("b");
            h.Send("b", new IntentMsg(i.Id, i.Visit, IntentKind.Test, (int)TestHook.Busy | (600 << 8)).Frame());
            h.Send("a", new IntentMsg(i.Id, i.Visit, IntentKind.Test, (int)TestHook.Defeat).Frame());
            h.Send("a", new IntentMsg(i.Id, i.Visit, IntentKind.Test, (int)TestHook.Mat | ((('z' << 8) | 2) << 8)).Frame()); // matz02
            h.Send("a", new IntentMsg(i.Id, i.Visit, IntentKind.Test, 9).Frame()); // no such hook: ignored
            h.Seal(200);
            var tests = h.P("a").Ticks().SelectMany(t => t.Rec.Events).Where(e => e.Type == Ev.Test).ToList();
            if (!on)
            {
                Assert.Empty(tests);
                continue;
            }
            Assert.Equal(3, tests.Count);
            Assert.Contains(tests, e => e.Slot == h.Mem("a").Slot && e.Payload[1] == (byte)TestHook.Mat &&
                                        System.Buffers.Binary.BinaryPrimitives.ReadInt32LittleEndian(e.Payload.AsSpan(2)) == (('z' << 8) | 2));
            Assert.Equal(1, h.Hub.Counters["test_hook_ignored"]);
            Assert.Contains(tests, e => e.Slot == h.Mem("b").Slot && e.Payload[1] == (byte)TestHook.Busy &&
                                        System.Buffers.Binary.BinaryPrimitives.ReadInt32LittleEndian(e.Payload.AsSpan(2)) == 600);
            Assert.Contains(tests, e => e.Slot == h.Mem("a").Slot && e.Payload[1] == (byte)TestHook.Defeat);
        }
        // The wire: an INTENT of kind test parses (a kind beyond it does not).
        var frame = new IntentMsg(1, 1, IntentKind.Test, 2).Frame()[Wire.HeaderBytes..];
        Assert.Equal(IntentKind.Test, IntentMsg.Parse(frame).Kind);
        frame[8] = 0x7E;
        Assert.Throws<ProtocolException>(() => IntentMsg.Parse(frame));
    }

    [Fact]
    public void The_test_barrier_hold_keeps_a_gos_barrier_undecided_for_its_time_then_it_is_decided()
    {
        var h = GoPair(out var o, world: new WorldHubOptions { TestGoBarrierHoldMs = 5000 });
        _ = StampedGoEvents(h, o);
        var oks = h.P("b").All(Msg.BarrierOk, BarrierOkMsg.Parse).Count;
        h.Barrier("b", "dng204b1");
        Assert.Equal(oks, h.P("b").All(Msg.BarrierOk, BarrierOkMsg.Parse).Count); // held
        h.Advance(3000);
        h.Hub.Housekeep(h.Now);
        Assert.Equal(oks, h.P("b").All(Msg.BarrierOk, BarrierOkMsg.Parse).Count);
        h.Advance(2500);
        h.Hub.Housekeep(h.Now);
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.Join, h.I("a").Id), (ok.Decision, ok.NewInst));
    }

    [Fact]
    public void A_gos_barrier_whose_entrants_left_meanwhile_becomes_a_new_stage_group_of_the_instance_there()
    {
        // W31c2: b's GO is stamped while a is in dng204b1; the test hold keeps b's barrier undecided while a goes back out.
        var h = GoPair(out var o, world: new WorldHubOptions { TestGoBarrierHoldMs = 5000 });
        _ = StampedGoEvents(h, o);
        var w = W(h, "a");
        h.Barrier("b", "dng204b1");
        Assert.Contains(h.Mem("b"), o.Entrants); // (an entrant from its GO barrier on: the offer stays open for it)
        h.Snapshot("a");
        h.Barrier("a", "stag0202");
        Assert.True(o.Open);
        h.Advance(6000);
        h.Hub.Housekeep(h.Now);
        var ok = h.Decision("b");
        Assert.Equal(BarrierDecision.New, ok.Decision);
        Assert.Equal(w, h.Mem("b").World); // the seat kept: a new stage group of the instance, not home, not another instance
        Assert.Contains(w.Groups, x => !x.Ended && x.Stage == "dng204b1" && x.Members.Contains(h.Mem("b")));
    }

    [Fact]
    public void Go_events_follow_their_slot_at_a_fork_and_the_wire_round_trips()
    {
        var e = TickEvent.Go(2, 3, "dng204b1");
        Assert.Equal((2, ("dng204b1", (byte)3)), (e.Slot, e.GoTarget!.Value));
        var offer = new OfferMsg(3, OfferOp.Open, OfferMsg.Notice | OfferMsg.Late, [5, 9], "dng204b1", 20, OfferClose.None);
        Assert.Equal(offer.Entrants, OfferMsg.Parse(offer.Frame()[Wire.HeaderBytes..]).Entrants);
        Assert.Equal(offer with { Entrants = [] }, OfferMsg.Parse(offer.Frame()[Wire.HeaderBytes..]) with { Entrants = [] });
        Assert.Throws<ProtocolException>(() => OfferMsg.Parse(new OfferMsg(3, (OfferOp)4, 0, [], "", 20, OfferClose.None).Frame()[Wire.HeaderBytes..]));
        Assert.Equal((byte)0b0100, HeadingMsg.Parse(new HeadingMsg(7, "stag0201", 0, 16, 0b0100).Frame()[Wire.HeaderBytes..]).Asked);
    }
}
