using DW4Online.Domain.Rooms;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// Decision 49 (the user's answer of 2026-11-14; SEAMLESS.md 19.7.6): decision 48 covers an explicit *New instance* choice; without any
/// choice the remembered default visibility decides the same way. (a) A player at the gate while a stranger's instance is starting gets
/// OPTIONS "starting" (no form, the automatic landing) only where his remembered visibility lets that host in (Public any, Friends a
/// friend's, Private nobody's); otherwise the starting instance is as if it did not exist for him: the form for other rows, else a new
/// instance with his remembered visibility. (b) An arrival without any answer (a plan never answered, a CHOICE the relay never got, a
/// plan id unknown after a reconnect) is the co-arrival of a base or joins a starting instance only where its remembered visibility lets
/// that host in. Unchanged: the members of one stage group leaving through the same barrier, travel inside an instance, explicit rows.
/// And the guard against a stale admission: an admission that read the account before the relay's newest remembered visibility was
/// written keeps the relay's value.
/// </summary>
public partial class HubLogicTests
{
    /// <summary>A block between the two players of a pair, if any, and who blocks.</summary>
    public enum PairBlock { None, ByPlayer, ByHost }

    /// <summary>
    /// s (remembered Public) and p (remembered <paramref name="v"/>), friends or not, a block or not. p's admission and s's, account
    /// 100 (s) and 101 (p).
    /// </summary>
    private static (HubAdmit S, HubAdmit P) Pair(Visibility v, bool friends, PairBlock block) =>
        (Player(100, "s") with
        {
            Friends = friends ? [101] : [], Blocks = block == PairBlock.ByHost ? [101] : [], BlockedBy = block == PairBlock.ByPlayer ? [101] : [],
        },
         Player(101, "p") with
         {
             DefaultVisibility = v, Friends = friends ? [100] : [], Blocks = block == PairBlock.ByPlayer ? [100] : [],
             BlockedBy = block == PairBlock.ByHost ? [100] : [],
         });

    /// <summary>The harness with s and p at home (and k's Public instance in dng101b1 when <paramref name="row"/>: a row at stag0101).</summary>
    private static Harness PairHarness(Visibility v, bool friends, PairBlock block, bool row = false)
    {
        var h = PromptHarness();
        if (row)
        {
            Admit(h, "k", Player(99, "k"));
            HostJoinable(h, "k", "dng101b1");
        }
        var (s, p) = Pair(v, friends, block);
        Admit(h, "s", s);
        Admit(h, "p", p);
        return h;
    }

    /// <summary>The second one's barrier came: when it waits for the first's starting stage group (a JOIN at its entrance), that group
    /// gets joinable; then both have BARRIER_OK.</summary>
    private static void Settle(Harness h, string first, string second)
    {
        if (!h.P(second).Has(Msg.BarrierOk) && h.Mem(second).Arrival is { Hold: HoldKind.Join })
        {
            h.Snapshot(first);
            h.Log(first, 0, 60);
            h.Joinable(first);
        }
        h.Advance(100);
        h.Housekeep();
    }

    public static TheoryData<Visibility, bool, PairBlock> RememberedCases()
    {
        var data = new TheoryData<Visibility, bool, PairBlock>();
        foreach (var v in new[] { Visibility.Public, Visibility.Friends, Visibility.Private })
            foreach (var friends in new[] { false, true })
                foreach (var block in new[] { PairBlock.None, PairBlock.ByPlayer, PairBlock.ByHost })
                    data.Add(v, friends, block);
        return data;
    }

    /// <summary>Does the remembered <paramref name="v"/> let the other one in (and no block)?</summary>
    private static bool Lands(Visibility v, bool friends, PairBlock block) => block == PairBlock.None && Admits(v, friends);

