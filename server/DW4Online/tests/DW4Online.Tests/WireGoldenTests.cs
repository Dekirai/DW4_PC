using DW4Online.Domain.Rooms;
using DW4Online.Relay.Protocol;

namespace DW4Online.Tests;

/// <summary>
/// One DW4R2 layout for the relay and the game client: the same messages give the same bytes here and in the C++ client's
/// tests (tests/online_wire_tests.cpp, "golden" checks), and the same room settings give the same settings hash
/// (tests/online_api_tests.cpp). A change on one side fails both until the other follows (SEAMLESS.md 6.3 and its appendix).
/// </summary>
public class WireGoldenTests
{
    private static string Hex(ReadOnlySpan<byte> bytes) => Convert.ToHexString(bytes).ToLowerInvariant();

    private static string PayloadHex(byte[] frame) => Hex(frame.AsSpan(Wire.HeaderBytes));

    [Fact]
    public void Tick_record_bytes_equal_the_clients()
    {
        var t = new TickRecord { Mask = 0b1011 };
        t.Pads[0] = new Pad(0x4000, 10, -20, 0, 0);
        t.Pads[1] = new Pad(0x0008, -74, 74, 3, -3);
        t.Pads[3] = new Pad(0, 0, 0, 59, 0);
        t.Events.Add(TickEvent.Join(2, 5, [1, 2, 3, 4]));
        t.Events.Add(TickEvent.Leave(1));
        t.Events.Add(TickEvent.Intent(3, IntentKind.GiveUp, -77));
        t.Events.Add(TickEvent.LeadFrom(123456));
        t.Events.Add(TickEvent.Delay(0, 4));
        Assert.Equal("0b0500400aec00000800b64a03fd000000003b00" + "010600020501020304" + "02010001" + "0306000302b3ffffff" + "04040040e20100" + "0502000004",
            Hex(t.ToBytes()));
    }

    [Fact]
    public void Story_messages_equal_the_clients()
    {
        // tests/online_wire_tests.cpp "golden STORY" / "golden STORY_ACK" (SEAMLESS.md 8.3).
        var story = new DW4Online.Domain.Story.StoryDelta { Phase = 3, Flags = [19, 392], Folder = [1], Counters = [new(7, 4, 2, 1)] };
        Assert.Equal("07000000" + "02000000" + "0c000000" + "03" + "0200" + "1300" + "8801" + "01" + "01" + "01" + "07" + "04000000" + "02000000" + "01000000" +
                     "05000000" + "01",
            PayloadHex(new StoryMsg(7, 2, 12, story, 5, 1).Frame()));
        var back = StoryMsg.Parse(new StoryMsg(7, 2, 12, story).Frame().AsSpan(Wire.HeaderBytes));
        Assert.Equal((7u, 2u, 12u, (byte)3), (back.Inst, back.Visit, back.Revision, back.Delta.Phase));
        Assert.Equal([19, 392], back.Delta.Flags);
        Assert.Equal(new DW4Online.Domain.Story.CounterGrowth(7, 4, 2, 1), back.Delta.Counters.Single());
        var ack = new StoryAckMsg(13, [], StoryAckMsg.Answer,
            [new(DW4Online.Domain.Story.StoryKeyKind.Flag, 23, DW4Online.Domain.Story.StoryRefusal.NotStory),
             new(DW4Online.Domain.Story.StoryKeyKind.Phase, 5, DW4Online.Domain.Story.StoryRefusal.PhaseJump)], 5, 1);
        Assert.Equal("0d000000" + "00000000" + "01" + "0200" + "01" + "1700" + "07" + "00" + "0500" + "04" + "05000000" + "01", PayloadHex(ack.Frame()));
        Assert.Equal((5u, (byte)1), (StoryAckMsg.Parse(ack.Frame().AsSpan(Wire.HeaderBytes)).OwnerMember, StoryAckMsg.Parse(ack.Frame().AsSpan(Wire.HeaderBytes)).OwnerBox));
        var withStory = new StoryAckMsg(13, new DW4Online.Domain.Story.RoomStoryData().ToBlob(), 0, []).Frame();
        Assert.Equal(4 + 4 + 1100 + 1 + 2 + 5, withStory.Length - Wire.HeaderBytes);
        Assert.Throws<ProtocolException>(() => StoryAckMsg.Parse(new StoryAckMsg(1, new byte[1099], 0, []).Frame().AsSpan(Wire.HeaderBytes)));
        var tooMany = new StoryMsg(7, 2, 12, story).Frame()[Wire.HeaderBytes..];
        tooMany[13] = 0x01;
        tooMany[14] = 0x04;
        Assert.Throws<ProtocolException>(() => StoryMsg.Parse(tooMany));
    }

