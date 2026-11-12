using DW4Online.Domain.Rooms;
using DW4Online.Domain.Story;
using DW4Online.Domain.World;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// The world without rooms (SEAMLESS.md 19, round I1a) on the hub without sockets: an instance's creation and its seats, the
/// automatic join by Visible() and decision 40's order, the guest's NEW by the gate and visited rules, the lone host never
/// prompted, the end with HOME (sealed and through JOIN_REQ home), the DISSOLVE with the own story and its redirects, M3's
/// defeated host, the host's grace, INSTANCE_OP, area_escape, the replaced session, the live re-check and the staff role.
/// </summary>
public partial class HubLogicTests
{
    /// <summary>A member with the given admission (staff, friends, blocks, story, default visibility).</summary>
    private static Peer Admit(Harness h, string name, HubAdmit admit)
    {
        var peer = h.NewPeer();
        var m = h.Hub.Connect(admit, peer, h.Now);
        h.M[name] = (m, peer);
        return peer;
    }

    private static HubAdmit Player(long account, string name) => new(account, name);

    /// <summary>A story at a phase (one-hot flag), with the training's 503 (the lobby gate offers stag0101).</summary>
    private static RoomStoryData StoryAt(int phase)
    {
        var s = new RoomStoryData();
        s.Flags[phase] = 1;
        s.Flags[503] = 1;
        for (var d = 0; d < StorySets.Difficulties; ++d) StoryMerge.DeriveCopies(s, d); // (every difficulty's copies: a keeper rebases)
        return s;
    }

    private static void BarrierWith(Harness h, string name, string stage, RoomSettings? settings = null, byte alive = 0xF, byte point = 0)
    {
        var i = h.Mem(name).Instance!;
        h.Send(name, new BarrierMsg(i.Id, i.Visit, 0, stage, point, (byte)(i.PresentMask & alive), i.PresentMask, 5, [(byte)name[0]], 0, 0, settings).Frame());
    }

    private static WorldInstance W(Harness h, string name) => h.Mem(name).World!;

