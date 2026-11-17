using System.Diagnostics;
using DW4Online.Domain.Story;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// fix-021 (DW4Online 0.2.1), the relay findings left open after 0.2.0 (docs/ROUND_021_NOTES.md, SEAMLESS.md 19.6.7, 19.32):
/// 1. a host in a grace started by silence (a half-open link counts as connected) stayed the STATE reporter, so the guests' STATEs
///    were ignored and a joiner was let in on stale flags or waited Instances:JoinHoldSeconds for a NEW (HoldExpired);
/// 2. the reporter's own STATE took effect without the newer-by-tick check: a host back from a drop overrode a guest's newer STATE
///    with the ones of the ticks he caught up;
/// 3. a host who dropped exactly at a gate still shared the parked stage group with his guests, so his absence debt shortened his
///    grace although they went on at once;
/// 4. the lag rule's gap: drops each shorter than Relay:LagLeaveSeconds with short bursts of pads in time between them never reached
///    the limit (the fill streak ends at any pad in time), for guests and for a host below World:HostSilenceSeconds;
/// 5. a member removed while it played on alone (between its removal and its next barrier) was not on a foreign story: a STORY with
///    its own key reached its own keeper with the host's progress, and the achievements' context said "home".
/// Each test fails without its fix.
/// </summary>
public partial class HubLogicTests
{
    /// <summary>c, a stranger, arrives at <paramref name="stage"/> from home: its prompt is answered with the first row (a's instance),
    /// which holds its barrier for a JOIN of the stage group there while it is not joinable.</summary>
    private static void ArriveC(Harness h, string stage)
    {
        Admit(h, "c", Player(500, "c"));
        h.Barrier("c", stage);
    }

    [Fact]
    public void A_host_silent_in_his_grace_hands_the_flags_to_his_guest_so_a_held_joiner_joins_when_the_guests_scene_ends()
    {
        var r = new FlapRig();
        var h = r.H;
        r.Play(1000);
        // a reports (connected longest): joinable once (the instance no longer starting), then a scene, b's too; c arrives meanwhile and
        // waits for the stage group to be joinable.
        StateAt(h, "a", r.I.NextSeal - 30, joinable: true);
        StateAt(h, "a", r.I.NextSeal - 1, joinable: false);
        StateAt(h, "b", r.I.NextSeal - 1, joinable: false);
        ArriveC(h, "stag0201");
        var c = h.Mem("c");
        Assert.Equal(HoldKind.Join, c.Arrival!.Hold);
        Assert.Equal(WaitReason.NotJoinable, h.P("c").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason);
        // (While a plays in time, b's STATE is not in effect: a reports.)
        r.Play(500);
        StateAt(h, "b", r.I.NextSeal - 1, joinable: false, StateReason.Boss);
        Assert.Equal(StateReason.Scene, r.I.Reason);
        // a's link hangs open (connected, silent): after World:HostSilenceSeconds his grace starts.
        r.Mode["a"] = 0;
        r.Play(4500);
        Assert.True(r.W.HostAway);
        Assert.True(h.Mem("a").Connected);
        Assert.Equal(HoldKind.Join, c.Arrival!.Hold);
        // b's engine ends its scene: its STATE takes effect at once, and c joins b's stage group. Before, a stayed the reporter for the
        // whole grace (60 s): b's STATE was ignored, c waited Instances:JoinHoldSeconds (30 s) and got a NEW (HoldExpired).
        StateAt(h, "b", r.I.NextSeal - 1, joinable: true);
        Assert.Same(h.Mem("b"), r.I.StateFrom);
        Assert.True(r.I.Joinable);
        var ok = h.Decision("c");
        Assert.Equal((BarrierDecision.Join, r.I.Id), (ok.Decision, ok.NewInst));
        Assert.NotEqual(BarrierNotice.HoldExpired, ok.Notice);
    }

