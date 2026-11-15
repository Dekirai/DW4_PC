using DW4Online.Domain.Rooms;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// Decision 48's review (fix-d43, 2026-11-14): the base's side asks the base's chosen visibility, not a default a live re-check may
/// have put back; an answered gate plan outlasts a reconnect before its BARRIER; a member whose barrier came is awaited by no other
/// base; a base's co-arrivals are checked again when its hold ends (a block or an unfriending while it waited).
/// </summary>
public partial class HubLogicTests
{
    /// <summary>The members seated in the member's instance, by name.</summary>
    private static string[] Seated(Harness h, string name) => W(h, name).Seats.Keys.Select(x => x.Name).OrderBy(x => x).ToArray();

    /// <summary>k's Public instance plays joinable at stag0101 (a row to choose there); lee and max at home, strangers.</summary>
    private static Harness RowHarness()
    {
        var h = PromptHarness();
        Admit(h, "k", Player(100, "k"));
        Admit(h, "lee", Player(101, "lee"));
        Admit(h, "max", Player(102, "max"));
        HostJoinable(h, "k", "stag0101");
        return h;
    }

    [Theory]
    [InlineData(Visibility.Private)]
    [InlineData(Visibility.Friends)]
    public void A_readmission_that_puts_back_the_stored_default_never_lets_a_private_or_friends_choosers_base_take_a_stranger(Visibility chosen)
    {
        // lee chose *New instance* Private (or Friends), max, a stranger, *New instance* Public. A live re-check of lee (a friend request
        // to him: FriendsChanged) read his account before the remembered choice was written: Readmitted puts the stored Public back. Before,
        // lee's base then held for max and took him: a stranger in lee's Private instance. The base asks lee's choice now.
        var h = TogetherHarness();
        Plan(h, "lee", 1, "stag0101");
        Plan(h, "max", 1, "stag0101");
        Choose(h, "lee", 1, ChoiceKind.New, visibility: chosen);
        Choose(h, "max", 1, ChoiceKind.New, visibility: Visibility.Public);
        Assert.Equal(chosen, h.Mem("lee").DefaultVisibility);
        h.Hub.Readmitted(101, Player(101, "lee"), h.Now);
        Assert.Equal(Visibility.Public, h.Mem("lee").DefaultVisibility); // (the stored value: the race)
        Heading(h, "lee", "stag0101");
        Heading(h, "max", "stag0101");
        h.Advance(300);
        BarrierPlan(h, "lee", "stag0101", 1);
        Assert.True(h.P("lee").Has(Msg.BarrierOk), $"lee: hold {h.Mem("lee").Arrival?.Hold} awaiting [{string.Join(",", h.Mem("lee").Arrival?.Awaited.Select(x => x.Name) ?? [])}] choice {h.Mem("lee").Arrival?.PromptChoice?.Visibility}");
        Assert.Equal((BarrierDecision.New, chosen), (h.Decision("lee").Decision, h.Decision("lee").World.Visibility));
        h.Advance(400);
        BarrierPlan(h, "max", "stag0101", 1);
        h.Advance(100);
        h.Housekeep();
        Assert.Equal((BarrierDecision.New, Visibility.Public), (h.Decision("max").Decision, h.Decision("max").World.Visibility));
        Assert.NotSame(W(h, "lee"), W(h, "max"));
        Assert.Equal(["lee"], Seated(h, "lee"));
        Assert.Equal(["max"], Seated(h, "max"));
    }

