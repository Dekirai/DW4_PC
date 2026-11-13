using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// Decision 46 (the user's answer of 2026-11-12 20:40, round I1b's integration): a stage group of an instance at a barrier heading to
/// the arrival stage shows as a row *Arriving now* with its host's name; selectable, never preselected (decision 40 unchanged); the
/// choice waits for their stage group at the stage and joins it, at most a JOIN hold's time.
/// </summary>
public partial class HubLogicTests
{
    /// <summary>a and b play sealed at stag0201 (a's instance); c, a friend of a, waits at home. a reaches the barrier to stag0202,
    /// b not yet.</summary>
    private static Harness ArrivingPair(out WorldInstance w, out uint tick, InstanceOptions? instances = null, WorldHubOptions? world = null)
    {
        var h = SealedPair(out _, instances: instances, stage: "stag0201", world: world ?? new WorldHubOptions());
        h.Joinable("a");
        h.Seal(500);
        w = W(h, "a");
        var i = h.I("a");
        tick = i.NextSeal - 1;
        Admit(h, "c", Player(300, "c") with { Friends = [h.Mem("a").AccountId] });
        h.Barrier("a", "stag0202", tick: tick);
        Assert.NotNull(i.Barrier);
        return h;
    }

    [Fact]
    public void Decision_46_a_group_at_a_barrier_to_the_stage_is_an_arriving_row_never_preselected_and_its_choice_joins_them_there()
    {
        var h = ArrivingPair(out var w, out var tick);
        Plan(h, "c", 4, "stag0202");
        var o = LastOptions(h, "c");
        var row = Assert.Single(o.Rows);
        Assert.Equal((w.Wid, OptionState.ArrivingNow, (byte)0, "a"), (row.Wid, row.State, row.Here, row.HostName));
        // A friend's row with nobody here yet: never preselected (decision 40 would take a friend's first row).
        Assert.Equal(OptionsMsg.NewInstance, o.Preselected);
        // A on it: c's barrier waits for them (BARRIER_WAIT names them), also past a housekeeping pass.
        Choose(h, "c", 4, ChoiceKind.Join, w.Wid);
        BarrierPlan(h, "c", "stag0202", 4);
        Assert.Equal(HoldKind.Arriving, h.Mem("c").Arrival!.Hold);
        Assert.Equal(WaitReason.NotJoinable, h.P("c").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason);
        h.Advance(1_100);
        h.Housekeep();
        Assert.False(h.P("c").Has(Msg.BarrierOk));
        Assert.Null(h.Mem("c").World); // (no seat while it waits)
        // b reaches the barrier too: their stage group at stag0202; once it is joinable c joins it.
        h.Barrier("b", "stag0202", tick: tick);
        Assert.Equal(BarrierDecision.New, h.Decision("a").Decision);
        Assert.Equal("stag0202", h.I("a").Stage.ToLowerInvariant());
        h.Snapshot("a");
        h.Send("b", new SnapHashMsg(h.I("a").Id, h.I("a").Visit, 0xAA).Frame());
        h.Joinable("a");
        h.Advance(1_100);
        h.Housekeep();
        var ok = h.Decision("c");
        Assert.Equal((BarrierDecision.Join, ArrivalKind.AutoJoin, w.Wid), (ok.Decision, ok.World.Arrival, ok.World.Wid));
        Assert.Same(w, W(h, "c"));
        Assert.Equal(1, h.Hub.Counters["arriving_holds"]);
    }

    [Fact]
    public void Decision_46_an_arriving_row_is_never_the_automatic_join_and_its_wait_ends_with_a_join_holds_notice()
    {
        // (World:ArrivalPrompt off: I1a's automatic join takes the row the prompt would preselect, never an arriving one.)
        var off = ArrivingPair(out _, out _, world: new WorldHubOptions { ArrivalPrompt = false });
        BarrierWith(off, "c", "stag0202");
        var auto = off.Decision("c");
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (auto.Decision, auto.World.Arrival));
        Assert.NotSame(W(off, "a"), W(off, "c"));

        // Chosen, and they do not arrive within Instances:JoinHoldSeconds (here 3 s, b's report later than that; the relay's barrier
        // waits 5 s for it): a NEW with 3.4's hold-expired notice, as a JOIN's hold.
        var h = ArrivingPair(out var w, out _, new InstanceOptions { JoinHoldSeconds = 3 });
        Plan(h, "c", 9, "stag0202");
        Choose(h, "c", 9, ChoiceKind.Join, w.Wid);
        BarrierPlan(h, "c", "stag0202", 9);
        Assert.Equal(HoldKind.Arriving, h.Mem("c").Arrival!.Hold);
        h.Advance(4_000);
        h.Housekeep();
        Assert.NotNull(h.I("a").Barrier); // (still on their way)
        var ok = h.Decision("c");
        Assert.Equal((BarrierDecision.New, BarrierNotice.HoldExpired), (ok.Decision, ok.Notice));
        Assert.NotSame(w, W(h, "c"));
        Assert.Equal(1, h.Hub.Counters["arriving_expired"]);
    }

    [Fact]
    public void Decision_46_arriving_rows_follow_the_rows_enterable_now_and_never_take_the_cursor()
    {
        var h = ArrivingPair(out var w, out _);
        CandidateRow Row(RowState state, bool friend) => new(w, null, state, 0, friend, 0, 10);
        var arriving = Row(RowState.Arriving, true);
        var joinable = Row(RowState.GuestNew, false);
        Assert.Equal([joinable, arriving], Matching.Order([arriving, joinable]));
        Assert.True(arriving.Choosable);
        Assert.Null(Matching.Preselected([arriving]));
        Assert.Null(Matching.AutoJoinRow([arriving]));
        // (A friend's joinable row before it keeps decision 40's preselection.)
        var friendNow = Row(RowState.GuestNew, true);
        Assert.Same(friendNow, Matching.Preselected(Matching.Order([arriving, friendNow])));
    }
}
