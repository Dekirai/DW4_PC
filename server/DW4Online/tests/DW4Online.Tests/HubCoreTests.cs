using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;

namespace DW4Online.Tests;

/// <summary>The DW4R2 building blocks: wire codec, tick records, the tick log, sealing streams, the delay rule, slot rules.</summary>
public class HubCoreTests
{
    private static Pad P(int i) => new((ushort)(i * 16), (sbyte)(i % 70), (sbyte)(-(i % 70)), (sbyte)(i % 50), 0);

    [Fact]
    public void Every_message_round_trips()
    {
        byte[] Payload(byte[] frame)
        {
            Assert.Equal(frame.Length - Wire.HeaderBytes, (int)BitConverter.ToUInt32(frame, 1));
            return frame[Wire.HeaderBytes..];
        }
        var input = InputMsg.Parse(Payload(new InputMsg(7, 2, 100, [P(1), P(2)]).Frame()));
        Assert.Equal((7u, 2u, 100u), (input.Inst, input.Visit, input.FirstTick));
        Assert.Equal([P(1), P(2)], input.Pads);
        Assert.Equal([P(3)], LogMsg.Parse(Payload(new LogMsg(1, 1, 5, [P(3)]).Frame())).Pads);
        Assert.Equal(new HeadingMsg(3, "stag0101", 2, 0xABCDEF), HeadingMsg.Parse(Payload(new HeadingMsg(3, "stag0101", 2, 0xABCDEF).Frame())));
        var barrier = BarrierMsg.Parse(Payload(new BarrierMsg(3, 4, 999, "dng101b1", 1, 5, 7, 42, [1, 2, 3]).Frame()));
        Assert.Equal((3u, 4u, 999u, "dng101b1", (byte)1, (byte)5, (byte)7, 42ul), (barrier.Inst, barrier.Visit, barrier.Tick, barrier.Stage, barrier.Point,
            barrier.AliveMask, barrier.PresentMask, barrier.ContextHash));
        Assert.Equal([1, 2, 3], barrier.Carry);
        Assert.Equal(new SplitMsg(1, 2, 3, 4, SplitKind.InStage), SplitMsg.Parse(Payload(new SplitMsg(1, 2, 3, 4, SplitKind.InStage).Frame())));
        Assert.Equal(new SnapHashMsg(1, 2, 3), SnapHashMsg.Parse(Payload(new SnapHashMsg(1, 2, 3).Frame())));
        Assert.Equal(new JoinAtMsg(1, 2, 3), JoinAtMsg.Parse(Payload(new JoinAtMsg(1, 2, 3).Frame())));
        Assert.Equal(new NetFpMsg(1, 2, 3, 4), NetFpMsg.Parse(Payload(new NetFpMsg(1, 2, 3, 4).Frame())));
        Assert.Equal(new StateMsg(1, 2, 3, 7, 5, StateReason.Boss), StateMsg.Parse(Payload(new StateMsg(1, 2, 3, 7, 5, StateReason.Boss).Frame())));
        Assert.Equal(new IntentMsg(1, 2, IntentKind.GiveUp, -5), IntentMsg.Parse(Payload(new IntentMsg(1, 2, IntentKind.GiveUp, -5).Frame())));
        var presence = new PresenceMsg("stagML11", 9, 1.5f, -2, 3, 0.25f, 4, 99, 500, 600, 70, 80);
        Assert.Equal(presence, PresenceMsg.Parse(Payload(presence.Frame())));
        Assert.Equal(new PresenceOutMsg(3, presence), PresenceOutMsg.Parse(Payload(new PresenceOutMsg(3, presence).Frame())));
        Assert.Equal(new ResumeMsg(1, 2, 3, 4), ResumeMsg.Parse(Payload(new ResumeMsg(1, 2, 3, 4).Frame())));
        Assert.Equal(new PongMsg(5, 6), PongMsg.Parse(Payload(new PongMsg(5, 6).Frame())));
        Assert.Equal(new WelcomeMsg(1, 2, 3, 0, InstanceMode.Sealed, "stag0101", 99, 3, 30, 1),
            WelcomeMsg.Parse(Payload(new WelcomeMsg(1, 2, 3, 0, InstanceMode.Sealed, "stag0101", 99, 3, 30, 1).Frame())));
        Assert.Equal(new JoinReqMsg(1, 2, 3, 4), JoinReqMsg.Parse(Payload(new JoinReqMsg(1, 2, 3, 4).Frame())));
        var ok = new BarrierOkMsg(1, 2, BarrierDecision.New, 3, 4, "stag0102", 5, 1, [0, -1, 1, -1], InstanceMode.Sealed, 3,
            [new CoArrival(2, 7, [9, 9])], 0, [], [], BarrierNotice.Full, ["kim"]);
        var back = BarrierOkMsg.Parse(Payload(ok.Frame()));
        Assert.Equal((ok.Decision, ok.NewInst, ok.NewVisit, ok.Stage, ok.OwnSlot, ok.Mode, ok.Delay, ok.Notice), (back.Decision, back.NewInst, back.NewVisit,
            back.Stage, back.OwnSlot, back.Mode, back.Delay, back.Notice));
        Assert.Equal(ok.SlotMap, back.SlotMap);
        Assert.Equal((byte)2, back.CoArrivals.Single().Slot);
        Assert.Equal(["kim"], back.NoticeNames);
        var wait = BarrierWaitMsg.Parse(Payload(new BarrierWaitMsg(1, WaitReason.BossLock, 15, ["a", "b"], BarrierWaitMsg.ChoiceNeeded).Frame()));
        Assert.Equal((WaitReason.BossLock, (ushort)15, BarrierWaitMsg.ChoiceNeeded), (wait.Reason, wait.SecondsLeft, wait.Flags));
        Assert.Equal(["a", "b"], wait.Names);
        Assert.Equal(new ReassignMsg(1, 2, 3, 4, InstanceMode.SoloLed, 1), ReassignMsg.Parse(Payload(new ReassignMsg(1, 2, 3, 4, InstanceMode.SoloLed, 1).Frame())));
        Assert.Equal(new RoomMsg(RoomNotice.Lagging, 2, 77, "kim", 0), RoomMsg.Parse(Payload(new RoomMsg(RoomNotice.Lagging, 2, 77, "kim", 0).Frame())));
        Assert.Equal(new PingMsg(1, 2, -3, 4), PingMsg.Parse(Payload(new PingMsg(1, 2, -3, 4).Frame())));
        Assert.Equal(new CloseMsg(CloseReason.Kicked, "bye"), CloseMsg.Parse(Payload(new CloseMsg(CloseReason.Kicked, "bye").Frame())));
        var roster = RosterMsg.Parse(Payload(new RosterMsg([new RosterEntry(1, "stag0101", 2, 3, 4, 5, RosterEntry.Owner)]).Frame()));
        Assert.Equal(new RosterEntry(1, "stag0101", 2, 3, 4, 5, RosterEntry.Owner), roster.Entries.Single());
    }