    [Fact]
    public void A_guests_boss_fight_while_the_hosts_link_hangs_holds_an_arrival_instead_of_letting_it_in_on_the_hosts_old_flags()
    {
        var r = new FlapRig();
        var h = r.H;
        r.Play(1000);
        StateAt(h, "a", r.I.NextSeal - 1, joinable: true);
        Assert.True(r.I.Joinable);
        // a's link hangs open (2 s: his grace has not started yet); b walks into a boss fight (its STATE: boss-locked, not joinable).
        r.Mode["a"] = 0;
        r.Play(2000);
        Assert.False(r.W.HostAway);
        h.Send("b", new StateMsg(r.I.Id, r.I.Visit, r.I.NextSeal - 1, StateMsg.LoadEnded | StateMsg.BossLock, 5, StateReason.Boss).Frame());
        Assert.Equal((true, false), (r.I.BossLock, r.I.Joinable));
        // c arrives: it waits for the fight (or goes alone). Before, a's old joinable STATE stayed in effect while his link hung (the
        // reporter was the connected member longest), so c was let into the boss fight at once.
        ArriveC(h, "stag0201");
        var c = h.Mem("c");
        Assert.Null(c.Joining);
        Assert.DoesNotContain(h.P("c").All(Msg.BarrierOk, BarrierOkMsg.Parse), o => o.Decision == BarrierDecision.Join);
        Assert.Equal(WaitReason.BossLock, h.P("c").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason);
    }

    [Fact]
    public void A_host_back_from_a_drop_never_overrides_a_guests_newer_state_with_the_ones_of_the_ticks_he_catches_up()
    {
        var r = new FlapRig();
        var h = r.H;
        r.Play(1000);
        var t0 = r.I.NextSeal - 1;
        StateAt(h, "a", t0, joinable: true);
        // a's link closes; b reports while a is away: a boss fight at t1.
        r.Drop("a");
        r.Mode["a"] = 0;
        r.Play(5000);
        var t1 = r.I.NextSeal - 1;
        h.Send("b", new StateMsg(r.I.Id, r.I.Visit, t1, StateMsg.LoadEnded | StateMsg.BossLock, 5, StateReason.Boss).Frame());
        Assert.True(r.I.BossLock);
        // a is back; his client's pads come in time again at once (they follow the wall clock) while his engine still catches up on
        // the ticks sealed meanwhile: he is the reporter again.
        r.Back("a");
        r.Mode["a"] = 2;
        r.Play(1000);
        Assert.False(r.W.HostAway);
        Assert.Same(h.Mem("b"), r.I.StateFrom);
        Assert.Equal(h.Mem("a"), r.I.StateReporter);
        // His engine passes a scene between t0 and t1 and reports it: older than b's STATE in effect, it is kept, not applied. Before,
        // the reporter's every STATE took effect: the stage group read "not in a boss fight" (a join let in) until his engine caught up.
        var stale = h.Hub.Counters.GetValueOrDefault("state_stale");
        StateAt(h, "a", t0 + (t1 - t0) / 2, joinable: false);
        Assert.Equal(stale + 1, h.Hub.Counters.GetValueOrDefault("state_stale"));
        Assert.True(r.I.BossLock);
        Assert.Equal((h.Mem("b"), t1), (r.I.StateFrom, r.I.StateTick));
        // At the tick of b's STATE it is still b's (the same tick of another member changes nothing); past it, a's own takes effect.
        StateAt(h, "a", t1, joinable: true);
        Assert.True(r.I.BossLock);
        StateAt(h, "a", t1 + 30, joinable: true);
        Assert.Equal((false, true, h.Mem("a")), (r.I.BossLock, r.I.Joinable, r.I.StateFrom));
        // His own next STATE at that tick (a second change in one tick) takes effect too.
        StateAt(h, "a", t1 + 30, joinable: false);
        Assert.False(r.I.Joinable);
    }

    [Fact]
    public void A_returned_host_who_still_lags_is_not_the_reporter_until_he_plays_in_time()
    {
        var r = new FlapRig();
        var h = r.H;
        r.Play(1000);
        StateAt(h, "a", r.I.NextSeal - 1, joinable: false);
        r.Drop("a");
        r.Mode["a"] = 0;
        r.Play(5000);
        r.Back("a");
        r.Mode["a"] = 1; // (late pads: his slot still fills)
        r.Play(1000);
        Assert.False(r.W.HostAway);
        Assert.Same(h.Mem("b"), r.I.StateReporter);
        StateAt(h, "b", r.I.NextSeal - 1, joinable: true);
        Assert.True(r.I.Joinable);
        r.Mode["a"] = 2;
        r.Play(600);
        Assert.Same(h.Mem("a"), r.I.StateReporter);
        Assert.True(r.I.Joinable); // (a's newest is older than b's in effect)
    }

