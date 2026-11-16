using DW4Online.Domain.Rooms;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// Decision 48 (the user's answer of 2026-11-14, "Private stays private"; SEAMLESS.md 19.7.6): a player whose choice was *New instance*
/// with Private is never the co-arrival of another player's base nor joins another's starting instance; with Friends only a friend's;
/// with Public as decision 43 says. The base's side already held: a Private or Friends base takes no co-arrival its visibility does not
/// let in. So two *New instance* choosers land together exactly when both choices let the other in, whichever barrier comes first.
/// The members of one stage group who leave through the same barrier are one arrival and stay together whatever the visibility.
/// </summary>
public partial class HubLogicTests
{
    /// <summary>Does <paramref name="v"/> let the other player in (strangers, or friends both ways), no block?</summary>
    private static bool Admits(Visibility v, bool friends) => v == Visibility.Public || (v == Visibility.Friends && friends);

    /// <summary>
    /// lee and max both chose *New instance* at the gate to stag0101 (k's Public instance plays in the area, so both got the gate form);
    /// lee's barrier comes first with <paramref name="first"/>, max's 0.4 s later with <paramref name="second"/>. <paramref name="late"/>:
    /// max's HEADING comes after lee's barrier (no hold: lee's new instance is starting when max arrives), else before it (lee's base may
    /// hold for max). Returns whether they share an instance, and the visibility of each one's instance.
    /// </summary>
    private static (bool Together, Visibility Lee, Visibility Max) LeaveTogether(Visibility first, Visibility second, bool friends, bool late,
        Func<HubAdmit, HubAdmit>? lee = null, Func<HubAdmit, HubAdmit>? max = null)
    {
        var h = TogetherHarness(
            lee: (lee ?? (x => x))(Player(101, "lee") with { Friends = friends ? [102] : [] }),
            max: (max ?? (x => x))(Player(102, "max") with { Friends = friends ? [101] : [] }));
        Plan(h, "lee", 1, "stag0101");
        Plan(h, "max", 1, "stag0101");
        Choose(h, "lee", 1, ChoiceKind.New, visibility: first);
        Choose(h, "max", 1, ChoiceKind.New, visibility: second);
        Heading(h, "lee", "stag0101");
        if (!late) Heading(h, "max", "stag0101");
        h.Advance(300);
        BarrierPlan(h, "lee", "stag0101", 1);
        h.Advance(late ? 2000 : 400);
        if (late) Heading(h, "max", "stag0101");
        BarrierPlan(h, "max", "stag0101", 1);
        if (!h.P("max").Has(Msg.BarrierOk))
        {
            // (max joins lee's starting instance at its entrance once its stage group is joinable, 19.7.6.)
            h.Snapshot("lee");
            h.Log("lee", 0, 60);
            h.Joinable("lee");
        }
        Assert.True(h.P("lee").Has(Msg.BarrierOk) && h.P("max").Has(Msg.BarrierOk));
        var wl = W(h, "lee");
        var wm = W(h, "max");
        return (ReferenceEquals(wl, wm), wl.Visibility, wm.Visibility);
    }

    public static TheoryData<Visibility, Visibility, bool> ChoicePairs()
    {
        var data = new TheoryData<Visibility, Visibility, bool>();
        foreach (var a in new[] { Visibility.Public, Visibility.Friends, Visibility.Private })
            foreach (var b in new[] { Visibility.Public, Visibility.Friends, Visibility.Private })
                foreach (var friends in new[] { false, true })
                    data.Add(a, b, friends);
        return data;
    }

    [Theory]
    [MemberData(nameof(ChoicePairs))]
    public void Two_new_instance_choosers_land_together_exactly_when_both_choices_let_the_other_in_whichever_barrier_comes_first(Visibility a, Visibility b,
        bool friends)
    {
        var expected = Admits(a, friends) && Admits(b, friends);
        foreach (var late in new[] { false, true })
        {
            // a's barrier first, then b's; and the other order.
            var ab = LeaveTogether(a, b, friends, late);
            var ba = LeaveTogether(b, a, friends, late);
            var path = late ? "starting instance" : "co-arrival hold";
            Assert.True(expected == ab.Together, $"{a} first, {b} second, friends {friends}, {path}: together {ab.Together}");
            Assert.True(expected == ba.Together, $"{b} first, {a} second, friends {friends}, {path}: together {ba.Together}");
            // (Together: the first one's instance with his visibility; apart: each one's own instance with his own choice.)
            Assert.Equal((a, expected ? a : b), (ab.Lee, ab.Max));
            Assert.Equal((b, expected ? b : a), (ba.Lee, ba.Max));
        }
    }

