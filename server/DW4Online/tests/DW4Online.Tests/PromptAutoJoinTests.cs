using DW4Online.Domain.Rooms;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// Decision 49 on round I1a's automatic join (fix-d43; the user's answer of 2026-11-14: "the remembered New-instance visibility also
/// governs arrivals without a choice: whoever remembered Private never lands with someone else, not even a friend, Friends only with
/// friends; the own stage group stays together"). An arrival without a prompt (World:ArrivalPrompt off, I1a's configuration; with it
/// on, a fresh arrival whose departure stage is a field) and without an answer took the row the prompt would preselect
/// (<see cref="Matching.AutoJoinRow"/>): a stranger's running instance with players at the arrival point, whatever the arrival
/// remembered. Now only an instance whose host its remembered visibility lets in (<c>RoomHub.ChoiceAdmits</c>); a refused one is as if
/// it did not exist for the arrival (the preselection among the others, else a new instance with the remembered visibility).
/// </summary>
public partial class HubLogicTests
{
    /// <summary>The I1a configuration: no prompt, the automatic join.</summary>
    private static Harness AutoJoinHarness() => new(world: new WorldHubOptions { ArrivalPrompt = false });

    /// <summary>
    /// s (remembered Public) hosts a running instance at stag0101 (not starting any more) and stands there; p (remembered
    /// <paramref name="v"/>; friends with s or not, a block or not) takes the lobby gate to stag0101 without a prompt and without an
    /// answer (BARRIER plan 0). Returns whether p landed in s's instance, p's BARRIER_OK and the harness.
    /// </summary>
    private static (bool Together, BarrierOkMsg P, Harness H) LateComer(Visibility v, bool friends, PairBlock block)
    {
        var h = AutoJoinHarness();
        var (s, p) = Pair(v, friends, block);
        Admit(h, "s", s);
        Admit(h, "p", p);
        HostJoinable(h, "s", "stag0101");
        Assert.Equal(0, W(h, "s").StartingUntil);
        BarrierPlan(h, "p", "stag0101", 0);
        Assert.True(h.P("p").Has(Msg.BarrierOk), $"p {h.Mem("p").Arrival?.Hold}");
        return (ReferenceEquals(W(h, "s"), W(h, "p")), h.Decision("p"), h);
    }

