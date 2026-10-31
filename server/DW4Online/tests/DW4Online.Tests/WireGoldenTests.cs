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
        Assert.Equal("07000000" + "02000000" + "0c000000" + "03" + "0200" + "1300" + "8801" + "01" + "01" + "01" + "07" + "04000000" + "02000000" + "01000000",
            PayloadHex(new StoryMsg(7, 2, 12, story).Frame()));
        var back = StoryMsg.Parse(new StoryMsg(7, 2, 12, story).Frame().AsSpan(Wire.HeaderBytes));
        Assert.Equal((7u, 2u, 12u, (byte)3), (back.Inst, back.Visit, back.Revision, back.Delta.Phase));
        Assert.Equal([19, 392], back.Delta.Flags);
        Assert.Equal(new DW4Online.Domain.Story.CounterGrowth(7, 4, 2, 1), back.Delta.Counters.Single());
        var ack = new StoryAckMsg(13, [], StoryAckMsg.Answer,
            [new(DW4Online.Domain.Story.StoryKeyKind.Flag, 23, DW4Online.Domain.Story.StoryRefusal.NotStory),
             new(DW4Online.Domain.Story.StoryKeyKind.Phase, 5, DW4Online.Domain.Story.StoryRefusal.PhaseJump)]);
        Assert.Equal("0d000000" + "00000000" + "01" + "0200" + "01" + "1700" + "07" + "00" + "0500" + "04", PayloadHex(ack.Frame()));
        var withStory = new StoryAckMsg(13, new DW4Online.Domain.Story.RoomStoryData().ToBlob(), 0, []).Frame();
        Assert.Equal(4 + 4 + 1100 + 1 + 2, withStory.Length - Wire.HeaderBytes);
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
        Assert.Equal("0700000008737461673031303203cdab000000000000", PayloadHex(new HeadingMsg(7, "stag0102", 3, 0xABCD).Frame()));
        Assert.Equal("0700000002000000f401000008646e67313031623101030388776655443322110200000009" + "09",
            PayloadHex(new BarrierMsg(7, 2, 500, "dng101b1", 1, 3, 3, 0x1122334455667788, [9, 9]).Frame()));
        Assert.Equal("07000000030000003d00000005050000000000000005", PayloadHex(new StateMsg(7, 3, 61, 5, 5, StateReason.Boss).Frame()));
        Assert.Equal("032900000001000000000000ffffffff001e0000",
            PayloadHex(new WelcomeMsg(3, 41, 1, 0, InstanceMode.Hub, "", WelcomeMsg.NoTick, 0, 30, 0).Frame()));
        Assert.Equal("0700000003000000" + "04" + "0800000001000000" + "087374616730313031" + "02" + "01" + "ffffffff" + "02" + "03" + "00" +
                     "000000000000000000000000" + "00" + "00",
            PayloadHex(new BarrierOkMsg(7, 3, BarrierDecision.CoArrive, 8, 1, "stag0101", 2, 1, [-1, -1, -1, -1], InstanceMode.Sealed, 3, [], 0, [], []).Frame()));
        var blob = StateBlob.Build(StatePurpose.CoArrival, 1, [0xAA, 0xBB], [new ForkSide(800, 2, SplitKind.Exit, ForkSide.Leave)], 0,
            [new TickRecord { Mask = 0 }.ToBytes()]);
        Assert.Equal("02010002000000aabb0120030000020001000000000100000000" + "00", Hex(blob));
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