    [Fact]
    public void Message_bytes_equal_the_clients()
    {
        // tests/online_wire_tests.cpp "golden PRESENCE2" (round 60, SEAMLESS.md 12).
        var ghost = new Presence2Msg(1, new PresenceMsg("stagml11", 9, 1.5f, -2, 3, 0.25f, 4, 99, 500, 600, 70, 80), "runD",
            Presence2Msg.Loop | Presence2Msg.Busy, 7, 0x01020304, "w_a1");
        Assert.Equal("01" + "08737461676d6c3131" + "09000000" + "0000c03f" + "000000c0" + "00004040" + "0000803e" + "04" + "6300" + "f401" + "5802" +
            "4600" + "5000" + "0472756e44" + "03" + "07" + "04030201" + "04775f6131", PayloadHex(ghost.Frame()));
        Assert.Equal("070000000200000063000000010080010203" + "04", PayloadHex(new InputMsg(7, 2, 99, [new Pad(0x8000, 1, 2, 3, 4)]).Frame()));
        // NETSTAT (round 62): the member's link for its network indicator (tests/online_wire_tests.cpp "golden NETSTAT").
        Assert.Equal("1800" + "0600" + "03" + "feff" + "e8030000" + "05000000" + "07000000" + "01",
            PayloadHex(new NetStatMsg(24, 6, 3, -2, 1000, 5, 7, NetStatMsg.InStream).Frame()));
        Assert.Equal(new NetStatMsg(24, 6, 3, -2, 1000, 5, 7, 1), NetStatMsg.Parse(new NetStatMsg(24, 6, 3, -2, 1000, 5, 7, 1).Frame().AsSpan(Wire.HeaderBytes)));
        Assert.Equal(0x8F, new NetStatMsg(0, 0, 0, 0, 0, 0, 0, 0).Frame()[0]);
        Assert.Equal("0700000008737461673031303203cdab000000000000", PayloadHex(new HeadingMsg(7, "stag0102", 3, 0xABCD).Frame()));
        // Protocol 49: + travel token u32, plan id u16 and the member's own settings (11 B; the defaults: light 1).
        Assert.Equal("0700000002000000f401000008646e67313031623101030388776655443322110200000009" + "09" + "00000000" + "0000" +
                     "0000010000000000000000",
            PayloadHex(new BarrierMsg(7, 2, 500, "dng101b1", 1, 3, 3, 0x1122334455667788, [9, 9]).Frame()));
        Assert.Equal("07000000030000003d00000005050000000000000005", PayloadHex(new StateMsg(7, 3, 61, 5, 5, StateReason.Boss).Frame()));
        // Protocol 49: the member as u32; + wid u32 and the bound box u8.
        Assert.Equal("03000000" + "2900000001000000000000ffffffff001e0000" + "00000000" + "01",
            PayloadHex(new WelcomeMsg(3, 41, 1, 0, InstanceMode.Hub, "", WelcomeMsg.NoTick, 0, 30, 0).Frame()));
        // Protocol 49: no dropped blob; the world part (wid, host, visibility, arrival, settings, hash, owner key, story mode, redirect).
        Assert.Equal("0700000003000000" + "04" + "0800000001000000" + "087374616730313031" + "02" + "01" + "ffffffff" + "02" + "03" + "00" +
                     "0000000000000000" + "00" + "00" +
                     "00000000" + "00000000" + "00" + "00" + "0000010000000000000000" + "0000000000000000" + "00000000" + "01" + "00" + "00" + "00",
            PayloadHex(new BarrierOkMsg(7, 3, BarrierDecision.CoArrive, 8, 1, "stag0101", 2, 1, [-1, -1, -1, -1], InstanceMode.Sealed, 3, [], 0, []).Frame()));
        var blob = StateBlob.Build(StatePurpose.CoArrival, 1, [0xAA, 0xBB], [new ForkSide(800, 2, SplitKind.Exit, ForkSide.Leave)], 0,
            [new TickRecord { Mask = 0 }.ToBytes()]);
        Assert.Equal("02010002000000aabb0120030000020001000000000100000000" + "00", Hex(blob));
    }

