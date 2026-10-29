using System.Diagnostics;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using Microsoft.Extensions.Logging.Abstractions;

namespace DW4Online.Tests;

/// <summary>
/// The relay's room logic without sockets (SEAMLESS.md 3.4, 6.4-6.9): members are test peers that collect frames, the
/// clock is a number the test moves. Covers barrier decisions with holds, joins (solo-led and sealed), sealing and
/// fills, co-arrivals, forks, leaves, dissolves, the barrier's discard and the fingerprint vote.
/// </summary>
public class HubLogicTests
{
    public delegate T Parser<out T>(ReadOnlySpan<byte> payload);

    private sealed class Peer : IHubPeer
    {
        public List<(byte Type, byte[] Payload)> Frames { get; } = [];
        public CloseReason? Closed { get; private set; }

        public void Send(byte[] frame, bool bulk = false) => Frames.Add((frame[0], frame[Wire.HeaderBytes..]));
        public void Close(CloseReason reason, string text) => Closed = reason;

        public List<T> All<T>(byte type, Parser<T> parse) => Frames.Where(f => f.Type == type).Select(f => parse(f.Payload)).ToList();
        public T Last<T>(byte type, Parser<T> parse) => parse(Frames.Last(f => f.Type == type).Payload);
        public bool Has(byte type) => Frames.Any(f => f.Type == type);

        public List<(uint Tick, TickRecord Rec)> Ticks(byte type = Msg.Sealed) => Frames.Where(f => f.Type == type)
            .Select(f => TicksMsg.Parse(type, f.Payload)).SelectMany(m => m.Ticks.Select((t, k) => (m.FirstTick + (uint)k, t))).ToList();

        public byte[] StateBlob()
        {
            var chunks = All(Msg.StateData, StateDataMsg.Parse);
            var last = chunks.Where(c => c.Offset == 0).Last();
            var start = chunks.LastIndexOf(last);
            var buf = new byte[last.Total];
            foreach (var c in chunks.Skip(start)) c.Chunk.CopyTo(buf, (int)c.Offset);
            return buf;
        }
    }

    private sealed class Harness
    {
        public RoomHub Hub { get; }
        public long Now { get; private set; } = Stopwatch.Frequency * 1000;
        public Dictionary<string, (HubMember Member, Peer Peer)> M { get; } = [];

        public Harness(InstanceOptions? instances = null, RelayOptions? relay = null)
        {
            Hub = new RoomHub(new RoomInfo("ROOM01", 43, 1, 2, 10), relay ?? new RelayOptions(), new SealingOptions(),
                instances ?? new InstanceOptions(), NullLogger.Instance);
        }

        public void Advance(double ms) => Now += (long)(ms * Stopwatch.Frequency / 1000.0);

        public Peer Connect(string name, int index)
        {
            var peer = new Peer();
            var m = Hub.Connect(index, 100 + index, name, index == 0, peer, Now);
            M[name] = (m, peer);
            return peer;
        }

        public void Send(string name, byte[] frame) => Hub.Receive(M[name].Member, frame[0], frame[Wire.HeaderBytes..], Now);
        public Peer P(string name) => M[name].Peer;
        public HubMember Mem(string name) => M[name].Member;
        public uint Inst(string name) => Mem(name).Instance?.Id ?? Mem(name).Joining!.Target.Id;
        public HubInstance I(string name) => Mem(name).Instance!;

        /// <summary>The member parks at the barrier of its current instance and goes to <paramref name="stage"/>.</summary>
        public void Barrier(string name, string stage, uint tick = 0, ulong context = 5)
        {
            var i = Mem(name).Instance!;
            Send(name, new BarrierMsg(i.Id, i.Visit, tick, stage, 0, i.PresentMask, i.PresentMask, context, [(byte)name[0]]).Frame());
        }

        public BarrierOkMsg Decision(string name) => P(name).Last(Msg.BarrierOk, BarrierOkMsg.Parse);