    [Fact]
    public void A_lone_arrival_from_home_makes_a_public_instance_and_a_stranger_joins_it_automatically()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("a", "stag0101");
        var okA = h.Decision("a");
        var w = W(h, "a");
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance, w.Wid, h.Mem("a").Id, Visibility.Public, StoryMode.Merge, h.Mem("a").Id),
            (okA.Decision, okA.World.Arrival, okA.World.Wid, okA.World.HostMember, okA.World.Visibility, okA.World.StoryMode, okA.World.OwnerMember));
        Assert.Equal(AreaTableName.DeathValley, w.Area);
        Assert.Contains("stag0101", w.Visited);
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        h.Barrier("b", "stag0101");
        var okB = h.Decision("b");
        Assert.Equal((BarrierDecision.Join, ArrivalKind.AutoJoin, w.Wid, h.Mem("a").Id), (okB.Decision, okB.World.Arrival, okB.World.Wid, okB.World.OwnerMember));
        Assert.Same(w, W(h, "b"));
        Assert.Equal(2, w.Seats.Count);
        var info = h.P("b").Last(Msg.InstanceInfo, InstanceInfoMsg.Parse);
        Assert.Equal((w.Wid, h.Mem("a").Id, InstanceRole.Guest, (byte)2), (info.Wid, info.HostMember, info.Role, info.SeatsTaken));
        // The host hears of the guest by name first, then the notice.
        Assert.Equal("b", h.P("a").Last(Msg.MemberInfo, MemberInfoMsg.Parse).Name);
        Assert.Contains(h.P("a").All(Msg.WorldNotice, WorldNoticeMsg.Parse), n => n.Kind == WorldNoticeKind.Joined && n.Member == h.Mem("b").Id);
    }

    [Fact]
    public void Private_and_friends_instances_take_friends_only_a_playing_moderator_too_and_staff_who_play_are_audited()
    {
        var h = new Harness();
        Admit(h, "a", Player(100, "a") with { DefaultVisibility = Visibility.Private });
        Admit(h, "b", Player(101, "b"));
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        h.Barrier("b", "stag0101");
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (h.Decision("b").Decision, h.Decision("b").World.Arrival));
        Assert.NotSame(W(h, "a"), W(h, "b"));
        // fix-019: a moderator who simply plays is a player for the automatic join too: a's Private instance is not his to enter (the
        // staff bypass is for the staff member's own choice of the row, I1b's prompt and I3's browser, 19.14). Before, c joined a's
        // instance at once (its story replaced by a stranger's). (The automatic path, World:ArrivalPrompt off; the prompt's staff-only
        // rows: PromptVisibilityTests.)
        var s = new Harness(world: new WorldHubOptions { ArrivalPrompt = false });
        var audits = new List<string>();
        s.Hub.Audit = (evt, _, _, _) => audits.Add(evt);
        Admit(s, "a", Player(100, "a") with { DefaultVisibility = Visibility.Private });
        Admit(s, "c", Player(102, "c") with { Staff = true });
        s.Barrier("a", "stag0101");
        s.Snapshot("a");
        s.Log("a", 0, 60);
        s.Joinable("a");
        s.Advance(11_000);
        s.Hub.Housekeep(s.Now);
        s.Barrier("c", "stag0101");
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (s.Decision("c").Decision, s.Decision("c").World.Arrival));
        Assert.NotSame(W(s, "a"), W(s, "c"));
        Assert.DoesNotContain("staff_join", audits);
        Assert.False(Matching.Visible(W(s, "a"), s.Mem("c"), s.Now));
        Assert.True(Matching.Visible(W(s, "a"), s.Mem("c"), s.Now, staffChoice: true)); // (his own choice of the row: the prompt, the browser)
        // A Public instance: the playing moderator joins as a guest with the staff role, audited.
        var p = new Harness();
        var pAudits = new List<string>();
        p.Hub.Audit = (evt, _, _, _) => pAudits.Add(evt);
        Admit(p, "a", Player(100, "a"));
        Admit(p, "c", Player(102, "c") with { Staff = true });
        p.Barrier("a", "stag0101");
        p.Snapshot("a");
        p.Log("a", 0, 60);
        p.Joinable("a");
        p.Advance(11_000);
        p.Hub.Housekeep(p.Now);
        p.Barrier("c", "stag0101");
        Assert.Equal((BarrierDecision.Join, W(p, "a").Wid), (p.Decision("c").Decision, p.Decision("c").World.Wid));
        Assert.Contains("staff_join", pAudits);
        Assert.Equal(InstanceRole.Staff, p.P("c").Last(Msg.InstanceInfo, InstanceInfoMsg.Parse).Role);
        // Friends: a friend of the host joins, a stranger does not, nor a moderator who is no friend (automatically).
        var g = new Harness(world: new WorldHubOptions { ArrivalPrompt = false });
        Admit(g, "a", Player(100, "a") with { DefaultVisibility = Visibility.Friends });
        Admit(g, "f", Player(101, "f") with { Friends = [100] });
        Admit(g, "s", Player(102, "s"));
        Admit(g, "m", Player(103, "m") with { Staff = true });
        g.Barrier("a", "stag0101");
        g.Snapshot("a");
        g.Log("a", 0, 60);
        g.Joinable("a");
        g.Barrier("f", "stag0101");
        Assert.Same(W(g, "a"), W(g, "f"));
        g.Barrier("s", "stag0101");
        Assert.NotSame(W(g, "a"), W(g, "s"));
        g.Advance(11_000); // (s's instance no longer *starting*)
        g.Hub.Housekeep(g.Now);
        g.Barrier("m", "stag0101");
        Assert.NotSame(W(g, "a"), W(g, "m"));
    }

    [Fact]
    public void A_block_either_way_with_any_member_hides_an_instance_from_a_player_and_from_a_playing_moderator()
    {
        var h = new Harness();
        Admit(h, "a", Player(100, "a"));
        Admit(h, "g", Player(101, "g"));
        Admit(h, "x", Player(102, "x") with { Blocks = [101] }); // x blocks the guest g
        Admit(h, "m", Player(103, "m") with { Staff = true });
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        h.Barrier("g", "stag0101");
        h.Send("g", new ReadyMsg(h.Decision("g").NewInst, h.Decision("g").NewVisit, 60, 0, 5000, [7]).Frame());
        h.Barrier("x", "stag0101");
        Assert.NotSame(W(h, "a"), W(h, "x"));
        // A moderator a member blocked is a player for blocks: the instance is not his to join without the override (19.7.2).
        h.Hub.Readmitted(101, Player(101, "g") with { Blocks = [103] }, h.Now);
        h.Barrier("m", "stag0101");
        Assert.NotSame(W(h, "a"), W(h, "m"));
    }

    [Fact]
    public void A_guest_enters_at_a_stage_the_host_story_opened_or_the_instance_visited_else_it_gets_its_own()
    {
        var h = new Harness();
        Admit(h, "a", Player(100, "a") with { Story = StoryAt(4) }); // Dry Land's gate open (phase 4)
        Admit(h, "b", Player(101, "b") with { Story = StoryAt(2), Friends = [100] });
        Admit(h, "c", Player(102, "c"));
        Admit(h, "d", Player(103, "d"));
        Admit(h, "e", Player(104, "e"));
        var hostSettings = new RoomSettings(1, 1, 0, 4242, true, 1, 0, false);
        // a arrives on Dry Land by the return mat (stag0202) and walks into dng205b1.
        BarrierWith(h, "a", "stag0202", hostSettings);
        h.Snapshot("a");
        var w = W(h, "a");
        Assert.Equal(AreaTableName.DryLand, w.Area);
        BarrierWith(h, "a", "dng205b1", hostSettings);
        h.Snapshot("a");
        Assert.Contains("dng205b1", w.Visited);
        // e, a stranger, takes the gate to stag0201: nobody of a's instance there and not a friend's: New instance (fix-019, the I1a
        // interim rule: the automatic join takes only the row I1b's prompt would preselect, decision 40). Before, e became a guest
        // in a stranger's story elsewhere in the area.
        // (Round I1b: e's loading-screen prompt lists a's row with the cursor on New instance, decision 40; e takes the cursor.)
        h.P("e").AutoAnswer = false;
        BarrierWith(h, "e", "stag0201", RoomSettings.Default);
        var oe = h.P("e").Last(Msg.Options, OptionsMsg.Parse);
        Assert.Equal((w.Wid, OptionsMsg.NewInstance), (Assert.Single(oe.Rows).Wid, oe.Preselected));
        h.Send("e", new ChoiceMsg(0, oe.Inst, oe.Visit, ChoiceKind.New, 0, oe.Visibility, 0, ChoiceMsg.DefaultBackPoint).Frame());
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance, StoryMode.Merge), (h.Decision("e").Decision, h.Decision("e").World.Arrival,
            h.Decision("e").World.StoryMode));
        Assert.NotSame(w, W(h, "e"));
        h.Advance(11_000); // (e's instance no longer *starting*)
        h.Housekeep();
        // b, a's friend, takes the gate to stag0201: no stage group of a's there, a gate the host's story opened: a guest's NEW with
        // the host's story (the friend's row first, before e's with e at stag0201).
        BarrierWith(h, "b", "stag0201", RoomSettings.Default);
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.New, ArrivalKind.GuestNew, StoryMode.Replace, w.Wid, h.Mem("a").Id), (ok.Decision, ok.World.Arrival,
            ok.World.StoryMode, ok.World.Wid, ok.World.OwnerMember));
        Assert.Equal(hostSettings, ok.World.Settings);
        Assert.Equal(1, RoomStoryData.FromBlob(ok.Story, ok.StoryRevision).Flags[4]); // the host's phase, not b's own
        Assert.Same(w, W(h, "b"));
        Assert.Equal(2, w.Groups.Count);
        // (Round I1b: b's loading-screen prompt listed a's row joinable with nobody of it at stag0201 first, the host b's friend, so the
        // cursor on it by decision 40, then e's instance loading with e there; the harness took the first choosable row.)
        var ob = h.P("b").Last(Msg.Options, OptionsMsg.Parse);
        Assert.Equal([w.Wid, W(h, "e").Wid], ob.Rows.Select(r => r.Wid));
        Assert.Equal((OptionState.Joinable, (byte)0, (byte)0), (ob.Rows[0].State, ob.Rows[0].Here, ob.Preselected));
        // c arrives by the return mat on dng205b2: never visited, no gate: "Story not here yet", its own instance.
        h.Barrier("c", "dng205b2");
        Assert.NotSame(w, W(h, "c"));
        Assert.Equal(ArrivalKind.NewInstance, h.Decision("c").World.Arrival);
        // (The greyed rows of the prompt, round I1b: a's, and e's own Dry Land instance.)
        Assert.All(h.P("c").Last(Msg.Options, OptionsMsg.Parse).Rows, r => Assert.Equal(OptionState.StoryNotHere, r.State));
        Assert.Contains(h.P("c").Last(Msg.Options, OptionsMsg.Parse).Rows, r => r.Wid == w.Wid);
        // After the instance has been at dng205b2, d's arrival there enters it.
        BarrierWith(h, "a", "dng205b2", hostSettings);
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        h.Advance(11_000); // (c's own instance at dng205b2 is no longer *starting*)
        h.Housekeep();
        h.Barrier("d", "dng205b2");
        Assert.Same(w, W(h, "d"));
    }

    [Fact]
    public void A_gate_the_host_story_has_not_opened_is_story_not_here_yet_and_a_training_instance_is_no_candidate_at_stag0101()
    {
        var h = new Harness();
        Admit(h, "a", Player(100, "a") with { Story = StoryAt(2) }); // phase 2: Dry Land's gate is closed on a's story
        Admit(h, "b", Player(101, "b"));
        BarrierWith(h, "a", "stag0202");
        h.Snapshot("a");
        h.Barrier("b", "stag0201");
        Assert.NotSame(W(h, "a"), W(h, "b"));
        // A newcomer in the training (stag0001, Death Valley, before 503): a stranger's arrival at stag0101 never plays its story.
        var g = new Harness();
        Admit(g, "n", Player(100, "n") with { Story = new RoomStoryData() });
        Admit(g, "s", Player(101, "s"));
        g.Barrier("n", "stag0001");
        g.Snapshot("n");
        g.Barrier("s", "stag0101");
        Assert.NotSame(W(g, "n"), W(g, "s"));
    }

    [Fact]
    public void The_automatic_join_takes_rows_with_players_at_the_arrival_stage_first_and_a_friends_row_before_them()
    {
        var h = new Harness();
        Admit(h, "x", Player(100, "x") with { Story = StoryAt(2) }); // (Dry Land's gate closed on x's story: y gets its own instance)
        Admit(h, "y", Player(101, "y") with { Story = StoryAt(4) });
        Admit(h, "b", Player(102, "b"));
        Admit(h, "f", Player(103, "f") with { Friends = [100] });
        // x arrives by the return mat on stag0202; y at the gate stage stag0201 finds x's row "Story not here yet": its own.
        BarrierWith(h, "x", "stag0202");
        h.Snapshot("x");
        BarrierWith(h, "y", "stag0201");
        h.Snapshot("y");
        h.Log("y", 0, 60);
        h.Joinable("y");
        Assert.NotSame(W(h, "x"), W(h, "y"));
        // x walks to stag0201 and back (visited now; nobody of x's instance there any more).
        BarrierWith(h, "x", "stag0201");
        h.Snapshot("x");
        BarrierWith(h, "x", "stag0202");
        h.Snapshot("x");
        Assert.Contains("stag0201", W(h, "x").Visited);
        h.Advance(11_000);
        h.Housekeep();
        // b, a stranger to both: y's row (players here) first, although x's instance is older.
        h.Barrier("b", "stag0201");
        Assert.Same(W(h, "y"), W(h, "b"));
        // f, x's friend: x's row first (friends before players here), entering where it walked in as a guest.
        h.Barrier("f", "stag0201");
        Assert.Same(W(h, "x"), W(h, "f"));
        Assert.Equal(ArrivalKind.GuestNew, h.Decision("f").World.Arrival);
        // The order itself, and the prompt's preselection (I1b): New instance over a stranger's row with nobody here.
        var rows = h.Hub.Candidates(new ArrivalGroup { From = new HubInstance(0), BarrierTick = 0, Stage = "stag0201", Point = 0, Context = 5, ParkedAt = h.Now }
            .With(h.Mem("b")), h.Now);
        Assert.Null(Matching.Preselected(rows.Where(r => r.Here == 0).ToList()));
    }

    [Fact]
    public void A_lone_host_moving_inside_his_area_is_never_matched_with_another_instance()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("c", 2);
        h.Barrier("c", "dng101b1");
        h.Snapshot("c");
        h.Log("c", 0, 60);
        h.Joinable("c");
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        var wa = W(h, "a");
        // (Round I1b: a's arrival from home had a prompt, c's instance in the area; its move inside its own instance has none.)
        var prompts = h.P("a").All(Msg.Options, OptionsMsg.Parse).Count;
        Assert.Equal(1, prompts);
        var waits = h.P("a").All(Msg.BarrierWait, BarrierWaitMsg.Parse).Count(x => x.Reason == WaitReason.Choice);
        h.Barrier("a", "dng101b1"); // where c's instance has a stage group
        var ok = h.Decision("a");
        Assert.Equal((BarrierDecision.New, ArrivalKind.None, wa.Wid), (ok.Decision, ok.World.Arrival, ok.World.Wid));
        Assert.Same(wa, W(h, "a"));
        Assert.NotSame(W(h, "c"), wa);
        Assert.Equal(prompts, h.P("a").All(Msg.Options, OptionsMsg.Parse).Count); // decision 41: never prompted
        Assert.Equal(waits, h.P("a").All(Msg.BarrierWait, BarrierWaitMsg.Parse).Count(x => x.Reason == WaitReason.Choice));
    }

    [Fact]
    public void The_host_going_home_ends_his_instance_and_every_stage_group_gets_home_after_the_notice()
    {
        var h = new Harness();
        var history = new List<WorldHistoryEvent>();
        h.Hub.History = history.Add;
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        var w = W(h, "a");
        // b joins and walks on to stag0102 (a stage group of its own in a's instance).
        h.Barrier("b", "stag0101");
        var join = h.Decision("b");
        h.Send("b", new ReadyMsg(join.NewInst, join.NewVisit, 60, 0, 5000, [7]).Frame());
        var req = h.P("a").Last(Msg.JoinReq, JoinReqMsg.Parse);
        h.Send("a", new JoinAtMsg(req.Inst, req.Visit, 70).Frame());
        h.Seal(200);
        var sealedGroup = h.I("a");
        Assert.Same(sealedGroup, h.I("b"));
        // b leaves for stag0102 by itself: a fork, its own solo-led stage group.
        h.Send("a", new SplitMsg(sealedGroup.Id, sealedGroup.Visit, sealedGroup.NextSeal - 1, 0b10, SplitKind.Exit).Frame());
        h.Send("b", new SplitMsg(sealedGroup.Id, sealedGroup.Visit, sealedGroup.NextSeal - 1, 0b10, SplitKind.Exit).Frame());
        var child = h.I("b");
        Assert.NotSame(sealedGroup, child);
        Assert.Contains(child, w.Groups);
        h.Seal(100);
        h.Barrier("b", "stag0102");
        h.Snapshot("b");
        Assert.Equal(ArrivalKind.None, h.Decision("b").World.Arrival);
        Assert.Equal(InstanceMode.SoloLed, h.I("b").Mode);
        // a takes the gate disc home (on purpose): END host_left; b gets the notice now and HOME after 3 s (JOIN_REQ kind home).
        h.Barrier("a", "stagML11");
        Assert.True(w.Ending);
        Assert.Equal(EndReason.HostLeft, w.EndReason);
        Assert.Contains(h.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse), n => n.Kind == WorldNoticeKind.Ended && n.Argument == (uint)EndReason.HostLeft);
        Assert.Equal((StoryMode.Merge, ""), (h.Decision("a").World.StoryMode, h.Decision("a").World.RedirectStage)); // the host's story is his own
        Assert.DoesNotContain(h.P("b").Frames, f => f.Type == Msg.JoinReq && JoinReqMsg.Parse(f.Payload).Kind == JoinReqKind.Home);
        h.Advance(3100);
        h.Housekeep();
        var home = h.P("b").Last(Msg.JoinReq, JoinReqMsg.Parse);
        Assert.Equal((JoinReqKind.Home, (byte)(1 << h.Mem("b").Slot)), (home.Kind, home.Slot));
        h.Log("b", 0, 200); // (the leader logs the solo ticks up to J, as for a join)
        h.Send("b", new JoinAtMsg(home.Inst, home.Visit, 200).Frame());
        h.Seal(400);
        var ticks = h.P("b").Ticks();
        Assert.Contains(ticks, t => t.Rec.Events.Any(e => e.Type == Ev.Home && e.HomeMask == home.Slot && e.Payload[1] == (byte)HomeReason.Ended));
        Assert.Contains(ticks, t => t.Rec.Events.Any(e => e.Type == Ev.LeadFrom));
        // b's home departure: DISSOLVE on its own story (replace: its engine held a's), its seat frees, the instance is gone.
        h.Barrier("b", "stagML11");
        var back = h.Decision("b");
        Assert.Equal((BarrierDecision.Dissolve, StoryMode.Replace, h.Mem("b").Id, 0u), (back.Decision, back.World.StoryMode, back.World.OwnerMember, back.World.Wid));
        Assert.Null(h.Mem("b").World);
        Assert.Null(h.Hub.WorldInstance(w.Wid));
        Assert.Contains(history, e => e is HistoryEnd { Reason: "host_left" });
        Assert.Equal(2, history.OfType<HistoryJoined>().Count());
        Assert.Equal(2, history.OfType<HistoryLeft>().Count());
    }

    [Fact]
    public void Home_is_stamped_in_a_sealed_stage_group_and_follows_its_slots_at_a_fork()
    {
        var h = SealedPair(out _);
        var w = W(h, "a");
        Assert.Same(w, W(h, "b"));
        h.Seal(500);
        // b's own *Leave instance*: HOME for its slot only (sealed: stamped), reason leave.
        h.Send("b", new IntentMsg(h.Inst("b"), h.I("b").Visit, IntentKind.Leave, 0).Frame());
        h.Seal(200);
        var ev = h.P("a").Ticks().SelectMany(t => t.Rec.Events).Single(e => e.Type == Ev.Home);
        Assert.Equal(((byte)0b10, (byte)HomeReason.Leave), (ev.HomeMask, ev.Payload[1]));
        // The host's leave ends the instance: HOME for every present slot 180 ticks ahead; at a fork its mask splits by side.
        var g = SealedPair(out _);
        var i = g.I("a");
        var wg = W(g, "a");
        g.Seal(500);
        g.Send("a", new IntentMsg(i.Id, i.Visit, IntentKind.Leave, 0).Frame());
        Assert.True(wg.Ending);
        var pending = i.PendingEvents.SelectMany(kv => kv.Value.Select(e => (kv.Key, e))).Single(x => x.e.Type == Ev.Home);
        Assert.Equal((byte)0b11, pending.e.HomeMask);
        Assert.True(pending.Key >= i.NextSeal + 180);
        g.Send("a", new SplitMsg(i.Id, i.Visit, i.NextSeal - 1, 0b10, SplitKind.Exit).Frame());
        g.Send("b", new SplitMsg(i.Id, i.Visit, i.NextSeal - 1, 0b10, SplitKind.Exit).Frame());
        var child = g.I("b");
        Assert.Equal((byte)0b01, i.PendingEvents.Values.SelectMany(l => l).Single(e => e.Type == Ev.Home).HomeMask);
        Assert.Equal((byte)0b10, child.PendingEvents.Values.SelectMany(l => l).Single(e => e.Type == Ev.Home).HomeMask);
    }

    [Fact]
    public void A_defeated_host_ends_his_instance_as_defeated_and_a_give_up_copy_as_left()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        var w = W(h, "a");
        // M3: the barrier's alive mask 0 is the game over, whatever stage it names (here the game over's own stagML11; into a field:
        // A_hosts_barrier_with_no_living_slot_into_a_field_is_his_defeat_and_goes_home).
        BarrierWith(h, "a", "stagML11", alive: 0);
        Assert.Equal(EndReason.HostDefeated, w.EndReason);
        var g = SealedPair(out _);
        var wg = W(g, "a");
        var i = g.I("a");
        g.Seal(300);
        g.Send("a", new IntentMsg(i.Id, i.Visit, IntentKind.GiveUp, 0).Frame());
        Assert.True(g.Mem("a").GaveUp);
        g.Seal(200);
        g.Send("a", new SplitMsg(i.Id, i.Visit, i.NextSeal - 1, 0b01, SplitKind.Exit).Frame());
        g.Send("b", new SplitMsg(i.Id, i.Visit, i.NextSeal - 1, 0b01, SplitKind.Exit).Frame());
        BarrierWith(g, "a", "stagML11", alive: 0);
        Assert.Equal(EndReason.HostLeft, wg.EndReason); // a give up is on purpose (answers 24, 37)
    }

    [Fact]
    public void A_guest_whose_barrier_leads_into_the_briefing_room_is_redirected_to_the_lobby_on_its_own_story()
    {
        var h = SealedPair(out _);
        var i = h.I("a");
        h.Seal(300);
        h.Barrier("a", "stagML02");
        h.Barrier("b", "stagML02");
        var a = h.Decision("a");
        var b = h.Decision("b");
        Assert.Equal((BarrierDecision.Dissolve, "", StoryMode.Merge), (a.Decision, a.World.RedirectStage, a.World.StoryMode));
        Assert.Equal((BarrierDecision.Dissolve, RoomHub.HomeStage, RoomHub.HomePoint, StoryMode.Replace, h.Mem("b").Id),
            (b.Decision, b.World.RedirectStage, b.World.RedirectPoint, b.World.StoryMode, b.World.OwnerMember));
        Assert.Equal("stagML11", h.I("b").Stage);
        Assert.Null(h.Mem("b").World);
        _ = i;
    }

    [Fact]
    public void A_barrier_in_an_ended_instance_is_redirected_home_and_a_detached_group_too()
    {
        var h = SealedPair(out _);
        var w = W(h, "a");
        h.Seal(300);
        h.Hub.StaffEnd(w.Wid, 999, h.Now);
        Assert.True(w.Ending);
        // Both reach a field barrier before their HOME: the decision sends them home (stagML11 point 3) on their own stories.
        h.Barrier("a", "stag0102");
        h.Barrier("b", "stag0102");
        Assert.Equal((BarrierDecision.Dissolve, RoomHub.HomeStage), (h.Decision("a").Decision, h.Decision("a").World.RedirectStage));
        Assert.Equal((BarrierDecision.Dissolve, RoomHub.HomeStage), (h.Decision("b").Decision, h.Decision("b").World.RedirectStage));
        Assert.Equal(EndReason.Staff, w.EndReason);
        Assert.Null(h.Hub.WorldInstance(w.Wid));
    }

    [Fact]
    public void A_barrier_out_of_the_instances_area_is_counted_and_decided_inside_the_instance()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        var w = W(h, "a");
        h.Barrier("a", "dng204b1"); // Dry Land: a table mistake must not end a player's instance
        Assert.Equal(1, h.Hub.Counters["area_escape"]);
        Assert.Same(w, W(h, "a"));
        Assert.False(w.Ending);
    }

    [Fact]
    public void The_Fortress_and_stag0205s_plateau_are_in_Death_Valleys_scope()
    {
        // 19.30.2: a Death Valley host walks through the Fortress to stag0205's plateau (Dry Land's by its card) and back:
        // all of it inside his instance's scope, no area_escape (W32).
        var h = new Harness();
        h.Connect("a", 0);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        var w = W(h, "a");
        foreach (var stage in new[] { "staggf01", "staggf02", "staggf04", "stag0205", "staggf04" })
        {
            h.Barrier("a", stage);
            h.Snapshot("a");
        }
        Assert.Equal(0, h.Hub.Counters.GetValueOrDefault("area_escape"));
        Assert.Same(w, W(h, "a"));
        Assert.Equal(AreaTable.DeathValley, w.Area);
        Assert.True(AreaTable.Default.InScope(AreaTable.DeathValley, "stag0205"));
        Assert.False(AreaTable.Default.InScope(AreaTable.DryLand, "staggf04"));
    }

    [Fact]
    public void The_host_grace_holds_the_guests_story_and_his_return_merges_it_or_its_end_ends_the_instance()
    {
        var h = SealedPair(out _, relay: new RelayOptions { LagLeaveSeconds = 600 });
        var w = W(h, "a");
        h.Seal(300);
        h.Hub.Disconnect(h.Mem("a"), h.P("a"), h.Now);
        Assert.True(w.HostAway);
        Assert.True(w.Keeper.Held);
        Assert.Contains(h.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse), n => n.Kind == WorldNoticeKind.HostAway);
        // Past the reconnect window the host's slot stays (filled with neutral pads), no LEAVE.
        h.Seal(12_000);
        h.Housekeep();
        Assert.Same(h.I("b"), h.Mem("a").Instance);
        Assert.True(h.I("b").Streams[h.Mem("a").Slot]!.NeutralFill);
        // A guest's value meanwhile is held, not merged.
        var i = h.I("b");
        h.Send("b", new StoryMsg(i.Id, i.Visit, 0, new StoryDelta { Flags = [67] }, h.Mem("a").Id, 1).Frame());
        var ack = h.P("b").Last(Msg.StoryAck, StoryAckMsg.Parse);
        Assert.Equal(h.Mem("a").Id, ack.OwnerMember);
        // The host back (a reconnect, resumed): "Kim is back", the held values merge.
        var a2 = h.Connect("a", 0);
        Assert.False(w.HostAway);
        Assert.False(w.Keeper.Held);
        Assert.Contains(h.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse), n => n.Kind == WorldNoticeKind.HostBack);
        Assert.Equal(w.Wid, a2.Last(Msg.Welcome, WelcomeMsg.Parse).Wid);
        // Not back within 60 s: END host_lost and HOME.
        var g = SealedPair(out _, relay: new RelayOptions { LagLeaveSeconds = 600 });
        var wg = W(g, "a");
        g.Seal(300);
        g.Hub.Disconnect(g.Mem("a"), g.P("a"), g.Now);
        g.Seal(59_000);
        g.Housekeep();
        Assert.False(wg.Ending);
        g.Seal(2_000);
        g.Housekeep();
        Assert.Equal(EndReason.HostLost, wg.EndReason);
        Assert.Contains(g.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse), n => n.Kind == WorldNoticeKind.Ended && n.Argument == (uint)EndReason.HostLost);
    }

    [Fact]
    public void The_grace_starts_after_four_seconds_of_silence_on_an_open_connection_and_lag_takes_no_host_out()
    {
        var h = SealedPair(out _, relay: new RelayOptions { LagLeaveSeconds = 10 });
        var w = W(h, "a");
        var i = h.I("a");
        // b keeps sending; a sends nothing (a half-open connection: no FIN, no RST).
        uint next = 0;
        void Pads(double ms)
        {
            var end = h.Now + (long)(ms * System.Diagnostics.Stopwatch.Frequency / 1000);
            while (h.Now < end)
            {
                h.Send("b", new InputMsg(i.Id, i.Visit, next, [new Pad(1, 0, 0, 0, 0)]).Frame());
                ++next;
                h.Seal(16.7);
                if (next % 15 == 0) h.Housekeep();
            }
        }
        Pads(3000);
        Assert.False(w.HostAway);
        Pads(1500);
        Assert.True(w.HostAway);
        Pads(12_000);
        Assert.Same(i, h.Mem("a").Instance); // no lagOut for a host in his grace
        Assert.False(h.Hub.Counters.ContainsKey("lag_leave"));
    }

    [Fact]
    public void Instance_op_visibility_is_the_hosts_and_staffs_and_is_remembered()
    {
        var h = SealedPair(out _);
        var w = W(h, "a");
        var remembered = new List<(long, Visibility)>();
        h.Hub.RememberVisibility = (a, v) => remembered.Add((a, v));
        h.Send("b", new InstanceOpMsg(InstanceOpKind.Visibility, 2).Frame()); // a guest: refused
        Assert.Equal(Visibility.Public, w.Visibility);
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Visibility, 1).Frame());
        Assert.Equal(Visibility.Friends, w.Visibility);
        Assert.Contains(h.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse), n => n.Kind == WorldNoticeKind.VisibilityChanged && n.Argument == 1);
        h.Housekeep();
        Assert.Equal(Visibility.Friends, h.P("b").Last(Msg.InstanceInfo, InstanceInfoMsg.Parse).Visibility);
        // (Remembered once the choice settled.)
        Assert.Empty(remembered);
        h.Advance(RoomHub.VisibilitySettleMs + 100);
        h.Housekeep();
        Assert.Equal([(h.Mem("a").AccountId, Visibility.Friends)], remembered);
    }

    [Fact]
    public void A_removed_member_goes_home_its_seat_and_story_owner_switch_at_once_and_a_bar_keeps_it_out()
    {
        var h = SealedPair(out _);
        var w = W(h, "a");
        h.Seal(300);
        Assert.True(h.Hub.StaffRemove(w.Wid, h.Mem("b").AccountId, 999, h.Now));
        Assert.Null(h.Mem("b").World);
        Assert.Contains(h.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse), n => n.Kind == WorldNoticeKind.Removed);
        h.Seal(100);
        Assert.Contains(h.P("a").Ticks().SelectMany(t => t.Rec.Events), e => e.Type == Ev.Home && e.HomeMask == 0b10 && e.Payload[1] == (byte)HomeReason.Staff);
        // Its STORY with the host's key is refused at once (owner).
        var i = h.I("b");
        h.Send("b", new StoryMsg(i.Id, i.Visit, 0, new StoryDelta { Flags = [67] }, h.Mem("a").Id, 1).Frame());
        Assert.Equal(StoryRefusal.Owner, h.P("b").Last(Msg.StoryAck, StoryAckMsg.Parse).Refused.Single().Reason);
        Assert.True(w.Bars.ContainsKey(h.Mem("b").AccountId));
        Assert.False(Matching.Visible(w, h.Mem("b"), h.Now));
        h.Advance(601_000);
        Assert.True(Matching.Visible(w, h.Mem("b"), h.Now));
    }

    [Fact]
    public void The_hosts_remove_op_sends_the_guest_home_removed_and_is_audited_as_instance_remove()
    {
        var h = SealedPair(out _);
        var audits = new List<(string Event, long? Account, long? Actor)>();
        h.Hub.Audit = (e, account, actor, _) => audits.Add((e, account, actor));
        var w = W(h, "a");
        h.Seal(300);
        h.Send("b", new InstanceOpMsg(InstanceOpKind.Remove, h.Mem("a").Id).Frame()); // a guest: refused
        Assert.Same(w, W(h, "b"));
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, h.Mem("b").Id).Frame());
        Assert.Null(h.Mem("b").World);
        h.Seal(100);
        Assert.Contains(h.P("a").Ticks().SelectMany(t => t.Rec.Events), e => e.Type == Ev.Home && e.HomeMask == 0b10 && e.Payload[1] == (byte)HomeReason.Removed);
        Assert.Equal([("instance_remove", h.Mem("b").AccountId, h.Mem("a").AccountId)], audits);
    }

    [Fact]
    public void Play_on_another_pc_replaces_the_session_and_ends_a_hosted_instance()
    {
        var h = SealedPair(out _);
        var w = W(h, "a");
        var id = h.Mem("a").Id;
        var old = h.P("a");
        var fresh = Admit(h, "a", Player(h.Mem("a").AccountId, "a") with { NewSession = true });
        Assert.Equal(CloseReason.Replaced, old.Closed);
        Assert.Equal(EndReason.HostLeft, w.EndReason);
        Assert.Equal((id, 0u), (fresh.Last(Msg.Welcome, WelcomeMsg.Parse).Member, fresh.Last(Msg.Welcome, WelcomeMsg.Parse).Wid));
    }

    [Fact]
    public void A_reconnect_after_the_instance_ended_gets_a_welcome_without_it()
    {
        var h = SealedPair(out _);
        var w = W(h, "a");
        h.Seal(300);
        h.Hub.Disconnect(h.Mem("b"), h.P("b"), h.Now);
        h.Hub.StaffEnd(w.Wid, 999, h.Now);
        var b2 = h.Connect("b", 1);
        Assert.Equal(0u, b2.Last(Msg.Welcome, WelcomeMsg.Parse).Wid);
        Assert.Equal(0, b2.Last(Msg.Welcome, WelcomeMsg.Parse).Flags & WelcomeMsg.Resumable); // (review: never resumable without its instance)
        Assert.Equal(0u, b2.Last(Msg.InstanceInfo, InstanceInfoMsg.Parse).Wid);
    }

    [Fact]
    public void A_live_recheck_that_refuses_closes_the_member_and_a_demotion_counts_at_once()
    {
        var h = new Harness();
        Admit(h, "m", Player(100, "m") with { Staff = true });
        Admit(h, "p", Player(101, "p"));
        h.Hub.Readmitted(100, Player(100, "m"), h.Now);
        Assert.False(h.Mem("m").Staff);
        h.Hub.Readmitted(101, null, h.Now);
        Assert.Equal(CloseReason.Account, h.P("p").Closed);
        Assert.Null(h.Hub.MemberOf(101));
    }

    [Fact]
    public void A_story_replaced_from_outside_ends_the_hosted_instance_and_reloads_the_keeper()
    {
        var h = SealedPair(out _);
        var w = W(h, "a");
        var account = h.Mem("a").AccountId;
        Assert.True(h.Hub.StoryReplacing(account, 1, h.Now));
        Assert.Equal(EndReason.Staff, w.EndReason);
        Assert.True(h.Hub.KeeperOf(account)!.Replacing);
        var fresh = new RoomStoryData { Revision = 7 };
        h.Hub.StoryReloaded(account, 1, fresh);
        Assert.False(h.Hub.KeeperOf(account)!.Replacing);
        Assert.Equal(7u, h.P("a").Last(Msg.StoryAck, StoryAckMsg.Parse).Revision);
    }

    [Fact]
    public void The_snapshot_and_the_play_context_name_the_instance()
    {
        var h = SealedPair(out _);
        var w = W(h, "a");
        h.Housekeep();
        var row = h.Hub.CachedSnapshot.Instances.Single();
        Assert.Equal((w.Wid, h.Mem("a").AccountId, "death_valley".Length > 0 ? row.Area : "", "public", 2), (row.Wid, row.HostAccountId, row.Area, row.Visibility,
            row.Members.Count));
        Assert.Equal("host", row.Members[0].Role);
        var ctx = h.Hub.PlayContext(h.Mem("b").AccountId, h.Now)!;
        Assert.Equal((w.Wid, false, 2), (ctx.Wid, ctx.IsHost, ctx.Players));
        Assert.True(h.Hub.PlayContext(h.Mem("a").AccountId, h.Now)!.IsHost);
    }

    [Fact]
    public void A_barrier_is_decided_without_a_lost_host_and_his_resent_barrier_joins_his_own_instance_there()
    {
        var h = SealedPair(out _, relay: new RelayOptions { LagLeaveSeconds = 600 });
        var w = W(h, "a");
        h.Seal(300);
        // Both reach the barrier for stag0102; a's connection goes before its report: b's barrier is decided without a.
        h.Barrier("b", "stag0102");
        h.Hub.Disconnect(h.Mem("a"), h.P("a"), h.Now);
        Assert.True(w.HostAway);
        var okB = h.Decision("b");
        Assert.Equal((BarrierDecision.New, ArrivalKind.None, w.Wid), (okB.Decision, okB.World.Arrival, okB.World.Wid));
        h.Snapshot("b");
        h.Log("b", 0, 60);
        h.Joinable("b");
        // a back within its grace, still parked at that barrier: its BARRIER again joins its own instance's stage group there.
        h.Advance(20_000);
        var a2 = h.Connect("a", 0);
        Assert.Equal(w.Wid, a2.Last(Msg.Welcome, WelcomeMsg.Parse).Wid);
        Assert.False(w.HostAway);
        h.Send("a", new BarrierMsg(9999, 1, 0, "stag0102", 0, 1, 1, 5, [(byte)'a']).Frame());
        var okA = h.Decision("a");
        Assert.Equal((BarrierDecision.Join, ArrivalKind.None, w.Wid), (okA.Decision, okA.World.Arrival, okA.World.Wid));
        Assert.Same(w, W(h, "a"));
    }

    [Fact]
    public void A_guest_that_continues_alone_after_a_reconnect_is_filed_at_its_stage_and_its_next_barrier_stays_inside()
    {
        var h = SealedPair(out _);
        var w = W(h, "a");
        h.Seal(300);
        // b's connection goes; past the reconnect window its slot leaves; it reconnects (not resumable) and names its stage.
        h.Hub.Disconnect(h.Mem("b"), h.P("b"), h.Now);
        h.Seal(11_000);
        h.Housekeep();
        var b2 = h.Connect("b", 1);
        var welcome = b2.Last(Msg.Welcome, WelcomeMsg.Parse);
        Assert.Equal((0, w.Wid), (welcome.Flags & WelcomeMsg.Resumable, welcome.Wid));
        h.Send("b", new ResumeMsg(9, 1, 1, 500, "stag0201").Frame());
        Assert.Equal("stag0201", h.Mem("b").LoneStage);
        Assert.Equal(1, h.Hub.Counters["resume_filed_alone"]);
        // Its next barrier (alone) is decided inside a's instance: a new stage group of it, not a new instance.
        h.Send("b", new BarrierMsg(9, 1, 0, "stag0202", 0, 1, 1, 5, [(byte)'b']).Frame());
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.New, ArrivalKind.None, w.Wid), (ok.Decision, ok.World.Arrival, ok.World.Wid));
    }

    [Fact]
    public void A_fresh_join_replaces_the_story_and_its_fallback_is_a_stage_group_of_the_chosen_instance()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Joinable("a");
        h.Log("a", 0, 200);
        h.Barrier("b", "stag0101");
        var join = h.Decision("b");
        var w = W(h, "a");
        Assert.Equal((BarrierDecision.Join, ArrivalKind.AutoJoin, StoryMode.Replace, h.Mem("a").Id),
            (join.Decision, join.World.Arrival, join.World.StoryMode, join.World.OwnerMember));
        // a walks on before b's READY: b's engine holds a's story now, so its fallback is a new stage group of a's instance.
        h.Barrier("a", "stag0102", tick: 199);
        var fallback = h.Decision("b");
        Assert.Equal((BarrierDecision.New, ArrivalKind.None, w.Wid, StoryMode.Merge, h.Mem("a").Id),
            (fallback.Decision, fallback.World.Arrival, fallback.World.Wid, fallback.World.StoryMode, fallback.World.OwnerMember));
        Assert.Same(w, W(h, "b"));
        Assert.Equal(2, w.Seats.Count);
    }

    [Fact]
    public void A_fresh_arrivals_candidates_are_the_running_instances_of_its_area_only()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Connect("c", 2);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        h.Barrier("b", "stag0201");
        h.Snapshot("b");
        h.Log("b", 0, 60);
        h.Joinable("b");
        var wa = W(h, "a");
        // a goes home alone: his instance ends and goes with its last seat (out of the area's index too).
        h.Barrier("a", "stagML11");
        Assert.Null(h.Hub.WorldInstance(wa.Wid));
        // c at Death Valley's stag0101: b's instance (Dry Land) is no candidate, a's is gone: an own instance.
        h.Barrier("c", "stag0101");
        var ok = h.Decision("c");
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance, h.Mem("c").Id), (ok.Decision, ok.World.Arrival, ok.World.HostMember));
        Assert.NotEqual(W(h, "b").Wid, ok.World.Wid);
    }

    [Fact]
    public void A_barrier_whose_plan_is_new_instance_never_joins_automatically()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        // (DW4_ONLINE_AUTOCHOICE=new: plan id 0xFFFF.)
        var i = h.Mem("b").Instance!;
        h.Send("b", new BarrierMsg(i.Id, i.Visit, 0, "stag0101", 0, i.PresentMask, i.PresentMask, 5, [(byte)'b'], 0, BarrierMsg.PlanNewInstance).Frame());
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance, h.Mem("b").Id, StoryMode.Merge),
            (ok.Decision, ok.World.Arrival, ok.World.HostMember, ok.World.StoryMode));
        Assert.NotSame(W(h, "a"), W(h, "b"));
    }

    [Fact]
    public async Task The_history_queue_writes_an_instances_rows_in_order_with_its_id()
    {
        var repo = new FakeHistory();
        var audits = new List<string>();
        var queue = new WorldHistoryQueue(repo, new FakeAudit(audits), null);
        var h = new Harness();
        h.Hub.History = queue.Enqueue;
        h.Hub.Audit = queue.Audit;
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        h.Barrier("b", "stag0101");
        var w = W(h, "a");
        h.Barrier("a", "stagML11");
        Assert.True(await queue.DrainAsync(TimeSpan.FromSeconds(5)));
        Assert.Equal(77L, w.HistoryId);
        Assert.Equal(["start", "joined host", "joined guest", "end host_left", "left ended"], repo.Calls);
        Assert.Equal(0, queue.Failed);
        await queue.DisposeAsync();
    }

    private sealed class FakeHistory : DW4Online.Domain.Abstractions.IInstanceHistoryRepository
    {
        public List<string> Calls { get; } = [];
        public Task<long> StartAsync(DW4Online.Domain.Abstractions.InstanceHistoryStart s, CancellationToken ct)
        {
            Calls.Add("start");
            return Task.FromResult(77L);
        }
        public Task EndAsync(long id, string endReason, int peakPlayers, DateTime endedAt, CancellationToken ct)
        {
            Calls.Add("end " + endReason);
            return Task.CompletedTask;
        }
        public Task MemberJoinedAsync(long id, long accountId, int box, string role, DateTime joinedAt, CancellationToken ct)
        {
            Calls.Add("joined " + role);
            return Task.CompletedTask;
        }
        public Task MemberLeftAsync(long id, long accountId, DateTime joinedAt, string leaveReason, DateTime leftAt, bool sharedStory, CancellationToken ct)
        {
            Calls.Add("left " + leaveReason);
            return Task.CompletedTask;
        }
        public Task<int> CloseOpenAsync(DateTime now, CancellationToken ct) => Task.FromResult(0);
    }

    private sealed class FakeAudit(List<string> rows) : DW4Online.Domain.Abstractions.IAuditLog
    {
        public Task WriteAsync(string evt, long? accountId, long? actorId, byte[]? ip, object? detail, DateTime now, CancellationToken ct = default)
        {
            lock (rows) rows.Add(evt);
            return Task.CompletedTask;
        }
    }
}

/// <summary>The area names of AreaTable (shortcuts for the tests).</summary>
internal static class AreaTableName
{
    public const string DeathValley = DW4Online.Domain.World.AreaTable.DeathValley;
    public const string DryLand = DW4Online.Domain.World.AreaTable.DryLand;
}

internal static class ArrivalGroupTest
{
    public static ArrivalGroup With(this ArrivalGroup g, HubMember m)
    {
        g.Members.Add(new Arrival(m, 0, []));
        return g;
    }
}