    [Fact]
    public void A_returned_host_whose_engine_still_catches_up_is_not_the_reporter_though_his_pads_come_in_time()
    {
        var r = new FlapRig();
        var h = r.H;
        r.Play(2000);
        // a's engine reports the ticks it executed (NETFP, every 60th); he reports the stage group's STATE.
        var before = (r.I.NextSeal - 1) / 60 * 60;
        h.Send("a", new NetFpMsg(r.I.Id, r.I.Visit, before, 1).Frame());
        StateAt(h, "a", r.I.NextSeal - 1, joinable: true);
        r.Drop("a");
        r.Mode["a"] = 0;
        r.Play(10_000);
        // Back: his client's pads follow the wall clock and come in time at once, while his engine replays the ten seconds sealed
        // meanwhile (its NETFP still from before the drop). Before, he was the reporter again a housekeeping pass after his return, and
        // the stage group's flags followed his replay, seconds behind b's play.
        r.Back("a");
        r.Mode["a"] = 2;
        r.Play(1000);
        Assert.False(r.W.HostAway);
        Assert.True(r.I.Streams[h.Mem("a").Slot]!.FillStreak < SlotStream.LagFills);
        Assert.Same(h.Mem("b"), r.I.StateReporter);
        h.Send("b", new StateMsg(r.I.Id, r.I.Visit, r.I.NextSeal - 1, StateMsg.LoadEnded | StateMsg.BossLock, 5, StateReason.Boss).Frame());
        Assert.True(r.I.BossLock);
        // His engine at the ticks a second ago: caught up, the reporter again (b's STATE stays in effect: his newest is older).
        h.Send("a", new NetFpMsg(r.I.Id, r.I.Visit, (r.I.NextSeal - 1) / 60 * 60, 1).Frame());
        r.Play(300);
        Assert.Same(h.Mem("a"), r.I.StateReporter);
        Assert.True(r.I.BossLock);
    }

    [Fact]
    public void A_host_who_drops_exactly_at_a_gate_gets_a_fresh_grace_while_his_guest_goes_on()
    {
        var r = new FlapRig();
        var h = r.H;
        r.Play(1000);
        // Earlier drops left a debt: away 50 s, back with 3 s of late pads and 2 s in time (51 s).
        r.Drop("a");
        r.Mode["a"] = 0;
        r.Play(50_000);
        r.Back("a");
        r.Mode["a"] = 1;
        r.Play(3000);
        r.Mode["a"] = 2;
        r.Play(2000);
        Assert.InRange(r.W.HostDebt / (double)Stopwatch.Frequency, 49, 53);
        // b reaches the exit to stag0202 first and parks there; a's link closes before his own BARRIER.
        r.Mode["b"] = 0;
        h.Barrier("b", "stag0202", tick: r.I.NextSeal + 5);
        Assert.NotNull(r.I.Barrier);
        r.DropAndNote("a");
        r.Play(300);
        // b goes on at once (the barrier is decided without a); a's grace is a fresh 60 s: he holds nobody up. Before, the parked group
        // counted as shared with b: the grace was the 19 s his debt left of 70, and the instance ended for both before his return.
        Assert.Equal(60u, r.GraceStarts.Single());
        Assert.False(h.Hub.Counters.ContainsKey("host_grace_shortened"));
        var okB = h.Decision("b");
        Assert.Equal((BarrierDecision.New, "stag0202"), (okB.Decision, okB.Stage.ToLowerInvariant()));
        Assert.Equal("stag0202", h.Mem("b").Instance!.Stage.ToLowerInvariant());
        Assert.Null(h.Mem("a").Instance);
        Assert.Same(r.W, h.Mem("b").Instance!.World);
        r.Mode["a"] = 0;
        h.Advance(30_000);
        h.Housekeep();
        Assert.False(r.W.Ending);
        Assert.True(r.W.HostAway);
        // Back after 30 s: in time.
        r.Back("a");
        Assert.False(r.W.HostAway);
        Assert.False(r.W.Ending);
        Assert.Same(h.Mem("a"), r.W.Host);
    }