    /// <summary>
    /// Every frame type has one number on both sides (SEAMLESS.md 6.3, "Message types"): a number given twice reached the
    /// review of round 62 (round 60's PRESENCE2 to clients on 0x8F, the number NETSTAT holds since DW4Online 0.1.2, both
    /// at protocol 45), and a client that cannot decode a frame of a known type drops the connection. A new type takes the
    /// next free number of its direction and goes into this list and into tests/online_wire_tests.cpp "type registry".
    /// </summary>
    [Fact]
    public void Message_types_are_unique_and_registered()
    {
        var types = typeof(Msg).GetFields(System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Static)
            .Where(f => f.IsLiteral && f.FieldType == typeof(byte))
            .ToDictionary(f => f.Name, f => (byte)f.GetRawConstantValue()!);
        var twice = types.GroupBy(x => x.Value).Where(g => g.Count() > 1).Select(g => $"0x{g.Key:X2}: {string.Join(", ", g.Select(x => x.Key))}");
        Assert.Empty(twice);
        var registry = new Dictionary<string, byte>
        {
            ["Input"] = 0x01, ["Log"] = 0x02, ["Heading"] = 0x03, ["Barrier"] = 0x04, ["BarrierChoice"] = 0x05, ["Split"] = 0x06,
            ["Snapshot"] = 0x07, ["SnapHash"] = 0x08, ["Ready"] = 0x09, ["JoinAt"] = 0x0A, ["NetFp"] = 0x0B, ["State"] = 0x0C,
            // Round I1 (protocol 49, SEAMLESS.md 19.18): 0x1A, 0x1C, 0x1D are I3's / I4's (GOTO, BROWSE, PARTY_OP).
            ["InstanceOp"] = 0x1B,
            // Round I1b: the arrival prompt (PLAN, CHOICE; OPTIONS below).
            ["Plan"] = 0x18, ["Choice"] = 0x19,
            // Round I1b, moderation (i1b-contract.md 16:25): *Report player*.
            ["Report"] = 0x1E,
            ["Intent"] = 0x0D, ["Story"] = 0x0E, ["Presence"] = 0x0F, ["Resume"] = 0x10, ["Rate"] = 0x11, ["Pong"] = 0x12, ["Bye"] = 0x13,
            // Round 60: PRESENCE2 to the relay.
            ["Presence2"] = 0x14,
            // Round J: the checkpoints (SEAMLESS.md 6.13.7).
            ["StateHash"] = 0x15, ["Checkpoint"] = 0x16, ["CheckpointBad"] = 0x17,
            ["Welcome"] = 0x80, ["Sealed"] = 0x81, ["Live"] = 0x82, ["JoinReq"] = 0x83, ["BarrierOk"] = 0x84, ["BarrierWait"] = 0x85,
            ["StateData"] = 0x86, ["PresenceOut"] = 0x87, ["Resync"] = 0x88, ["StoryAck"] = 0x89, ["Room"] = 0x8A, ["Ping"] = 0x8B,
            ["Close"] = 0x8C, ["Roster"] = 0x8D, ["Reassign"] = 0x8E,
            // Round 62, in the field since DW4Online 0.1.2 (sent to every member once a second): never another type's.
            ["NetStat"] = 0x8F,
            // Round 60: PRESENCE2 to the members that send it (first 0x8F: moved when round 62 merged).
            ["Presence2Out"] = 0x90,
            // Round J (0x91 stays free; 0x93 is round R's EVENT_AHEAD).
            ["CheckpointAsk"] = 0x92,
            // Round I1 (protocol 49): 0x94 INSTANCE_LIST and 0x98-0x9B are I1b's / I2's / I3's / I4's.
            ["InstanceInfo"] = 0x95, ["WorldNotice"] = 0x96, ["MemberInfo"] = 0x97,
            // Round I1b: the arrival prompt's rows.
            ["Options"] = 0x91,
        };
        Assert.Equal(registry.OrderBy(x => x.Value), types.OrderBy(x => x.Value));
        foreach (var (name, value) in types) Assert.NotEqual("0x" + value.ToString("X2"), Msg.Name(value));
    }