        public void Snapshot(string name, byte[]? bytes = null)
        {
            var i = I(name);
            bytes ??= Enumerable.Range(0, 100).Select(x => (byte)x).ToArray();
            Send(name, new SnapshotMsg(i.Id, i.Visit, (uint)bytes.Length, 0, bytes).Frame());
            Send(name, new SnapHashMsg(i.Id, i.Visit, 0xAA).Frame());
        }

        public void Log(string name, uint first, int count)
        {
            var i = I(name);
            Send(name, new LogMsg(i.Id, i.Visit, first, Enumerable.Range(0, count).Select(k => new Pad((ushort)(first + k), 0, 0, 0, 0)).ToArray()).Frame());
        }

        public void Joinable(string name, bool joinable = true, ulong context = 5)
        {
            var i = I(name);
            Send(name, new StateMsg(i.Id, i.Visit, 0, (byte)(joinable ? StateMsg.Joinable | StateMsg.MembershipSafe : 0), context,
                joinable ? StateReason.None : StateReason.EntryScene).Frame());
        }

        public void Seal(double ms)
        {
            var end = Now + (long)(ms * Stopwatch.Frequency / 1000.0);
            while (Now < end)
            {
                Advance(Math.Min(4, (end - Now) * 1000.0 / Stopwatch.Frequency));
                Hub.Seal(Now);
            }
        }
    }

    [Fact]
    public void A_member_starts_in_a_solo_hub_instance_and_a_lone_arrival_leads_its_own_logged_instance()
    {
        var h = new Harness();
        var a = h.Connect("a", 0);
        var welcome = a.Last(Msg.Welcome, WelcomeMsg.Parse);
        Assert.Equal((InstanceMode.Hub, (byte)0, ""), (welcome.Mode, welcome.Slot, welcome.Stage));
        h.Barrier("a", "stag0101");
        var ok = h.Decision("a");
        Assert.Equal((BarrierDecision.New, InstanceMode.SoloLed, (byte)0), (ok.Decision, ok.Mode, ok.OwnSlot));
        Assert.Equal(new sbyte[] { 0, -1, -1, -1 }, ok.SlotMap);
        h.Snapshot("a");
        h.Log("a", 0, 30);
        h.Log("a", 20, 20); // a resent overlap is fine
        Assert.Equal(40u, h.I("a").Log.Count);
        Assert.Throws<ProtocolException>(() => h.Log("a", 50, 5)); // a gap is not
        Assert.True(h.I("a").Snapshot.Complete);
    }

    [Fact]
    public void A_later_arrival_waits_for_the_entry_scene_joins_by_catch_up_and_the_solo_leader_seals_from_J()
    {
        var h = new Harness();
        h.Connect("a", 0);
        var b = h.Connect("b", 1);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 120);
        h.Barrier("b", "stag0101");
        var wait = b.Last(Msg.BarrierWait, BarrierWaitMsg.Parse);
        Assert.Equal(WaitReason.NotJoinable, wait.Reason);
        Assert.Equal(["a"], wait.Names);
        Assert.False(b.Has(Msg.BarrierOk));