    [Theory]
    [MemberData(nameof(RememberedCases))]
    public void Without_a_prompt_a_late_comer_joins_a_running_instance_with_players_at_the_arrival_point_only_where_his_remembered_visibility_lets_its_host_in(
        Visibility v, bool friends, PairBlock block)
    {
        var expected = Lands(v, friends, block);
        var r = LateComer(v, friends, block);
        Assert.True(expected == r.Together, $"{v}, friends {friends}, block {block}: together {r.Together}");
        if (expected)
            Assert.Equal((BarrierDecision.Join, ArrivalKind.AutoJoin), (r.P.Decision, r.P.World.Arrival));
        else
            Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance, v), (r.P.Decision, r.P.World.Arrival, r.P.World.Visibility));
        // (The refusal by the remembered visibility is counted; a block keeps the row out already, Visible().)
        Assert.Equal(!expected && block == PairBlock.None ? 1 : 0, r.H.Hub.Counters.GetValueOrDefault("auto_join_refused_remembered"));
        Assert.Equal(0, r.H.Hub.Counters.GetValueOrDefault("starting_refused_remembered"));
    }

    [Fact]
    public void Without_a_prompt_a_remembered_private_friend_is_no_guest_of_a_friends_instance_elsewhere_in_the_area_and_friends_still_is()
    {
        // k's instance visited stag0101 and plays in stag0102 now: a row at stag0101 with nobody of it there, entered as a guest's NEW
        // (19.30.4), preselected only for the host's friend (decision 40). p, k's friend, takes the gate to stag0101 without a prompt.
        foreach (var v in new[] { Visibility.Private, Visibility.Friends, Visibility.Public })
        {
            var h = AutoJoinHarness();
            Admit(h, "k", Player(99, "k") with { Friends = [101] });
            Admit(h, "p", Player(101, "p") with { DefaultVisibility = v, Friends = [99] });
            BarrierPlan(h, "k", "stag0101", BarrierMsg.PlanNewInstance);
            h.Snapshot("k");
            h.Log("k", 0, 60);
            h.Joinable("k");
            BarrierWith(h, "k", "stag0102");
            h.Snapshot("k");
            h.Advance(11_000);
            h.Housekeep();
            var wk = W(h, "k");
            Assert.Contains("stag0101", wk.Visited);
            BarrierPlan(h, "p", "stag0101", 0);
            var ok = h.Decision("p");
            if (v == Visibility.Private)
            {
                // Decision 49: Private lands with nobody else, not even a friend: p's own Private instance.
                Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance, Visibility.Private), (ok.Decision, ok.World.Arrival, ok.World.Visibility));
                Assert.NotSame(wk, W(h, "p"));
                Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("auto_join_refused_remembered"));
            }
            else
            {
                // Friends lets a friend's instance in, Public any: the guest's NEW as before.
                Assert.Equal((BarrierDecision.New, ArrivalKind.GuestNew, wk.Wid), (ok.Decision, ok.World.Arrival, ok.World.Wid));
                Assert.Same(wk, W(h, "p"));
            }
        }
    }

    [Fact]
    public void Without_a_prompt_a_remembered_friends_late_comer_joins_no_strangers_instance_a_friend_plays_in()
    {
        // s hosts at stag0101 with f, p's friend, as a guest there (friends inside: the row ranks with the friends', 19.7.3); s is no
        // friend of p's, so p's remembered Friends refuses s's instance (the host decides, as for decision 48's choice).
        var h = AutoJoinHarness();
        Admit(h, "s", Player(100, "s"));
        Admit(h, "f", Player(102, "f") with { Friends = [101] });
        Admit(h, "p", Player(101, "p") with { DefaultVisibility = Visibility.Friends, Friends = [102] });
        HostJoinable(h, "s", "stag0101");
        BarrierPlan(h, "f", "stag0101", 0);
        Assert.Same(W(h, "s"), W(h, "f"));
        h.Advance(1000);
        h.Housekeep();
        BarrierPlan(h, "p", "stag0101", 0);
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance, Visibility.Friends),
            (h.Decision("p").Decision, h.Decision("p").World.Arrival, h.Decision("p").World.Visibility));
        Assert.NotSame(W(h, "s"), W(h, "p"));
        // (Remembered Public: the same row is joined as before.)
        var g = AutoJoinHarness();
        Admit(g, "s", Player(100, "s"));
        Admit(g, "f", Player(102, "f") with { Friends = [101] });
        Admit(g, "p", Player(101, "p") with { Friends = [102] });
        HostJoinable(g, "s", "stag0101");
        BarrierPlan(g, "f", "stag0101", 0);
        g.Advance(1000);
        g.Housekeep();
        BarrierPlan(g, "p", "stag0101", 0);
        Assert.Equal((BarrierDecision.Join, ArrivalKind.AutoJoin), (g.Decision("p").Decision, g.Decision("p").World.Arrival));
        Assert.Same(W(g, "s"), W(g, "p"));
    }

    [Fact]
    public void With_the_prompt_a_late_comer_at_the_lobby_gate_still_gets_the_form_and_his_explicit_row_choice_joins()
    {
        // The shipped configuration: a remembered Private player at the gate while a stranger's instance runs there with its player at
        // the arrival point gets the form (the row preselected, decision 40); a row he chooses himself is joined (rows chosen explicitly
        // stay as they were, decision 49). No automatic landing on this path.
        var h = PairHarness(Visibility.Private, friends: false, PairBlock.None);
        HostJoinable(h, "s", "stag0101");
        Plan(h, "p", 1, "stag0101");
        var o = LastOptions(h, "p");
        Assert.Equal(((byte)0, (byte)0), (o.Flags, o.Preselected));
        Choose(h, "p", 1, ChoiceKind.Join, o.Rows[0].Wid);
        BarrierPlan(h, "p", "stag0101", 1);
        Assert.Equal((BarrierDecision.Join, W(h, "s").Wid), (h.Decision("p").Decision, h.Decision("p").World.Wid));
        Assert.Equal(0, h.Hub.Counters.GetValueOrDefault("auto_join_refused_remembered"));
    }
}
