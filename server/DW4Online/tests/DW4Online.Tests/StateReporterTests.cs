using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;

namespace DW4Online.Tests;

/// <summary>
/// fix-d43, W7f's race (SEAMLESS.md 19.32): a stage group's STATE flags come from one member, the connected one in it longest (then
/// the lowest slot; <c>RoomHub.OnState</c>). The STATE of another member was dropped, and a client sends STATE only on a change, so a
/// change of that reporter (a reconnect, a LEAVE, a lost link) could leave the flags with a state no engine had any more. Now every
/// member's newest STATE of the visit is kept and a change of the reporter takes the new reporter's newest one: when it is newer (by
/// tick) than the one in effect, or the member whose STATE is in effect left the stage group.
/// </summary>
public partial class HubLogicTests
{
    /// <summary>A STATE of <paramref name="name"/>'s engine at <paramref name="tick"/> (after the visit's load end).</summary>
    private static void StateAt(Harness h, string name, uint tick, bool joinable, StateReason reason = StateReason.Scene)
    {
        var i = h.I(name);
        h.Send(name, new StateMsg(i.Id, i.Visit, tick, (byte)((joinable ? StateMsg.Joinable | StateMsg.MembershipSafe : 0) | StateMsg.LoadEnded), 5,
            joinable ? StateReason.None : reason).Frame());
    }

    [Fact]
    public void W7f_a_state_sent_while_a_reconnecting_member_held_the_reporters_slot_takes_effect_at_its_leave_and_its_join_goes_at_once() =>
        W7fRace(oldTimeline: false);

    [Fact]
    public void W7f_a_state_of_the_old_timeline_from_the_reconnected_member_never_holds_back_the_others_newest_after_its_leave() =>
        W7fRace(oldTimeline: true);