        h.Joinable("a");
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.Join, h.Inst("a"), (byte)1), (ok.Decision, ok.NewInst, ok.OwnSlot));
        var blob = StateBlob.Parse(b.StateBlob());
        Assert.Equal((StatePurpose.Join, (byte)1, 100, 120), (blob.Purpose, blob.Slot, blob.Snapshot.Length, blob.Ticks.Count));
        h.Log("a", 120, 30);
        Assert.Equal(30, b.Ticks(Msg.Live).Count); // the newest ticks follow as LIVE

        h.Send("b", new ReadyMsg(ok.NewInst, ok.NewVisit, 120, 0, 5000, [7]).Frame());
        var req = h.P("a").Last(Msg.JoinReq, JoinReqMsg.Parse);
        Assert.Equal((byte)1, req.Slot);
        h.Send("a", new JoinAtMsg(req.Inst, req.Visit, 155).Frame());
        var inst = h.I("a");
        Assert.Equal(InstanceMode.Sealed, inst.Mode);
        Assert.Same(inst, h.Mem("b").Instance);
        h.Log("a", 150, 5); // the leader's LOG still covers the ticks before J
        h.Seal(400);
        var aTicks = h.P("a").Ticks();
        Assert.Equal(155u, aTicks[0].Tick);
        var join = aTicks[0].Rec.Events.Single(e => e.Type == Ev.Join);
        Assert.Equal(1, join.Slot);
        Assert.Equal([7], join.Payload[2..]);
        Assert.Contains(aTicks[0].Rec.Events, e => e.Type == Ev.Delay && e.Slot == 0);
        var bTicks = b.Ticks();
        Assert.Equal(Enumerable.Range(150, bTicks.Count).Select(x => (uint)x), bTicks.Select(t => t.Tick)); // in order, no gap
        Assert.Equal((byte)0b11, aTicks[1].Rec.Mask);
        Assert.Equal(inst.NextSeal, inst.Log.Count);
    }

    private static Harness SealedPair(out uint inst, RelayOptions? relay = null)
    {
        var h = new Harness(relay: relay);
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Send("a", new HeadingMsg(h.Inst("a"), "stag0201", 0, 5).Frame());
        h.Send("b", new HeadingMsg(h.Inst("b"), "stag0201", 0, 5).Frame());
        h.Barrier("a", "stag0201");
        Assert.Equal(WaitReason.CoArrival, h.P("a").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason);
        h.Barrier("b", "stag0201");
        var okA = h.Decision("a");
        var okB = h.Decision("b");
        Assert.Equal((BarrierDecision.New, InstanceMode.Sealed), (okA.Decision, okA.Mode));
        Assert.Equal((byte)1, okA.CoArrivals.Single().Slot);
        Assert.Equal([(byte)'b'], okA.CoArrivals.Single().Carry);
        Assert.Equal((BarrierDecision.CoArrive, okA.NewInst, (byte)1), (okB.Decision, okB.NewInst, okB.OwnSlot));
        h.Snapshot("a");
        Assert.Equal(StatePurpose.CoArrival, StateBlob.Parse(h.P("b").StateBlob()).Purpose);
        Assert.False(h.I("a").Sealing); // b's hash is not in yet
        h.Send("b", new SnapHashMsg(okB.NewInst, okB.NewVisit, 0xAA).Frame());
        Assert.True(h.I("a").Sealing);
        inst = okA.NewInst;
        return h;
    }

    [Fact]
    public void Announced_groups_arrive_together_and_seal_from_tick_zero_with_fills()
    {
        var h = SealedPair(out var inst);
        var i = h.I("a");
        var visit = i.Visit;
        h.Send("a", new InputMsg(inst, visit, 0, [new Pad(1, 0, 0, 0, 0), new Pad(2, 0, 0, 0, 0), new Pad(3, 0, 0, 0, 0)]).Frame());
        h.Send("b", new InputMsg(inst, visit, 0, [new Pad(9, 0, 0, 0, 0)]).Frame());
        h.Seal(100); // ticks 0..5 are due (S(0) = now + two ticks)
        var ticks = h.P("a").Ticks();
        Assert.True(ticks.Count >= 4, ticks.Count.ToString());
        Assert.Equal((ushort)1, ticks[0].Rec.Pads[0].Buttons);
        Assert.Equal((ushort)9, ticks[0].Rec.Pads[1].Buttons);
        Assert.Equal((ushort)9, ticks[1].Rec.Pads[1].Buttons); // b's pad filled with its last one
        Assert.Equal((ushort)3, ticks[3].Rec.Pads[0].Buttons); // a's too, after its three
        Assert.True(i.Streams[1]!.Filled >= 3);
        Assert.Equal(h.P("a").Ticks().Select(t => t.Tick), h.P("b").Ticks().Select(t => t.Tick));
        // A pad for a tick already sealed is dropped and counted.
        h.Send("b", new InputMsg(inst, visit, 1, [new Pad(5, 0, 0, 0, 0)]).Frame());
        Assert.Equal(1, h.Hub.Counters["late_pads"]);
    }

    [Fact]
    public void Another_context_a_full_instance_or_a_long_catch_up_open_a_new_instance_with_a_notice()
    {
        var h = new Harness(new InstanceOptions { MaxCatchUpSeconds = 30 });
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Connect("c", 2);
        h.Barrier("a", "stag0401", context: 1);
        h.Snapshot("a");
        h.Joinable("a", context: 1);
        h.Barrier("b", "stag0401", context: 2); // another quest
        Assert.Equal((BarrierDecision.New, BarrierNotice.OtherContext), (h.Decision("b").Decision, h.Decision("b").Notice));
        Assert.Equal(["a"], h.Decision("b").NoticeNames);
        h.Log("a", 0, 120);
        h.Mem("c").Rate = 61; // a weak machine: 120 ticks at (61 - 59.94) steps/s is about two minutes
        h.Barrier("c", "stag0401", context: 1);
        Assert.Equal(BarrierNotice.TooLongToCatchUp, h.Decision("c").Notice);
    }

    [Fact]
    public void A_boss_locked_target_lets_the_arrival_wait_or_go_alone()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("a", "stagBS01");
        h.Snapshot("a");
        var i = h.I("a");
        h.Send("a", new StateMsg(i.Id, i.Visit, 0, StateMsg.BossLock, 5, StateReason.Boss).Frame());
        h.Barrier("b", "stagBS01");
        var wait = h.P("b").Last(Msg.BarrierWait, BarrierWaitMsg.Parse);
        Assert.Equal((WaitReason.BossLock, BarrierWaitMsg.ChoiceNeeded), (wait.Reason, wait.Flags));
        h.Send("b", new BarrierChoiceMsg(0, BarrierChoiceMsg.Wait).Frame());
        Assert.False(h.P("b").Has(Msg.BarrierOk));
        h.Send("a", new StateMsg(i.Id, i.Visit, 0, StateMsg.Joinable, 5, StateReason.None).Frame()); // the boss fell
        Assert.Equal(BarrierDecision.Join, h.Decision("b").Decision);

        var g = new Harness();
        g.Connect("a", 0);
        g.Connect("b", 1);
        g.Barrier("a", "stagBS01");
        g.Snapshot("a");
        var gi = g.I("a");
        g.Send("a", new StateMsg(gi.Id, gi.Visit, 0, StateMsg.BossLock, 5, StateReason.Boss).Frame());
        g.Barrier("b", "stagBS01");
        g.Send("b", new BarrierChoiceMsg(0, BarrierChoiceMsg.Alone).Frame());
        Assert.Equal((BarrierDecision.New, BarrierNotice.WentAlone), (g.Decision("b").Decision, g.Decision("b").Notice));
    }

    [Fact]
    public void A_join_hold_that_runs_out_ends_in_new_and_a_hub_target_dissolves_the_group()
    {
        var h = new Harness(new InstanceOptions { JoinHoldSeconds = 2 });
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("a", "dng101b1");
        h.Snapshot("a");
        h.Barrier("b", "dng101b1");
        Assert.False(h.P("b").Has(Msg.BarrierOk));
        h.Advance(2100);
        h.Hub.Housekeep(h.Now);
        Assert.Equal((BarrierDecision.New, BarrierNotice.HoldExpired), (h.Decision("b").Decision, h.Decision("b").Notice));

        var p = SealedPair(out _);
        p.Barrier("a", "stagML11", tick: 0);
        Assert.DoesNotContain(p.P("a").All(Msg.BarrierOk, BarrierOkMsg.Parse), o => o.Decision == BarrierDecision.Dissolve); // b has not reported
        p.Barrier("b", "stagML11", tick: 0);
        var da = p.Decision("a");
        var db = p.Decision("b");
        Assert.Equal((BarrierDecision.Dissolve, InstanceMode.Hub, (byte)0), (da.Decision, da.Mode, da.OwnSlot));
        Assert.Equal(new sbyte[] { -1, 0, -1, -1 }, db.SlotMap);
        Assert.NotEqual(da.NewInst, db.NewInst);
    }

    [Fact]
    public void A_fork_moves_the_leavers_to_a_departing_child_that_shares_the_log()
    {
        var h = new Harness();
        foreach (var (n, index) in new[] { ("a", 0), ("b", 1), ("c", 2) }) h.Connect(n, index);
        foreach (var n in new[] { "a", "b", "c" }) h.Send(n, new HeadingMsg(h.Inst(n), "stag0102", 0, 5).Frame());
        h.Barrier("a", "stag0102");
        h.Barrier("b", "stag0102");
        h.Barrier("c", "stag0102");
        h.Snapshot("a");
        h.Send("b", new SnapHashMsg(h.Inst("a"), h.I("a").Visit, 0xAA).Frame());
        h.Send("c", new SnapHashMsg(h.Inst("a"), h.I("a").Visit, 0xAA).Frame());
        var parent = h.I("a");
        Assert.True(parent.Sealing);
        h.Seal(300);
        var k = parent.NextSeal;
        foreach (var n in new[] { "a", "b", "c" }) h.Send(n, new SplitMsg(parent.Id, parent.Visit, k - 3, 0b100, SplitKind.Exit).Frame());
        var child = h.I("c");
        Assert.NotSame(parent, child);
        Assert.True(child.Departing);
        Assert.Equal(parent.Id, child.Parent);
        Assert.Equal(new ForkSide(k - 3, 0b100, SplitKind.Exit, ForkSide.Leave), child.Forks.Single());
        Assert.Equal(ForkSide.Stay, parent.Forks.Single().Side);
        var reassign = h.P("c").Last(Msg.Reassign, ReassignMsg.Parse);
        Assert.Equal((parent.Id, child.Id, k, ReassignMsg.Departing), (reassign.OldInst, reassign.NewInst, reassign.FromTick, reassign.Flags));
        Assert.Equal(k, child.Log.Count);
        Assert.Equal(parent.Log.Range(0, k), child.Log.Range(0, k));
        h.Seal(200);
        // The child of one member leads alone from k + 1; the parent seals a and b only.
        Assert.Equal(InstanceMode.SoloLed, child.Mode);
        var cTicks = h.P("c").Ticks();
        Assert.Contains(cTicks, t => t.Tick == k && t.Rec.Events.Any(e => e.Type == Ev.LeadFrom));
        Assert.Equal((byte)0b011, h.P("a").Ticks().Last().Rec.Mask);
        Assert.Equal(1, h.I("c").Count);
        Assert.Equal(2, parent.Count);
    }

    [Fact]
    public void A_member_gone_past_the_reconnect_window_leaves_and_the_other_leads_alone()
    {
        // a sends no pads in this test either: the lag limit is out of the way (it would take a out too).
        var h = SealedPair(out _, new RelayOptions { LagLeaveSeconds = 600 });
        var i = h.I("a");
        h.Seal(100);
        h.Hub.Disconnect(h.Mem("b"), h.P("b"), h.Now);
        h.Seal(5000);
        h.Hub.Housekeep(h.Now);
        Assert.Same(i, h.Mem("b").Instance); // still filled inside the window
        h.Seal(5100);
        h.Hub.Housekeep(h.Now);
        Assert.Null(h.Mem("b").Instance);
        h.Seal(300);
        var ticks = h.P("a").Ticks();
        var leave = ticks.First(t => t.Rec.Events.Any(e => e.Type == Ev.Leave));
        Assert.Equal(1, leave.Rec.Events.First(e => e.Type == Ev.Leave).Slot);
        Assert.Contains(ticks, t => t.Rec.Events.Any(e => e.Type == Ev.LeadFrom));
        Assert.Equal(InstanceMode.SoloLed, i.Mode);
    }

    [Fact]
    public void A_barrier_discards_ticks_sealed_after_it_and_a_fingerprint_minority_gets_resync()
    {
        var h = SealedPair(out var inst);
        var i = h.I("a");
        h.Seal(1500);
        Assert.True(i.NextSeal > 70);
        // Net fingerprints at tick 60: two members disagree, the longer member (a) wins.
        h.Send("a", new NetFpMsg(inst, i.Visit, 60, 1).Frame());
        h.Send("b", new NetFpMsg(inst, i.Visit, 60, 2).Frame());
        Assert.True(h.P("b").Has(Msg.Resync));
        Assert.Equal(StatePurpose.Resync, StateBlob.Parse(h.P("b").StateBlob()).Purpose);
        Assert.False(h.P("a").Has(Msg.Resync));

        h.Barrier("a", "stag0101", tick: 50);
        Assert.False(i.Sealing);
        h.Barrier("b", "stag0101", tick: 50);
        Assert.Equal((BarrierDecision.New, i.Id), (h.Decision("a").Decision, h.Decision("a").NewInst));
        Assert.Equal(0u, i.Log.Count); // the next visit's log
    }

    /// <summary>A barrier only one member reports (a desync, a forged BARRIER) must not hang the others.</summary>
    [Fact]
    public void A_barrier_that_only_some_members_reach_splits_them_off_and_the_others_play_on()
    {
        var h = SealedPair(out var inst, new RelayOptions { LagLeaveSeconds = 600 });
        var i = h.I("a");
        h.Seal(500);
        var stoppedAt = i.NextSeal;
        h.Barrier("a", "stag0102", tick: stoppedAt - 2);
        Assert.False(i.Sealing);
        h.Seal(2000);
        Assert.Equal(stoppedAt, i.NextSeal); // nothing sealed while the barrier waits for b
        h.Advance(3100);
        h.Hub.Housekeep(h.Now);
        var ok = h.Decision("a");
        Assert.Equal((BarrierDecision.New, "stag0102"), (ok.Decision, ok.Stage));
        Assert.NotEqual(inst, ok.NewInst);
        Assert.True(i.Sealing);
        Assert.Same(i, h.Mem("b").Instance);
        h.Seal(300);
        var ticks = h.P("b").Ticks().Where(t => t.Tick >= stoppedAt).ToList();
        Assert.NotEmpty(ticks);
        Assert.Contains(ticks, t => t.Rec.Events.Any(e => e.Type == Ev.Leave && e.Slot == 0));
        Assert.Contains(ticks, t => t.Rec.Events.Any(e => e.Type == Ev.LeadFrom));
        Assert.Equal(1, h.Hub.Counters["barrier_split"]);
    }

    [Fact]
    public void An_observer_falls_back_to_new_when_its_target_leaves_for_another_stage()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Joinable("a");
        h.Log("a", 0, 200);
        h.Barrier("b", "stag0101");
        Assert.Equal(BarrierDecision.Join, h.Decision("b").Decision);
        h.Barrier("a", "stag0102", tick: 199);
        var fallback = h.Decision("b");
        Assert.Equal((BarrierDecision.New, InstanceMode.SoloLed, "stag0101"), (fallback.Decision, fallback.Mode, fallback.Stage));
        Assert.NotEqual(h.Inst("a"), fallback.NewInst);
    }

    [Fact]
    public void Room_changes_reach_every_member()
    {
        var h = new Harness();
        var a = h.Connect("a", 0);
        var b = h.Connect("b", 1);
        Assert.Contains(a.All(Msg.Room, RoomMsg.Parse), r => r.Kind == RoomNotice.Connected && r.Member == 1);
        Assert.Equal(2, b.All(Msg.Room, RoomMsg.Parse).Count(r => r.Kind == RoomNotice.MemberJoined));
        h.Hub.RemoveMember(101, CloseReason.Kicked, "bye", h.Now);
        Assert.Equal(CloseReason.Kicked, b.Closed);
        Assert.Contains(a.All(Msg.Room, RoomMsg.Parse), r => r.Kind == RoomNotice.MemberLeft && r.Member == 1);
        h.Advance(1100);
        h.Hub.Housekeep(h.Now);
        Assert.True(a.Has(Msg.Ping));
        Assert.Single(a.Last(Msg.Roster, RosterMsg.Parse).Entries);
    }
}
