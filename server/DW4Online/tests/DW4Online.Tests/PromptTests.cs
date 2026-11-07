using DW4Online.Domain.Rooms;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// Round I1b, the arrival prompt on the hub without sockets (SEAMLESS.md 19.7): PLAN answered by OPTIONS (nothing to choose, a starting
/// instance, rows in decision 40's order with its preselection), the throttle and the test hook that drops plans, CHOICE in the gate form
/// carried by the BARRIER's plan id and checked again at the decision, the loading-screen form's hold without a time limit with its
/// refresh, *Back to the lobby* and the hold forgotten at a disconnect.
/// </summary>
public partial class HubLogicTests
{
    private static Harness PromptHarness() => new(world: new WorldHubOptions());

    private static void Plan(Harness h, string name, ushort plan, string stage, byte point = 0, ulong context = 5) =>
        h.Send(name, new PlanMsg(plan, PlanPurpose.Gate, stage, point, context, 1).Frame());

    private static void Choose(Harness h, string name, ushort plan, ChoiceKind choice, uint wid = 0, Visibility visibility = Visibility.Public,
        uint inst = 0, uint visit = 0, byte back = ChoiceMsg.DefaultBackPoint) =>
        h.Send(name, new ChoiceMsg(plan, inst, visit, choice, wid, visibility, 0, back).Frame());

    private static void BarrierPlan(Harness h, string name, string stage, ushort plan, RoomSettings? settings = null)
    {
        var i = h.Mem(name).Instance!;
        h.Send(name, new BarrierMsg(i.Id, i.Visit, 0, stage, 0, i.PresentMask, i.PresentMask, 5, [(byte)name[0]], 0, plan, settings).Frame());
    }

    private static OptionsMsg LastOptions(Harness h, string name) => h.P(name).Last(Msg.Options, OptionsMsg.Parse);

    /// <summary>a hosts a public instance at <paramref name="stage"/> whose stage group is joinable, and it no longer counts as starting.</summary>
    private static void HostJoinable(Harness h, string host, string stage)
    {
        BarrierPlan(h, host, stage, BarrierMsg.PlanNewInstance); // (New instance: a host never takes another's row here)
        h.Snapshot(host);
        h.Log(host, 0, 60);
        h.Joinable(host);
        h.Advance(11_000);
        h.Hub.Housekeep(h.Now);
    }