    /// <summary>W7f's interleaving. <paramref name="oldTimeline"/>: a's client, back in the new visit with its engine still at the old
    /// barrier, also sends a STATE under the new visit with its old tick (past every tick b's stage group sealed): in effect while a
    /// reports, never a reason to keep b's newer STATE out once a left.</summary>
    private static void W7fRace(bool oldTimeline)
    {
        // a and b (strangers, a hosts) park at stag0201's exit together; the sealed NEW of both is decided and a's link goes with its
        // BARRIER_OK (lost with it).
        var h = SealedPair(out _, stage: "stag0201");
        h.Joinable("a");
        h.Seal(6000);
        var tick = h.I("a").NextSeal - 1;
        h.Barrier("a", "stag0202", tick: tick);
        h.Barrier("b", "stag0202", tick: tick);
        var next = h.I("b");
        Assert.Same(next, h.Mem("a").Instance);
        h.Hub.Disconnect(h.Mem("a"), h.P("a"), h.Now);
        // b loads (SNAPSHOT_ASK makes it the uploader) and reports while a is away: its load, then its entry scene.
        h.Advance(2100);
        h.Housekeep();
        h.Snapshot("b");
        StateAt(h, "b", 0, joinable: false, StateReason.Loading);
        h.Seal(1000);
        StateAt(h, "b", 40, joinable: false);
        Assert.Equal((false, StateReason.Scene), (next.Joinable, next.Reason));
        // a comes back into the new visit (WELCOME names it, slot 0): it reports again, its engine still at the old barrier.
        var pa = Admit(h, "a", Player(100, "a"));
        var welcome = pa.Last(Msg.Welcome, WelcomeMsg.Parse);
        Assert.Equal((next.Id, next.Visit, (byte)0), (welcome.Inst, welcome.Visit, welcome.Slot));
        if (oldTimeline)
        {
            StateAt(h, "a", tick, joinable: false, StateReason.Loading);
            Assert.Equal((tick, StateReason.Loading), (next.StateTick, next.Reason));
        }
        // b's entry scene ends in the 26 ms before a's LEAVE: its joinable STATE is not the reporter's, so not in effect yet.
        var ignored = h.Hub.Counters.GetValueOrDefault("state_ignored");
        StateAt(h, "b", 70, joinable: true);
        Assert.Equal(ignored + 1, h.Hub.Counters.GetValueOrDefault("state_ignored"));
        Assert.False(next.Joinable);
        // a re-sends its old barrier: past b's sealed ticks, decided apart (a's LEAVE): b reports from now on, its joinable STATE takes
        // effect, and a's JOIN of b's stage group goes at once. Before, b's STATE was dropped, b sent none again (no change), a's JOIN
        // waited Instances:JoinHoldSeconds for nothing and became a stage group of its own (HoldExpired).
        h.Barrier("a", "stag0202", tick: tick + 1);
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("barrier_apart"));
        Assert.Equal((true, StateReason.None), (next.Joinable, next.Reason));
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("state_reporter_changed"));
        Assert.False(pa.Has(Msg.BarrierWait));
        var ok = h.Decision("a");
        Assert.Equal((BarrierDecision.Join, next.Id, next.Visit), (ok.Decision, ok.NewInst, ok.NewVisit));
        Assert.Same(W(h, "b"), W(h, "a"));
    }

    [Fact]
    public void A_reporter_whose_link_goes_hands_the_flags_to_the_next_members_newest_state_at_once()
    {
        // a (slot 0) reports for a and b; b's engine is a moment ahead and joinable first: not in effect while a reports.
        var h = SealedPair(out _);
        var i = h.I("a");
        h.Seal(500);
        StateAt(h, "a", 10, joinable: false);
        StateAt(h, "b", 25, joinable: true);
        Assert.False(i.Joinable);
        // a's link goes: b reports now, and its newest STATE takes effect without waiting for b's next change (b sends none: nothing
        // changed for its engine). Before, the stage group stayed not joinable until b's engine changed again.
        h.Hub.Disconnect(h.Mem("a"), h.P("a"), h.Now);
        Assert.Equal((true, true, StateReason.None), (i.Joinable, i.MembershipSafe, i.Reason));
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("state_reporter_changed"));
    }

    [Fact]
    public void A_member_that_leaves_the_stage_group_keeps_no_state_there_though_the_reporter_stays()
    {
        // a reports for a and b; b's STATE is kept, not in effect. b is taken out for lag while a plays on (the reporter stays a): b's
        // STATE goes with it (back in this visit's stage group later, a STATE from before is not its engine's). Before the refinement
        // only a change of the reporter dropped it.
        var h = SealedPair(out var inst, new RelayOptions { LagLeaveSeconds = 10 });
        var i = h.I("a");
        h.Seal(500);
        StateAt(h, "a", 10, joinable: true);
        StateAt(h, "b", 20, joinable: false, StateReason.Boss);
        Assert.Equal(2, i.States.Count);
        Assert.Equal((true, StateReason.None), (i.Joinable, i.Reason));
        h.Seal(12_000); // neither sends pads: nobody is taken out while everyone lags
        for (var k = 0; k < 60; ++k)
        {
            h.Send("a", new InputMsg(inst, i.Visit, i.NextSeal + 3, [new Pad(1, 0, 0, 0, 0), new Pad(1, 0, 0, 0, 0)]).Frame());
            h.Seal(17);
        }
        h.Housekeep();
        Assert.Null(h.Mem("b").Instance);
        Assert.Same(h.Mem("a"), Assert.Single(i.States).Key);
        Assert.Same(h.Mem("a"), i.StateReporter);
        Assert.Equal((true, StateReason.None), (i.Joinable, i.Reason));
        Assert.Equal(0, h.Hub.Counters.GetValueOrDefault("state_reporter_changed"));
    }

    [Fact]
    public void A_reporter_that_comes_back_never_takes_a_newer_state_back_with_its_older_one_and_a_new_visit_keeps_nothing_of_the_last()
    {
        var h = SealedPair(out _);
        var i = h.I("a");
        h.Seal(500);
        StateAt(h, "a", 10, joinable: false); // (in effect: a reports)
        h.Hub.Disconnect(h.Mem("a"), h.P("a"), h.Now);
        StateAt(h, "b", 30, joinable: true); // (b reports while a is away)
        Assert.True(i.Joinable);
        // a is back (the reporter again): its newest STATE (tick 10, Scene) is older than b's in effect: the flags stay b's until a's
        // engine reports a change.
        var pa = Admit(h, "a", Player(100, "a"));
        Assert.NotEqual(0, pa.Last(Msg.Welcome, WelcomeMsg.Parse).Flags & WelcomeMsg.Resumable);
        Assert.True(i.Joinable);
        Assert.Equal(0, h.Hub.Counters.GetValueOrDefault("state_reporter_changed"));
        StateAt(h, "a", 40, joinable: false, StateReason.Boss);
        Assert.Equal((false, StateReason.Boss), (i.Joinable, i.Reason));
        // The next visit (a NEW of both): the STATEs of the last one are gone; nothing takes effect at a change of the reporter there
        // until a STATE of the new visit came.
        StateAt(h, "b", 60, joinable: true);
        var t = i.NextSeal - 1;
        h.Barrier("a", "stag0202", tick: t);
        h.Barrier("b", "stag0202", tick: t);
        var next = h.I("a");
        Assert.Empty(next.States);
        Assert.False(next.StateApplied);
        h.Hub.Disconnect(h.Mem("a"), h.P("a"), h.Now);
        Assert.False(next.Joinable);
        Assert.Equal(0, h.Hub.Counters.GetValueOrDefault("state_reporter_changed"));
    }
}