    /// <summary>
    /// (a), the gate form's OPTIONS: the first one's barrier (plan 0xFFFF, his remembered default) makes a new instance, which is starting
    /// when the second one PLANs at the same gate a moment later; the second's BARRIER then names that plan without a CHOICE (the client
    /// sends no CHOICE after "starting" or "nothing"). <paramref name="playerFirst"/>: p's instance is the starting one (s at the gate),
    /// else s's (p at the gate). Returns whether they share an instance, the second's OPTIONS, and each one's instance visibility.
    /// </summary>
    private static (bool Together, OptionsMsg Options, Visibility S, Visibility P) StartingAtTheGate(Visibility v, bool friends, PairBlock block,
        bool playerFirst, bool row = false)
    {
        var h = PairHarness(v, friends, block, row);
        var (first, second) = playerFirst ? ("p", "s") : ("s", "p");
        BarrierPlan(h, first, "stag0101", BarrierMsg.PlanNewInstance);
        Assert.Equal(BarrierDecision.New, h.Decision(first).Decision);
        Assert.NotEqual(0L, W(h, first).StartingUntil);
        h.Advance(2000);
        Plan(h, second, 4, "stag0101");
        var options = LastOptions(h, second);
        if (options.Rows.Count > 0) return (false, options, W(h, "s").Visibility, Visibility.Public); // (the form: an explicit choice follows)
        h.Advance(1500);
        BarrierPlan(h, second, "stag0101", 4);
        Settle(h, first, second);
        Assert.True(h.P("s").Has(Msg.BarrierOk) && h.P("p").Has(Msg.BarrierOk), $"s {h.Mem("s").Arrival?.Hold} p {h.Mem("p").Arrival?.Hold}");
        return (ReferenceEquals(W(h, "s"), W(h, "p")), options, W(h, "s").Visibility, W(h, "p").Visibility);
    }

    [Theory]
    [MemberData(nameof(RememberedCases))]
    public void A_starting_instance_takes_a_player_at_the_gate_only_where_his_remembered_visibility_lets_its_host_in_in_both_orders(Visibility v,
        bool friends, PairBlock block)
    {
        var expected = Lands(v, friends, block);
        foreach (var playerFirst in new[] { false, true })
        {
            var order = playerFirst ? "p's instance starting, s at the gate" : "s's instance starting, p at the gate";
            var r = StartingAtTheGate(v, friends, block, playerFirst);
            Assert.True(expected == r.Together, $"{v}, friends {friends}, block {block}, {order}: together {r.Together}");
            // ("starting": no form, the press goes out; else nothing to choose: a new instance of its own with the remembered visibility.)
            Assert.Equal(expected ? OptionsMsg.Starting : OptionsMsg.Nothing, r.Options.Flags);
            Assert.Empty(r.Options.Rows);
            Assert.Equal((expected && playerFirst ? v : Visibility.Public, expected && !playerFirst ? Visibility.Public : v), (r.S, r.P));
        }
    }

    [Theory]
    [MemberData(nameof(RememberedCases))]
    public void A_refused_starting_instance_is_no_row_of_the_gate_form_and_the_other_rows_still_get_the_form(Visibility v, bool friends,
        PairBlock block)
    {
        // k's instance plays in dng101b1 (a row at stag0101 with nobody there: not preselected). s's instance is starting at stag0101.
        var r = StartingAtTheGate(v, friends, block, playerFirst: false, row: true);
        if (Lands(v, friends, block))
        {
            Assert.Equal(OptionsMsg.Starting, r.Options.Flags);
            Assert.True(r.Together);
            return;
        }
        // (As if s's instance did not exist: the form with k's row alone, the cursor on *New instance* with p's remembered visibility.)
        Assert.Equal((byte)0, r.Options.Flags);
        Assert.Equal("k", Assert.Single(r.Options.Rows).HostName);
        Assert.Equal((OptionsMsg.NewInstance, v), (r.Options.Preselected, r.Options.Visibility));
    }