    private static ReadOnlySpan<byte> Body(byte[] frame) => frame.AsSpan(Wire.HeaderBytes);

    /// <summary>The messages and changes of round I1 (protocol 49): the layouts of i1a-contract.md ("client 12:40", "relay 13:05");
    /// tests/online_wire_tests.cpp holds the same bytes.</summary>
    [Fact]
    public void World_messages_equal_the_clients()
    {
        var settings = new RoomSettings(1, 1, 0, 4242, true, 1, 2, true);
        var info = new InstanceInfoMsg(7, 5, Visibility.Friends, settings, "dry_land", "",
            [new InstanceInfoMember(5, "stag0201", 23, InstanceInfoMember.Connected | InstanceInfoMember.Host),
             new InstanceInfoMember(9, "dng204b1", 12, InstanceInfoMember.Connected | InstanceInfoMember.SameGroup)], 2, 0, 0, 0, InstanceRole.Guest);
        Assert.Equal("07000000" + "05000000" + "01" + "0101000192100000010102" + "086472795f6c616e64" + "00" + "02" +
                     "05000000" + "087374616730323031" + "1700" + "09" + "09000000" + "08646e673230346231" + "0c00" + "11" +
                     "02" + "00" + "00" + "0000" + "01", PayloadHex(info.Frame()));
        var infoBack = InstanceInfoMsg.Parse(Body(info.Frame()));
        Assert.Equal((info.Wid, info.HostMember, info.Visibility, info.Settings, info.Area, info.Role), (infoBack.Wid, infoBack.HostMember,
            infoBack.Visibility, infoBack.Settings, infoBack.Area, infoBack.Role));
        Assert.Equal(info.Members, infoBack.Members);
        Assert.Equal("00000000" + "00000000" + "00" + "0000010000000000000000" + "00" + "00" + "00" + "00" + "00" + "00" + "0000" + "00",
            PayloadHex(InstanceInfoMsg.Home.Frame()));
        Assert.Equal("08" + "05000000" + "036b696d" + "01000000" + "00", PayloadHex(new WorldNoticeMsg(WorldNoticeKind.Ended, 5, "kim", 1, "").Frame()));
        Assert.Equal(new WorldNoticeMsg(WorldNoticeKind.WentTo, 5, "kim", 0, "dng101b1"),
            WorldNoticeMsg.Parse(Body(new WorldNoticeMsg(WorldNoticeKind.WentTo, 5, "kim", 0, "dng101b1").Frame())));
        Assert.Equal("09000000" + "036c6565" + "0c00" + "09", PayloadHex(new MemberInfoMsg(9, "lee", 12, Relation.Friend | Relation.SameInstance).Frame()));
        Assert.Equal("01" + "02000000", PayloadHex(new InstanceOpMsg(InstanceOpKind.Visibility, 2).Frame()));
        Assert.Throws<ProtocolException>(() => InstanceOpMsg.Parse(Body(new InstanceOpMsg(InstanceOpKind.Visibility, 3).Frame())));
        // Round I1b, moderation (i1b-contract.md 16:25): INSTANCE_OP remove, REPORT and its answer (WORLD_NOTICE 14); the same
        // bytes are in tests/online_wire_tests.cpp "moderation".
        Assert.Equal("02" + "09000000", PayloadHex(new InstanceOpMsg(InstanceOpKind.Remove, 9).Frame()));
        Assert.Equal("09000000" + "02" + "07000000", PayloadHex(new ReportMsg(9, 2, 7).Frame()));
        Assert.Equal(new ReportMsg(9, 4, 0), ReportMsg.Parse(Body(new ReportMsg(9, 4, 0).Frame())));
        Assert.Throws<ProtocolException>(() => ReportMsg.Parse(Body(new ReportMsg(9, 5, 0).Frame())));
        Assert.Throws<ProtocolException>(() => ReportMsg.Parse(Body(new ReportMsg(9, 0, 0).Frame())));
        Assert.Equal("0e" + "09000000" + "036c6565" + "01000000" + "00",
            PayloadHex(new WorldNoticeMsg(WorldNoticeKind.Report, 9, "lee", (uint)ReportResult.Limit, "").Frame()));
        Assert.Equal("07000000" + "02000000" + "01" + "03" + "02", PayloadHex(new JoinReqMsg(7, 2, 1, 3, JoinReqKind.Home).Frame()));
        Assert.Equal("07000000" + "02000000" + "01" + "03" + "01", PayloadHex(new JoinReqMsg(7, 2, 1, 3).Frame()));
        var home = new TickRecord { Mask = 0 };
        home.Events.Add(TickEvent.Home(0b0101, HomeReason.Ended));
        Assert.Equal("0001" + "0602000502", Hex(home.ToBytes()));
        Assert.Equal("07000000" + "02000000" + "03" + "00000000", PayloadHex(new IntentMsg(7, 2, IntentKind.Leave, 0).Frame()));
        var presence = new PresenceMsg("stagml11", 0, 1, 2, 3, 0, 4, 5, 6, 7, 8, 9);
        Assert.Equal("09000000" + "08" + "08737461676d6c3131", PayloadHex(new PresenceOutMsg(9, presence, Relation.SameInstance).Frame())[..28]);
        Assert.Equal(new PresenceOutMsg(9, presence, 8), PresenceOutMsg.Parse(Body(new PresenceOutMsg(9, presence, 8).Frame())));
        Assert.Equal("01000000" + "02000000" + "03" + "04000000" + "087374616730313031", PayloadHex(new ResumeMsg(1, 2, 3, 4, "stag0101").Frame()));
        // BARRIER_OK's world part with a guest's NEW (19.30.4): the host's story replaces the engine's, the host's settings.
        var world = new BarrierWorld(7, 5, Visibility.Private, ArrivalKind.GuestNew, settings, 0x0102030405060708, 5, 1, StoryMode.Replace, "stagml11", 3);
        var ok = new BarrierOkMsg(1, 2, BarrierDecision.New, 3, 1, "stag0201", 0, 0, [0, -1, -1, -1], InstanceMode.SoloLed, 0, [new CoArrival(1, 0x01020304, [])],
            9, []) { World = world };
        var okHex = PayloadHex(ok.Frame());
        Assert.EndsWith("07000000" + "05000000" + "02" + "04" + "0101000192100000010102" + "0807060504030201" + "05000000" + "01" + "01" +
                        "08737461676d6c3131" + "03", okHex);
        Assert.Contains("01" + "01" + "04030201" + "00000000", okHex); // (one co-arrival: slot 1, member 0x01020304, an empty carry)
        Assert.Equal(world, BarrierOkMsg.Parse(Body(ok.Frame())).World);
        Assert.Equal(0x01020304u, BarrierOkMsg.Parse(Body(ok.Frame())).CoArrivals.Single().Member);
        var state = new StateMsg(1, 2, 3, StateMsg.Joinable | StateMsg.NoLivingMember, 5, StateReason.None);
        Assert.Equal(state, StateMsg.Parse(Body(state.Frame())));
        Assert.Equal(49, Wire.WorldProtocol);
    }

    [Fact]
    public void Settings_hash_equals_the_clients()
    {
        var s = new RoomSettings(1, 1, 0, 4242, true, 1, 2, true);
        Assert.Equal("DW4R2-settings/1;protocol=44;data=123;difficulty=1;camera=1;light=0;rand=4242,1,1,2;fast=1", s.Canonical(44, 123));
        Assert.Equal(0x54c0f0b064c906baUL, s.Hash(44, 123));
        Assert.Equal(0x3832ef80ddfb9134UL, RoomSettings.Default.Hash(44, 123));
    }
}
