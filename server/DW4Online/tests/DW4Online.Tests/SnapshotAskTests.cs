using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// Round I1b's integration, W7e (the prompt lane's runs r11 / r13): a sealed NEW names the member in its lowest slot the uploader of
/// the visit's load snapshot. When that member's link is cut before its BARRIER_OK reached it, nobody ever uploaded the snapshot and
/// every JOIN of that stage group waited until HoldExpired. Now the relay asks the connected member in the lowest slot
/// (SNAPSHOT_ASK, protocol 50) and takes its upload; the old uploader's chunks are dropped, not a protocol error.
/// </summary>
public partial class HubLogicTests
{
    /// <summary>a and b arrive at stag0201 together (a sealed NEW, a the uploader); a's link is cut before it uploads.</summary>
    private static Harness DroppedUploader(out HubInstance i)
    {
        var h = new Harness(world: new WorldHubOptions { ArrivalPrompt = false });
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Send("a", new HeadingMsg(h.Inst("a"), "stag0201", 0, 5).Frame());
        h.Send("b", new HeadingMsg(h.Inst("b"), "stag0201", 0, 5).Frame());
        h.Barrier("a", "stag0201");
        h.Barrier("b", "stag0201");
        Assert.Equal(BarrierDecision.CoArrive, h.Decision("b").Decision);
        i = h.I("b");
        Assert.Same(h.Mem("a"), i.Snapshot.Uploader);
        h.Hub.Disconnect(h.Mem("a"), h.P("a"), h.Now);
        return h;
    }

    [Fact]
    public void W7e_a_NEWs_snapshot_uploader_that_drops_before_its_upload_is_replaced_by_the_next_member()
    {
        var h = DroppedUploader(out var i);
        // (Not at once: a short reconnect at the stage load keeps its upload.)
        h.Housekeep();
        Assert.False(h.P("b").Has(Msg.SnapshotAsk));
        h.Advance(2_500);
        h.Housekeep();
        var ask = h.P("b").Last(Msg.SnapshotAsk, SnapshotAskMsg.Parse);
        Assert.Equal((i.Id, i.Visit), (ask.Inst, ask.Visit));
        Assert.Same(h.Mem("b"), i.Snapshot.Uploader);
        Assert.Equal(1, h.Hub.Counters["snapshot_asked"]);
        h.Advance(1_000);
        h.Housekeep();
        Assert.Single(h.P("b").All(Msg.SnapshotAsk, SnapshotAskMsg.Parse)); // (asked once)
        // b uploads its load snapshot: the visit has its snapshot, so a JOIN of it can be decided.
        h.Snapshot("b");
        Assert.True(i.Snapshot.Complete);
        Assert.Equal(100, i.Snapshot.Bytes!.Length);
    }

    [Fact]
    public void W7e_the_old_uploaders_chunks_after_the_ask_are_dropped_without_a_protocol_error()
    {
        var h = DroppedUploader(out var i);
        h.Advance(2_500);
        h.Housekeep();
        Assert.Same(h.Mem("b"), i.Snapshot.Uploader);
        // A chunk of a member that is not the uploader (here b's partner as it would come after a reconnect): dropped, counted.
        var a = h.Mem("a");
        h.Hub.Receive(a, Msg.Snapshot, new SnapshotMsg(i.Id, i.Visit, 100, 0, new byte[50]).Frame()[Wire.HeaderBytes..], h.Now);
        Assert.False(i.Snapshot.Complete);
        h.Snapshot("b");
        Assert.True(i.Snapshot.Complete);
    }

    [Fact]
    public void A_snapshot_ask_has_the_clients_bytes()
    {
        // (tests/online_wire_tests.cpp snapshot_ask: inst 7, visit 3.)
        var frame = new SnapshotAskMsg(7, 3).Frame();
        Assert.Equal(Msg.SnapshotAsk, frame[0]);
        Assert.Equal("07000000" + "03000000", Convert.ToHexString(frame.AsSpan(Wire.HeaderBytes)).ToLowerInvariant());
        Assert.Equal(new SnapshotAskMsg(7, 3), SnapshotAskMsg.Parse(frame.AsSpan(Wire.HeaderBytes)));
        Assert.Throws<ProtocolException>(() => SnapshotAskMsg.Parse(Convert.FromHexString("0700000003000000ff")));
        Assert.Equal("SNAPSHOT_ASK", Msg.Name(Msg.SnapshotAsk));
    }
}
