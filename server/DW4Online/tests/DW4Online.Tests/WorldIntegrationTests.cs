using DW4Online.Domain.Rooms;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// Round I1a's integration (branch int-i1a): the rules the W scenarios with real game clients found broken on the merged tree,
/// each with a test that fails without its fix (docs/ROUND_I1A_INT_NOTES.md).
/// </summary>
public partial class HubLogicTests
{
    /// <summary>
    /// W7b (and W12's first run): the lost host's END stamps HOME World:EndNoticeTicks ahead in the sealed stream; his member
    /// timeout has passed long ago, so he leaves the world in the same pass and his LEAVE takes an EARLIER tick. The guest then
    /// leads alone from the LEAVE on and the stamped HOME was never sealed: the guest played on in an ended instance. Now the
    /// HOME waits again for the solo leader and goes out as JOIN_REQ home when its tick would have come.
    /// </summary>
    [Fact]
    public void A_lost_hosts_end_reaches_the_guest_that_leads_alone_after_his_leave()
    {
        var g = SealedPair(out _, relay: new RelayOptions { LagLeaveSeconds = 600 });
        var w = W(g, "a");
        var bSlot = g.Mem("b").Slot;
        g.Seal(300);
        g.Hub.Disconnect(g.Mem("a"), g.P("a"), g.Now);
        g.Seal(59_000);
        g.Hub.Housekeep(g.Now);
        Assert.False(w.Ending);
        g.Seal(2_000);
        g.Hub.Housekeep(g.Now);
        Assert.Equal(EndReason.HostLost, w.EndReason);
        Assert.True(g.Mem("a").LeftWorld); // (the same pass: his member timeout counts from the disconnect once the grace is over)
        g.Seal(1_000);
        var i = g.I("b");
        Assert.Equal(InstanceMode.SoloLed, i.Mode);
        Assert.DoesNotContain(i.PendingEvents.Values.SelectMany(l => l), e => e.Type == Ev.Home); // nothing left that nobody seals
        // The end notice's 3 s first, then JOIN_REQ home for b's slot (its JOIN_AT seals HOME with the reason).
        Assert.DoesNotContain(g.P("b").Frames, f => f.Type == Msg.JoinReq && JoinReqMsg.Parse(f.Payload).Kind == JoinReqKind.Home);
        g.Advance(3_100);
        g.Hub.Housekeep(g.Now);
        var home = g.P("b").Last(Msg.JoinReq, JoinReqMsg.Parse);
        Assert.Equal((JoinReqKind.Home, (byte)(1 << bSlot)), (home.Kind, home.Slot));
        var j = i.Log.Count + 30;
        g.Log("b", i.Log.Count, 30);
        g.Send("b", new JoinAtMsg(home.Inst, home.Visit, j).Frame());
        g.Seal(400);
        Assert.Contains(g.P("b").Ticks(), t => t.Rec.Events.Any(e => e.Type == Ev.Home && e.HomeMask == home.Slot &&
            e.Payload[1] == (byte)HomeReason.Ended));
        Assert.Equal(1, g.Hub.Counters["home_refiled"]);
    }
}
