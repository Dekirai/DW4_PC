using DW4Online.Domain.Rooms;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// Round I1b's review of the prompt lane: a chosen row is never lost to a stranger's co-arrival, a base holds for and takes no member
/// that chose, CHOICE names only offered rows (no host's name of any other instance, no flood at a held barrier), a full instance of
/// another context is no row, a friend's row in a short scene keeps its place before a stranger's, PLAN's throttle tolerates the
/// network's jitter, and a row greyed meanwhile for another reason than *Full* gets no misleading line.
/// </summary>
public partial class HubLogicTests
{
    private static void Heading(Harness h, string name, string stage, ulong context = 5) =>
        h.Send(name, new HeadingMsg(h.Mem(name).Instance!.Id, stage, 0, context).Frame());

    [Fact]
    public void A_member_that_chose_a_row_holds_for_no_stranger_and_joins_the_row_it_chose()
    {
        // k's instance is Friends only; lee (k's friend) chooses it at the gate; max, a stranger whose PLAN found nothing (k's instance is
        // not his to see), heads for the same stage. Lee's barrier never holds for max: lee joins k, max gets his own instance.
        var h = PromptHarness();
        Admit(h, "k", Player(100, "k") with { DefaultVisibility = Visibility.Friends });
        Admit(h, "lee", Player(101, "lee") with { Friends = [100] });
        Admit(h, "max", Player(102, "max"));
        HostJoinable(h, "k", "stag0101");
        var wk = W(h, "k");
        Plan(h, "lee", 1, "stag0101");
        Assert.Equal((wk.Wid, (byte)0), (Assert.Single(LastOptions(h, "lee").Rows).Wid, LastOptions(h, "lee").Preselected));
        Choose(h, "lee", 1, ChoiceKind.Join, wk.Wid);
        h.Advance(500);
        Plan(h, "max", 1, "stag0101");
        Assert.Equal(OptionsMsg.Nothing, LastOptions(h, "max").Flags);
        Heading(h, "max", "stag0101");
        h.Advance(1500);
        BarrierPlan(h, "lee", "stag0101", 1);
        Assert.False(h.P("lee").Has(Msg.BarrierWait) && h.P("lee").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason == WaitReason.CoArrival);
        var ok = h.Decision("lee");
        Assert.Equal((BarrierDecision.Join, ArrivalKind.AutoJoin, wk.Wid), (ok.Decision, ok.World.Arrival, ok.World.Wid));
        Assert.Same(wk, W(h, "lee"));
        h.Advance(1000);
        BarrierPlan(h, "max", "stag0101", 1);
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (h.Decision("max").Decision, h.Decision("max").World.Arrival));
        Assert.NotSame(wk, W(h, "max"));
        Assert.Equal(2, wk.Seats.Count);
        Assert.Equal(0, h.Hub.Counters.GetValueOrDefault("co_arrival_base_chose_row"));

