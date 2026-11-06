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

        public Harness(InstanceOptions? instances = null, RelayOptions? relay = null, Action<StateJob>? stateRunner = null,
            int protocol = Wire.CheckpointProtocol)
        {
            Hub = new RoomHub(new RoomInfo("ROOM01", protocol, 1, 2, 10), relay ?? new RelayOptions(), new SealingOptions(),
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

        /// <summary>A STATE (by default after the visit's load end, as every joinable report of a protocol 47 client).</summary>
        public void Joinable(string name, bool joinable = true, ulong context = 5, bool loaded = true)
        {
            var i = I(name);
            Send(name, new StateMsg(i.Id, i.Visit, 0,
                (byte)((joinable ? StateMsg.Joinable | StateMsg.MembershipSafe : 0) | (loaded ? StateMsg.LoadEnded : 0)), context,
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

    private static Harness SealedPair(out uint inst, RelayOptions? relay = null, InstanceOptions? instances = null,
        int protocol = Wire.CheckpointProtocol)
    {
        var h = new Harness(instances, relay, protocol: protocol);
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
    public void A_later_arrival_joins_a_dungeon_whose_own_load_changed_the_context_but_not_one_whose_play_did()
    {
        // dng101b1's start.lua sets COUNTER 2 at its load (a context value): the running instance reports another context
        // than the one its members arrived with, while a partner's barrier (taken in the previous stage) still has the old
        // one. Round 63's M2 saw the partner 15 s later get OtherContext and an instance of its own.
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Connect("c", 2);
        h.Barrier("a", "dng101b1", context: 1);
        h.Snapshot("a");
        h.Joinable("a", joinable: false, context: 1, loaded: false);
        h.Joinable("a", joinable: false, context: 7, loaded: false); // the load's COUNTERset(2, 101) (start.lua, before the load end)
        h.Joinable("a", joinable: false, context: 7); // the load end: the entry context
        h.Joinable("a", context: 7);
        h.Barrier("b", "dng101b1", context: 1);
        Assert.Equal(BarrierDecision.Join, h.Decision("b").Decision);
        Assert.Equal(h.I("a").Id, h.Inst("b"));
        // A change made by the play after the entry (a quest step) is the instance's own: an arrival with the old context
        // no longer fits, one with the new context does.
        h.Joinable("a", context: 8);
        h.Barrier("c", "dng101b1", context: 1);
        Assert.Equal((BarrierDecision.New, BarrierNotice.OtherContext), (h.Decision("c").Decision, h.Decision("c").Notice));
    }

    [Fact]
    public void Arrival_and_entry_context_follow_a_reused_instance_and_a_fork()
    {
        var x = new HubInstance(1) { ContextHash = 7, ArrivalContext = 1, EntryContext = 7 };
        Assert.True(HubRules.ContextMatches(x, 1));
        Assert.True(HubRules.ContextMatches(x, 7));
        Assert.False(HubRules.ContextMatches(x, 2));
        x.ContextHash = 8;
        Assert.False(HubRules.ContextMatches(x, 1));
        Assert.True(HubRules.ContextMatches(x, 8));
        // Before the load end only the running context counts (review of round J): what changed it is not known yet.
        var loading = new HubInstance(2) { ContextHash = 7, ArrivalContext = 1 };
        Assert.False(HubRules.ContextMatches(loading, 1));
        Assert.True(HubRules.ContextMatches(loading, 7));
    }

    // Review of round J (rev70's probes, kept as regression tests): the entry context is the load end's, not the first
    // joinable report's. A visit that runs from its entry into a boss is never joinable before the kill; the kill writes a
    // context value (dngq1b2e's ded_e_mammos.lua sets 384). Before, the kill's context became the entry and an arrival with
    // the old context joined after it (Join / None instead of New / OtherContext): progress it did not earn.

    [Fact]
    public void A_boss_kill_before_the_first_joinable_state_is_not_the_entry_context()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("a", "stagBS01", context: 5);
        h.Snapshot("a");
        var i = h.I("a");
        h.Send("a", new StateMsg(i.Id, i.Visit, 0, StateMsg.BossLock, 5, StateReason.Boss).Frame());
        h.Barrier("b", "stagBS01", context: 5);
        h.Send("b", new BarrierChoiceMsg(0, BarrierChoiceMsg.Wait).Frame());
        h.Send("a", new StateMsg(i.Id, i.Visit, 0, StateMsg.Joinable | StateMsg.MembershipSafe, 9, StateReason.None).Frame()); // the kill: 9
        var d = h.Decision("b");
        Assert.Equal((BarrierDecision.New, BarrierNotice.OtherContext), (d.Decision, d.Notice));
    }

    [Fact]
    public void A_late_arrival_after_a_boss_kill_that_preceded_the_first_joinable_state_gets_its_own_instance()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("c", 2);
        h.Barrier("a", "stagBS01", context: 5);
        h.Snapshot("a");
        var i = h.I("a");
        h.Send("a", new StateMsg(i.Id, i.Visit, 0, StateMsg.BossLock, 5, StateReason.Boss).Frame());
        h.Send("a", new StateMsg(i.Id, i.Visit, 0, StateMsg.Joinable | StateMsg.MembershipSafe, 9, StateReason.None).Frame());
        h.Barrier("c", "stagBS01", context: 5);
        var d = h.Decision("c");
        Assert.Equal((BarrierDecision.New, BarrierNotice.OtherContext), (d.Decision, d.Notice));
    }

    [Fact]
    public void The_load_end_report_fixes_the_entry_context_and_a_boss_kill_after_it_ends_the_old_contexts_joins()
    {
        // The protocol 47 client: its load end's STATE (boss-locked from the entry) names the entry context; the kill changes
        // the running context; a waiting arrival with the old context gets its own instance.
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("a", "stagBS01", context: 5);
        h.Snapshot("a");
        var i = h.I("a");
        h.Send("a", new StateMsg(i.Id, i.Visit, 0, StateMsg.BossLock | StateMsg.LoadEnded, 5, StateReason.Boss).Frame());
        Assert.Equal(5UL, i.EntryContext);
        h.Barrier("b", "stagBS01", context: 5);
        h.Send("b", new BarrierChoiceMsg(0, BarrierChoiceMsg.Wait).Frame());
        h.Send("a", new StateMsg(i.Id, i.Visit, 0, StateMsg.Joinable | StateMsg.MembershipSafe | StateMsg.LoadEnded, 9, StateReason.None).Frame());
        Assert.Equal(5UL, i.EntryContext);
        Assert.Equal((BarrierDecision.New, BarrierNotice.OtherContext), (h.Decision("b").Decision, h.Decision("b").Notice));
    }

    [Fact]
    public void A_held_join_whose_target_changes_its_context_meanwhile_is_decided_again()
    {
        // b waits for a's entry scene (Join hold); a's play changes the context before the scene ends: b may not join.
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("a", "stag0101", context: 5);
        h.Snapshot("a");
        h.Joinable("a", joinable: false, context: 5);
        h.Barrier("b", "stag0101", context: 5);
        Assert.Equal(WaitReason.NotJoinable, h.P("b").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason);
        h.Joinable("a", joinable: false, context: 6);
        Assert.Equal((BarrierDecision.New, BarrierNotice.OtherContext), (h.Decision("b").Decision, h.Decision("b").Notice));
        Assert.Equal(1, h.Hub.Counters["hold_context_changed"]);
    }

    // ---- Round J: joins from a checkpoint (SEAMLESS.md 6.13.4-6.13.5)

    /// <summary>A solo leader in stag0101 that reached its load end (K = 20) and reported its STATEHASH of tick 600.</summary>
    private static Harness CheckpointTarget(InstanceOptions? options = null, int protocol = Wire.CheckpointProtocol)
    {
        var h = new Harness(options, protocol: protocol);
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Advance(12000); // (a LOG never runs ahead of the game's pace)
        h.Log("a", 0, 700);
        h.Joinable("a");
        var i = h.I("a");
        h.Send("a", new StateHashMsg(i.Id, i.Visit, 600, 0xC0FFEE, 20, 1).Frame());
        return h;
    }

    private static void Upload(Harness h, string donor, uint tick, ulong hash, byte[] bytes, int chunk = 7)
    {
        var i = h.I(donor);
        for (var off = 0; off < bytes.Length; off += chunk)
            h.Send(donor, new CheckpointMsg(i.Id, i.Visit, tick, hash, 20, (uint)bytes.Length, (uint)off,
                bytes.AsSpan(off, Math.Min(chunk, bytes.Length - off)).ToArray()).Frame());
    }

    /// <summary>The joiner's READY from a's checkpoint of 650 that the relay takes at once: in a protocol 47 room at 690 (off
    /// the 60-tick raster: unchecked), in a 48 room at the check tick 664 with a's report of it first (equal). Its tick.</summary>
    private static uint ReadyFrom650(Harness h, string joiner, BarrierOkMsg ok, int protocol, ulong fp = 0x77)
    {
        if (protocol < Wire.ReadyCheckProtocol)
        {
            h.Send(joiner, new ReadyMsg(ok.NewInst, ok.NewVisit, 690, fp, 0, [7], ReadyMsg.FromCheckpoint).Frame());
            return 690;
        }
        var i = h.I("a");
        h.Send("a", new NetFpMsg(i.Id, i.Visit, 664, fp).Frame());
        h.Send(joiner, new ReadyMsg(ok.NewInst, ok.NewVisit, 664, fp, 0, [7], ReadyMsg.FromCheckpoint).Frame());
        return 664;
    }

    [Theory]
    [InlineData(Wire.CheckpointProtocol)]
    [InlineData(Wire.ReadyCheckProtocol)]
    public void A_join_asks_the_donor_and_gets_the_load_ticks_the_checkpoint_and_the_ticks_from_it(int protocol)
    {
        var h = CheckpointTarget(protocol: protocol);
        var i = h.I("a");
        Assert.Equal(20u, i.AgreedLoadEnd);
        h.Barrier("b", "stag0101");
        Assert.Equal(BarrierDecision.Join, h.Decision("b").Decision);
        // No state yet: the relay asked the donor for its next state-safe tick.
        Assert.False(h.P("b").Has(Msg.StateData));
        var ask = h.P("a").Last(Msg.CheckpointAsk, CheckpointAskMsg.Parse);
        Assert.Equal((i.Id, i.Visit, 0u), (ask.Inst, ask.Visit, ask.Tick));
        var cp = Enumerable.Range(0, 40).Select(x => (byte)(x * 3)).ToArray();
        Upload(h, "a", 650, 0xBEEF, cp);
        var blob = StateBlob.Parse(h.P("b").StateBlob());
        Assert.Equal((StatePurpose.Join, (byte)1), (blob.Purpose, blob.Slot));
        Assert.Equal(20, blob.LoadTicks.Count);
        Assert.Equal((650u, 0xBEEFUL), (blob.CheckpointTick, blob.CheckpointHash));
        Assert.Equal(cp, blob.Checkpoint);
        Assert.Equal((650u, 50), (blob.FirstTick, blob.Ticks.Count)); // the log's 700 ticks from 650
        Assert.Equal(1, h.Hub.Counters["checkpoint_joins"]);
        // READY from the checkpoint: the join goes on as always; the rebuild is noted for the suspicion rule.
        var ok = h.Decision("b");
        var ready = ReadyFrom650(h, "b", ok, protocol);
        Assert.True(h.P("a").Has(Msg.JoinReq));
        Assert.Equal(ready, i.RebuildTick);
    }

    [Theory]
    [InlineData(Wire.CheckpointProtocol)]
    [InlineData(Wire.ReadyCheckProtocol)]
    public void A_fresh_checkpoint_serves_at_once_and_a_bad_one_falls_back_to_the_classic_state(int protocol)
    {
        var h = CheckpointTarget(protocol: protocol);
        var i = h.I("a");
        i.CheckpointAskedAt = h.Now;
        i.AskedDonor = h.Mem("a"); // (an ask: only the asked donor's upload is taken)
        Upload(h, "a", 640, 0xBEEF, [1, 2, 3]);
        Assert.Single(i.Checkpoints);
        h.Barrier("b", "stag0101");
        Assert.False(h.P("a").Has(Msg.CheckpointAsk)); // fresh: no ask
        var blob = StateBlob.Parse(h.P("b").StateBlob());
        Assert.Equal(640u, blob.CheckpointTick);
        // The rebuild fails: the classic state follows (the snapshot and every tick).
        var ok = h.Decision("b");
        h.Send("b", new CheckpointBadMsg(ok.NewInst, ok.NewVisit, 640, CheckpointBadReason.Hash, "nodes").Frame());
        var classic = StateBlob.Parse(h.P("b").StateBlob());
        Assert.Null(classic.Checkpoint);
        Assert.Equal((0u, 700), (classic.FirstTick, classic.Ticks.Count));
        Assert.Equal(1, h.Hub.Counters["checkpoint_bad"]);
    }

    [Theory]
    [InlineData(Wire.CheckpointProtocol)]
    [InlineData(Wire.ReadyCheckProtocol)]
    public void Without_the_donors_checkpoint_in_time_the_join_takes_the_classic_state_and_the_switch_turns_the_path_off(int protocol)
    {
        var h = CheckpointTarget(new InstanceOptions { CheckpointWaitMs = 500 }, protocol);
        h.Barrier("b", "stag0101");
        Assert.True(h.P("a").Has(Msg.CheckpointAsk));
        Assert.False(h.P("b").Has(Msg.StateData));
        h.Advance(600);
        h.Hub.Housekeep(h.Now);
        var classic = StateBlob.Parse(h.P("b").StateBlob());
        Assert.Null(classic.Checkpoint);
        Assert.Equal(700, classic.Ticks.Count);

        var off = CheckpointTarget(new InstanceOptions { CheckpointJoins = false }, protocol);
        off.Barrier("b", "stag0101");
        Assert.False(off.P("a").Has(Msg.CheckpointAsk));
        Assert.Null(StateBlob.Parse(off.P("b").StateBlob()).Checkpoint);
    }

    [Theory]
    [InlineData(Wire.CheckpointProtocol)]
    [InlineData(Wire.ReadyCheckProtocol)]
    public void A_desync_soon_after_a_rebuild_makes_the_checkpoint_path_suspect(int protocol)
    {
        var h = CheckpointTarget(protocol: protocol);
        var i = h.I("a");
        h.Barrier("b", "stag0101");
        Upload(h, "a", 650, 0xBEEF, [9, 9, 9]);
        var ok = h.Decision("b");
        ReadyFrom650(h, "b", ok, protocol);
        var req = h.P("a").Last(Msg.JoinReq, JoinReqMsg.Parse);
        h.Send("a", new JoinAtMsg(req.Inst, req.Visit, 720).Frame());
        h.Log("a", 700, 20);
        h.Seal(2000);
        Assert.Equal(InstanceMode.Sealed, i.Mode);
        // (A multiple of 60: a protocol 48 room drops other NETFP ticks but a donor's check tick.)
        h.Send("a", new NetFpMsg(i.Id, i.Visit, 780, 1).Frame());
        h.Send("b", new NetFpMsg(i.Id, i.Visit, 780, 2).Frame());
        Assert.True(i.CheckpointSuspect);
        Assert.Equal(1, h.Hub.Counters["checkpoint_suspect"]);
    }

    [Theory]
    [InlineData(Wire.CheckpointProtocol)]
    [InlineData(Wire.ReadyCheckProtocol)]
    public void A_resync_starts_from_the_newest_agreed_checkpoint_and_its_ready_never_judges(int protocol)
    {
        var h = CheckpointTarget(new InstanceOptions { CheckpointSuspectTicks = 10 }, protocol);
        var i = h.I("a");
        h.Barrier("b", "stag0101");
        Upload(h, "a", 650, 0xBEEF, [9, 9, 9]);
        var ok = h.Decision("b");
        // (b's READY as a classic joiner's: a rebuilt member's own desync makes the path suspect, the next test.)
        h.Send("b", new ReadyMsg(ok.NewInst, ok.NewVisit, 690, 0, 0, [7]).Frame());
        var req = h.P("a").Last(Msg.JoinReq, JoinReqMsg.Parse);
        h.Send("a", new JoinAtMsg(req.Inst, req.Visit, 720).Frame());
        h.Log("a", 700, 20);
        h.Seal(4000);
        // Both report the same state at 780; the periodic upload of that tick becomes agreed.
        h.Send("a", new StateHashMsg(i.Id, i.Visit, 780, 0xABCD, 20, 1).Frame());
        h.Send("b", new StateHashMsg(i.Id, i.Visit, 780, 0xABCD, 20, 1).Frame());
        i.AskedDonor = h.Mem("a"); // (the periodic ask)
        Upload(h, "a", 780, 0xABCD, [4, 4, 4, 4]);
        Assert.True(i.Checkpoints.Last().Agreed);
        // b's engine goes off: its resync takes the checkpoint of 780.
        h.Send("a", new NetFpMsg(i.Id, i.Visit, 840, 1).Frame());
        h.Send("b", new NetFpMsg(i.Id, i.Visit, 840, 2).Frame());
        Assert.True(h.Mem("b").Resyncing);
        var blob = StateBlob.Parse(h.P("b").StateBlob());
        Assert.Equal((StatePurpose.Resync, 780u), (blob.Purpose, blob.CheckpointTick));
        Assert.False(i.CheckpointSuspect);
        h.Send("b", new ReadyMsg(i.Id, i.Visit, 840, 1, 0, [], ReadyMsg.FromCheckpoint).Frame());
        Assert.False(h.Mem("b").Resyncing);
        Assert.Empty(i.ReplayFp); // never a judge
        Assert.Equal(840u, i.RebuildTick);
        Assert.True(h.Mem("b").RebuiltFromCheckpoint);
    }

    // ---- Round J's review: the relay's checkpoint rules

    /// <summary>b joined a's stag0101 from a checkpoint (READY at 690 from it); a and b sealed from 720.</summary>
    private static Harness RebuiltPair(InstanceOptions options, int protocol = Wire.CheckpointProtocol)
    {
        var h = CheckpointTarget(options, protocol);
        h.Barrier("b", "stag0101");
        Upload(h, "a", 650, 0xBEEF, [9, 9, 9]);
        var ok = h.Decision("b");
        ReadyFrom650(h, "b", ok, protocol);
        var req = h.P("a").Last(Msg.JoinReq, JoinReqMsg.Parse);
        h.Send("a", new JoinAtMsg(req.Inst, req.Visit, 720).Frame());
        h.Log("a", 700, 20);
        h.Seal(4000);
        Assert.True(h.Mem("b").RebuiltFromCheckpoint);
        return h;
    }

    [Theory]
    [InlineData(Wire.CheckpointProtocol)]
    [InlineData(Wire.ReadyCheckProtocol)]
    public void A_desync_a_rebuilt_member_is_part_of_makes_the_path_suspect_at_any_time(int protocol)
    {
        // The suspicion window (CheckpointSuspectTicks) is long past at 900: before, nothing marked the path, and a field the
        // rebuild missed sent the member into a resync loop from the next agreed checkpoint until ResyncLimit took it out.
        var h = RebuiltPair(new InstanceOptions { CheckpointSuspectTicks = 10 }, protocol);
        var i = h.I("a");
        h.Send("a", new NetFpMsg(i.Id, i.Visit, 900, 1).Frame());
        h.Send("b", new NetFpMsg(i.Id, i.Visit, 900, 2).Frame());
        Assert.True(i.CheckpointSuspect);
        Assert.True(h.Mem("b").Resyncing); // the rebuilt member is the one resynced (it never judges)
        Assert.False(h.Mem("a").Resyncing);
        Assert.Null(StateBlob.Parse(h.P("b").StateBlob()).Checkpoint); // the classic way
    }

    [Theory]
    [InlineData(Wire.CheckpointProtocol)]
    [InlineData(Wire.ReadyCheckProtocol)]
    public void A_statehash_mismatch_makes_the_path_suspect_outside_the_window_too(int protocol)
    {
        var h = SealedPair(out _, protocol: protocol);
        var i = h.I("a");
        h.Send("a", new StateHashMsg(i.Id, i.Visit, 600, 7, 20, 1).Frame());
        h.Send("b", new StateHashMsg(i.Id, i.Visit, 600, 8, 20, 1).Frame());
        Assert.True(i.CheckpointSuspect);
    }

    [Theory]
    [InlineData(Wire.CheckpointProtocol)]
    [InlineData(Wire.ReadyCheckProtocol)]
    public void A_ready_from_a_checkpoint_waits_for_the_members_fingerprint_of_its_tick(int protocol)
    {
        foreach (var equal in new[] { true, false })
        {
            var h = CheckpointTarget(protocol: protocol);
            var i = h.I("a");
            h.Barrier("b", "stag0101");
            Upload(h, "a", 650, 0xBEEF, [9, 9, 9]);
            var ok = h.Decision("b");
            // The first fingerprint tick the joiner replayed (720); the leader has not reported it yet.
            h.Send("b", new ReadyMsg(ok.NewInst, ok.NewVisit, 720, 0x77, 0, [7], ReadyMsg.FromCheckpoint).Frame());
            Assert.False(h.P("a").Has(Msg.JoinReq));
            Assert.Equal(1, h.Hub.Counters["ready_deferred"]);
            var states = h.P("b").Frames.Count(f => f.Type == Msg.StateData);
            h.Send("a", new NetFpMsg(i.Id, i.Visit, 720, equal ? 0x77UL : 0x78UL).Frame());
            if (equal)
            {
                Assert.True(h.P("a").Has(Msg.JoinReq));
                Assert.False(i.CheckpointSuspect);
            }
            else
            {
                // The rebuild is off: the path is suspect, the joiner gets the classic state instead of joining.
                Assert.False(h.P("a").Has(Msg.JoinReq));
                Assert.True(i.CheckpointSuspect);
                Assert.True(h.P("b").Frames.Count(f => f.Type == Msg.StateData) > states);
            }
        }
        // No fingerprint within Instances:ReadyFingerprintWaitMs: taken unchecked.
        var late = CheckpointTarget(protocol: protocol);
        late.Barrier("b", "stag0101");
        Upload(late, "a", 650, 0xBEEF, [9, 9, 9]);
        var lateOk = late.Decision("b");
        late.Send("b", new ReadyMsg(lateOk.NewInst, lateOk.NewVisit, 720, 0x77, 0, [7], ReadyMsg.FromCheckpoint).Frame());
        late.Advance(2100);
        late.Hub.Housekeep(late.Now);
        Assert.True(late.P("a").Has(Msg.JoinReq));
        Assert.Equal(1, late.Hub.Counters["ready_unchecked"]);
    }

    // ---- Protocol 48: the READY check at the checkpoint's check tick (SEAMLESS.md 6.13.5)

    [Fact]
    public void A_ready_from_a_checkpoint_is_checked_at_its_check_tick_against_the_donors_fingerprint()
    {
        // (Before, the READY named the first multiple of 60 after the checkpoint and waited for it: up to a second more.)
        foreach (var (equal, readyFirst) in new[] { (true, true), (true, false), (false, true), (false, false) })
        {
            var h = CheckpointTarget(protocol: Wire.ReadyCheckProtocol);
            var i = h.I("a");
            h.Barrier("b", "stag0101");
            Upload(h, "a", 650, 0xBEEF, [9, 9, 9]);
            Assert.Equal(650u + Wire.ReadyCheckTicks - 1, i.Checkpoints.Last().CheckTick);
            var ok = h.Decision("b");
            var donorFp = equal ? 0x77UL : 0x78UL;
            if (!readyFirst) h.Send("a", new NetFpMsg(i.Id, i.Visit, 664, donorFp).Frame());
            var states = h.P("b").Frames.Count(f => f.Type == Msg.StateData);
            h.Send("b", new ReadyMsg(ok.NewInst, ok.NewVisit, 664, 0x77, 0, [7], ReadyMsg.FromCheckpoint).Frame());
            if (readyFirst)
            {
                // The joiner was quicker than the donor's report: the READY waits for it.
                Assert.False(h.P("a").Has(Msg.JoinReq));
                Assert.Equal(1, h.Hub.Counters["ready_deferred"]);
                h.Send("a", new NetFpMsg(i.Id, i.Visit, 664, donorFp).Frame());
            }
            Assert.Equal(1, h.Hub.Counters["check_fingerprints"]);
            Assert.Equal(donorFp, i.Checkpoints.Last().CheckFp);
            Assert.False(i.Fingerprints.ContainsKey(664)); // (it votes on nothing)
            if (equal)
            {
                Assert.True(h.P("a").Has(Msg.JoinReq));
                Assert.False(i.CheckpointSuspect);
                Assert.Equal(664u, i.RebuildTick);
            }
            else
            {
                // The rebuild is off: the path is suspect, the joiner gets the classic state instead of joining.
                Assert.False(h.P("a").Has(Msg.JoinReq));
                Assert.True(i.CheckpointSuspect);
                Assert.Equal(1, h.Hub.Counters["ready_mismatch"]);
                Assert.True(h.P("b").Frames.Count(f => f.Type == Msg.StateData) > states);
                Assert.Null(StateBlob.Parse(h.P("b").StateBlob()).Checkpoint);
            }
        }
    }

    [Fact]
    public void Only_the_donors_check_tick_is_taken_off_the_raster_and_a_later_multiple_of_60_is_checked_as_before()
    {
        var h = CheckpointTarget(protocol: Wire.ReadyCheckProtocol);
        var i = h.I("a");
        h.Barrier("b", "stag0101");
        Upload(h, "a", 650, 0xBEEF, [9, 9, 9]);
        var ok = h.Decision("b");
        // Another tick off the raster: no checkpoint's check tick, ignored.
        h.Send("a", new NetFpMsg(i.Id, i.Visit, 665, 0x55).Frame());
        Assert.Equal(1, h.Hub.Counters["netfp_offgrid"]);
        Assert.False(i.Fingerprints.ContainsKey(665));
        Assert.Null(i.Checkpoints.Last().CheckFp);
        // The joiner replayed past a multiple of 60 after the check tick: the members' NETFP of that tick decides.
        h.Send("a", new NetFpMsg(i.Id, i.Visit, 664, 0x77).Frame());
        h.Send("a", new NetFpMsg(i.Id, i.Visit, 720, 0x99).Frame());
        h.Send("b", new ReadyMsg(ok.NewInst, ok.NewVisit, 720, 0x98, 0, [7], ReadyMsg.FromCheckpoint).Frame());
        Assert.Equal(1, h.Hub.Counters["ready_mismatch"]);
        Assert.True(i.CheckpointSuspect);

        // The donor's report never comes (it forked or went in that step): the READY is taken unchecked after the wait.
        var late = CheckpointTarget(protocol: Wire.ReadyCheckProtocol);
        late.Barrier("b", "stag0101");
        Upload(late, "a", 650, 0xBEEF, [9, 9, 9]);
        var lateOk = late.Decision("b");
        late.Send("b", new ReadyMsg(lateOk.NewInst, lateOk.NewVisit, 664, 0x77, 0, [7], ReadyMsg.FromCheckpoint).Frame());
        Assert.False(late.P("a").Has(Msg.JoinReq));
        late.Advance(2100);
        late.Hub.Housekeep(late.Now);
        Assert.True(late.P("a").Has(Msg.JoinReq));
        Assert.Equal(1, late.Hub.Counters["ready_unchecked"]);
    }

    [Fact]
    public void In_a_protocol_47_room_a_ready_at_the_check_tick_is_not_awaited()
    {
        // (A 47 joiner names a multiple of 60 and a 47 donor sends no check fingerprint: the room keeps 47's rule.)
        var h = CheckpointTarget();
        var i = h.I("a");
        h.Barrier("b", "stag0101");
        Upload(h, "a", 650, 0xBEEF, [9, 9, 9]);
        var ok = h.Decision("b");
        h.Send("b", new ReadyMsg(ok.NewInst, ok.NewVisit, 664, 0x77, 0, [7], ReadyMsg.FromCheckpoint).Frame());
        Assert.True(h.P("a").Has(Msg.JoinReq));
        Assert.Null(i.Checkpoints.Last().CheckFp);
        Assert.False(h.Hub.Counters.ContainsKey("ready_deferred"));
    }

    [Fact]
    public void In_a_protocol_48_room_a_ready_from_a_checkpoint_off_its_check_tick_gets_the_classic_state()
    {
        // (Before its check tick: fewer than Wire.ReadyCheckTicks replayed steps; off the raster: nobody reports that tick.
        // The client never sends either; before lane jready's review the relay took them unchecked at once.)
        foreach (var tick in new uint[] { 660, 690 })
        {
            var h = CheckpointTarget(protocol: Wire.ReadyCheckProtocol);
            var i = h.I("a");
            h.Barrier("b", "stag0101");
            Upload(h, "a", 650, 0xBEEF, [9, 9, 9]);
            var ok = h.Decision("b");
            h.Send("a", new NetFpMsg(i.Id, i.Visit, 664, 0x77).Frame());
            h.Send("a", new NetFpMsg(i.Id, i.Visit, 660, 0x77).Frame());
            h.Send("b", new ReadyMsg(ok.NewInst, ok.NewVisit, tick, 0x77, 0, [7], ReadyMsg.FromCheckpoint).Frame());
            Assert.False(h.P("a").Has(Msg.JoinReq));
            Assert.Equal(1, h.Hub.Counters["ready_offgrid"]);
            Assert.Null(StateBlob.Parse(h.P("b").StateBlob()).Checkpoint); // the classic state
            Assert.False(i.CheckpointSuspect);
            // Its READY after the classic catch-up joins as always.
            h.Send("b", new ReadyMsg(ok.NewInst, ok.NewVisit, 660, 0x77, 0, [7]).Frame());
            Assert.True(h.P("a").Has(Msg.JoinReq));
        }
    }

    [Fact]
    public void A_ready_whose_checkpoint_newer_uploads_pushed_out_is_still_checked_at_its_check_tick()
    {
        // (Instances:KeepCheckpoints: the donor's report of the check tick reaches the joiner's checkpoint too; before lane
        // jready's review only the stored ones got it, and the joiner waited Instances:ReadyFingerprintWaitMs.)
        var h = CheckpointTarget(protocol: Wire.ReadyCheckProtocol);
        var i = h.I("a");
        h.Barrier("b", "stag0101");
        Upload(h, "a", 650, 0xBEEF, [9, 9, 9]);
        var ok = h.Decision("b");
        var used = h.Mem("b").Joining!.Checkpoint!;
        foreach (var tick in new uint[] { 655, 660 })
        {
            i.AskedDonor = h.Mem("a");
            i.CheckpointAskedAt = h.Now;
            Upload(h, "a", tick, 0xBEEF + tick, [1, 2]);
        }
        Assert.DoesNotContain(i.Checkpoints, c => ReferenceEquals(c, used));
        h.Send("a", new NetFpMsg(i.Id, i.Visit, 664, 0x77).Frame());
        Assert.Equal(0x77UL, used.CheckFp);
        h.Send("b", new ReadyMsg(ok.NewInst, ok.NewVisit, 664, 0x77, 0, [7], ReadyMsg.FromCheckpoint).Frame());
        Assert.True(h.P("a").Has(Msg.JoinReq));
        Assert.Equal(664u, i.RebuildTick);
        Assert.False(h.Hub.Counters.ContainsKey("ready_deferred"));
    }

    /// <summary>a and b sealed in stag0201 (protocol 48) with their load end agreed; c arrives, and the asked donor uploads its
    /// state of a tick 20 behind the seal (its check tick is sealed already).</summary>
    private static Harness SealedJoinFromCheckpoint(out string donor, out uint tick, out BarrierOkMsg ok, InstanceOptions? options = null)
    {
        var h = SealedPair(out _, instances: options, protocol: Wire.ReadyCheckProtocol);
        var i = h.I("a");
        h.Connect("c", 2);
        h.Seal(3000);
        h.Joinable("a");
        h.Joinable("b");
        h.Send("a", new StateHashMsg(i.Id, i.Visit, 60, 0xC0FFEE, 20, 0).Frame());
        h.Send("b", new StateHashMsg(i.Id, i.Visit, 60, 0xC0FFEE, 20, 0).Frame());
        Assert.Equal(20u, i.AgreedLoadEnd);
        h.Barrier("c", "stag0201");
        ok = h.Decision("c");
        Assert.Equal(BarrierDecision.Join, ok.Decision);
        donor = i.AskedDonor == h.Mem("a") ? "a" : "b";
        tick = i.NextSeal - 20;
        Upload(h, donor, tick, 0xBEEF, [9, 9, 9]);
        Assert.NotNull(StateBlob.Parse(h.P("c").StateBlob()).Checkpoint);
        return h;
    }

    [Fact]
    public void Only_the_donor_reports_off_the_raster_and_a_donor_that_forked_away_leaves_the_ready_unchecked()
    {
        var h = SealedJoinFromCheckpoint(out var donor, out var t, out var ok);
        var i = h.I("a");
        var other = donor == "a" ? "b" : "a";
        var check = t + Wire.ReadyCheckTicks - 1;
        h.Send(other, new NetFpMsg(i.Id, i.Visit, check, 0x55).Frame());
        Assert.Equal(1, h.Hub.Counters["netfp_offgrid"]);
        Assert.Null(h.Mem("c").Joining!.Checkpoint!.CheckFp);
        // The donor takes an exit (both report the fork) before its check tick: no report of it comes, nothing waits.
        var leaver = (byte)(1 << h.Mem(donor).Slot);
        h.Send(donor, new SplitMsg(i.Id, i.Visit, t + 3, leaver, SplitKind.Exit).Frame());
        h.Send(other, new SplitMsg(i.Id, i.Visit, t + 3, leaver, SplitKind.Exit).Frame());
        Assert.NotSame(i, h.I(donor));
        h.Send("c", new ReadyMsg(ok.NewInst, ok.NewVisit, check, 0x77, 0, [7], ReadyMsg.FromCheckpoint).Frame());
        Assert.Equal(1, h.Hub.Counters["ready_from_checkpoint"]);
        Assert.Equal(1, h.Hub.Counters["ready_without_reference"]);
        Assert.False(h.Hub.Counters.ContainsKey("ready_deferred"));
    }

    [Fact]
    public void A_fork_between_the_checkpoint_and_its_check_tick_holds_the_ready_until_it_is_settled()
    {
        // Lane jready's review: the donor leaves by a fork three ticks after its upload; until every member's SPLIT is in, its
        // reports still come under the instance's id, and its check fingerprint is its own side's (the stayers gone from its
        // engine). The joiner keeps the instance's side: compared with it, a healthy rebuild was a mismatch.
        foreach (var confirmed in new[] { true, false })
        {
            var h = SealedJoinFromCheckpoint(out var donor, out var t, out var ok, new InstanceOptions { ForkConfirmMs = 1000 });
            var i = h.I("a");
            var other = donor == "a" ? "b" : "a";
            var check = t + Wire.ReadyCheckTicks - 1;
            var leaver = (byte)(1 << h.Mem(donor).Slot);
            h.Send(donor, new SplitMsg(i.Id, i.Visit, t + 3, leaver, SplitKind.Exit).Frame());
            h.Send(donor, new NetFpMsg(i.Id, i.Visit, check, 0xA1).Frame());
            h.Send("c", new ReadyMsg(ok.NewInst, ok.NewVisit, check, 0xB2, 0, [7], ReadyMsg.FromCheckpoint).Frame());
            Assert.False(h.Hub.Counters.ContainsKey("ready_mismatch"));
            Assert.Equal(1, h.Hub.Counters["ready_deferred"]);
            Assert.False(h.Mem("c").Joining!.Ready);
            if (confirmed)
            {
                // The other member's SPLIT: forked; the donor's report is the other side's, the READY is taken at once.
                h.Send(other, new SplitMsg(i.Id, i.Visit, t + 3, leaver, SplitKind.Exit).Frame());
                Assert.NotSame(i, h.I(donor));
                Assert.Equal(1, h.Hub.Counters["ready_from_checkpoint"]);
                Assert.Equal(1, h.Hub.Counters["ready_without_reference"]);
                Assert.False(i.CheckpointSuspect);
            }
            else
            {
                // Nobody else reports it: turned down after Instances:ForkConfirmMs (the members' engines still agree), and
                // the READY is compared with the donor's report: here the joiner's rebuild is off.
                h.Advance(1100);
                h.Hub.Housekeep(h.Now);
                Assert.Equal(1, h.Hub.Counters["split_unconfirmed"]);
                Assert.Equal(1, h.Hub.Counters["ready_mismatch"]);
                Assert.True(i.CheckpointSuspect);
                Assert.Null(StateBlob.Parse(h.P("c").StateBlob()).Checkpoint);
            }
        }
    }

    [Theory]
    [InlineData(Wire.CheckpointProtocol)]
    [InlineData(Wire.ReadyCheckProtocol)]
    public void A_joiner_that_never_sends_ready_falls_back_to_its_own_stage(int protocol)
    {
        var h = CheckpointTarget(new InstanceOptions { MaxCatchUpSeconds = 5 }, protocol);
        h.Barrier("b", "stag0101");
        Upload(h, "a", 650, 0xBEEF, [9, 9, 9]);
        Assert.NotNull(h.Mem("b").Joining);
        h.Advance(30_000);
        h.Hub.Housekeep(h.Now);
        Assert.NotNull(h.Mem("b").Joining);
        h.Advance(6_000);
        h.Hub.Housekeep(h.Now);
        Assert.Null(h.Mem("b").Joining);
        Assert.Equal(1, h.Hub.Counters["join_ready_timeout"]);
        Assert.Equal((BarrierDecision.New, BarrierNotice.JoinFailed), (h.Decision("b").Decision, h.Decision("b").Notice));
    }

    [Theory]
    [InlineData(Wire.CheckpointProtocol)]
    [InlineData(Wire.ReadyCheckProtocol)]
    public void The_classic_fallback_of_a_planned_checkpoint_join_checks_the_whole_visits_catch_up(int protocol)
    {
        // A long visit (3,000 ticks): the ranking takes the checkpoint path's K + 1,200 ticks (about 5 s for this joiner); the
        // donor does not answer, and the whole visit would take about 12 s: NEW, not a long countdown.
        var h = CheckpointTarget(new InstanceOptions { MaxCatchUpSeconds = 10, CheckpointWaitMs = 500 }, protocol);
        h.Advance(40_000);
        for (var first = 700u; first < 3000u; first += 230) h.Log("a", first, 230);
        h.Mem("b").Rate = 300;
        h.Barrier("b", "stag0101");
        Assert.Equal(BarrierDecision.Join, h.Decision("b").Decision);
        h.Advance(600);
        h.Hub.Housekeep(h.Now);
        Assert.Equal(1, h.Hub.Counters["classic_too_long"]);
        Assert.Equal((BarrierDecision.New, BarrierNotice.TooLongToCatchUp), (h.Decision("b").Decision, h.Decision("b").Notice));
        Assert.False(h.P("b").Has(Msg.StateData));
    }

    [Theory]
    [InlineData(Wire.CheckpointProtocol)]
    [InlineData(Wire.ReadyCheckProtocol)]
    public void Only_the_asked_donors_upload_is_taken_and_an_abandoned_one_frees_the_periodic_ask(int protocol)
    {
        var h = CheckpointTarget(protocol: protocol);
        var i = h.I("a");
        Upload(h, "a", 640, 0xBEEF, [1, 2, 3]); // unasked
        Assert.Empty(i.Checkpoints);
        Assert.Null(i.Upload);
        Assert.True(h.Hub.Counters["checkpoint_unasked"] >= 1);
        // An asked upload that stops after its first chunk.
        i.AskedDonor = h.Mem("a");
        i.CheckpointAskedAt = h.Now;
        h.Send("a", new CheckpointMsg(i.Id, i.Visit, 640, 0xBEEF, 20, 10, 0, [1, 2, 3]).Frame());
        Assert.NotNull(i.Upload);
        h.Advance(4100);
        h.Hub.Housekeep(h.Now);
        Assert.Null(i.Upload);
        Assert.Equal(1, h.Hub.Counters["checkpoint_upload_abandoned"]);
    }

    [Fact]
    public void A_solo_leader_tells_its_load_end_alone_and_gets_no_periodic_asks()
    {
        var h = new Harness(new InstanceOptions { CheckpointPeriodicMs = 1000 });
        h.Connect("a", 0);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Joinable("a");
        var i = h.I("a");
        h.Send("a", new StateHashMsg(i.Id, i.Visit, 600, 0, 20, StateHashMsg.LoadEndOnly).Frame());
        Assert.Equal(20u, i.AgreedLoadEnd);
        Assert.Empty(i.StateHashes);
        h.Advance(5000);
        h.Hub.Housekeep(h.Now);
        Assert.False(h.P("a").Has(Msg.CheckpointAsk));
    }

    [Theory]
    [InlineData(Wire.CheckpointProtocol)]
    [InlineData(Wire.ReadyCheckProtocol)]
    public void A_fork_drops_the_parents_state_of_the_leavers_side_after_the_fork_tick(int protocol)
    {
        var h = SealedPair(out var inst, protocol: protocol);
        var i = h.I("a");
        h.Seal(600);
        var k = i.NextSeal - 3;
        Assert.True(k > 21);
        // a (the leaver) was asked and uploads the state of k + 1, the child's side, before the relay forks.
        i.AskedDonor = h.Mem("a");
        h.Send("a", new SplitMsg(inst, i.Visit, k, 0b01, SplitKind.Exit).Frame());
        h.Send("a", new CheckpointMsg(i.Id, i.Visit, k + 1, 0xA1, 20, 3, 0, [1, 2, 3]).Frame());
        h.Send("a", new StateHashMsg(i.Id, i.Visit, k + 1, 0xA1, 20, 0).Frame());
        h.Send("b", new StateHashMsg(i.Id, i.Visit, k + 1, 0xB2, 20, 0).Frame());
        Assert.False(h.Hub.Counters.ContainsKey("statehash_mismatch")); // not compared while the fork waits
        Assert.Single(i.Checkpoints);
        h.Send("b", new SplitMsg(inst, i.Visit, k, 0b01, SplitKind.Exit).Frame());
        Assert.NotSame(i, h.I("a"));
        Assert.Empty(i.Checkpoints);
        Assert.DoesNotContain(i.StateHashes.Keys, t => t > k);
        Assert.False(i.CheckpointSuspect);
        Assert.Empty(h.I("a").Checkpoints); // the child keeps only the parent's checkpoints up to the fork
    }

    [Fact]
    public void Statehash_reports_agree_on_the_load_end_and_messages_round_trip()
    {
        var sh = new StateHashMsg(3, 4, 1200, 0x0123456789ABCDEFUL, 17, 1);
        Assert.Equal(sh, StateHashMsg.Parse(sh.Frame()[Wire.HeaderBytes..]));
        // Golden bytes shared with tests/online_wire_tests.cpp.
        Assert.Equal("03000000" + "04000000" + "b0040000" + "efcdab8967452301" + "11000000" + "01", Convert.ToHexStringLower(sh.Frame()[Wire.HeaderBytes..]));
        var cp = new CheckpointMsg(3, 4, 1200, 5, 17, 10, 4, [1, 2, 3]);
        var back = CheckpointMsg.Parse(cp.Frame()[Wire.HeaderBytes..]);
        Assert.Equal((cp.Tick, cp.Total, cp.Offset), (back.Tick, back.Total, back.Offset));
        Assert.Equal(cp.Chunk, back.Chunk);
        Assert.Throws<ProtocolException>(() => CheckpointMsg.Parse(new CheckpointMsg(3, 4, 10, 5, 17, 2, 1, [1, 2]).Frame()[Wire.HeaderBytes..]));
        Assert.Throws<ProtocolException>(() => StateHashMsg.Parse(new StateHashMsg(3, 4, 10, 5, 11, 0).Frame()[Wire.HeaderBytes..]));
        var bad = new CheckpointBadMsg(1, 2, 3, CheckpointBadReason.Topology, "journal");
        Assert.Equal(bad, CheckpointBadMsg.Parse(bad.Frame()[Wire.HeaderBytes..]));
        var ask = new CheckpointAskMsg(1, 2, 0, CheckpointAskMsg.Periodic);
        Assert.Equal(ask, CheckpointAskMsg.Parse(ask.Frame()[Wire.HeaderBytes..]));
        var ready = new ReadyMsg(1, 2, 3, 4, 5, [6], ReadyMsg.FromCheckpoint);
        Assert.Equal(ReadyMsg.FromCheckpoint, ReadyMsg.Parse(ready.Frame()[Wire.HeaderBytes..]).Flags);
        Assert.Equal(0, ReadyMsg.Parse(new ReadyMsg(1, 2, 3, 4, 5, [6]).Frame()[Wire.HeaderBytes..]).Flags);
        // Two members disagree: no agreed load end yet; then they agree.
        var h = SealedPair(out _);
        var i = h.I("a");
        Assert.Same(i, h.I("b"));
        h.Send("a", new StateHashMsg(i.Id, i.Visit, 600, 7, 20, 1).Frame());
        Assert.Null(i.AgreedLoadEnd);
        h.Send("b", new StateHashMsg(i.Id, i.Visit, 600, 8, 20, 1).Frame());
        Assert.Null(i.AgreedLoadEnd);
        Assert.Equal(1, h.Hub.Counters["statehash_mismatch"]);
        Assert.True(i.CheckpointSuspect); // (round J's review: any mismatch)
        h.Send("a", new StateHashMsg(i.Id, i.Visit, 1200, 9, 20, 1).Frame());
        h.Send("b", new StateHashMsg(i.Id, i.Visit, 1200, 9, 20, 1).Frame());
        Assert.Equal(20u, i.AgreedLoadEnd);
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
    public void World_members_are_not_announced_to_everyone_and_a_staff_lock_closes_the_member()
    {
        // The world (19.4): no room roster, no ROOM notices; a member hears of another only through its own instance, a figure or
        // a friend (MEMBER_INFO before the first mention). A staff lock (RemoveMember) closes the connection; pings go on.
        var h = new Harness();
        var a = h.Connect("a", 0);
        var b = h.Connect("b", 1);
        Assert.False(a.Has(Msg.Room) || a.Has(Msg.Roster) || a.Has(Msg.MemberInfo));
        Assert.Equal(InstanceInfoMsg.Home.Frame()[Wire.HeaderBytes..], a.Frames.Last(f => f.Type == Msg.InstanceInfo).Payload);
        h.Hub.RemoveMember(h.Mem("b").AccountId, CloseReason.Account, "bye", h.Now);
        Assert.Equal(CloseReason.Account, b.Closed);
        Assert.Null(h.Hub.MemberOf(h.Mem("b").AccountId));
        h.Advance(1100);
        h.Hub.Housekeep(h.Now);
        Assert.True(a.Has(Msg.Ping));
        Assert.False(a.Has(Msg.Roster));
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
        h.Advance(61_000); // (a hosts its instance: its grace of 60 s, 19.6.7)
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
        Assert.Equal(WelcomeMsg.Resumable | WelcomeMsg.PresenceV2 | WelcomeMsg.Checkpoints | WelcomeMsg.World, b2.Last(Msg.Welcome, WelcomeMsg.Parse).Flags);
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
