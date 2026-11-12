using DW4Online.Domain.Rooms;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// The relay's rules for an arrival from home in the production configuration (World:ArrivalPrompt on, round I1b's review): the I1a
/// tests of Visible() (private, friends, staff), the blocks either way, the removal bar, another data hash, the host reconnecting and the
/// boss lock run with the automatic join (their Harness turns the prompt off); here the same rules through PLAN / OPTIONS / CHOICE.
/// </summary>
public partial class HubLogicTests
{
    [Fact]
    public void The_prompt_lists_a_private_instance_only_for_staff_and_a_friends_instance_only_for_the_hosts_friends()
    {
        var h = PromptHarness();
        var audits = new List<string>();
        h.Hub.Audit = (evt, _, _, _) => audits.Add(evt);
        Admit(h, "a", Player(100, "a") with { DefaultVisibility = Visibility.Private });
        Admit(h, "b", Player(101, "b") with { Friends = [100] }); // (a friend: Private is not for friends before round I4's party)
        Admit(h, "c", Player(102, "c") with { Staff = true });
        HostJoinable(h, "a", "stag0101");
        var w = W(h, "a");
        Assert.Equal(Visibility.Private, w.Visibility);
        Plan(h, "b", 1, "stag0101");
        Assert.Equal((OptionsMsg.Nothing, 0), (LastOptions(h, "b").Flags, LastOptions(h, "b").Rows.Count));
        Plan(h, "c", 1, "stag0101");
        var o = LastOptions(h, "c");
        Assert.Equal((w.Wid, (byte)Visibility.Private, (byte)0), (Assert.Single(o.Rows).Wid, o.Rows[0].Visibility, o.Preselected));
        Choose(h, "c", 1, ChoiceKind.Join, w.Wid);
        BarrierPlan(h, "c", "stag0101", 1);
        Assert.Equal((BarrierDecision.Join, w.Wid), (h.Decision("c").Decision, h.Decision("c").World.Wid));
        Assert.Contains("staff_join", audits);

        var g = PromptHarness();
        Admit(g, "a", Player(100, "a") with { DefaultVisibility = Visibility.Friends });
        Admit(g, "f", Player(101, "f") with { Friends = [100] });
        Admit(g, "s", Player(102, "s"));
        HostJoinable(g, "a", "stag0101");
        var wf = W(g, "a");
        Plan(g, "f", 1, "stag0101");
        o = LastOptions(g, "f");
        Assert.Equal((wf.Wid, (byte)(OptionRow.FriendHost | OptionRow.FriendsIn), OptionRow.Hidden, (byte)0), (Assert.Single(o.Rows).Wid, o.Rows[0].Relation, o.Rows[0].Visibility,
            o.Preselected));
        Plan(g, "s", 1, "stag0101");
        Assert.Equal(OptionsMsg.Nothing, LastOptions(g, "s").Flags);
        // The stranger's barrier: no prompt, its own instance.
        BarrierPlan(g, "s", "stag0101", 1);
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (g.Decision("s").Decision, g.Decision("s").World.Arrival));
        Assert.False(g.P("s").Has(Msg.BarrierWait));
    }

    [Fact]
    public void The_prompt_hides_an_instance_with_a_block_either_way_from_a_player_and_from_a_playing_moderator()
    {
        var h = PromptHarness();
        Admit(h, "a", Player(100, "a"));
        Admit(h, "g", Player(101, "g"));
        Admit(h, "x", Player(102, "x") with { Blocks = [101] }); // x blocks the guest g
        Admit(h, "y", Player(104, "y"));
        Admit(h, "m", Player(103, "m") with { Staff = true });
        HostJoinable(h, "a", "stag0101");
        var w = W(h, "a");
        Plan(h, "g", 1, "stag0101");
        Choose(h, "g", 1, ChoiceKind.Join, w.Wid);
        BarrierPlan(h, "g", "stag0101", 1);
        Assert.Same(w, W(h, "g"));
        Plan(h, "x", 1, "stag0101");
        Assert.Equal(OptionsMsg.Nothing, LastOptions(h, "x").Flags);
        BarrierPlan(h, "x", "stag0101", 0); // (the loading-screen form: nothing to hold for either)
        Assert.Equal(ArrivalKind.NewInstance, h.Decision("x").World.Arrival);
        Assert.NotSame(w, W(h, "x"));
        // The host blocks y (the other direction).
        h.Hub.Readmitted(100, Player(100, "a") with { Blocks = [104] }, h.Now);
        Plan(h, "y", 1, "stag0101");
        Assert.DoesNotContain(LastOptions(h, "y").Rows, r => r.Wid == w.Wid);
        // A moderator a member blocked is a player for blocks (19.7.2).
        h.Hub.Readmitted(101, Player(101, "g") with { Blocks = [103] }, h.Now);
        Plan(h, "m", 1, "stag0101");
        Assert.DoesNotContain(LastOptions(h, "m").Rows, r => r.Wid == w.Wid);
    }

    [Fact]
    public void The_prompt_lists_no_instance_under_a_removal_bar_and_none_of_another_data_hash()
    {
        var h = PromptHarness();
        Admit(h, "a", Player(100, "a") with { DataHash = 1 });
        Admit(h, "b", Player(101, "b") with { DataHash = 1 });
        Admit(h, "c", Player(102, "c") with { DataHash = 2 });
        HostJoinable(h, "a", "stag0101");
        var w = W(h, "a");
        Plan(h, "b", 1, "stag0101");
        Assert.Equal(w.Wid, Assert.Single(LastOptions(h, "b").Rows).Wid);
        Plan(h, "c", 1, "stag0101");
        Assert.Equal(OptionsMsg.Nothing, LastOptions(h, "c").Flags);
        // b under a live removal bar of a's instance (19.6.6): no row, and its barrier is no prompt.
        w.Bars[h.Mem("b").AccountId] = h.Now + 600L * System.Diagnostics.Stopwatch.Frequency;
        h.Advance(1_000);
        Plan(h, "b", 2, "stag0101");
        Assert.Equal(OptionsMsg.Nothing, LastOptions(h, "b").Flags);
        BarrierPlan(h, "b", "stag0101", 0);
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (h.Decision("b").Decision, h.Decision("b").World.Arrival));
        Assert.NotSame(w, W(h, "b"));
    }

    [Fact]
    public void A_host_reconnecting_row_is_greyed_never_preselected_and_a_choice_of_it_holds_the_barrier()
    {
        var h = PromptHarness();
        Admit(h, "a", Player(100, "a"));
        Admit(h, "b", Player(101, "b"));
        HostJoinable(h, "a", "stag0101");
        var w = W(h, "a");
        Plan(h, "b", 1, "stag0101");
        Assert.Equal((byte)0, LastOptions(h, "b").Preselected);
        Choose(h, "b", 1, ChoiceKind.Join, w.Wid);
        // a loses the connection between b's answer and b's barrier.
        h.Hub.Disconnect(h.Mem("a"), h.P("a"), h.Now);
        Assert.True(w.HostAway);
        BarrierPlan(h, "b", "stag0101", 1);
        Assert.False(h.P("b").Has(Msg.BarrierOk));
        var o = LastOptions(h, "b");
        Assert.Equal((OptionState.HostAway, OptionsMsg.NewInstance, (byte)0), (Assert.Single(o.Rows).State, o.Preselected, o.Notice));
        Assert.Equal(HoldKind.Prompt, h.Mem("b").Arrival!.Hold);
        // New instance: b's own.
        var home = h.Mem("b").Arrival!.From;
        Choose(h, "b", 0, ChoiceKind.New, inst: home.Id, visit: home.Visit);
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (h.Decision("b").Decision, h.Decision("b").World.Arrival));
        Assert.NotSame(w, W(h, "b"));
    }

    [Fact]
    public void A_boss_fight_row_is_choosable_and_its_choice_waits_for_the_end_or_goes_alone_on_the_own_story()
    {
        foreach (var alone in new[] { false, true })
        {
            var h = PromptHarness();
            Admit(h, "a", Player(100, "a"));
            Admit(h, "b", Player(101, "b"));
            HostJoinable(h, "a", "stag0101");
            var w = W(h, "a");
            var i = h.I("a");
            h.Send("a", new StateMsg(i.Id, i.Visit, 0, StateMsg.BossLock, 5, StateReason.Boss).Frame());
            Plan(h, "b", 1, "stag0101");
            var o = LastOptions(h, "b");
            Assert.Equal((OptionState.BossFight, (byte)1, (byte)0), (Assert.Single(o.Rows).State, o.Rows[0].Here, o.Preselected));
            Choose(h, "b", 1, ChoiceKind.Join, w.Wid);
            BarrierPlan(h, "b", "stag0101", 1);
            var wait = h.P("b").Last(Msg.BarrierWait, BarrierWaitMsg.Parse);
            Assert.Equal((WaitReason.BossLock, BarrierWaitMsg.ChoiceNeeded), (wait.Reason, wait.Flags));
            if (alone)
            {
                // Go alone: a new instance of b's own on its own story, never a second stage group of a's (19.7.7).
                h.Send("b", new BarrierChoiceMsg(0, BarrierChoiceMsg.Alone).Frame());
                var ok = h.Decision("b");
                Assert.Equal((BarrierDecision.New, BarrierNotice.WentAlone, ArrivalKind.NewInstance, StoryMode.Merge),
                    (ok.Decision, ok.Notice, ok.World.Arrival, ok.World.StoryMode));
                Assert.NotSame(w, W(h, "b"));
                Assert.Single(w.Seats);
            }
            else
            {
                h.Send("b", new BarrierChoiceMsg(0, BarrierChoiceMsg.Wait).Frame());
                Assert.False(h.P("b").Has(Msg.BarrierOk));
                h.Send("a", new StateMsg(i.Id, i.Visit, 0, StateMsg.Joinable, 5, StateReason.None).Frame()); // the boss fell
                Assert.Equal((BarrierDecision.Join, w.Wid), (h.Decision("b").Decision, h.Decision("b").World.Wid));
            }
        }
    }
}