    [Fact]
    public void A_plan_without_candidates_says_nothing_to_choose_and_its_barrier_makes_a_new_instance()
    {
        var h = PromptHarness();
        Admit(h, "a", Player(100, "a"));
        Plan(h, "a", 7, "stag0101");
        var o = LastOptions(h, "a");
        Assert.Equal((7, OptionsMsg.Nothing, 0, OptionsMsg.NewInstance), (o.Plan, o.Flags, o.Rows.Count, o.Preselected));
        BarrierPlan(h, "a", "stag0101", 7);
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (h.Decision("a").Decision, h.Decision("a").World.Arrival));
        Assert.False(h.P("a").Has(Msg.BarrierWait));
    }

    [Fact]
    public void W1_the_prompt_preselects_an_instance_with_players_here_and_the_choice_joins_it_at_the_entrance()
    {
        var h = PromptHarness();
        Admit(h, "a", Player(100, "a"));
        Admit(h, "b", Player(101, "b"));
        HostJoinable(h, "a", "stag0101");
        var w = W(h, "a");
        Plan(h, "b", 3, "stag0101");
        var o = LastOptions(h, "b");
        Assert.Equal((3, (byte)0, 1, (byte)0), (o.Plan, o.Flags, o.Rows.Count, o.Preselected));
        var row = o.Rows[0];
        Assert.Equal((w.Wid, h.Mem("a").Id, "a", "stag0101", (byte)1, (byte)1, OptionState.Joinable, OptionRow.Hidden, (byte)0),
            (row.Wid, row.Host, row.HostName, row.HostStage, row.Players, row.Here, row.State, row.Visibility, row.Relation));
        Assert.Equal("a", Assert.Single(row.Members).Name);
        // A on the row: CHOICE goes before the held press; the BARRIER names the plan.
        Choose(h, "b", 3, ChoiceKind.Join, w.Wid);
        Assert.True(h.Mem("b").Prompt.Chose);
        BarrierPlan(h, "b", "stag0101", 3);
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.Join, ArrivalKind.AutoJoin, StoryMode.Replace, w.Wid), (ok.Decision, ok.World.Arrival, ok.World.StoryMode, ok.World.Wid));
        Assert.Same(w, W(h, "b"));
        Assert.Equal(0, h.Mem("b").Prompt.Plan); // (the plan is done)
    }

    [Fact]
    public void Decision_40_rows_with_players_here_first_the_cursor_on_them_and_a_strangers_row_elsewhere_is_never_preselected()
    {
        // W30c (without friends): a's Dry Land instance plays in dng205b1, c's instance has c at stag0201; b, a stranger to both, plans
        // the gate to stag0201.
        var h = PromptHarness();
        Admit(h, "a", Player(100, "a") with { Story = StoryAt(4) });
        Admit(h, "c", Player(102, "c") with { Story = StoryAt(4) });
        Admit(h, "b", Player(101, "b"));
        BarrierWith(h, "a", "stag0201");
        h.Snapshot("a");
        BarrierWith(h, "a", "dng205b1");
        h.Snapshot("a");
        h.Advance(11_000);
        h.Hub.Housekeep(h.Now);
        HostJoinable(h, "c", "stag0201");
        var (wa, wc) = (W(h, "a"), W(h, "c"));
        Assert.NotSame(wa, wc); // (c was a lone arrival with a's row elsewhere: c's own, as c's prompt would have offered)
        Plan(h, "b", 1, "stag0201");
        var o = LastOptions(h, "b");
        Assert.Equal(new uint[] { wc.Wid, wa.Wid }, o.Rows.Select(r => r.Wid).ToArray());
        Assert.Equal(((byte)1, "dng205b1", (byte)0), (o.Rows[0].Here, o.Rows[1].HostStage, o.Rows[1].Here));
        Assert.Equal((byte)0, o.Preselected); // c's row: players here
        Assert.Equal("Chapter 4", o.Rows[1].StoryLabel);
        // c goes home: its instance ends. b's next plan lists a's row with the cursor on New instance.
        BarrierWith(h, "c", "stagML11");
        h.Advance(1_100);
        Plan(h, "b", 2, "stag0201");
        o = LastOptions(h, "b");
        Assert.Equal((wa.Wid, OptionsMsg.NewInstance), (Assert.Single(o.Rows).Wid, o.Preselected));
        // A on a's row still enters b at stag0201 as a guest in a new stage group of a's instance (decision 40).
        Choose(h, "b", 2, ChoiceKind.Join, wa.Wid);
        BarrierPlan(h, "b", "stag0201", 2);
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.New, ArrivalKind.GuestNew, StoryMode.Replace, wa.Wid), (ok.Decision, ok.World.Arrival, ok.World.StoryMode, ok.World.Wid));
        Assert.Equal("stag0201", ok.Stage);
    }

    [Fact]
    public void Without_an_answer_the_barrier_waits_for_the_prompt_without_a_time_limit_and_back_takes_it_into_the_lobby()
    {
        var h = PromptHarness();
        Admit(h, "a", Player(100, "a"));
        Admit(h, "b", Player(101, "b"));
        HostJoinable(h, "a", "stag0101");
        BarrierWith(h, "b", "stagML11"); // (b stands in its lobby: its hub stage group's stage)
        var home = h.Mem("b").Instance!;
        Assert.Equal("stagML11", home.Stage);
        var oks = h.P("b").Frames.Count(f => f.Type == Msg.BarrierOk);
        BarrierPlan(h, "b", "stag0101", 0);
        var wait = h.P("b").Last(Msg.BarrierWait, BarrierWaitMsg.Parse);
        Assert.Equal(WaitReason.Choice, wait.Reason);
        var o = LastOptions(h, "b");
        Assert.Equal((0, home.Id, home.Visit, (byte)0), (o.Plan, o.Inst, o.Visit, o.Preselected));
        Assert.Equal(HoldKind.Prompt, h.Mem("b").Arrival!.Hold);
        // No time limit: minutes later it still waits, and OPTIONS is not sent again while nothing changed.
        var sent = h.P("b").All(Msg.Options, OptionsMsg.Parse).Count;
        for (var k = 0; k < 120; ++k)
        {
            h.Advance(1_000);
            h.Hub.Housekeep(h.Now);
        }
        Assert.Equal(oks, h.P("b").Frames.Count(f => f.Type == Msg.BarrierOk));
        Assert.Equal(sent, h.P("b").All(Msg.Options, OptionsMsg.Parse).Count);
        // B: Back to the lobby: a DISSOLVE redirected to stagML11 point 3, on its own story (merge: it never left it).
        Choose(h, "b", 0, ChoiceKind.Back, inst: home.Id, visit: home.Visit);
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.Dissolve, "stagML11", (byte)3, StoryMode.Merge, 0u), (ok.Decision, ok.World.RedirectStage, ok.World.RedirectPoint,
            ok.World.StoryMode, ok.World.Wid));
        Assert.Null(h.Mem("b").World);
        Assert.Single(W(h, "a").Seats);
    }

    [Fact]
    public void The_held_prompt_answers_join_new_with_its_visibility_and_is_refreshed_once_a_second_on_a_change()
    {
        var h = PromptHarness();
        var remembered = new List<(long, Visibility)>();
        h.Hub.RememberVisibility = (account, v) => remembered.Add((account, v));
        Admit(h, "a", Player(100, "a"));
        Admit(h, "b", Player(101, "b"));
        Admit(h, "c", Player(102, "c"));
        HostJoinable(h, "a", "stag0101");
        BarrierPlan(h, "b", "stag0101", 0);
        var first = h.P("b").All(Msg.Options, OptionsMsg.Parse).Count;
        // c joins a's instance at stag0101: a's row now has two players here; within the second nothing goes, then one refresh.
        Plan(h, "c", 1, "stag0101");
        Choose(h, "c", 1, ChoiceKind.Join, W(h, "a").Wid);
        BarrierPlan(h, "c", "stag0101", 1);
        h.Advance(200);
        h.Hub.Housekeep(h.Now);
        Assert.Equal(first, h.P("b").All(Msg.Options, OptionsMsg.Parse).Count);
        h.Advance(1_000);
        h.Hub.Housekeep(h.Now);
        var o = LastOptions(h, "b");
        Assert.Equal(first + 1, h.P("b").All(Msg.Options, OptionsMsg.Parse).Count);
        Assert.Equal((OptionsMsg.Refresh, (byte)2), (o.Flags, o.Rows[0].Players));
        // New instance, Friends: b's own instance with that visibility, remembered.
        var home = h.Mem("b").Arrival!.From;
        Choose(h, "b", 0, ChoiceKind.New, visibility: Visibility.Friends, inst: home.Id, visit: home.Visit);
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance, Visibility.Friends), (ok.Decision, ok.World.Arrival, ok.World.Visibility));
        Assert.NotSame(W(h, "a"), W(h, "b"));
        Assert.Equal((101L, Visibility.Friends), Assert.Single(remembered));
    }

    [Fact]
    public void A_chosen_row_that_ended_meanwhile_holds_the_barrier_with_a_refreshed_prompt_naming_it()
    {
        var h = PromptHarness();
        Admit(h, "a", Player(100, "a"));
        Admit(h, "b", Player(101, "b"));
        Admit(h, "c", Player(102, "c"));
        HostJoinable(h, "a", "stag0101");
        HostJoinable(h, "c", "dng101b1"); // (c's instance in Death Valley elsewhere: a second row for b)
        Plan(h, "b", 9, "stag0101");
        Assert.Equal(2, LastOptions(h, "b").Rows.Count);
        Choose(h, "b", 9, ChoiceKind.Join, W(h, "a").Wid);
        // a goes home before b's barrier: a's instance ends.
        var wid = W(h, "a").Wid;
        BarrierWith(h, "a", "stagML11");
        BarrierPlan(h, "b", "stag0101", 9);
        Assert.False(h.P("b").Has(Msg.BarrierOk));
        Assert.Equal(WaitReason.Choice, h.P("b").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason);
        var o = LastOptions(h, "b");
        Assert.Equal((OptionsMsg.NoticeEnded, "a"), (o.Notice, o.NoticeName));
        Assert.DoesNotContain(o.Rows, r => r.Wid == wid);
        Assert.Equal(OptionsMsg.NewInstance, o.Preselected); // (c's row: a stranger elsewhere)
    }

    [Fact]
    public void A_starting_instance_takes_a_planned_arrival_without_a_prompt()
    {
        var h = PromptHarness();
        Admit(h, "a", Player(100, "a"));
        Admit(h, "b", Player(101, "b"));
        BarrierWith(h, "a", "stag0101");
        h.Snapshot("a");
        Plan(h, "b", 4, "stag0101");
        Assert.Equal((OptionsMsg.Starting, 0), (LastOptions(h, "b").Flags, LastOptions(h, "b").Rows.Count));
        BarrierPlan(h, "b", "stag0101", 4);
        Assert.Same(W(h, "a"), W(h, "b"));
        Assert.False(h.P("b").Has(Msg.BarrierWait) && h.P("b").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason == WaitReason.Choice);
    }

    [Fact]
    public void Plan_is_throttled_to_one_a_second_and_the_test_hook_drops_every_plan()
    {
        var h = PromptHarness();
        Admit(h, "a", Player(100, "a"));
        Plan(h, "a", 1, "stag0101");
        Plan(h, "a", 2, "stag0101");
        Assert.Equal(new ushort[] { 1 }, h.P("a").All(Msg.Options, OptionsMsg.Parse).Select(o => o.Plan).ToArray());
        h.Advance(1_000);
        Plan(h, "a", 3, "stag0101");
        Assert.Equal(3, LastOptions(h, "a").Plan);
        var g = new Harness(world: new WorldHubOptions { TestDropPlans = true });
        Admit(g, "a", Player(100, "a"));
        Plan(g, "a", 1, "stag0101");
        Assert.False(g.P("a").Has(Msg.Options));
    }

    [Fact]
    public void A_held_prompt_is_forgotten_at_a_disconnect_and_a_members_choice_keeps_it_out_of_a_foreign_base()
    {
        var h = PromptHarness();
        Admit(h, "a", Player(100, "a"));
        Admit(h, "b", Player(101, "b"));
        HostJoinable(h, "a", "stag0101");
        BarrierPlan(h, "b", "stag0101", 0);
        Assert.NotNull(h.Mem("b").Arrival);
        h.Hub.Disconnect(h.Mem("b"), h.P("b"), h.Now);
        Assert.Null(h.Mem("b").Arrival);
        // Staff see the row's visibility; players never.
        var s = PromptHarness();
        Admit(s, "a", Player(100, "a") with { DefaultVisibility = Visibility.Friends });
        Admit(s, "m", Player(103, "m") with { Staff = true });
        HostJoinable(s, "a", "stag0101");
        Plan(s, "m", 1, "stag0101");
        Assert.Equal((byte)Visibility.Friends, LastOptions(s, "m").Rows[0].Visibility);
    }

    [Fact]
    public void The_census_no_static_edge_leads_from_a_hub_stage_into_a_field()
    {
        // 19.7.4's census: every departure from home into a joinable stage is a computed NEXTSTAGE of stagML11's mate00 (the lobby
        // gate, quests included) or mate02 (the return mat), which Engine::pending_departure_choice names (dw4_world_probe census);
        // the graph of all stage scripts has no other edge out of the hub stages.
        using var doc = System.Text.Json.JsonDocument.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "data", "stage_graph.json")));
        var out_of_home = doc.RootElement.GetProperty("edges").EnumerateArray()
            .Select(e => (From: e.GetProperty("from").GetString()!, To: e.GetProperty("to").GetString()!, Script: e.GetProperty("script").GetString()!))
            .Where(e => StageNames.IsHub(e.From) && !StageNames.IsHub(e.To)).ToList();
        Assert.Empty(out_of_home);
        Assert.Contains(doc.RootElement.GetProperty("edges").EnumerateArray(), e => e.GetProperty("from").GetString() == "stagml11");
    }

    [Fact]
    public void Prompt_messages_have_the_agreed_bytes()
    {
        // (online_wire_tests.cpp holds the same bytes: the client's encoder and decoder.)
        Assert.Equal("0700" + "01" + "08" + "7374616730323031" + "00" + "0500000000000000" + "01",
            Convert.ToHexString(new PlanMsg(7, PlanPurpose.Gate, "stag0201", 0, 5, 1).Frame()[Wire.HeaderBytes..]).ToLowerInvariant());
        Assert.Equal("0300" + "00000000" + "00000000" + "01" + "09000000" + "00" + "00" + "ff",
            Convert.ToHexString(new ChoiceMsg(3, 0, 0, ChoiceKind.Join, 9, Visibility.Public, 0, 0xFF).Frame()[Wire.HeaderBytes..]).ToLowerInvariant());
        var options = new OptionsMsg(3, 0, 0, "stag0201", 0, Visibility.Friends, 0, 38, 0, "", [
            new OptionRow(9, 5, "kim", OptionRow.FriendHost, 1, "dng205b1", 2, 0, 2, 0, 0, "Chapter 4, Hard", 0, 1, OptionRow.Randomizer,
                OptionState.Joinable, OptionRow.Hidden, 41, [new OptionMember("kim", 23, 4)]),
        ]);
        Assert.Equal("0300" + "00000000" + "00000000" + "08" + "7374616730323031" + "00" + "01" + "00" + "2600" + "00" + "00" + "01" +
                     "09000000" + "05000000" + "03" + "6b696d" + "02" + "01" + "08" + "646e673230356231" + "02" + "00" + "02" + "00" + "00" +
                     "0f" + "436861707465722034" + "2c2048617264" + "00" + "01" + "01" + "00" + "ff" + "2900" + "01" + "03" + "6b696d" + "1700" + "04",
            Convert.ToHexString(options.Frame()[Wire.HeaderBytes..]).ToLowerInvariant());
        var back = OptionsMsg.Parse(options.Frame()[Wire.HeaderBytes..]);
        Assert.Equal(options.Rows[0] with { Members = back.Rows[0].Members }, back.Rows[0]);
        Assert.Throws<ProtocolException>(() => OptionsMsg.Parse(new OptionsMsg(3, 0, 0, "stag0201", 0, Visibility.Public, 2, 0, 0, "", []).Frame()[Wire.HeaderBytes..]));
        Assert.Throws<ProtocolException>(() => PlanMsg.Parse(new PlanMsg(0, PlanPurpose.Gate, "stag0201", 0, 5, 1).Frame()[Wire.HeaderBytes..]));
    }
}