    [Fact]
    public void An_answered_plan_outlasts_a_reconnect_so_a_private_choice_still_keeps_its_barrier_apart()
    {
        // lee chose *New instance* Public, max, a stranger, *New instance* Private; lee's barrier made lee's instance (starting). max's
        // link drops at the gate's fade and comes back (a new connection: his lobby is a new hub instance) before his engine sends the
        // BARRIER of plan 1. Before, the disconnect dropped the plan with its answer: the BARRIER was decided as one without an answer
        // and joined lee's starting instance (a stranger's Public story).
        var h = TogetherHarness();
        Plan(h, "lee", 1, "stag0101");
        Plan(h, "max", 1, "stag0101");
        Choose(h, "lee", 1, ChoiceKind.New, visibility: Visibility.Public);
        Choose(h, "max", 1, ChoiceKind.New, visibility: Visibility.Private);
        Heading(h, "lee", "stag0101");
        Heading(h, "max", "stag0101");
        h.Advance(300);
        BarrierPlan(h, "lee", "stag0101", 1);
        Assert.Equal((BarrierDecision.New, Visibility.Public), (h.Decision("lee").Decision, h.Decision("lee").World.Visibility));
        h.Hub.Disconnect(h.Mem("max"), h.P("max"), h.Now);
        Assert.Equal((ushort)1, h.Mem("max").Prompt.Plan);
        h.Advance(400);
        Admit(h, "max", Player(102, "max")); // (the stored default: Public)
        h.Advance(200);
        BarrierPlan(h, "max", "stag0101", 1);
        Assert.True(h.P("max").Has(Msg.BarrierOk), $"max hold {h.Mem("max").Arrival?.Hold}");
        var ok = h.Decision("max");
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance, Visibility.Private), (ok.Decision, ok.World.Arrival, ok.World.Visibility));
        Assert.NotSame(W(h, "lee"), W(h, "max"));
        Assert.Equal(["max"], Seated(h, "max"));
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("new_choice_starting_refused"));
        Assert.Equal((ushort)0, h.Mem("max").Prompt.Plan); // (its BARRIER ended the plan)
    }

    [Fact]
    public void A_chosen_row_outlasts_a_reconnect_before_the_barrier_and_an_unanswered_plan_does_not()
    {
        // max chose k's row at the gate; his link drops and comes back before his BARRIER: the BARRIER joins k's instance as chosen
        // (before: decided as one without an answer, the loading-screen form asked him again).
        var h = RowHarness();
        var wk = W(h, "k");
        Plan(h, "max", 1, "stag0101");
        Assert.Equal(wk.Wid, Assert.Single(LastOptions(h, "max").Rows).Wid);
        Choose(h, "max", 1, ChoiceKind.Join, wk.Wid);
        h.Hub.Disconnect(h.Mem("max"), h.P("max"), h.Now);
        h.Advance(400);
        Admit(h, "max", Player(102, "max"));
        h.Advance(200);
        BarrierPlan(h, "max", "stag0101", 1);
        Assert.NotEqual(HoldKind.Prompt, h.Mem("max").Arrival?.Hold ?? HoldKind.None);
        Assert.Equal((BarrierDecision.Join, wk.Wid), (h.Decision("max").Decision, h.Decision("max").World.Wid));
        Assert.Same(wk, W(h, "max"));

        // A plan without an answer (the overlay open, the press held) answers nobody after a disconnect, as before.
        Plan(h, "lee", 1, "stag0101");
        Assert.True(h.Mem("lee").Prompt.Offered);
        h.Hub.Disconnect(h.Mem("lee"), h.P("lee"), h.Now);
        Assert.Equal((ushort)0, h.Mem("lee").Prompt.Plan);
    }

    [Fact]
    public void A_base_a_chooser_refused_stops_waiting_for_him_when_another_base_takes_him()
    {
        // lee (Public, he blocks c) holds for max, who has no answer yet. c, max's friend, holds a base of his own for d. max then chooses
        // *New instance* Friends: lee is no friend of his, c is: max is c's co-arrival, and lee's base stops waiting for him at once.
        // Before, only a group no base took freed the bases it refused: lee's base waited Instances:CoArrivalHoldMs for max.
        var h = TogetherHarness(lee: Player(101, "lee") with { Blocks = [103] }, max: Player(102, "max") with { Friends = [103] });
        Admit(h, "c", Player(103, "c") with { Friends = [102], BlockedBy = [101] });
        Admit(h, "d", Player(104, "d"));
        Plan(h, "lee", 1, "stag0101");
        Plan(h, "max", 1, "stag0101");
        Plan(h, "c", 1, "stag0101");
        Choose(h, "lee", 1, ChoiceKind.New, visibility: Visibility.Public);
        Choose(h, "c", 1, ChoiceKind.New, visibility: Visibility.Public);
        Heading(h, "lee", "stag0101");
        Heading(h, "max", "stag0101");
        h.Advance(300);
        BarrierPlan(h, "lee", "stag0101", 1);
        Assert.Equal([h.Mem("max")], h.Mem("lee").Arrival!.Awaited);
        Heading(h, "c", "stag0101");
        Heading(h, "d", "stag0101");
        h.Advance(200);
        BarrierPlan(h, "c", "stag0101", 1);
        Assert.Equal([h.Mem("d")], h.Mem("c").Arrival!.Awaited);
        Choose(h, "max", 1, ChoiceKind.New, visibility: Visibility.Friends);
        h.Advance(200);
        BarrierPlan(h, "max", "stag0101", 1);
        Assert.Same(h.Mem("c").Arrival, h.Mem("max").Arrival); // (c's co-arrival)
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("co_arrival_refused_choice"));
        Assert.Empty(h.Mem("lee").Arrival!.Awaited);
        h.Advance(100);
        h.Housekeep();
        Assert.Equal((BarrierDecision.New, Visibility.Public), (h.Decision("lee").Decision, h.Decision("lee").World.Visibility));
        Assert.Equal(["lee"], Seated(h, "lee"));
        Assert.Equal(HoldKind.CoArrival, h.Mem("c").Arrival!.Hold); // (c still waits for d, with max)
    }

    [Fact]
    public void A_base_stops_waiting_for_a_member_who_chose_a_row_after_its_hold_began()
    {
        // lee's base holds for max (no answer yet at lee's barrier); max then chooses k's row: he joins it and never comes to lee's base,
        // which ends its hold at the next housekeeping pass (before: after Instances:CoArrivalHoldMs).
        var h = RowHarness();
        var wk = W(h, "k");
        Plan(h, "lee", 1, "stag0101");
        Plan(h, "max", 1, "stag0101");
        Choose(h, "lee", 1, ChoiceKind.New, visibility: Visibility.Public);
        Heading(h, "lee", "stag0101");
        Heading(h, "max", "stag0101");
        h.Advance(300);
        BarrierPlan(h, "lee", "stag0101", 1);
        Assert.Equal([h.Mem("max")], h.Mem("lee").Arrival!.Awaited);
        Choose(h, "max", 1, ChoiceKind.Join, wk.Wid);
        h.Advance(200);
        BarrierPlan(h, "max", "stag0101", 1);
        Assert.Equal((BarrierDecision.Join, wk.Wid), (h.Decision("max").Decision, h.Decision("max").World.Wid));
        Assert.Empty(h.Mem("lee").Arrival!.Awaited);
        h.Advance(100);
        h.Housekeep();
        Assert.Equal((BarrierDecision.New, Visibility.Public), (h.Decision("lee").Decision, h.Decision("lee").World.Visibility));
        Assert.Equal(["lee"], Seated(h, "lee"));
    }

    [Fact]
    public void A_block_during_the_bases_hold_keeps_the_two_apart()
    {
        // lee (Public) holds for his friend max (*New instance* Friends) and for d; max becomes lee's co-arrival. While the base still
        // waits for d, lee unfriends and blocks max. Before, nothing checked the co-arrivals again (Readmitted removes a blocked guest
        // only from an instance that exists): both landed in lee's instance. Now max leaves the base at its end and decides on his own.
        var h = TogetherHarness(lee: Player(101, "lee") with { Friends = [102] }, max: Player(102, "max") with { Friends = [101] });
        Admit(h, "d", Player(104, "d"));
        Plan(h, "lee", 1, "stag0101");
        Plan(h, "max", 1, "stag0101");
        Choose(h, "lee", 1, ChoiceKind.New, visibility: Visibility.Public);
        Choose(h, "max", 1, ChoiceKind.New, visibility: Visibility.Friends);
        Heading(h, "lee", "stag0101");
        Heading(h, "max", "stag0101");
        Heading(h, "d", "stag0101");
        h.Advance(300);
        BarrierPlan(h, "lee", "stag0101", 1);
        Assert.Equal(2, h.Mem("lee").Arrival!.Awaited.Count);
        h.Advance(200);
        BarrierPlan(h, "max", "stag0101", 1);
        Assert.Same(h.Mem("lee").Arrival, h.Mem("max").Arrival);
        h.Hub.Readmitted(101, Player(101, "lee") with { Blocks = [102] }, h.Now);
        h.Hub.Readmitted(102, Player(102, "max") with { BlockedBy = [101] }, h.Now);
        h.Advance(3000);
        h.Housekeep();
        Assert.True(h.P("lee").Has(Msg.BarrierOk) && h.P("max").Has(Msg.BarrierOk), $"lee {h.Mem("lee").Arrival?.Hold} ok {h.P("lee").Has(Msg.BarrierOk)}; max {h.Mem("max").Arrival?.Hold} ok {h.P("max").Has(Msg.BarrierOk)} same {ReferenceEquals(h.Mem("max").Arrival, h.Mem("lee").Arrival)}; dropped {h.Hub.Counters.GetValueOrDefault("co_arrival_dropped")}");
        Assert.NotSame(W(h, "lee"), W(h, "max"));
        Assert.Equal((Visibility.Public, Visibility.Friends), (W(h, "lee").Visibility, W(h, "max").Visibility));
        Assert.Equal(["lee"], Seated(h, "lee"));
        Assert.Equal(["max"], Seated(h, "max"));
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("co_arrival_dropped"));
    }

    [Fact]
    public void An_unfriending_during_a_friends_bases_hold_drops_only_that_co_arrival()
    {
        // lee chose *New instance* Friends; his friends max (*New instance* Public) and d (no answer) leave with him. max becomes lee's
        // co-arrival; lee unfriends max before d's barrier ends the hold: lee's Friends instance takes d, and max, no friend any more,
        // decides on his own (his own Public instance; lee's starting instance is not his to see).
        var h = TogetherHarness(lee: Player(101, "lee") with { Friends = [102, 104] }, max: Player(102, "max") with { Friends = [101] });
        Admit(h, "d", Player(104, "d") with { Friends = [101] });
        Plan(h, "lee", 1, "stag0101");
        Plan(h, "max", 1, "stag0101");
        Choose(h, "lee", 1, ChoiceKind.New, visibility: Visibility.Friends);
        Choose(h, "max", 1, ChoiceKind.New, visibility: Visibility.Public);
        Heading(h, "lee", "stag0101");
        Heading(h, "max", "stag0101");
        Heading(h, "d", "stag0101");
        h.Advance(300);
        BarrierPlan(h, "lee", "stag0101", 1);
        Assert.Equal(2, h.Mem("lee").Arrival!.Awaited.Count);
        h.Advance(200);
        BarrierPlan(h, "max", "stag0101", 1);
        Assert.Same(h.Mem("lee").Arrival, h.Mem("max").Arrival);
        h.Hub.Readmitted(101, Player(101, "lee") with { Friends = [104] }, h.Now);
        h.Hub.Readmitted(102, Player(102, "max"), h.Now);
        h.Advance(200);
        BarrierPlan(h, "d", "stag0101", 0);
        Assert.True(h.P("lee").Has(Msg.BarrierOk) && h.P("max").Has(Msg.BarrierOk) && h.P("d").Has(Msg.BarrierOk));
        Assert.Same(W(h, "lee"), W(h, "d"));
        Assert.Equal(Visibility.Friends, W(h, "lee").Visibility);
        Assert.Equal(["d", "lee"], Seated(h, "lee"));
        Assert.Equal((BarrierDecision.New, Visibility.Public), (h.Decision("max").Decision, h.Decision("max").World.Visibility));
        Assert.Equal(["max"], Seated(h, "max"));
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("co_arrival_dropped"));
    }
}