    [Fact]
    public void A_private_new_instance_chooser_never_lands_in_a_strangers_public_new_instance_whichever_barrier_comes_first()
    {
        // The review's case (finding 3 of the fix stage): lee chose New instance Public, max (a stranger) New instance Private, the same
        // gate. Before decision 48, lee's barrier first made max the co-arrival of lee's base: a guest on lee's Public story.
        var h = TogetherHarness();
        Plan(h, "lee", 1, "stag0101");
        Plan(h, "max", 1, "stag0101");
        Choose(h, "lee", 1, ChoiceKind.New, visibility: Visibility.Public);
        Choose(h, "max", 1, ChoiceKind.New, visibility: Visibility.Private);
        Heading(h, "lee", "stag0101");
        Heading(h, "max", "stag0101");
        h.Advance(300);
        BarrierPlan(h, "lee", "stag0101", 1);
        // (lee's base holds for nobody: max's choice would never let lee in.)
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance, Visibility.Public),
            (h.Decision("lee").Decision, h.Decision("lee").World.Arrival, h.Decision("lee").World.Visibility));
        Assert.False(h.P("lee").Has(Msg.BarrierWait));
        h.Advance(400);
        BarrierPlan(h, "max", "stag0101", 1);
        var okMax = h.Decision("max");
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance, Visibility.Private), (okMax.Decision, okMax.World.Arrival, okMax.World.Visibility));
        Assert.NotSame(W(h, "lee"), W(h, "max"));
        Assert.Equal([h.Mem("max")], W(h, "max").Seats.Keys);
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("new_choice_starting_refused")); // (lee's starting instance: not for max)
        Assert.Equal(0, h.Hub.Counters.GetValueOrDefault("new_choice_starting"));

        // The other order gives the same: max's Private base holds for nobody, lee never sees max's instance.
        Assert.False(LeaveTogether(Visibility.Private, Visibility.Public, friends: false, late: false).Together);
        Assert.False(LeaveTogether(Visibility.Public, Visibility.Private, friends: false, late: true).Together);
        Assert.False(LeaveTogether(Visibility.Private, Visibility.Public, friends: false, late: true).Together);
    }

    [Fact]
    public void A_base_that_held_for_a_member_who_then_chose_private_stops_waiting_for_him()
    {
        // lee's base holds for max (max had no answer yet at lee's barrier); max then chooses New instance Private: max is not lee's
        // co-arrival, lands in his own Private instance, and lee's hold ends at the next housekeeping pass, not after CoArrivalHoldMs.
        var h = TogetherHarness();
        Plan(h, "lee", 1, "stag0101");
        Plan(h, "max", 1, "stag0101");
        Choose(h, "lee", 1, ChoiceKind.New, visibility: Visibility.Public);
        Heading(h, "lee", "stag0101");
        Heading(h, "max", "stag0101");
        h.Advance(300);
        BarrierPlan(h, "lee", "stag0101", 1);
        Assert.Equal([h.Mem("max")], h.Mem("lee").Arrival!.Awaited);
        Choose(h, "max", 1, ChoiceKind.New, visibility: Visibility.Private);
        h.Advance(200);
        BarrierPlan(h, "max", "stag0101", 1);
        Assert.Equal((BarrierDecision.New, Visibility.Private), (h.Decision("max").Decision, h.Decision("max").World.Visibility));
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("co_arrival_refused_choice"));
        Assert.Empty(h.Mem("lee").Arrival!.Awaited);
        h.Advance(100);
        h.Housekeep();
        Assert.Equal((BarrierDecision.New, Visibility.Public), (h.Decision("lee").Decision, h.Decision("lee").World.Visibility));
        Assert.NotSame(W(h, "lee"), W(h, "max"));
    }

    [Fact]
    public void Plan_new_instance_without_a_plan_keeps_a_remembered_private_default_apart_in_both_orders()
    {
        // Plan 0xFFFF (the client's New instance without a gate form) chooses New instance with the remembered default visibility:
        // max's is Private, so he is no co-arrival of lee's Public base (lee's base stops waiting for him) and joins no starting instance.
        foreach (var maxFirst in new[] { false, true })
        {
            var h = TogetherHarness(max: Player(102, "max") with { DefaultVisibility = Visibility.Private });
            Plan(h, "lee", 1, "stag0101");
            Choose(h, "lee", 1, ChoiceKind.New, visibility: Visibility.Public);
            Heading(h, "lee", "stag0101");
            Heading(h, "max", "stag0101");
            h.Advance(300);
            if (maxFirst) BarrierPlan(h, "max", "stag0101", BarrierMsg.PlanNewInstance);
            else BarrierPlan(h, "lee", "stag0101", 1);
            h.Advance(400);
            if (maxFirst) BarrierPlan(h, "lee", "stag0101", 1);
            else BarrierPlan(h, "max", "stag0101", BarrierMsg.PlanNewInstance);
            h.Advance(100);
            h.Housekeep();
            Assert.True(h.P("lee").Has(Msg.BarrierOk) && h.P("max").Has(Msg.BarrierOk), $"max first {maxFirst}");
            Assert.NotSame(W(h, "lee"), W(h, "max"));
            Assert.Equal((Visibility.Public, Visibility.Private), (W(h, "lee").Visibility, W(h, "max").Visibility));
        }
    }

    [Fact]
    public void Friends_choosers_who_are_friends_but_blocked_land_apart_in_both_orders()
    {
        // ("Not blocked either way": a block, either direction, keeps even two Public choosers apart.)
        foreach (var v in new[] { Visibility.Friends, Visibility.Public })
            foreach (var late in new[] { false, true })
            {
                Assert.False(LeaveTogether(v, v, friends: true, late, lee: x => x with { Blocks = [102] }, max: x => x with { BlockedBy = [101] }).Together);
                Assert.False(LeaveTogether(v, v, friends: true, late, lee: x => x with { BlockedBy = [102] }, max: x => x with { Blocks = [101] }).Together);
            }
    }

    [Fact]
    public void A_private_or_friends_base_takes_no_co_arrival_its_own_visibility_does_not_let_in_with_or_without_a_choice()
    {
        // The base's side (MaySeeHost / MaySeeBase with the base's chosen visibility, Visible() for its starting instance) held before
        // decision 48: lee's Friends base holds for his friend c and not for max, a stranger without a choice; max's barrier is no
        // co-arrival of it (he gets the loading-screen prompt), c's is.
        var h = TogetherHarness(lee: Player(101, "lee") with { Friends = [103] });
        Admit(h, "c", Player(103, "c") with { Friends = [101] });
        Plan(h, "lee", 1, "stag0101");
        Choose(h, "lee", 1, ChoiceKind.New, visibility: Visibility.Friends);
        Heading(h, "lee", "stag0101");
        Heading(h, "max", "stag0101");
        Heading(h, "c", "stag0101");
        h.Advance(300);
        BarrierPlan(h, "lee", "stag0101", 1);
        Assert.Equal([h.Mem("c")], h.Mem("lee").Arrival!.Awaited);
        h.Advance(200);
        BarrierPlan(h, "max", "stag0101", 0);
        Assert.Equal(HoldKind.Prompt, h.Mem("max").Arrival!.Hold);
        BarrierPlan(h, "c", "stag0101", 0);
        Assert.Equal((BarrierDecision.CoArrive, Visibility.Friends), (h.Decision("c").Decision, h.Decision("c").World.Visibility));
        Assert.Same(W(h, "lee"), W(h, "c"));
        Assert.DoesNotContain(h.Mem("max"), W(h, "lee").Seats.Keys);
        Choose(h, "max", 0, ChoiceKind.New, visibility: Visibility.Public, inst: h.Mem("max").Arrival!.From.Id, visit: h.Mem("max").Arrival!.From.Visit);
        Assert.NotSame(W(h, "lee"), W(h, "max"));

        // A Private base waits for nobody, not even a friend; its starting instance takes no later arrival, with or without a choice.
        var g = TogetherHarness(lee: Player(101, "lee") with { Friends = [102] }, max: Player(102, "max") with { Friends = [101] });
        Plan(g, "lee", 1, "stag0101");
        Choose(g, "lee", 1, ChoiceKind.New, visibility: Visibility.Private);
        Heading(g, "lee", "stag0101");
        Heading(g, "max", "stag0101");
        g.Advance(300);
        BarrierPlan(g, "lee", "stag0101", 1);
        Assert.Equal((BarrierDecision.New, Visibility.Private), (g.Decision("lee").Decision, g.Decision("lee").World.Visibility));
        g.Advance(1000);
        BarrierPlan(g, "max", "stag0101", 0);
        Assert.Equal(HoldKind.Prompt, g.Mem("max").Arrival!.Hold);
        Assert.DoesNotContain(LastOptions(g, "max").Rows, r => r.Wid == W(g, "lee").Wid);
    }

    [Fact]
    public void A_private_instances_stage_group_that_leaves_through_one_barrier_stays_together()
    {
        // The members of one stage group leaving through the same barrier are one arrival group: decision 48 never splits them (the
        // instance is Private; its guest b came in while it was Public).
        var h = SealedPair(out _);
        var w = W(h, "a");
        Assert.Same(w, W(h, "b"));
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Visibility, (uint)Visibility.Private).Frame());
        Assert.Equal(Visibility.Private, w.Visibility);
        h.Barrier("a", "stag0202");
        h.Barrier("b", "stag0202");
        var okA = h.Decision("a");
        var okB = h.Decision("b");
        Assert.Equal((BarrierDecision.New, BarrierDecision.New), (okA.Decision, okB.Decision));
        Assert.Equal(okA.NewInst, okB.NewInst);
        Assert.Same(h.I("a"), h.I("b"));
        Assert.Same(w, W(h, "a"));
        Assert.Same(w, W(h, "b"));
        Assert.Equal(0, h.Hub.Counters.GetValueOrDefault("co_arrival_refused_choice"));
    }
}