    /// <summary>
    /// (b), an arrival without any answer: s and p take the gate to stag0101 with nothing running there, neither answers a prompt (no
    /// PLAN at all: BARRIER plan 0, or a plan id the relay does not know). <paramref name="hold"/>: both HEADINGs before the first's
    /// barrier (the first is a co-arrival base that may hold for the second), else the second's HEADING after it (the first's new
    /// instance is starting when the second arrives). <paramref name="playerFirst"/>: p's barrier first.
    /// </summary>
    private static (bool Together, BarrierOkMsg Second, Visibility S, Visibility P, Harness H) WithoutAnAnswer(Visibility v, bool friends,
        PairBlock block, bool hold, bool playerFirst, ushort plan = 0)
    {
        var h = PairHarness(v, friends, block);
        var (first, second) = playerFirst ? ("p", "s") : ("s", "p");
        Heading(h, first, "stag0101");
        if (hold) Heading(h, second, "stag0101");
        h.Advance(300);
        BarrierPlan(h, first, "stag0101", plan);
        h.Advance(hold ? 400 : 2000);
        if (!hold) Heading(h, second, "stag0101");
        BarrierPlan(h, second, "stag0101", plan);
        Settle(h, first, second);
        Assert.True(h.P("s").Has(Msg.BarrierOk) && h.P("p").Has(Msg.BarrierOk), $"s {h.Mem("s").Arrival?.Hold} p {h.Mem("p").Arrival?.Hold}");
        return (ReferenceEquals(W(h, "s"), W(h, "p")), h.Decision(second), W(h, "s").Visibility, W(h, "p").Visibility, h);
    }