    [Fact]
    public void Malformed_payloads_are_refused()
    {
        Assert.Throws<ProtocolException>(() => InputMsg.Parse([1, 2, 3]));
        Assert.Throws<ProtocolException>(() => InputMsg.Parse(new InputMsg(1, 1, 1, [P(1)]).Frame()[Wire.HeaderBytes..].Concat(new byte[] { 0 }).ToArray()));
        var badStick = new InputMsg(1, 1, 1, [new Pad(0, 100, 0, 0, 0)]).Frame()[Wire.HeaderBytes..];
        Assert.Throws<ProtocolException>(() => InputMsg.Parse(badStick)); // no GameCube stick gives 100
        var zero = new byte[13];
        Assert.Throws<ProtocolException>(() => InputMsg.Parse(zero)); // count 0
        var stage = new HeadingMsg(1, "stag0101", 0, 0).Frame()[Wire.HeaderBytes..];
        stage[5] = (byte)'/';
        Assert.Throws<ProtocolException>(() => HeadingMsg.Parse(stage));
        Assert.Throws<ProtocolException>(() => SplitMsg.Parse(new SplitMsg(1, 1, 1, 0x10, SplitKind.Exit).Frame()[Wire.HeaderBytes..]));
        Assert.Throws<ProtocolException>(() => SplitMsg.Parse(new SplitMsg(1, 1, 1, 0, SplitKind.Exit).Frame()[Wire.HeaderBytes..]));
        Assert.Throws<ProtocolException>(() => SnapshotMsg.Parse(new SnapshotMsg(1, 1, 4, 3, [1, 2]).Frame()[Wire.HeaderBytes..]));
        Assert.Throws<ProtocolException>(() => TickRecord.Parse([0x10, 0]));
    }

