using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;

namespace DW4Online.Tests;

/// <summary>
/// The second S1 review (round 55): floods that made the relay build state after state, a fork confirmed while the
/// parent waits at a barrier, a gap before J that never closes, a JOIN_AT nobody waits for, the resync rules of 6.9 (one
/// report per tick, the replay as the judge, three resyncs and the member continues alone) and five arrivals at once.
/// </summary>
public partial class HubLogicTests
{
    private static Harness JoinableLeader(out HubInstance inst, uint logged, InstanceOptions? instances = null, RelayOptions? relay = null,
        Action<StateJob>? stateRunner = null)
    {
        var h = new Harness(instances, relay ?? new RelayOptions { LagLeaveSeconds = 600 }, stateRunner);
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, (int)logged);
        h.Joinable("a");
        inst = h.I("a");
        return h;
    }

    /// <summary>
    /// One member's flood of BARRIER frames that each arrive alone: one decision a second, none while its state is still
    /// being built, a replaced state is not built, and a member past its state budget is closed (4,276 BARRIER built
    /// states for minutes and froze a sealed pair in another room for over a minute).
    /// </summary>
    [Fact]
    public void A_flood_of_lone_barriers_is_decided_once_a_second_within_the_state_budget()
    {
        var jobs = new List<StateJob>();
        var h = JoinableLeader(out _, 120, stateRunner: jobs.Add);
        for (var k = 0; k < 500; ++k) h.Send("b", new BarrierMsg(9000 + (uint)k, 7, 0, "stag0101", 0, 2, 2, 5, [1]).Frame());
        Assert.Single(jobs); // one JOIN with its state
        Assert.Equal(499, h.Hub.Counters["barrier_throttled"]);
        h.Advance(1100);
        h.Send("b", new BarrierMsg(1, 8, 0, "stag0101", 0, 2, 2, 5, [1]).Frame());
        Assert.Single(jobs); // its state is still being built
        Assert.False(jobs[0].Superseded);
        jobs[0].Build();
        h.Hub.FinishState(jobs[0]);
        h.Send("b", new BarrierMsg(2, 9, 0, "stag0101", 0, 2, 2, 5, [1]).Frame());
        Assert.Equal(2, jobs.Count);
        h.Mem("b").PendingState = null; // (a newer state would replace it: the relay does not build it)
        Assert.True(jobs[1].Superseded);

        // The state budget: a member that keeps asking for states is closed as abuse.
        var g = JoinableLeader(out _, 120, relay: new RelayOptions { LagLeaveSeconds = 600, StateBytesPerMinute = 1000 });
        g.Barrier("b", "stag0101");
        Assert.Equal(BarrierDecision.Join, g.Decision("b").Decision); // a full budget passes one large state
        g.Advance(1100);
        g.Send("b", new BarrierMsg(5, 5, 0, "stag0101", 0, 2, 2, 5, [1]).Frame());
        Assert.Equal(1, g.Hub.Counters["state_refused"]);
        Assert.Equal(CloseReason.Abuse, g.P("b").Closed);
    }

    /// <summary>A visit has one snapshot upload: its first chunk fixes the total, and the buffer grows with the chunks.</summary>
    [Fact]
    public void A_snapshot_upload_keeps_its_total_and_grows_with_its_chunks()
    {
        var s = new SnapshotStore();
        Assert.False(s.AddChunk(4_000_000, 0, new byte[17]));
        Assert.Throws<ProtocolException>(() => s.AddChunk(3_000_000, 0, new byte[17]));
        Assert.Throws<ProtocolException>(() => s.AddChunk(4_000_000, 0, new byte[17]));
        var bytes = Enumerable.Range(0, 150_000).Select(x => (byte)(x * 7)).ToArray();
        var t = new SnapshotStore();
        for (var off = 0; off < bytes.Length; off += 60_000)
            t.AddChunk((uint)bytes.Length, (uint)off, bytes[off..Math.Min(bytes.Length, off + 60_000)]);
        Assert.True(t.Complete);
        Assert.Equal(bytes, t.Bytes);
    }

    /// <summary>
    /// A fork confirmed while the parent waits at a barrier of the side that left (the leaver reached its stage change
    /// before the stayer's SPLIT came): the leaver's report goes with it and it parks at once; the stayer plays on and
    /// leads alone (before, the child never sealed and the stayer froze for good).
    /// </summary>
    [Fact]
    public void A_fork_confirmed_while_the_parent_waits_at_the_leavers_barrier_parks_the_leaver_and_the_stayer_plays_on()
    {
        var h = SealedPair(out var inst, new RelayOptions { LagLeaveSeconds = 600 });
        var i = h.I("a");
        h.Seal(1000);
        var f = i.NextSeal - 3;
        h.Send("a", new SplitMsg(inst, i.Visit, f, 0b01, SplitKind.Exit).Frame()); // a's copy took the exit
        h.Seal(500);
        h.Send("a", new BarrierMsg(inst, i.Visit, i.NextSeal - 2, "stag0202", 0, 1, 1, 5, [1]).Frame());
        Assert.False(i.Sealing);
        h.Seal(500);
        h.Send("b", new SplitMsg(inst, i.Visit, f, 0b01, SplitKind.Exit).Frame()); // b's SPLIT comes late
        var reassign = h.P("a").Last(Msg.Reassign, ReassignMsg.Parse);
        var ok = h.Decision("a");
        Assert.Equal((BarrierDecision.New, "stag0202", reassign.NewInst), (ok.Decision, ok.Stage, ok.Inst));
        Assert.True(h.P("a").Frames.FindLastIndex(x => x.Type == Msg.Reassign) < h.P("a").Frames.FindLastIndex(x => x.Type == Msg.BarrierOk));
        Assert.True(i.Sealing);
        Assert.Same(i, h.Mem("b").Instance);
        h.Seal(300);
        Assert.Contains(h.P("b").Ticks(), t => t.Rec.Events.Any(e => e.Type == Ev.LeadFrom));
        Assert.Equal(InstanceMode.SoloLed, i.Mode);
        Assert.False(h.Hub.Counters.ContainsKey("barrier_split"));

        // Four members: a and b leave, c reports, d's SPLIT comes three seconds late; a and b reach their barrier.
        var g = new Harness(relay: new RelayOptions { LagLeaveSeconds = 600 });
        var names = new[] { "a", "b", "c", "d" };
        for (var k = 0; k < 4; ++k) g.Connect(names[k], k);
        foreach (var n in names) g.Send(n, new HeadingMsg(g.Inst(n), "stag0102", 0, 5).Frame());
        foreach (var n in names) g.Barrier(n, "stag0102");
        g.Snapshot("a");
        foreach (var n in names[1..]) g.Send(n, new SnapHashMsg(g.Inst("a"), g.I("a").Visit, 0xAA).Frame());
        var p = g.I("a");
        g.Seal(1000);
        var fork = p.NextSeal - 3;
        foreach (var n in new[] { "a", "b", "c" }) g.Send(n, new SplitMsg(p.Id, p.Visit, fork, 0b0011, SplitKind.Exit).Frame());
        g.Seal(500);
        foreach (var n in new[] { "a", "b" }) g.Send(n, new BarrierMsg(p.Id, p.Visit, p.NextSeal - 1, "stag0103", 0, 0b11, 0b11, 5, [1]).Frame());
        var stopped = p.NextSeal;
        g.Seal(1600);
        Assert.Equal(stopped, p.NextSeal);
        g.Hub.Housekeep(g.Now); // ForkConfirmMs: both sides reported
        Assert.Equal(BarrierDecision.New, g.Decision("a").Decision);
        Assert.Equal(BarrierDecision.New, g.Decision("b").Decision);
        Assert.Same(g.Mem("a").Instance, g.Mem("b").Instance);
        Assert.True(p.Sealing);
        g.Seal(300);
        Assert.True(p.NextSeal > stopped + 5);
        Assert.Equal(2, p.Count);
        Assert.False(g.Hub.Counters.ContainsKey("barrier_split"));
    }

    /// <summary>
    /// Sealed from J while the leader's LOG ends before J: the leader goes (past the reconnect window) and the gap can
    /// never close; the joiner goes back to its own stage as NEW and nothing piles up. A leader whose LOG stays behind
    /// for Instances:MaxAheadSeconds is closed.
    /// </summary>
    [Fact]
    public void A_gap_before_J_that_never_closes_sends_the_joiner_back_to_its_own_stage()
    {
        var h = JoinableLeader(out var inst, 60);
        h.Barrier("b", "stag0101");
        var ok = h.Decision("b");
        h.Send("b", new ReadyMsg(ok.NewInst, ok.NewVisit, 60, 0, 5000, [7]).Frame());
        var req = h.P("a").Last(Msg.JoinReq, JoinReqMsg.Parse);
        h.Send("a", new JoinAtMsg(req.Inst, req.Visit, 70).Frame());
        Assert.Same(inst, h.Mem("b").Instance);
        h.Hub.Disconnect(h.Mem("a"), h.P("a"), h.Now); // lost before its LOG reached J
        h.Seal(11_000);
        h.Hub.Housekeep(h.Now);
        h.Hub.Housekeep(h.Now);
        Assert.Equal(1, h.Hub.Counters["gap_lost"]);
        Assert.True(inst.Ended);
        var back = h.Decision("b");
        Assert.Equal((BarrierDecision.New, BarrierNotice.JoinFailed, "stag0101"), (back.Decision, back.Notice, back.Stage));
        Assert.Equal(InstanceMode.SoloLed, h.I("b").Mode);
        Assert.DoesNotContain(h.P("b").Ticks(), t => t.Tick >= 70);

        // Connected, but its LOG never reaches J.
        var g = JoinableLeader(out var gi, 60);
        g.Barrier("b", "stag0101");
        var gok = g.Decision("b");
        g.Send("b", new ReadyMsg(gok.NewInst, gok.NewVisit, 60, 0, 5000, [7]).Frame());
        var greq = g.P("a").Last(Msg.JoinReq, JoinReqMsg.Parse);
        g.Send("a", new JoinAtMsg(greq.Inst, greq.Visit, 70).Frame());
        g.Seal(14_000);
        g.Hub.Housekeep(g.Now);
        Assert.False(gi.Ended);
        g.Seal(2_000);
        g.Hub.Housekeep(g.Now);
        Assert.True(gi.Ended);
        Assert.Equal(CloseReason.Protocol, g.P("a").Closed);
        Assert.Equal(BarrierDecision.New, g.Decision("b").Decision);
    }

    /// <summary>
    /// The joiner went between JOIN_REQ and JOIN_AT: the leader's client already waits for SEALED(J). The relay seals J with
    /// the leader alone and LEAD_FROM J + 1 once its LOG reaches J; then the instance is solo-led again (before, the
    /// leader froze for good).
    /// </summary>
    [Fact]
    public void A_join_at_whose_joiner_went_lets_the_leader_lead_alone_again()
    {
        foreach (var how in new[] { "bye", "barrier", "tries" })
        {
            var h = JoinableLeader(out var inst, 60);
            h.Barrier("b", "stag0101");
            var ok = h.Decision("b");
            h.Send("b", new ReadyMsg(ok.NewInst, ok.NewVisit, 60, 0, 5000, [7]).Frame());
            var req = h.P("a").Last(Msg.JoinReq, JoinReqMsg.Parse);
            switch (how)
            {
                case "bye":
                    h.Hub.Disconnect(h.Mem("b"), h.P("b"), h.Now, graceful: true);
                    break;
                case "barrier":
                    h.Advance(1100);
                    h.Send("b", new BarrierMsg(ok.Inst, ok.Visit, 0, "stag0102", 0, 1, 1, 5, [1]).Frame());
                    break;
                default:
                    for (var k = 0; k < 4; ++k)
                    {
                        h.Advance(5100);
                        h.Hub.Housekeep(h.Now);
                    }
                    h.Log("a", 60, 300);
                    break;
            }
            var j = inst.Log.Count + 10;
            h.Send("a", new JoinAtMsg(req.Inst, req.Visit, j).Frame());
            Assert.Equal(1, h.Hub.Counters["join_at_orphan"]);
            Assert.Equal(InstanceMode.Sealed, inst.Mode);
            h.Seal(400);
            Assert.True(inst.SoloPending, how);
            Assert.Empty(h.P("a").Ticks()); // tick J waits for the leader's LOG before it
            h.Log("a", inst.Log.Count, (int)(j - inst.Log.Count));
            var sealedJ = h.P("a").Ticks().Single();
            Assert.Equal(j, sealedJ.Tick);
            Assert.Equal((byte)0b01, sealedJ.Rec.Mask);
            Assert.Contains(sealedJ.Rec.Events, e => e.Type == Ev.LeadFrom);
            Assert.Equal(InstanceMode.SoloLed, inst.Mode);
            h.Log("a", j + 1, 30); // the leader logs alone again
            Assert.Equal(j + 31, inst.Log.Count);
        }
    }

    /// <summary>One NETFP per member and tick: a second report of a tick that compared equal starts no vote.</summary>
    [Fact]
    public void A_second_fingerprint_of_the_same_tick_is_no_vote()
    {
        var h = SealedPair(out var inst, new RelayOptions { LagLeaveSeconds = 600 });
        var i = h.I("a");
        h.Seal(3000);
        h.Send("a", new NetFpMsg(inst, i.Visit, 60, 1).Frame());
        h.Send("b", new NetFpMsg(inst, i.Visit, 60, 1).Frame());
        h.Send("b", new NetFpMsg(inst, i.Visit, 60, 2).Frame());
        Assert.False(h.P("a").Has(Msg.Resync));
        Assert.False(h.P("b").Has(Msg.Resync));
        Assert.Equal(1, h.Hub.Counters["netfp_repeat"]);
        // At a barrier nobody is resynced (the visit ends there; a RESYNC reached a parked member and threw it back).
        h.Barrier("b", "stag0202", tick: i.NextSeal - 1);
        h.Send("a", new NetFpMsg(inst, i.Visit, 120, 1).Frame());
        h.Send("b", new NetFpMsg(inst, i.Visit, 120, 2).Frame());
        Assert.False(h.P("b").Has(Msg.Resync));
        Assert.Equal(1, h.Hub.Counters["resync_at_barrier"]);
    }

    /// <summary>
    /// Two members, the longer one's engine is off: the first vote resyncs the healthy partner, but its replay's
    /// fingerprint (READY) contradicts the longer member, which is resynced and loses every vote after; after
    /// Instances:ResyncLimit resyncs in ten minutes it continues alone: LEAVE, and it is sent the ticks up to it (before,
    /// the healthy member was resynced every ten seconds until it was removed).
    /// </summary>
    [Fact]
    public void The_replay_decides_who_is_off_and_three_resyncs_in_ten_minutes_continue_alone()
    {
        var h = SealedPair(out var inst, new RelayOptions { LagLeaveSeconds = 600 });
        var i = h.I("a");
        h.Seal(4000);
        h.Send("a", new NetFpMsg(inst, i.Visit, 60, 0xA).Frame());
        h.Send("b", new NetFpMsg(inst, i.Visit, 60, 0xB).Frame());
        Assert.Single(h.P("b").All(Msg.Resync, ResyncMsg.Parse)); // the longer member wins a tie
        h.Send("a", new NetFpMsg(inst, i.Visit, 120, 0xA2).Frame());
        h.Send("b", new ReadyMsg(inst, i.Visit, 120, 0xB2, 4000, []).Frame()); // b's replay of the log disagrees with a
        Assert.Equal(1, h.Mem("a").ReplayMismatches);
        Assert.Single(h.P("a").All(Msg.Resync, ResyncMsg.Parse));
        h.Send("a", new ReadyMsg(inst, i.Visit, 180, 0xC, 4000, []).Frame());
        h.Send("b", new NetFpMsg(inst, i.Visit, 180, 0xC).Frame());
        Assert.Equal(1, h.Mem("a").ReplayMismatches);

        for (var round = 2; round <= 3; ++round)
        {
            h.Seal(11_000);
            var t = (i.NextSeal - 1) / 60 * 60;
            h.Send("a", new NetFpMsg(inst, i.Visit, t, 1).Frame());
            h.Send("b", new NetFpMsg(inst, i.Visit, t, 2).Frame());
            Assert.Equal(round, h.P("a").All(Msg.Resync, ResyncMsg.Parse).Count); // a loses: a replay contradicted it
            h.Send("a", new ReadyMsg(inst, i.Visit, t, 2, 4000, []).Frame()); // its replay agrees with b
        }
        Assert.Single(h.P("b").All(Msg.Resync, ResyncMsg.Parse));
        h.Seal(11_000);
        var last = (i.NextSeal - 1) / 60 * 60;
        h.Send("a", new NetFpMsg(inst, i.Visit, last, 1).Frame());
        h.Send("b", new NetFpMsg(inst, i.Visit, last, 2).Frame());
        Assert.Equal(3, h.P("a").All(Msg.Resync, ResyncMsg.Parse).Count);
        Assert.Equal(1, h.Hub.Counters["resync_limit"]);
        Assert.Null(h.Mem("a").Instance);
        h.Seal(500);
        // a is sent the ticks up to its LEAVE (its client continues alone); b leads alone after it.
        var aTicks = h.P("a").Ticks();
        var leave = aTicks.Single(t => t.Rec.Events.Any(e => e.Type == Ev.Leave && e.Slot == 0));
        Assert.Equal(leave.Tick, aTicks.Last().Tick);
        Assert.Contains(h.P("b").Ticks(), t => t.Rec.Events.Any(e => e.Type == Ev.LeadFrom));
        Assert.Equal(InstanceMode.SoloLed, i.Mode);
    }

    /// <summary>A SPLIT nobody confirms holds the fingerprints back only until it runs out; then they are compared.</summary>
    [Fact]
    public void An_unconfirmed_split_hides_no_fingerprint_comparison()
    {
        var h = SealedPair(out var inst, new RelayOptions { LagLeaveSeconds = 600 });
        var i = h.I("a");
        h.Seal(3000);
        var t = (i.NextSeal - 1) / 60 * 60;
        h.Send("b", new SplitMsg(inst, i.Visit, t - 1, 0b01, SplitKind.Exit).Frame()); // forged: only b reports it
        h.Send("a", new NetFpMsg(inst, i.Visit, t, 1).Frame());
        h.Send("b", new NetFpMsg(inst, i.Visit, t, 2).Frame());
        Assert.False(h.P("b").Has(Msg.Resync));
        h.Advance(2100);
        h.Hub.Housekeep(h.Now);
        Assert.Equal(1, h.Hub.Counters["split_unconfirmed"]);
        Assert.True(h.P("b").Has(Msg.Resync));
    }

    /// <summary>The instance's flags and context come from its lowest connected member only.</summary>
    [Fact]
    public void Only_the_lowest_member_reports_the_instance_state()
    {
        var h = SealedPair(out var inst);
        var i = h.I("a");
        h.Send("b", new StateMsg(inst, i.Visit, 0, StateMsg.BossLock, 99, StateReason.Boss).Frame());
        Assert.False(i.BossLock);
        Assert.Equal(1, h.Hub.Counters["state_ignored"]);
        h.Send("a", new StateMsg(inst, i.Visit, 0, StateMsg.BossLock, 5, StateReason.Boss).Frame());
        Assert.True(i.BossLock);
    }

    /// <summary>
    /// Five announced arrivals for one stage: the first holds for the three announced first (they fit), they arrive
    /// together; the fifth plays alone (before, the first stayed alone because not every announced member fit). The
    /// co-arrivals' BARRIER_OK names the others by slot.
    /// </summary>
    [Fact]
    public void Five_arrivals_at_once_hold_for_those_that_fit()
    {
        var h = new Harness();
        var names = new[] { "a", "b", "c", "d", "e" };
        for (var k = 0; k < 5; ++k)
        {
            h.Connect(names[k], k);
            h.Send(names[k], new HeadingMsg(h.Inst(names[k]), "stag0201", 0, 5).Frame());
            h.Advance(10);
        }
        h.Barrier("a", "stag0201");
        var wait = h.P("a").Last(Msg.BarrierWait, BarrierWaitMsg.Parse);
        Assert.Equal(WaitReason.CoArrival, wait.Reason);
        Assert.Equal(["b", "c", "d"], wait.Names);
        h.Barrier("e", "stag0201");
        var e = h.Decision("e");
        Assert.Equal((BarrierDecision.New, InstanceMode.SoloLed), (e.Decision, e.Mode));
        foreach (var n in new[] { "b", "c", "d" }) h.Barrier(n, "stag0201");
        var a = h.Decision("a");
        Assert.Equal((BarrierDecision.New, InstanceMode.Sealed, 3), (a.Decision, a.Mode, a.CoArrivals.Count));
        var b = h.Decision("b");
        Assert.Equal(BarrierDecision.CoArrive, b.Decision);
        Assert.Equal(new[] { (0, 0), (2, 2), (3, 3) }, b.CoArrivals.Select(c => ((int)c.Slot, (int)c.Member)).ToArray());
        Assert.NotEqual(e.NewInst, a.NewInst);
    }
}