    [Theory]
    [MemberData(nameof(RememberedCases))]
    public void An_arrival_without_an_answer_lands_with_another_players_base_or_starting_instance_only_where_its_remembered_visibility_lets_the_host_in(
        Visibility v, bool friends, PairBlock block)
    {
        var expected = Lands(v, friends, block);
        foreach (var hold in new[] { true, false })
            foreach (var playerFirst in new[] { false, true })
            {
                var path = (hold ? "co-arrival hold" : "starting instance") + (playerFirst ? ", p's barrier first" : ", s's barrier first");
                var r = WithoutAnAnswer(v, friends, block, hold, playerFirst);
                Assert.True(expected == r.Together, $"{v}, friends {friends}, block {block}, {path}: together {r.Together}");
                if (expected)
                    Assert.Equal(hold ? BarrierDecision.CoArrive : BarrierDecision.Join, r.Second.Decision);
                else
                    Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (r.Second.Decision, r.Second.World.Arrival));
                // (Together: the first one's instance with his remembered visibility; apart: each one's own with his own.)
                Assert.Equal((expected && playerFirst ? v : Visibility.Public, expected && !playerFirst ? Visibility.Public : v), (r.S, r.P));
            }
    }

    [Fact]
    public void A_remembered_private_arrival_without_an_answer_refuses_a_strangers_base_and_its_starting_instance_with_the_world_log_lines()
    {
        // s's base (no answer, Public) would hold for p, but p's remembered Private lets nobody else in (not even a friend, the matrix): no
        // hold for p; s's instance starts at once; p's barrier (no answer) refuses it and makes p's own Private instance.
        var r = WithoutAnAnswer(Visibility.Private, friends: false, PairBlock.None, hold: true, playerFirst: false);
        Assert.False(r.Together);
        Assert.False(r.H.P("s").Has(Msg.BarrierWait)); // (s's base waited for nobody)
        Assert.Equal(1, r.H.Hub.Counters.GetValueOrDefault("starting_refused_remembered"));
        Assert.Equal(0, r.H.Hub.Counters.GetValueOrDefault("new_choice_starting_refused")); // (decision 48's counter: an explicit choice)
        // p's barrier first: p's Private base holds for nobody; s never sees p's instance.
        var o = WithoutAnAnswer(Visibility.Private, friends: false, PairBlock.None, hold: true, playerFirst: true);
        Assert.False(o.Together);
        Assert.False(o.H.P("p").Has(Msg.BarrierWait));
        Assert.Equal(Visibility.Private, o.P);
    }

    [Fact]
    public void A_base_holds_for_a_remembered_friends_arrival_only_when_its_host_is_his_friend_and_a_refusing_base_is_none_for_him()
    {
        // s's base (Public, no answer) and p (remembered Friends, no answer): strangers: no hold, no co-arrival; s's friend c (remembered
        // Friends too) is awaited and comes as s's co-arrival.
        var h = PairHarness(Visibility.Friends, friends: false, PairBlock.None);
        Admit(h, "c", Player(102, "c") with { DefaultVisibility = Visibility.Friends, Friends = [100] });
        h.Mem("s").Friends = new HashSet<long> { 102 };
        Heading(h, "s", "stag0101");
        Heading(h, "p", "stag0101");
        Heading(h, "c", "stag0101");
        h.Advance(300);
        BarrierPlan(h, "s", "stag0101", 0);
        Assert.Equal([h.Mem("c")], h.Mem("s").Arrival!.Awaited);
        h.Advance(200);
        BarrierPlan(h, "p", "stag0101", 0);
        Assert.Equal((BarrierDecision.New, Visibility.Friends), (h.Decision("p").Decision, h.Decision("p").World.Visibility));
        BarrierPlan(h, "c", "stag0101", 0);
        Assert.Equal(BarrierDecision.CoArrive, h.Decision("c").Decision);
        Assert.Same(W(h, "s"), W(h, "c"));
        Assert.NotSame(W(h, "s"), W(h, "p"));
    }

    [Fact]
    public void A_plan_never_answered_a_lost_choice_and_a_plan_id_unknown_after_a_reconnect_are_arrivals_without_an_answer()
    {
        // s's Public instance is starting at stag0101; p (remembered Private, a stranger) PLANs: nothing to choose (s's instance is not
        // his). Its BARRIER names the plan: its own Private instance.
        var h = PairHarness(Visibility.Private, friends: false, PairBlock.None);
        BarrierPlan(h, "s", "stag0101", BarrierMsg.PlanNewInstance);
        h.Advance(1000);
        Plan(h, "p", 1, "stag0101");
        Assert.Equal(OptionsMsg.Nothing, LastOptions(h, "p").Flags);
        // The link drops before the BARRIER (the plan had no answer: it ends at the disconnect) and comes back: the BARRIER names a plan
        // id the relay no longer knows.
        h.Hub.Disconnect(h.Mem("p"), h.P("p"), h.Now);
        Assert.Equal((ushort)0, h.Mem("p").Prompt.Plan);
        h.Advance(400);
        Admit(h, "p", Pair(Visibility.Private, false, PairBlock.None).P);
        BarrierPlan(h, "p", "stag0101", 1);
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance, Visibility.Private),
            (h.Decision("p").Decision, h.Decision("p").World.Arrival, h.Decision("p").World.Visibility));
        Assert.NotSame(W(h, "s"), W(h, "p"));

        // A CHOICE the relay never got: k's row gives p (now q, a fresh stranger with remembered Private) the gate form; its CHOICE is lost
        // and the BARRIER names the plan: the loading-screen form with k's row, never s's starting instance (no automatic join).
        var g = PairHarness(Visibility.Private, friends: false, PairBlock.None, row: true);
        BarrierPlan(g, "s", "stag0101", BarrierMsg.PlanNewInstance);
        g.Advance(1000);
        Plan(g, "p", 1, "stag0101");
        Assert.Equal("k", Assert.Single(LastOptions(g, "p").Rows).HostName);
        g.Advance(1000);
        BarrierPlan(g, "p", "stag0101", 1);
        Assert.Equal(HoldKind.Prompt, g.Mem("p").Arrival!.Hold);
        Assert.Equal("k", Assert.Single(LastOptions(g, "p").Rows).HostName);
        Assert.Null(g.Mem("p").World);
        // The refresh keeps s's instance out while it starts (the same rule), and lists it once it runs (a row like any other).
        g.Advance(1100);
        g.Housekeep();
        Assert.DoesNotContain(LastOptions(g, "p").Rows, x => x.HostName == "s");
        g.Snapshot("s");
        g.Log("s", 0, 60);
        g.Joinable("s");
        g.Advance(1100);
        g.Housekeep();
        Assert.Equal(0, W(g, "s").StartingUntil);
        Assert.Contains(LastOptions(g, "p").Rows, x => x.HostName == "s");
    }

    [Fact]
    public void An_open_gate_form_gets_no_refused_starting_instance_by_its_refresh_while_it_starts()
    {
        // p (remembered Private) has the gate form open with k's row when s's instance starts at stag0101: the refresh does not add s's
        // row (it would be the first row, with s here: preselected under a quick A).
        var h = PairHarness(Visibility.Private, friends: false, PairBlock.None, row: true);
        Plan(h, "p", 1, "stag0101");
        Assert.Equal("k", Assert.Single(LastOptions(h, "p").Rows).HostName);
        BarrierPlan(h, "s", "stag0101", BarrierMsg.PlanNewInstance);
        h.Advance(1100);
        h.Housekeep();
        Assert.Equal("k", Assert.Single(LastOptions(h, "p").Rows).HostName);
        // (A Public p sees it in the refresh as before: an instance that started after the form opened is a row to choose.)
        var g = PairHarness(Visibility.Public, friends: false, PairBlock.None, row: true);
        Plan(g, "p", 1, "stag0101");
        BarrierPlan(g, "s", "stag0101", BarrierMsg.PlanNewInstance);
        g.Advance(1100);
        g.Housekeep();
        Assert.Contains(LastOptions(g, "p").Rows, x => x.HostName == "s");
    }

    [Fact]
    public void Two_public_defaults_without_an_answer_still_arrive_together_and_plan_new_instance_still_takes_the_remembered_default()
    {
        // Answer 21: two strangers with the default Public who take the gate together land together (the co-arrival, and the starting
        // instance for a later one).
        foreach (var hold in new[] { true, false })
        {
            var r = WithoutAnAnswer(Visibility.Public, friends: false, PairBlock.None, hold, playerFirst: false);
            Assert.True(r.Together, $"hold {hold}");
            Assert.Equal(hold ? BarrierDecision.CoArrive : BarrierDecision.Join, r.Second.Decision);
        }
        // Plan 0xFFFF (decision 48's *New instance* with the remembered default): Public lands together, Private apart, both paths.
        foreach (var v in new[] { Visibility.Public, Visibility.Private })
            foreach (var hold in new[] { true, false })
            {
                var r = WithoutAnAnswer(v, friends: false, PairBlock.None, hold, playerFirst: false, plan: BarrierMsg.PlanNewInstance);
                Assert.True((v == Visibility.Public) == r.Together, $"plan 0xFFFF {v}, hold {hold}");
            }
    }

    [Fact]
    public void A_stage_group_of_remembered_private_players_that_leaves_through_one_barrier_stays_together()
    {
        // The members of one stage group are one arrival group: inside their instance no visibility counts (a, the host, and b, a
        // stranger, both remember Private now).
        var h = SealedPair(out _);
        var w = W(h, "a");
        h.Hub.Readmitted(100, Player(100, "a") with { DefaultVisibility = Visibility.Private }, h.Now);
        h.Hub.Readmitted(101, Player(101, "b") with { DefaultVisibility = Visibility.Private }, h.Now);
        Assert.Equal((Visibility.Private, Visibility.Private), (h.Mem("a").DefaultVisibility, h.Mem("b").DefaultVisibility));
        h.Barrier("a", "stag0202");
        h.Barrier("b", "stag0202");
        var okA = h.Decision("a");
        var okB = h.Decision("b");
        Assert.Equal((BarrierDecision.New, BarrierDecision.New), (okA.Decision, okB.Decision));
        Assert.Equal(okA.NewInst, okB.NewInst);
        Assert.Same(w, W(h, "a"));
        Assert.Same(w, W(h, "b"));
        Assert.Equal(0, h.Hub.Counters.GetValueOrDefault("starting_refused_remembered"));
        Assert.Equal(0, h.Hub.Counters.GetValueOrDefault("co_arrival_refused_remembered"));
    }

    [Fact]
    public void A_member_that_reconnects_while_its_stage_group_arrives_still_lands_with_it_whatever_it_remembers()
    {
        // W7e / W7f's race with a remembered Private: a's link goes as the sealed NEW of a and b (strangers) is decided; a comes back
        // (its admission now says Private) and re-sends its old barrier, which lies past b's sealed ticks: decided apart, a JOIN of b's
        // stage group in their instance (travel inside the instance: no visibility).
        var h = SealedPair(out _, stage: "stag0201");
        h.Joinable("a");
        h.Seal(6000);
        var tick = h.I("a").NextSeal - 1;
        h.Barrier("a", "stag0202", tick: tick);
        h.Barrier("b", "stag0202", tick: tick);
        var next = h.I("b");
        h.Hub.Disconnect(h.Mem("a"), h.P("a"), h.Now);
        h.Advance(2100);
        h.Housekeep();
        h.Snapshot("b");
        h.Joinable("b");
        h.Seal(1000);
        var pa = Admit(h, "a", Player(100, "a") with { DefaultVisibility = Visibility.Private });
        Assert.Equal(Visibility.Private, h.Mem("a").DefaultVisibility);
        Assert.Equal((next.Id, next.Visit), (pa.Last(Msg.Welcome, WelcomeMsg.Parse).Inst, pa.Last(Msg.Welcome, WelcomeMsg.Parse).Visit));
        h.Barrier("a", "stag0202", tick: tick + 1);
        var ok = h.Decision("a");
        Assert.Equal((BarrierDecision.Join, next.Id, next.Visit), (ok.Decision, ok.NewInst, ok.NewVisit));
        Assert.Same(W(h, "b"), W(h, "a"));
    }

    // The staleness guard ---------------------------------------------------------------------------------------------------------

    /// <summary>lee at home (stored default Public) chose *New instance* Private in a gate form and withdrew it (B): his remembered
    /// default is Private in memory, its write queued (not done). Returns the harness and the writes the relay queued.</summary>
    private static (Harness H, List<(long Account, Visibility V)> Writes, long ChoseAt) RememberedPrivate()
    {
        var h = PromptHarness();
        var writes = new List<(long, Visibility)>();
        h.Hub.RememberVisibility = (a, v) => writes.Add((a, v));
        Admit(h, "s", Player(100, "s"));
        Admit(h, "lee", Player(101, "lee"));
        Plan(h, "lee", 1, "stag0101");
        var at = h.Now;
        Choose(h, "lee", 1, ChoiceKind.New, visibility: Visibility.Private);
        Choose(h, "lee", 1, ChoiceKind.Back); // (B: the gate's menu closes; the remembered choice stays, Q9)
        Assert.Equal(Visibility.Private, h.Mem("lee").DefaultVisibility);
        Assert.Equal([(101L, Visibility.Private)], writes);
        return (h, writes, at);
    }

    [Fact]
    public void A_recheck_that_read_the_account_before_the_remembered_visibility_was_written_keeps_the_relays_value()
    {
        var (h, _, choseAt) = RememberedPrivate();
        // A re-check (a friend request: FriendsChanged) that read the account before the write: the stored Public. Before, it put Public
        // back in memory (fix-d43's "left" item 2).
        h.Advance(50);
        h.Hub.Readmitted(101, Player(101, "lee") with { ReadAt = choseAt + 1 }, h.Now);
        Assert.Equal(Visibility.Private, h.Mem("lee").DefaultVisibility);
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("admit_visibility_kept"));
        // The write is done; a re-check that began to read before that may still have read the old value: kept.
        h.Advance(50);
        var writtenAt = h.Now;
        h.Hub.VisibilityWritten(101, writtenAt);
        h.Advance(50);
        h.Hub.Readmitted(101, Player(101, "lee") with { ReadAt = writtenAt - 1 }, h.Now);
        Assert.Equal(Visibility.Private, h.Mem("lee").DefaultVisibility);
        // One that read after the write brings the written value or a newer one (the website's /account/friends: Friends): taken, and the
        // guard ends (a later stale read is the database's value again).
        h.Hub.Readmitted(101, Player(101, "lee") with { DefaultVisibility = Visibility.Friends, ReadAt = writtenAt + 1 }, h.Now);
        Assert.Equal(Visibility.Friends, h.Mem("lee").DefaultVisibility);
        h.Hub.Readmitted(101, Player(101, "lee") with { DefaultVisibility = Visibility.Public, ReadAt = h.Now }, h.Now);
        Assert.Equal(Visibility.Public, h.Mem("lee").DefaultVisibility);
        // (An admission without a read time is applied as it is, as before: the tests' direct re-checks, decision 48's included.)
    }

    [Fact]
    public void A_reconnect_or_a_return_to_the_world_whose_admission_read_before_the_write_keeps_the_remembered_visibility()
    {
        var (h, _, choseAt) = RememberedPrivate();
        h.Hub.Disconnect(h.Mem("lee"), h.P("lee"), h.Now);
        h.Advance(300);
        Admit(h, "lee", Player(101, "lee") with { ReadAt = choseAt + 1 });
        Assert.Equal(Visibility.Private, h.Mem("lee").DefaultVisibility);
        // He leaves the world (a new member object at his return): the guard is the account's.
        h.Hub.Leave(101, h.Now);
        Assert.Null(h.Hub.MemberOf(101));
        h.Advance(300);
        Admit(h, "lee", Player(101, "lee") with { ReadAt = choseAt + 2 });
        Assert.Equal(Visibility.Private, h.Mem("lee").DefaultVisibility);
        // The guard outlives no written value: done, a minute later, and the housekeeping drops it.
        h.Hub.VisibilityWritten(101, h.Now);
        h.Advance(RoomHub.RememberedGuardMs + 100);
        h.Housekeep();
        h.Hub.Readmitted(101, Player(101, "lee") with { ReadAt = choseAt + 3 }, h.Now);
        Assert.Equal(Visibility.Public, h.Mem("lee").DefaultVisibility);
    }

    [Fact]
    public void A_hosts_visibility_change_before_it_settled_is_kept_against_a_recheck_and_written_once_it_settled()
    {
        var h = PromptHarness();
        var writes = new List<(long, Visibility)>();
        h.Hub.RememberVisibility = (a, v) => writes.Add((a, v));
        Admit(h, "a", Player(100, "a"));
        HostJoinable(h, "a", "stag0101");
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Visibility, (uint)Visibility.Private).Frame());
        Assert.Equal(Visibility.Private, h.Mem("a").DefaultVisibility);
        Assert.Empty(writes); // (written once it settled)
        h.Advance(100);
        h.Hub.Readmitted(100, Player(100, "a") with { ReadAt = h.Now }, h.Now);
        Assert.Equal(Visibility.Private, h.Mem("a").DefaultVisibility);
        h.Advance(RoomHub.VisibilitySettleMs + 100);
        h.Housekeep();
        Assert.Equal([(100L, Visibility.Private)], writes);
        h.Hub.Readmitted(100, Player(100, "a") with { ReadAt = h.Now }, h.Now);
        Assert.Equal(Visibility.Private, h.Mem("a").DefaultVisibility); // (queued, not done)
        h.Hub.VisibilityWritten(100, h.Now);
        h.Advance(10);
        h.Hub.Readmitted(100, Player(100, "a") with { DefaultVisibility = Visibility.Private, ReadAt = h.Now }, h.Now);
        Assert.Equal(Visibility.Private, h.Mem("a").DefaultVisibility);
    }

    [Fact]
    public void A_stale_recheck_never_lets_a_remembered_private_player_land_in_a_strangers_starting_instance()
    {
        // Why the guard matters for decision 49: lee's remembered Private is only in memory (its write queued); a re-check that read
        // the account before puts nothing back now. s's Public instance starts at stag0101; lee takes the gate there without a form:
        // nothing to choose, his own Private instance (with the stale Public he got "starting" and became s's guest).
        var (h, _, choseAt) = RememberedPrivate();
        h.Hub.Readmitted(101, Player(101, "lee") with { ReadAt = choseAt + 1 }, h.Now);
        BarrierPlan(h, "s", "stag0101", BarrierMsg.PlanNewInstance);
        h.Advance(1000);
        Plan(h, "lee", 2, "stag0101");
        Assert.Equal(OptionsMsg.Nothing, LastOptions(h, "lee").Flags);
        h.Advance(1000);
        BarrierPlan(h, "lee", "stag0101", 2);
        Settle(h, "s", "lee");
        Assert.Equal((BarrierDecision.New, Visibility.Private), (h.Decision("lee").Decision, h.Decision("lee").World.Visibility));
        Assert.NotSame(W(h, "s"), W(h, "lee"));
    }
}