    [Fact]
    public void A_host_whose_link_closes_a_moment_before_his_gate_gets_a_fresh_grace_once_his_guest_goes_on_without_him()
    {
        var r = new FlapRig();
        var h = r.H;
        r.Play(1000);
        r.Drop("a");
        r.Mode["a"] = 0;
        r.Play(50_000);
        r.Back("a");
        r.Mode["a"] = 1;
        r.Play(3000);
        r.Mode["a"] = 2;
        r.Play(2000);
        // a's link closes in the stage, his BARRIER lost with it: an ordinary drop, b plays on with a's standing Digimon, the grace what
        // his debt leaves (19 s).
        r.DropAndNote("a");
        r.Mode["a"] = 0;
        Assert.InRange(r.GraceStarts.Single(), 17u, 21u);
        // b reaches the exit a moment later: decided without a, b goes on alone. a shares no stage group with b any more: his grace is a
        // fresh 60 s from its start (b's countdown says so). Before, the instance ended 19 s after the drop, b's countdown never grew.
        r.Play(150);
        r.Mode["b"] = 0;
        h.Barrier("b", "stag0202", tick: r.I.NextSeal + 5);
        r.Play(600);
        Assert.Equal(BarrierDecision.New, h.Decision("b").Decision);
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("host_grace_fresh"));
        Assert.InRange(r.LastAway, 58u, 60u);
        h.Advance(30_000);
        h.Housekeep();
        Assert.False(r.W.Ending);
        r.Back("a");
        Assert.False(r.W.HostAway);
        Assert.False(r.W.Ending);
        // While he shared b's stage group, his grace stays what the debt leaves (the drop above, and A_drop_soon_after_a_long_absence_...).
    }

    /// <summary>A member's link that hangs open for <paramref name="cut"/> seconds at a time, with a burst between (0.1 s of late pads, then
    /// <paramref name="inTime"/> s in time: the held packets come out, the client's pads of the last ticks in time), from 1 s on, for at
    /// most <paramref name="limit"/> s. Returns the seconds from the first cut to the member's lag_leave (null: still in).</summary>
    private static double? HalfOpenFlaps(FlapRig r, string name, double cut, double inTime, double limit)
    {
        r.Play(1000);
        var start = r.Seconds;
        while (r.Seconds - start < limit)
        {
            r.Mode[name] = 0;
            if (r.Play(cut * 1000, () => r.Out(name))) break;
            r.Mode[name] = 1;
            if (r.Play(100, () => r.Out(name))) break;
            r.Mode[name] = 2;
            if (r.Play(inTime * 1000, () => r.Out(name))) break;
        }
        return r.LagLeft ? r.Seconds - start : null;
    }

    [Fact]
    public void A_guest_whose_link_hangs_open_again_and_again_below_the_lag_limit_is_taken_out_by_its_lag_debt()
    {
        // Cuts of 7 s with 0.2 s in time between them (W7g's shape for a guest), and of 3.5 s with 0.3 s: the debt reaches 10 s within
        // the second or fourth cut. Before, every burst ended the fill streak and b stayed in a's stage group for the whole 120 s.
        foreach (var (cut, inTime, within) in new[] { (7.0, 0.2, 11.0), (3.5, 0.3, 13.5) })
        {
            var r = new FlapRig();
            var outAfter = HalfOpenFlaps(r, "b", cut, inTime, 120);
            Assert.True(outAfter is not null, $"cuts of {cut} s: b still in after 120 s");
            Assert.InRange(outAfter!.Value, 10, within);
            Assert.Equal(1, r.H.Hub.Counters.GetValueOrDefault("lag_leave_debt"));
            Assert.Null(r.H.Mem("b").Instance);
            Assert.Same(r.I, r.H.Mem("a").Instance);
            Assert.False(r.W.Ending);
            Assert.Same(r.W, r.H.Mem("b").World); // (b plays on alone with its seat, 19.6.8)
            Assert.Contains(r.H.P("a").All(Msg.WorldNotice, WorldNoticeMsg.Parse), n => n.Kind == WorldNoticeKind.Apart);
        }
    }

    [Fact]
    public void A_host_whose_link_hangs_open_below_the_silence_that_starts_a_grace_is_taken_out_by_his_lag_debt_and_keeps_his_instance()
    {
        // 3.5 s cuts (no grace: World:HostSilenceSeconds is 4) with 0.3 s in time: no grace, no absence debt; the lag debt takes him out of
        // b's stage group, he plays apart and stays host. Before, he stayed in it for good.
        var r = new FlapRig();
        var outAfter = HalfOpenFlaps(r, "a", 3.5, 0.3, 120);
        Assert.True(outAfter is not null, "a still in after 120 s");
        Assert.InRange(outAfter!.Value, 10, 13.5);
        Assert.False(r.H.Hub.Counters.ContainsKey("host_grace"));
        Assert.False(r.W.Ending);
        Assert.Same(r.H.Mem("a"), r.W.Host);
        Assert.Same(r.I, r.H.Mem("b").Instance);
    }

    [Fact]
    public void Brief_hiccups_paid_back_by_play_in_time_never_take_a_member_out()
    {
        // The lag rule's old allowance stays: a stall of 9.9 s once; stalls of 2 s every 6 s; 5 s stalls with 5 s in time between;
        // half-open cuts of 3.5 s every 10 s. Neither member is taken out in 120 s.
        foreach (var (stall, inTime, once) in new[] { (9.9, 30.0, true), (2.0, 4.0, false), (5.0, 5.0, false), (3.5, 6.5, false) })
        {
            foreach (var name in new[] { "a", "b" })
            {
                var r = new FlapRig();
                r.Play(1000);
                var start = r.Seconds;
                while (r.Seconds - start < 120 && !r.LagLeft)
                {
                    r.Mode[name] = 0;
                    r.Play(stall * 1000, () => r.LagLeft);
                    r.Mode[name] = 2;
                    r.Play((once ? 120 : inTime) * 1000, () => r.LagLeft);
                }
                Assert.False(r.LagLeft, $"{name}: stalls of {stall} s with {inTime} s in time");
                Assert.False(r.W.Ending);
            }
        }
    }

    /// <summary>b plays on alone in stag0101 (taken out for lag; its seat kept), a plays on.</summary>
    private static void LagOutBAt0101(Harness h, uint inst)
    {
        var i = h.I("a");
        h.Seal(12_000);
        for (var k = 0; k < 60; ++k)
        {
            h.Send("a", new InputMsg(inst, i.Visit, i.NextSeal + 3, [new Pad(1, 0, 0, 0, 0), new Pad(1, 0, 0, 0, 0)]).Frame());
            h.Seal(17);
        }
        h.Housekeep();
        Assert.Null(h.Mem("b").Instance);
        Assert.Equal("stag0101", h.Mem("b").LoneStage);
    }

    [Fact]
    public void A_guest_removed_while_it_plays_on_alone_is_on_the_former_hosts_story_its_own_key_is_refused_and_it_is_not_home()
    {
        var h = SealedPair(out var inst, new RelayOptions { LagLeaveSeconds = 10 }, new InstanceOptions { StoryMinVisitSeconds = 1 }, stage: "stag0101");
        var w = W(h, "a");
        var a = h.Mem("a");
        var b = h.Mem("b");
        var visit = h.I("a").Visit;
        LagOutBAt0101(h, inst);
        // With its seat: on a's story, a's guest (the control).
        var seated = h.Hub.PlayContext(b.AccountId, h.Now)!;
        Assert.Equal((w.Wid, false), (seated.Wid, seated.IsHost));
        // a removes b while its engine plays on alone in stag0101 on a's story.
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, b.Id).Frame());
        Assert.Null(b.World);
        // Its STORY with its own key: refused (owner, foreign). Before, it reached b's own keeper with a's progress (as if at home).
        h.Send("b", new StoryMsg(inst, visit, 0, new StoryDelta { Flags = [505] }, b.Id, 1).Frame());
        var ack = h.P("b").Last(Msg.StoryAck, StoryAckMsg.Parse);
        Assert.Equal(StoryRefusal.Owner, ack.Refused.Single().Reason);
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("story_foreign"));
        Assert.Empty(h.Hub.StoryState(b.AccountId)!.Flags);
        // The achievements read it as a's former guest, not at home (before: Wid 0, IsHost true, its own settings).
        var ctx = h.Hub.PlayContext(b.AccountId, h.Now)!;
        Assert.Equal((w.Wid, false, w.Settings.Difficulty), (ctx.Wid, ctx.IsHost, ctx.Difficulty));
        // Its next barrier: home with the replace (fix-d43's review), and from then on its own story and home again.
        h.Send("b", new BarrierMsg(inst, visit, 0, "stag0102", 0, 0b10, 0b10, 5, [(byte)'b']).Frame());
        Assert.Equal((BarrierDecision.Dissolve, StoryMode.Replace), (h.Decision("b").Decision, h.Decision("b").World.StoryMode));
        var home = h.Hub.PlayContext(b.AccountId, h.Now)!;
        Assert.Equal((0u, true), (home.Wid, home.IsHost));
        Assert.Same(a, w.Host);
    }

    [Fact]
    public void A_guest_back_without_its_slot_and_removed_is_on_the_former_hosts_story_until_its_next_barrier()
    {
        // b's link times out (its slot taken), it comes back without a resume and files its field (plays on alone in an empty hub
        // stage group of its new connection), then a removes it: its own key is refused and it is not home.
        var h = SealedPair(out var inst, new RelayOptions { LagLeaveSeconds = 600, ReconnectSeconds = 10 }, new InstanceOptions { StoryMinVisitSeconds = 1 },
            stage: "stag0101");
        var i = h.I("a");
        var visit = i.Visit;
        var w = W(h, "a");
        var b = h.Mem("b");
        h.Hub.Disconnect(b, h.P("b"), h.Now);
        for (var k = 0; k < 650; ++k)
        {
            h.Send("a", new InputMsg(inst, visit, i.NextSeal + 3, [new Pad(1, 0, 0, 0, 0), new Pad(1, 0, 0, 0, 0)]).Frame());
            h.Seal(17);
        }
        h.Housekeep();
        var peer = h.NewPeer();
        h.Hub.Connect(1, b.AccountId, "b", false, peer, h.Now);
        h.M["b"] = (b, peer);
        h.Send("b", new ResumeMsg(inst, visit, 1, 0, "stag0101").Frame());
        Assert.Equal(InstanceMode.Hub, b.Instance!.Mode);
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, b.Id).Frame());
        Assert.Null(b.World);
        h.Advance(1500);
        h.Send("b", StoryOf(h, "b", new StoryDelta { Flags = [505] }, b.Id).Frame());
        Assert.Equal(StoryRefusal.Owner, h.P("b").Last(Msg.StoryAck, StoryAckMsg.Parse).Refused.Single().Reason);
        Assert.Empty(h.Hub.StoryState(b.AccountId)!.Flags);
        var ctx = h.Hub.PlayContext(b.AccountId, h.Now)!;
        Assert.Equal((w.Wid, false), (ctx.Wid, ctx.IsHost));
    }

    [Fact]
    public void A_host_playing_apart_keeps_his_own_story_and_his_hosts_context()
    {
        // (The control of the lone rule: the story his engine plays alone is his own.) a is taken out for lag and plays apart.
        var h = SealedPair(out var inst, new RelayOptions { LagLeaveSeconds = 10 }, new InstanceOptions { StoryMinVisitSeconds = 1 }, stage: "stag0101");
        var w = W(h, "a");
        var a = h.Mem("a");
        var i = h.I("a");
        h.Seal(12_000);
        for (var k = 0; k < 60; ++k)
        {
            h.Send("b", new InputMsg(inst, i.Visit, i.NextSeal + 3, [new Pad(1, 0, 0, 0, 0), new Pad(1, 0, 0, 0, 0)]).Frame());
            h.Seal(17);
        }
        h.Housekeep();
        Assert.Null(a.Instance);
        Assert.Same(w, a.LoneWorld);
        h.Send("a", new StoryMsg(inst, i.Visit, 0, new StoryDelta { Flags = [505] }, a.Id, 1).Frame());
        Assert.Empty(h.P("a").Last(Msg.StoryAck, StoryAckMsg.Parse).Refused);
        Assert.Equal(1, h.Hub.KeeperOf(a.AccountId)!.Story.Flags[505]);
        var ctx = h.Hub.PlayContext(a.AccountId, h.Now)!;
        Assert.Equal((w.Wid, true), (ctx.Wid, ctx.IsHost));
    }
}