    [Fact]
    public void Tick_records_and_their_length()
    {
        var r = new TickRecord { Mask = 0b1010 };
        r.Pads[1] = P(1);
        r.Pads[3] = P(3);
        r.Events.Add(TickEvent.Join(2, 4, [1, 2, 3]));
        r.Events.Add(TickEvent.Delay(2, 3));
        r.Events.Add(TickEvent.LeadFrom(77));
        var bytes = r.ToBytes();
        Assert.Equal(r.EncodedSize, bytes.Length);
        Assert.Equal(bytes.Length, TickLog.RecordLength(bytes));
        Assert.Equal(2 + 2 * 6 + (3 + 5) + (3 + 2) + (3 + 4), bytes.Length);
        var back = TickRecord.Parse(bytes);
        Assert.Equal((byte)0b1010, back.Mask);
        Assert.Equal(P(3), back.Pads[3]);
        Assert.Equal(Ev.Join, back.Events[0].Type);
        Assert.Equal(2, back.Events[0].Slot);
        Assert.Equal(-1, back.Events[2].Slot);
    }

    [Fact]
    public void The_log_compresses_full_blocks_and_children_share_the_prefix()
    {
        var log = new TickLog();
        var records = new List<byte[]>();
        for (var t = 0; t < 1500; ++t)
        {
            var r = new TickRecord { Mask = 0b11 };
            r.Pads[0] = P(t / 10);
            r.Pads[1] = P(t / 13);
            if (t % 500 == 7) r.Events.Add(TickEvent.Leave(1));
            records.Add(r.ToBytes());
            log.Append(records[^1]);
        }
        Assert.Equal(1500u, log.Count);
        Assert.True(log.StoredBytes < log.RawBytes / 3, $"{log.StoredBytes} of {log.RawBytes}");
        Assert.Equal(records, log.Range(0, 1500).ToList());
        Assert.Equal(records[590..610], log.Range(590, 610).ToList());
        Assert.Equal(records[1499], log.At(1499));
        Assert.Null(log.At(1500));

        var child = new TickLog(log, 1000);
        child.Append(records[0]);
        Assert.Equal(1001u, child.Count);
        Assert.Equal(records[995..1000].Append(records[0]).ToList(), child.Range(995, 1001).ToList());
        Assert.Throws<InvalidOperationException>(() => child.TruncateTo(10));

        log.TruncateTo(650);
        Assert.Equal(650u, log.Count);
        Assert.Equal(records[640..650], log.Range(640, 700).ToList());
        log.Append(records[42]);
        Assert.Equal(records[42], log.At(650));
    }

    [Fact]
    public void A_late_pad_is_filled_with_the_last_pad_six_times_then_neutral()
    {
        var s = new SlotStream(1, 10, 3);
        Assert.Equal(SlotStream.Submit.Late, s.Offer(9, P(1), 10, -5)); // before the stream
        Assert.Equal(SlotStream.Submit.Accepted, s.Offer(10, P(1), 10, 30));
        Assert.Equal(SlotStream.Submit.Duplicate, s.Offer(10, P(9), 10, 30));
        Assert.Equal(SlotStream.Submit.TooEarly, s.Offer(10 + SlotStream.MaxAhead, P(1), 10, 30));
        Assert.Equal(P(1), s.Take(10, 0, out var filled));
        Assert.False(filled);
        var fills = Enumerable.Range(11, 8).Select(t => s.Take((uint)t, t, out _)).ToList();
        Assert.Equal(Enumerable.Repeat(P(1), 6).Concat([Pad.Neutral, Pad.Neutral]), fills);
        Assert.Equal(8, s.FillStreak);
        Assert.Equal(11, s.FillStreakSince);
        Assert.Equal(SlotStream.Submit.Late, s.Offer(15, P(2), 19, -60));
        Assert.Equal(1, s.Late - 1); // the one before the stream plus this one
        Assert.Equal(SlotStream.Submit.Accepted, s.Offer(19, P(4), 19, 10));
        Assert.Equal(P(4), s.Take(19, 0, out filled));
        Assert.False(filled);
        Assert.Equal(0, s.FillStreak);
        Assert.Equal(8, s.Filled);
        Assert.Equal(19u, s.Horizon);

        // A fork moves the pads ahead of the split into the child's stream.
        Assert.Equal(SlotStream.Submit.Accepted, s.Offer(21, P(5), 20, 20));
        var child = new SlotStream(1, 20, 3);
        s.MoveAhead(child, 20);
        Assert.Equal(P(5), child.Take(21, 0, out filled));
        Assert.False(filled);
    }