        // The same with k's instance public and max without a plan (a lost PLAN): lee joins k at once; max's own barrier then gets the
        // loading-screen form (k's row).
        var g = PromptHarness();
        Admit(g, "k", Player(100, "k"));
        Admit(g, "lee", Player(101, "lee"));
        Admit(g, "max", Player(102, "max"));
        HostJoinable(g, "k", "stag0101");
        var wk2 = W(g, "k");
        Plan(g, "lee", 1, "stag0101");
        Choose(g, "lee", 1, ChoiceKind.Join, wk2.Wid);
        Heading(g, "max", "stag0101");
        g.Advance(1500);
        BarrierPlan(g, "lee", "stag0101", 1);
        Assert.Equal((BarrierDecision.Join, wk2.Wid), (g.Decision("lee").Decision, g.Decision("lee").World.Wid));
        g.Advance(1000);
        BarrierPlan(g, "max", "stag0101", 0);
        Assert.Equal(HoldKind.Prompt, g.Mem("max").Arrival!.Hold);
        Assert.Equal(wk2.Wid, Assert.Single(LastOptions(g, "max").Rows).Wid);
    }

    [Fact]
    public void A_base_holds_for_no_member_that_chose_a_row_but_for_a_new_instance_chooser()
    {
        // k's instance plays in dng101b1: a gate PLAN to stag0101 lists it (nobody here: the cursor on New instance). lee chooses k's
        // row; x and y head for stag0101 without a plan. x's base holds only for y; lee's barrier is never x's co-arrival.
        var h = PromptHarness();
        Admit(h, "k", Player(100, "k") with { Story = StoryAt(4) }); // (Goblin Pass's gate opened: a guest may enter at stag0101)
        Admit(h, "lee", Player(101, "lee"));
        Admit(h, "x", Player(102, "x"));
        Admit(h, "y", Player(103, "y"));
        HostJoinable(h, "k", "dng101b1");
        var wk = W(h, "k");
        Plan(h, "lee", 1, "stag0101");
        Assert.Equal(OptionsMsg.NewInstance, LastOptions(h, "lee").Preselected);
        Choose(h, "lee", 1, ChoiceKind.Join, wk.Wid);
        foreach (var n in new[] { "lee", "x", "y" }) Heading(h, n, "stag0101");
        h.Advance(300);
        h.Barrier("x", "stag0101");
        var x = h.Mem("x").Arrival!;
        Assert.Equal(HoldKind.CoArrival, x.Hold);
        Assert.Equal([h.Mem("y")], x.Awaited);
        BarrierPlan(h, "lee", "stag0101", 1);
        var ok = h.Decision("lee");
        Assert.Equal((BarrierDecision.New, ArrivalKind.GuestNew, wk.Wid), (ok.Decision, ok.World.Arrival, ok.World.Wid));
        Assert.Empty(x.CoArrivals);
        h.Barrier("y", "stag0101");
        Assert.Same(W(h, "x"), W(h, "y"));
        Assert.NotSame(W(h, "x"), W(h, "lee"));

        // Decision 43 (2026-11-11): lee chooses *New instance* instead: x's base holds for him as well and takes him as its co-arrival;
        // the three land in x's instance (x's visibility; lee's Friends is only remembered).
        var g = PromptHarness();
        Admit(g, "k", Player(100, "k"));
        Admit(g, "lee", Player(101, "lee"));
        Admit(g, "x", Player(102, "x"));
        Admit(g, "y", Player(103, "y"));
        HostJoinable(g, "k", "dng101b1");
        Plan(g, "lee", 1, "stag0101");
        Choose(g, "lee", 1, ChoiceKind.New, visibility: Visibility.Friends);
        foreach (var n in new[] { "lee", "x", "y" }) Heading(g, n, "stag0101");
        g.Advance(300);
        g.Barrier("x", "stag0101");
        var gx = g.Mem("x").Arrival!;
        Assert.Equal(HoldKind.CoArrival, gx.Hold);
        Assert.Equal([g.Mem("lee"), g.Mem("y")], gx.Awaited.OrderBy(m => m.Id));
        BarrierPlan(g, "lee", "stag0101", 1);
        Assert.False(g.P("lee").Has(Msg.BarrierOk));
        g.Barrier("y", "stag0101");
        var okX = g.Decision("x");
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance, Visibility.Public, 2), (okX.Decision, okX.World.Arrival, okX.World.Visibility, okX.CoArrivals.Count));
        Assert.Equal(BarrierDecision.CoArrive, g.Decision("lee").Decision);
        Assert.Same(W(g, "x"), W(g, "lee"));
        Assert.Same(W(g, "x"), W(g, "y"));
        Assert.Equal(Visibility.Friends, g.Mem("lee").DefaultVisibility);
    }

    [Fact]
    public void A_choice_of_a_row_never_offered_is_ignored_and_names_no_host()
    {
        // a's instance is private, z blocks b: b's PLAN finds nothing. A CHOICE naming a's or z's instance anyway is counted and ignored:
        // b's barrier makes b's own instance, and no OPTIONS ever names a or z.
        var h = PromptHarness();
        Admit(h, "a", Player(100, "secret_host") with { DefaultVisibility = Visibility.Private });
        Admit(h, "z", Player(103, "blocker") with { Blocks = [101] });
        Admit(h, "b", Player(101, "b"));
        HostJoinable(h, "a", "stag0101");
        HostJoinable(h, "z", "dng101b1");
        var (wa, wz) = (W(h, "a"), W(h, "z"));
        Plan(h, "b", 7, "stag0101");
        Assert.Equal(OptionsMsg.Nothing, LastOptions(h, "b").Flags);
        Choose(h, "b", 7, ChoiceKind.Join, wa.Wid);
        Assert.False(h.Mem("b").Prompt.ChoseRow);
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("choice_unoffered"));
        BarrierPlan(h, "b", "stag0101", 7);
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (h.Decision("b").Decision, h.Decision("b").World.Arrival));
        Assert.DoesNotContain(h.P("b").All(Msg.Options, OptionsMsg.Parse), o => o.NoticeName.Length > 0 || o.Rows.Count > 0);

        // At a held barrier (c's public instance offered): a's and z's wids are ignored as well, without an OPTIONS.
        var g = PromptHarness();
        Admit(g, "a", Player(100, "secret_host") with { DefaultVisibility = Visibility.Private });
        Admit(g, "z", Player(103, "blocker") with { Blocks = [101] });
        Admit(g, "c", Player(102, "c"));
        Admit(g, "b", Player(101, "b"));
        HostJoinable(g, "a", "stag0101");
        HostJoinable(g, "z", "dng101b1");
        HostJoinable(g, "c", "stag0102");
        BarrierPlan(g, "b", "stag0101", 0);
        var held = g.Mem("b").Arrival!;
        Assert.Equal(HoldKind.Prompt, held.Hold);
        Assert.Equal(W(g, "c").Wid, Assert.Single(LastOptions(g, "b").Rows).Wid);
        var sent = g.P("b").All(Msg.Options, OptionsMsg.Parse).Count;
        Choose(g, "b", 0, ChoiceKind.Join, W(g, "a").Wid, inst: held.From.Id, visit: held.From.Visit);
        Choose(g, "b", 0, ChoiceKind.Join, W(g, "z").Wid, inst: held.From.Id, visit: held.From.Visit);
        Assert.Equal(sent, g.P("b").All(Msg.Options, OptionsMsg.Parse).Count);
        Assert.Equal(HoldKind.Prompt, held.Hold);
        Assert.Equal(2, g.Hub.Counters.GetValueOrDefault("choice_unoffered"));
        Assert.DoesNotContain(g.P("b").All(Msg.Options, OptionsMsg.Parse), o => o.NoticeName is "secret_host" or "blocker");
    }

    [Fact]
    public void A_held_prompt_takes_one_choice_a_second_and_keeps_the_newest_for_its_turn()
    {
        // c's instance offered at b's held barrier, its host reconnecting: every CHOICE of it is refused (the row greyed). Fifty at once
        // re-check the candidates once; the newest waits a second, then is taken (one more refreshed prompt). The refused row is still
        // listed greyed: no "not open to you" line, its word says why.
        var h = PromptHarness();
        Admit(h, "c", Player(102, "c"));
        Admit(h, "b", Player(101, "b"));
        HostJoinable(h, "c", "stag0101");
        var wc = W(h, "c");
        BarrierPlan(h, "b", "stag0101", 0);
        var held = h.Mem("b").Arrival!;
        Assert.Equal(HoldKind.Prompt, held.Hold);
        h.Hub.Disconnect(h.Mem("c"), h.P("c"), h.Now);
        Assert.True(wc.HostAway);
        var sent = h.P("b").All(Msg.Options, OptionsMsg.Parse).Count;
        for (var k = 0; k < 51; ++k) Choose(h, "b", 0, ChoiceKind.Join, wc.Wid, inst: held.From.Id, visit: held.From.Visit);
        Assert.Equal(sent + 1, h.P("b").All(Msg.Options, OptionsMsg.Parse).Count);
        Assert.Equal(50, h.Hub.Counters.GetValueOrDefault("choice_throttled"));
        var o = LastOptions(h, "b");
        Assert.Equal(((byte)0, "", OptionState.HostAway, OptionsMsg.NewInstance), (o.Notice, o.NoticeName, Assert.Single(o.Rows).State, o.Preselected));
        Assert.Equal(HoldKind.Prompt, held.Hold);
        h.Advance(500);
        h.Hub.Housekeep(h.Now);
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("choice_refused"));
        h.Advance(600);
        h.Hub.Housekeep(h.Now);
        Assert.Equal(2, h.Hub.Counters.GetValueOrDefault("choice_refused"));
        Assert.Null(held.PendingChoice);
        // A quick second answer (New instance) right after a refused one is not lost: taken a second later.
        Choose(h, "b", 0, ChoiceKind.New, inst: held.From.Id, visit: held.From.Visit);
        Assert.False(h.P("b").Has(Msg.BarrierOk));
        h.Advance(1_000);
        h.Hub.Housekeep(h.Now);
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (h.Decision("b").Decision, h.Decision("b").World.Arrival));
    }

    [Fact]
    public void A_full_instance_of_another_session_context_is_no_row()
    {
        // a's instance at stag0101 is full; b is on a quest (flag 409: another session context): nothing to choose, no prompt.
        var h = PromptHarness();
        Admit(h, "a", Player(100, "a"));
        foreach (var (n, id) in new[] { ("c", 102L), ("d", 103L), ("e", 104L) }) Admit(h, n, Player(id, n));
        Admit(h, "b", Player(101, "b"));
        HostJoinable(h, "a", "stag0101");
        var wa = W(h, "a");
        ushort k = 1;
        foreach (var n in new[] { "c", "d", "e" })
        {
            Plan(h, n, k, "stag0101");
            Choose(h, n, k, ChoiceKind.Join, wa.Wid);
            BarrierPlan(h, n, "stag0101", k);
            ++k;
        }
        Assert.Equal(4, wa.Seats.Count);
        Plan(h, "b", 1, "stag0101", context: 0x409);
        var o = LastOptions(h, "b");
        Assert.Equal((OptionsMsg.Nothing, 0), (o.Flags, o.Rows.Count));
        var home = h.Mem("b").Instance!;
        h.Send("b", new BarrierMsg(home.Id, home.Visit, 0, "stag0101", 0, home.PresentMask, home.PresentMask, 0x409, [1], 0, 1).Frame());
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (h.Decision("b").Decision, h.Decision("b").World.Arrival));
        // The same context sees it greyed, Full.
        Admit(h, "f", Player(105, "f"));
        h.Advance(1_000);
        Plan(h, "f", 1, "stag0101");
        var full = LastOptions(h, "f");
        Assert.Equal((OptionState.Full, OptionsMsg.NewInstance), (full.Rows.Single(r => r.Wid == wa.Wid).State, full.Preselected));
    }

    [Fact]
    public void A_friends_row_in_a_short_scene_keeps_its_place_before_a_strangers_and_a_boss_fight_goes_after_them()
    {
        var h = PromptHarness();
        Admit(h, "s", Player(100, "stranger"));
        Admit(h, "f", Player(101, "friend"));
        Admit(h, "b", Player(102, "b") with { Friends = [101] });
        HostJoinable(h, "s", "stag0101");
        HostJoinable(h, "f", "stag0101");
        var (ws, wf) = (W(h, "s"), W(h, "f"));
        h.Joinable("f", joinable: false); // (an entry scene, a talk: a few seconds not joinable)
        Plan(h, "b", 1, "stag0101");
        var o = LastOptions(h, "b");
        Assert.Equal(new[] { wf.Wid, ws.Wid }, o.Rows.Select(r => r.Wid).ToArray());
        Assert.Equal((OptionState.Loading, (byte)0), (o.Rows[0].State, o.Preselected));
        // The friend's stage group in a boss fight: after every joinable row (choosing it waits for the fight's end).
        var i = h.I("f");
        h.Send("f", new StateMsg(i.Id, i.Visit, 0, StateMsg.BossLock, 5, StateReason.Boss).Frame());
        h.Advance(1_000);
        Plan(h, "b", 2, "stag0101");
        o = LastOptions(h, "b");
        Assert.Equal(new[] { ws.Wid, wf.Wid }, o.Rows.Select(r => r.Wid).ToArray());
        Assert.Equal((OptionState.BossFight, (byte)0), (o.Rows[1].State, o.Preselected)); // (the stranger's row: players here)
        // Equal rows: the one joinable now before the one in a scene.
        var rows = Matching.Order([
            new CandidateRow(wf, h.I("f"), RowState.Waiting, 1, false, 0, 30),
            new CandidateRow(ws, h.I("s"), RowState.Joinable, 1, false, 0, 30),
        ]);
        Assert.Equal(new[] { ws.Wid, wf.Wid }, rows.Select(r => r.World.Wid).ToArray());
    }

    [Fact]
    public void A_withdrawn_plan_is_closed_and_refreshed_no_more()
    {
        // The client withdraws a plan whose menu closed otherwise (CHOICE back with the plan): the relay closes it, and a change of
        // the candidates sends no refresh for the overlay that is gone.
        var h = PromptHarness();
        Admit(h, "a", Player(100, "a"));
        Admit(h, "b", Player(101, "b"));
        Admit(h, "c", Player(102, "c"));
        HostJoinable(h, "a", "stag0101");
        Plan(h, "b", 5, "stag0101");
        Assert.True(h.Mem("b").Prompt.Offered);
        Choose(h, "b", 5, ChoiceKind.Back);
        Assert.Equal((0, 1L), (h.Mem("b").Prompt.Plan, h.Hub.Counters.GetValueOrDefault("plan_closed")));
        var sent = h.P("b").All(Msg.Options, OptionsMsg.Parse).Count;
        HostJoinable(h, "c", "stag0102"); // (a second row for b's plan, were it open)
        h.Advance(1_100);
        h.Hub.Housekeep(h.Now);
        Assert.Equal(sent, h.P("b").All(Msg.Options, OptionsMsg.Parse).Count);
    }

    [Fact]
    public void Plan_s_throttle_tolerates_the_networks_jitter()
    {
        // The client spaces its PLANs a second apart by its own clock; one that arrives 950 ms after the first is answered.
        var h = PromptHarness();
        Admit(h, "a", Player(100, "a"));
        Plan(h, "a", 1, "stag0101");
        h.Advance(500);
        Plan(h, "a", 2, "stag0101");
        h.Advance(450);
        Plan(h, "a", 3, "stag0101");
        Assert.Equal(new ushort[] { 1, 3 }, h.P("a").All(Msg.Options, OptionsMsg.Parse).Select(o => o.Plan).ToArray());
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("plan_throttled"));
    }
}
