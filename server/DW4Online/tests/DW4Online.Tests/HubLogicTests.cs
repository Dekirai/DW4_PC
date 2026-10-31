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
public partial class HubLogicTests
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

        public Harness(InstanceOptions? instances = null, RelayOptions? relay = null, Action<StateJob>? stateRunner = null)
        {
            Hub = new RoomHub(new RoomInfo("ROOM01", 43, 1, 2, 10), relay ?? new RelayOptions(), new SealingOptions(),
                instances ?? new InstanceOptions(), NullLogger.Instance, null, stateRunner);
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
        // The answer names the visit the member parked in (the game client matches it), the reused id the next visit.
        Assert.Equal((welcome.Inst, welcome.Visit, welcome.Inst, welcome.Visit + 1), (ok.Inst, ok.Visit, ok.NewInst, ok.NewVisit));
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

    private static Harness SealedPair(out uint inst, RelayOptions? relay = null, InstanceOptions? instances = null)
    {
        var h = new Harness(instances, relay);
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

    // Round S1 review ---------------------------------------------------------------------------------------------------

    /// <summary>
    /// A BARRIER naming an instance the server does not have the member in (a client whose own five-second timeout went
    /// on as NEW, or a forged one) takes the member out of its old instance: no zombie instances with a filled slot, no
    /// joiner sent to them, and the housekeeping never indexes a slot -1 (it stopped every room's pings).
    /// </summary>
    [Fact]
    public void A_barrier_for_an_unknown_instance_leaves_the_old_one_and_leaves_no_zombies()
    {
        var h = new Harness();
        h.Connect("a", 0);
        var b = h.Connect("b", 1);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        var first = h.I("a");
        for (var k = 0; k < 500; ++k) h.Send("a", new BarrierMsg(0, 77, 0, "stag0101", 0, 1, 1, 5, [1]).Frame());
        Assert.True(first.Ended);
        Assert.True(h.Hub.Instances.Count <= 2, h.Hub.Instances.Count.ToString()); // a's one instance and b's hub
        foreach (var i in h.Hub.Instances)
            foreach (var (slot, m) in i.Seats)
                Assert.Equal((i, slot), (m.Instance!, m.Slot));
        // The member drops without BYE: past the reconnect window the housekeeping ends its instance and keeps going.
        h.Hub.Disconnect(h.Mem("a"), h.P("a"), h.Now);
        var pings = b.All(Msg.Ping, PingMsg.Parse).Count;
        h.Advance(11_000);
        h.Hub.Housekeep(h.Now);
        h.Advance(1_100);
        h.Hub.Housekeep(h.Now);
        Assert.True(b.All(Msg.Ping, PingMsg.Parse).Count > pings);
        Assert.False(h.Hub.Counters.ContainsKey("faults"));
        Assert.Null(h.Mem("a").Instance);
    }

    /// <summary>The records stay inside their u8 event count and u16 event length (a broken record broke the whole log).</summary>
    [Fact]
    public void Carries_and_intents_keep_every_record_readable()
    {
        Assert.Throws<ProtocolException>(() => BarrierMsg.Parse(new BarrierMsg(1, 1, 0, "stag0101", 0, 1, 1, 5, new byte[65534]).Frame()[Wire.HeaderBytes..]));
        Assert.Equal(65533, BarrierMsg.Parse(new BarrierMsg(1, 1, 0, "stag0101", 0, 1, 1, 5, new byte[65533]).Frame()[Wire.HeaderBytes..]).Carry.Length);
        Assert.Throws<ProtocolException>(() => ReadyMsg.Parse(new ReadyMsg(1, 1, 0, 0, 0, new byte[65534]).Frame()[Wire.HeaderBytes..]));
        var full = new TickRecord { Mask = 1 };
        for (var k = 0; k < 256; ++k) full.Events.Add(TickEvent.Leave(0));
        Assert.Throws<InvalidOperationException>(() => full.ToBytes());
        var join = TickRecord.Parse(new TickRecord { Mask = 1, Events = { TickEvent.Join(1, 0, new byte[65533]) } }.ToBytes());
        Assert.Equal(65535, join.Events.Single().Payload.Length);

        var h = SealedPair(out var inst);
        var i = h.I("a");
        h.Seal(200);
        for (var k = 0; k < 300; ++k) h.Send("a", new IntentMsg(inst, i.Visit, IntentKind.GiveUp, k).Frame());
        h.Seal(500);
        var ticks = h.P("b").Ticks(); // every record parses
        Assert.Single(ticks.SelectMany(t => t.Rec.Events), e => e.Type == Ev.Intent);
        Assert.Equal(299, h.Hub.Counters["intent_dropped"]);
    }

    /// <summary>A solo member's LOG follows the game's pace from the visit's start; a reported rate has a ceiling.</summary>
    [Fact]
    public void A_solo_log_cannot_run_ahead_of_the_clock_and_a_reported_rate_is_capped()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 600); // within the lead
        Assert.Throws<ProtocolException>(() => h.Log("a", 600, 600));
        h.Advance(20_000); // 20 s: about 1,200 ticks more
        h.Log("a", 600, 1200);
        Assert.Equal(1800u, h.I("a").Log.Count);
        h.Send("a", new RateMsg(1_000_000).Frame());
        Assert.Equal(20000u, h.Mem("a").Rate);
    }

    /// <summary>After JOIN_AT only the solo leader's LOG fills the ticks before J (a joiner's would rewrite its history).</summary>
    [Fact]
    public void Only_the_solo_leader_logs_the_ticks_before_J()
    {
        var h = new Harness();
        h.Connect("a", 0);
        var b = h.Connect("b", 1);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 120);
        h.Joinable("a");
        h.Barrier("b", "stag0101");
        var ok = h.Decision("b");
        h.Send("b", new ReadyMsg(ok.NewInst, ok.NewVisit, 120, 0, 5000, [7]).Frame());
        var req = h.P("a").Last(Msg.JoinReq, JoinReqMsg.Parse);
        h.Send("a", new JoinAtMsg(req.Inst, req.Visit, 140).Frame());
        var inst = h.I("a");
        Assert.Same(inst, h.Mem("b").Instance);
        h.Send("b", new LogMsg(inst.Id, inst.Visit, 120, Enumerable.Range(0, 10).Select(_ => new Pad(0x40, 0, 0, 0, 0)).ToArray()).Frame());
        Assert.Equal(120u, inst.Log.Count);
        Assert.Equal(1, h.Hub.Counters["log_foreign"]);
        h.Log("a", 120, 20);
        Assert.Equal(140u, inst.Log.Count);
        Assert.Equal((byte)0b01, TickRecord.Parse(inst.Log.At(130)!).Mask);
    }

    /// <summary>RESUME only after a resumable WELCOME, once; one fingerprint vote per tick, one resync at a time.</summary>
    [Fact]
    public void Resume_and_resync_are_bounded()
    {
        var h = SealedPair(out var inst, instances: new InstanceOptions { MaxResumeTicks = 60 });
        var i = h.I("a");
        h.Seal(3000);
        // No reconnect: a RESUME is refused (it made the relay send the whole log again, any number of times).
        var before = h.P("b").Frames.Count;
        h.Send("b", new ResumeMsg(inst, i.Visit, 1, 0).Frame());
        Assert.Equal(before, h.P("b").Frames.Count);
        Assert.Equal(1, h.Hub.Counters["resume_refused"]);
        // A vote, then more reports of the same tick and another lost vote within the cooldown: one RESYNC.
        h.Send("a", new NetFpMsg(inst, i.Visit, 60, 1).Frame());
        h.Send("b", new NetFpMsg(inst, i.Visit, 60, 2).Frame());
        for (ulong k = 3; k < 23; ++k) h.Send("b", new NetFpMsg(inst, i.Visit, 60, k).Frame());
        h.Send("a", new NetFpMsg(inst, i.Visit, 120, 1).Frame());
        h.Send("b", new NetFpMsg(inst, i.Visit, 120, 9).Frame());
        Assert.Single(h.P("b").All(Msg.Resync, ResyncMsg.Parse));
        Assert.True(h.Mem("b").Resyncing);
        // A fingerprint of a tick not sealed yet is no report.
        h.Send("a", new NetFpMsg(inst, i.Visit, i.NextSeal + 600, 1).Frame());
        Assert.Equal(1, h.Hub.Counters["netfp_ahead"]);
        // The resync ends with READY (no JOIN): the member is one again.
        h.Send("b", new ReadyMsg(inst, i.Visit, 120, 1, 4000, []).Frame());
        Assert.False(h.Mem("b").Resyncing);
        Assert.Equal(1, h.Hub.Counters["resync_ready"]);
        Assert.Same(i, h.Mem("b").Instance);
        // A reconnect within the window may RESUME once, for the gap only.
        h.Hub.Disconnect(h.Mem("b"), h.P("b"), h.Now);
        h.Seal(500);
        var b2 = h.Connect("b", 1);
        Assert.Equal(WelcomeMsg.Resumable, b2.Last(Msg.Welcome, WelcomeMsg.Parse).Flags);
        h.Send("b", new ResumeMsg(inst, i.Visit, 1, 0).Frame()); // the whole log: refused
        Assert.Equal(2, h.Hub.Counters["resume_refused"]);
        h.Send("b", new ResumeMsg(inst, i.Visit, 1, i.NextSeal - 30).Frame()); // once per connection
        Assert.Equal(3, h.Hub.Counters["resume_refused"]);
    }

    private static Harness CoArrivedPair(out uint inst)
    {
        var h = new Harness(relay: new RelayOptions { LagLeaveSeconds = 600 });
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Send("a", new HeadingMsg(h.Inst("a"), "stag0201", 0, 5).Frame());
        h.Send("b", new HeadingMsg(h.Inst("b"), "stag0201", 0, 5).Frame());
        h.Barrier("a", "stag0201");
        h.Barrier("b", "stag0201");
        inst = h.Decision("a").NewInst;
        h.Snapshot("a"); // a's hash is in, b's not yet
        Assert.False(h.I("a").Sealing);
        return h;
    }

    /// <summary>
    /// A co-arrival that goes before tick 0 (or never sends its hash) was spawned by every engine in the load step: it
    /// leaves by LEAVE, the other leads alone (LEAD_FROM), and sealing starts without it.
    /// </summary>
    [Fact]
    public void A_co_arrival_gone_before_tick_zero_leaves_by_LEAVE_and_the_base_plays_on()
    {
        var h = CoArrivedPair(out _);
        var i = h.I("a");
        h.Hub.Disconnect(h.Mem("b"), h.P("b"), h.Now, graceful: true);
        Assert.True(i.Sealing);
        h.Seal(500);
        var ticks = h.P("a").Ticks();
        Assert.Contains(ticks, t => t.Rec.Events.Any(e => e.Type == Ev.Leave && e.Slot == 1));
        Assert.Contains(ticks, t => t.Rec.Events.Any(e => e.Type == Ev.LeadFrom));
        Assert.Equal(InstanceMode.SoloLed, i.Mode);

        // Lost without BYE: sealing starts at once, the reconnect window runs.
        var g = CoArrivedPair(out _);
        var gi = g.I("a");
        g.Hub.Disconnect(g.Mem("b"), g.P("b"), g.Now);
        Assert.True(gi.Sealing);
        Assert.Same(gi, g.Mem("b").Instance);

        // Connected but silent: after Instances:SnapHashSeconds it leaves.
        var s = CoArrivedPair(out _);
        var si = s.I("a");
        s.Advance(61_000);
        s.Hub.Housekeep(s.Now);
        Assert.Equal(1, s.Hub.Counters["snaphash_timeout"]);
        Assert.True(si.Sealing);
        Assert.Null(s.Mem("b").Instance);
        s.Seal(500);
        Assert.Contains(s.P("a").Ticks(), t => t.Rec.Events.Any(e => e.Type == Ev.Leave && e.Slot == 1));
    }

    /// <summary>A single member's SPLIT moves nobody; every member's (or both sides' after a wait) forks.</summary>
    [Fact]
    public void A_fork_needs_the_reports_of_its_members()
    {
        var h = SealedPair(out var inst);
        var i = h.I("a");
        h.Seal(500);
        h.Send("b", new SplitMsg(inst, i.Visit, i.NextSeal - 5, 0b01, SplitKind.Exit).Frame()); // b names a as the leaver
        Assert.Same(i, h.I("a"));
        h.Advance(2100);
        h.Hub.Housekeep(h.Now);
        Assert.Same(i, h.I("a"));
        Assert.Equal(1, h.Hub.Counters["split_unconfirmed"]);
        h.Send("a", new SplitMsg(inst, i.Visit, i.NextSeal + 5, 0b01, SplitKind.Exit).Frame()); // a tick not sealed yet
        Assert.Equal(1, h.Hub.Counters["split_out_of_range"]);
        var k = i.NextSeal - 3;
        h.Send("a", new SplitMsg(inst, i.Visit, k, 0b01, SplitKind.Exit).Frame());
        Assert.Same(i, h.I("a"));
        h.Send("b", new SplitMsg(inst, i.Visit, k, 0b01, SplitKind.Exit).Frame());
        Assert.NotSame(i, h.I("a"));
        Assert.Same(i, h.I("b"));

        // Three members, one silent: the two sides' reports fork after Instances:ForkConfirmMs.
        var t = new Harness();
        foreach (var (n, index) in new[] { ("a", 0), ("b", 1), ("c", 2) }) t.Connect(n, index);
        foreach (var n in new[] { "a", "b", "c" }) t.Send(n, new HeadingMsg(t.Inst(n), "stag0102", 0, 5).Frame());
        foreach (var n in new[] { "a", "b", "c" }) t.Barrier(n, "stag0102");
        t.Snapshot("a");
        t.Send("b", new SnapHashMsg(t.Inst("a"), t.I("a").Visit, 0xAA).Frame());
        t.Send("c", new SnapHashMsg(t.Inst("a"), t.I("a").Visit, 0xAA).Frame());
        var parent = t.I("a");
        t.Seal(300);
        var f = parent.NextSeal - 3;
        t.Send("a", new SplitMsg(parent.Id, parent.Visit, f, 0b100, SplitKind.Exit).Frame());
        t.Send("c", new SplitMsg(parent.Id, parent.Visit, f, 0b100, SplitKind.Exit).Frame());
        Assert.Same(parent, t.I("c"));
        t.Advance(2100);
        t.Hub.Housekeep(t.Now);
        Assert.NotSame(parent, t.I("c"));
        Assert.Equal(2, parent.Count);
    }

    /// <summary>A solo leader that never answers JOIN_REQ: after Instances:JoinRequestTries the joiner plays the stage as NEW.</summary>
    [Fact]
    public void A_join_request_nobody_answers_ends_in_new()
    {
        var h = new Harness();
        h.Connect("a", 0);
        var b = h.Connect("b", 1);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 120);
        h.Joinable("a");
        h.Barrier("b", "stag0101");
        var ok = h.Decision("b");
        h.Send("b", new ReadyMsg(ok.NewInst, ok.NewVisit, 120, 0, 5000, [7]).Frame());
        for (var k = 0; k < 4; ++k)
        {
            h.Advance(5100);
            h.Hub.Housekeep(h.Now);
        }
        Assert.Equal(3, h.P("a").All(Msg.JoinReq, JoinReqMsg.Parse).Count);
        var fallback = h.Decision("b");
        Assert.Equal((BarrierDecision.New, BarrierNotice.JoinFailed), (fallback.Decision, fallback.Notice));
        Assert.Null(h.Mem("b").Joining);
    }

    /// <summary>
    /// A state built outside the room lock (the relay's runner): nothing of the stream reaches the member before it,
    /// the stream continues from its end; a member that moved on meanwhile gets nothing.
    /// </summary>
    [Fact]
    public void A_state_built_outside_the_lock_keeps_the_stream_in_order()
    {
        var jobs = new List<StateJob>();
        var h = new Harness(relay: new RelayOptions { LagLeaveSeconds = 600 }, stateRunner: jobs.Add);
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Send("a", new HeadingMsg(h.Inst("a"), "stag0201", 0, 5).Frame());
        h.Send("b", new HeadingMsg(h.Inst("b"), "stag0201", 0, 5).Frame());
        h.Barrier("a", "stag0201");
        h.Barrier("b", "stag0201");
        var inst = h.Decision("a").NewInst;
        h.Snapshot("a");
        Assert.Single(jobs); // b's co-arrival state
        jobs[0].Build();
        h.Hub.FinishState(jobs[0]);
        h.Send("b", new SnapHashMsg(inst, h.I("a").Visit, 0xAA).Frame());
        var i = h.I("a");
        h.Seal(2000);
        h.Send("a", new NetFpMsg(inst, i.Visit, 60, 1).Frame());
        h.Send("b", new NetFpMsg(inst, i.Visit, 60, 2).Frame());
        Assert.Equal(2, jobs.Count);
        var resync = h.P("b").Frames.FindLastIndex(f => f.Type == Msg.Resync);
        h.Seal(500); // sealed meanwhile: not sent to b yet
        Assert.DoesNotContain(h.P("b").Frames.Skip(resync + 1), f => f.Type == Msg.Sealed);
        jobs[1].Build();
        h.Hub.FinishState(jobs[1]);
        var after = h.P("b").Frames.Skip(resync + 1).ToList();
        var firstSealed = after.FindIndex(f => f.Type == Msg.Sealed);
        Assert.True(after.FindIndex(f => f.Type == Msg.StateData) < firstSealed && firstSealed > 0);
        Assert.Equal(jobs[1].End, TicksMsg.Parse(Msg.Sealed, after[firstSealed].Payload).FirstTick);
        Assert.Equal(StatePurpose.Resync, StateBlob.Parse(h.P("b").StateBlob()).Purpose);
    }

    /// <summary>A fork while the leader's snapshot still uploads: the uploader's side completes it for both sides.</summary>
    [Fact]
    public void A_fork_child_shares_a_snapshot_still_uploading()
    {
        var h = new Harness();
        foreach (var (n, index) in new[] { ("a", 0), ("b", 1), ("c", 2) }) h.Connect(n, index);
        foreach (var n in new[] { "a", "b", "c" }) h.Send(n, new HeadingMsg(h.Inst(n), "stag0102", 0, 5).Frame());
        foreach (var n in new[] { "a", "b", "c" }) h.Barrier(n, "stag0102");
        var parent = h.I("a");
        var bytes = Enumerable.Range(0, 100).Select(x => (byte)x).ToArray();
        h.Send("a", new SnapshotMsg(parent.Id, parent.Visit, 100, 0, bytes[..50]).Frame());
        foreach (var n in new[] { "a", "b", "c" }) h.Send(n, new SnapHashMsg(parent.Id, parent.Visit, 0xAA).Frame());
        h.Seal(300);
        var f = parent.NextSeal - 3;
        foreach (var n in new[] { "a", "b", "c" }) h.Send(n, new SplitMsg(parent.Id, parent.Visit, f, 0b001, SplitKind.Exit).Frame());
        var child = h.I("a");
        Assert.NotSame(parent, child);
        h.Send("a", new SnapshotMsg(parent.Id, parent.Visit, 100, 50, bytes[50..]).Frame()); // still names the parent's id
        Assert.True(child.Snapshot.Complete);
        Assert.True(parent.Snapshot.Complete);
    }

    /// <summary>
    /// Round 56 verification (A1 on a loaded machine): both members of a sealed pair lagged at once, both were taken out,
    /// the instance ended before either got the ticks up to its LEAVE, and both clients waited for good. When every
    /// connected member lags nobody is taken out; once one plays on, the other is.
    /// </summary>
    [Fact]
    public void Nobody_is_taken_out_for_lag_while_everyone_lags_one_who_plays_on_is_the_measure()
    {
        var h = SealedPair(out var inst, new RelayOptions { LagLeaveSeconds = 10 });
        var i = h.I("a");
        h.Seal(12_000); // neither sends pads
        h.Hub.Housekeep(h.Now);
        Assert.Same(i, h.Mem("a").Instance);
        Assert.Same(i, h.Mem("b").Instance);
        Assert.DoesNotContain(h.P("a").Ticks(), t => t.Rec.Events.Any(e => e.Type == Ev.Leave));
        // a plays on: its pads arrive again; b still lags and leaves.
        for (var k = 0; k < 60; ++k)
        {
            h.Send("a", new InputMsg(inst, i.Visit, i.NextSeal + 3, [new Pad(1, 0, 0, 0, 0), new Pad(1, 0, 0, 0, 0)]).Frame());
            h.Seal(17);
        }
        h.Hub.Housekeep(h.Now);
        Assert.Null(h.Mem("b").Instance);
        Assert.Same(i, h.Mem("a").Instance);
        h.Seal(300);
        var leave = h.P("a").Ticks().First(t => t.Rec.Events.Any(e => e.Type == Ev.Leave));
        Assert.Equal(1, leave.Rec.Events.First(e => e.Type == Ev.Leave).Slot);
    }
}
