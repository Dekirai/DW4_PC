using DW4Online.Domain.Rooms;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// Decision 43 (the user's answer of 2026-11-11 to decision 21's open question, SEAMLESS.md 19.7.6): two players who leave together and
/// both choose *New instance* land together. A *New instance* chooser is the co-arrival of a base it may see (the same rules as every
/// co-arrival: visibility, blocks), and joins the starting instance of a base that went a moment earlier unless that instance was a row
/// it saw and passed over. Players who chose a row still join their row.
/// </summary>
public partial class HubLogicTests
{
    /// <summary>k's public instance plays in dng101b1: a gate PLAN to stag0101 lists it with the cursor on New instance.</summary>
    private static Harness TogetherHarness(HubAdmit? lee = null, HubAdmit? max = null)
    {
        var h = PromptHarness();
        Admit(h, "k", Player(100, "k"));
        Admit(h, "lee", lee ?? Player(101, "lee"));
        Admit(h, "max", max ?? Player(102, "max"));
        HostJoinable(h, "k", "dng101b1");
        return h;
    }

    [Fact]
    public void Two_new_instance_choosers_who_leave_together_land_in_one_new_instance()
    {
        var h = TogetherHarness();
        var wk = W(h, "k");
        Plan(h, "lee", 1, "stag0101");
        Plan(h, "max", 1, "stag0101");
        Assert.Equal(wk.Wid, Assert.Single(LastOptions(h, "max").Rows).Wid);
        Choose(h, "lee", 1, ChoiceKind.New, visibility: Visibility.Public);
        Choose(h, "max", 1, ChoiceKind.New, visibility: Visibility.Private);
        Heading(h, "lee", "stag0101");
        Heading(h, "max", "stag0101");
        h.Advance(300);
        BarrierPlan(h, "lee", "stag0101", 1);
        var held = h.Mem("lee").Arrival!;
        Assert.Equal(HoldKind.CoArrival, held.Hold);
        Assert.Equal([h.Mem("max")], held.Awaited);
        h.Advance(400);
        BarrierPlan(h, "max", "stag0101", 1);
        var okLee = h.Decision("lee");
        var okMax = h.Decision("max");
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance, Visibility.Public), (okLee.Decision, okLee.World.Arrival, okLee.World.Visibility));
        Assert.Equal(okLee.World.Wid, okMax.World.Wid);
        Assert.Equal(BarrierDecision.CoArrive, okMax.Decision);
        Assert.Single(okLee.CoArrivals);
        var w = W(h, "lee");
        Assert.Same(w, W(h, "max"));
        Assert.NotSame(wk, w);
        Assert.Equal(2, w.Seats.Count);
        Assert.Same(h.Mem("lee"), w.Host);
        Assert.Equal(Visibility.Private, h.Mem("max").DefaultVisibility); // (his choice is remembered for his next New instance)
    }

    [Fact]
    public void New_instance_choosers_that_may_not_see_each_others_base_land_apart()
    {
        // A block either way: two instances.
        var h = TogetherHarness(lee: Player(101, "lee") with { Blocks = [102] }, max: Player(102, "max") with { BlockedBy = [101] });
        Plan(h, "lee", 1, "stag0101");
        Plan(h, "max", 1, "stag0101");
        Choose(h, "lee", 1, ChoiceKind.New);
        Choose(h, "max", 1, ChoiceKind.New);
        Heading(h, "lee", "stag0101");
        Heading(h, "max", "stag0101");
        h.Advance(300);
        BarrierPlan(h, "lee", "stag0101", 1);
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (h.Decision("lee").Decision, h.Decision("lee").World.Arrival));
        h.Advance(400);
        BarrierPlan(h, "max", "stag0101", 1);
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (h.Decision("max").Decision, h.Decision("max").World.Arrival));
        Assert.NotSame(W(h, "lee"), W(h, "max"));

        // The first one's New instance is Private: the other may not see it, so it is no base for him (nor its starting instance).
        var g = TogetherHarness();
        Plan(g, "lee", 1, "stag0101");
        Plan(g, "max", 1, "stag0101");
        Choose(g, "lee", 1, ChoiceKind.New, visibility: Visibility.Private);
        Choose(g, "max", 1, ChoiceKind.New);
        Heading(g, "lee", "stag0101");
        Heading(g, "max", "stag0101");
        g.Advance(300);
        BarrierPlan(g, "lee", "stag0101", 1);
        Assert.Equal((BarrierDecision.New, Visibility.Private), (g.Decision("lee").Decision, g.Decision("lee").World.Visibility));
        g.Advance(400);
        BarrierPlan(g, "max", "stag0101", 1);
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (g.Decision("max").Decision, g.Decision("max").World.Arrival));
        Assert.NotSame(W(g, "lee"), W(g, "max"));
    }

    [Fact]
    public void A_new_instance_chooser_a_moment_behind_joins_the_starting_instance_it_was_never_offered()
    {
        // Both chose New instance at the gate; lee's barrier came before max's HEADING (no hold): lee's new instance is starting, and
        // max's barrier joins it as a co-arrival does (19.7.6's starting window), at the entrance once it is joinable.
        var h = TogetherHarness();
        Plan(h, "lee", 1, "stag0101");
        Plan(h, "max", 1, "stag0101");
        Choose(h, "lee", 1, ChoiceKind.New);
        Choose(h, "max", 1, ChoiceKind.New);
        Heading(h, "lee", "stag0101");
        BarrierPlan(h, "lee", "stag0101", 1);
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (h.Decision("lee").Decision, h.Decision("lee").World.Arrival));
        var wl = W(h, "lee");
        Assert.NotEqual(0, wl.StartingUntil);
        h.Advance(2000);
        Heading(h, "max", "stag0101");
        BarrierPlan(h, "max", "stag0101", 1);
        Assert.False(h.P("max").Has(Msg.BarrierOk));
        h.Snapshot("lee");
        h.Log("lee", 0, 60);
        h.Joinable("lee");
        var ok = h.Decision("max");
        Assert.Equal((BarrierDecision.Join, ArrivalKind.Starting, wl.Wid), (ok.Decision, ok.World.Arrival, ok.World.Wid));
        Assert.Same(wl, W(h, "max"));
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("new_choice_starting"));

        // The same with plan 0xFFFF (DW4_ONLINE_AUTOCHOICE=new without a PLAN).
        var g = TogetherHarness();
        BarrierPlan(g, "lee", "stag0101", BarrierMsg.PlanNewInstance);
        var gl = W(g, "lee");
        g.Advance(2000);
        BarrierPlan(g, "max", "stag0101", BarrierMsg.PlanNewInstance);
        g.Snapshot("lee");
        g.Log("lee", 0, 60);
        g.Joinable("lee");
        Assert.Equal((BarrierDecision.Join, ArrivalKind.Starting, gl.Wid), (g.Decision("max").Decision, g.Decision("max").World.Arrival, g.Decision("max").World.Wid));
    }

    [Fact]
    public void A_starting_instance_offered_as_a_row_and_passed_over_with_new_instance_stays_passed_over()
    {
        // max's open gate form is refreshed with lee's starting instance as a row; he chooses New instance all the same: his own.
        var h = TogetherHarness();
        Plan(h, "lee", 1, "stag0101");
        Plan(h, "max", 1, "stag0101");
        Choose(h, "lee", 1, ChoiceKind.New);
        Heading(h, "lee", "stag0101");
        BarrierPlan(h, "lee", "stag0101", 1);
        var wl = W(h, "lee");
        h.Advance(1100);
        h.Hub.Housekeep(h.Now);
        var refreshed = LastOptions(h, "max");
        Assert.Contains(refreshed.Rows, r => r.Wid == wl.Wid);
        Choose(h, "max", 1, ChoiceKind.New);
        Heading(h, "max", "stag0101");
        BarrierPlan(h, "max", "stag0101", 1);
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (h.Decision("max").Decision, h.Decision("max").World.Arrival));
        Assert.NotSame(wl, W(h, "max"));
        Assert.Equal(0, h.Hub.Counters.GetValueOrDefault("new_choice_starting"));
    }
}