    [Fact]
    public void The_delay_rises_with_late_pads_and_falls_after_thirty_clean_seconds()
    {
        var d = new DelayController(3) { Max = 5 };
        for (var i = 0; i < 1000; ++i) d.Record(20, late: false);
        Assert.Null(d.Evaluate());
        for (var i = 0; i < 4; ++i) d.Record(-5, late: true); // 4 of 3,600 > 0.1 %
        Assert.Equal(4, d.Evaluate());
        for (var i = 0; i < 4; ++i) d.Record(-5, late: true);
        Assert.Equal(5, d.Evaluate());
        for (var i = 0; i < 4; ++i) d.Record(-5, late: true);
        Assert.Null(d.Evaluate()); // bound
        for (var i = 0; i < DelayController.LowerWindow; ++i) d.Record(40, late: false);
        Assert.Equal(4, d.Evaluate()); // the late pads left the 30 s window
        for (var i = 0; i < DelayController.LowerWindow; ++i) d.Record(i == 5 ? 10 : 40, late: false); // one margin below a tick, but above the 0.1 % quantile
        Assert.Equal(3, d.Evaluate());
        for (var i = 0; i < DelayController.LowerWindow; ++i) d.Record(i < 3 ? 10 : 40, late: false); // three: the 0.1 % quantile is 10 ms < one tick
        Assert.Null(d.Evaluate());
        var e = new DelayController(2);
        for (var i = 0; i < DelayController.LowerWindow - 1; ++i) e.Record(40, late: false);
        Assert.Null(e.Evaluate()); // not 30 s yet
        e.Record(40, late: false);
        Assert.Equal(1, e.Evaluate());
        for (var i = 0; i < DelayController.LowerWindow; ++i) e.Record(40, late: false);
        Assert.Null(e.Evaluate()); // the lower bound

        Assert.Equal(3, DelayController.FromRoundTrips([], 10));
        Assert.Equal(2, DelayController.FromRoundTrips([5, 6, 7], 10)); // ceil(8 / 16.68) + 1
        Assert.Equal(6, DelayController.FromRoundTrips(Enumerable.Repeat(60.0, 99).Append(70).ToList(), 10)); // ceil((60 + 10 + 1) / 16.68) + 1
        Assert.Equal(10, DelayController.FromRoundTrips([400], 10));
    }

    [Fact]
    public void Slot_rules()
    {
        // Preferred slots first, then the current one, then the lowest free; slot 0 is refilled at a stage load.
        Assert.Equal(new Dictionary<string, int> { ["a"] = 0, ["b"] = 2 }, HubRules.SlotMap([("a", 1, -1), ("b", 2, -1)]));
        Assert.Equal(new Dictionary<string, int> { ["a"] = 3, ["b"] = 0 }, HubRules.SlotMap([("a", 0, 3), ("b", 0, 1)]));
        Assert.Equal(new Dictionary<string, int> { ["a"] = 0, ["b"] = 1, ["c"] = 2 }, HubRules.SlotMap([("a", 0, 0), ("b", 0, 0), ("c", -1, -1)]));
        Assert.Equal(new Dictionary<string, int> { ["a"] = 0, ["b"] = 3, ["c"] = 2, ["d"] = 1 },
            HubRules.SlotMap([("a", 0, 0), ("b", 3, 3), ("c", 2, 2), ("d", -1, 2)]));
        Assert.Equal(0, HubRules.JoinSlot([true, true, false, true], 0));
        Assert.Equal(3, HubRules.JoinSlot([false, false, false, true], 2));
        Assert.Equal(-1, HubRules.JoinSlot([false, false, false, false], 1));
        Assert.Equal(30, HubRules.CatchUpSeconds(88_200, 3000), 0.5);
        Assert.True(double.IsPositiveInfinity(HubRules.CatchUpSeconds(1, 59)));
        Assert.True(StageNames.IsHub("stagML11") && StageNames.IsHub("stagml14") && StageNames.IsHub("stagML02") && StageNames.IsHub("stagIS12") && StageNames.IsHub(""));
        Assert.False(StageNames.IsHub("stagML15") || StageNames.IsHub("stag0101") || StageNames.IsHub("stagBS01") || StageNames.IsHub("dng101b1"));
    }
}
