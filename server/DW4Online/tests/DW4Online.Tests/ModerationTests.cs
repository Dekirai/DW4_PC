using DW4Online.Domain.Rooms;
using DW4Online.Domain.Story;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// Round I1b's moderation lane on the hub without sockets (SEAMLESS.md 19.6.6, 19.11, 19.16; i1b-contract.md "moderation 16:25"):
/// the removal of a guest in a solo-led stage group with JOIN_REQ home, the owner switch and the detachment of a group that never
/// answers, the bar against the removed player's next arrival, the host's block as a removal, who may remove whom with the audit
/// rows and the world log, the snapshot's bars, and REPORT with its answer.
/// </summary>
public partial class HubLogicTests
{
    /// <summary>a hosts Dry Land and plays in dng205b1; b took the gate to stag0201: a guest's NEW, a solo-led stage group of its own.</summary>
    private static (Harness H, WorldInstance W) GuestApart()
    {
        var h = new Harness();
        Admit(h, "a", Player(100, "a") with { Story = StoryAt(4) });
        Admit(h, "b", Player(101, "b") with { Story = StoryAt(2), Friends = [100] });
        BarrierWith(h, "a", "stag0202");
        h.Snapshot("a");
        BarrierWith(h, "a", "dng205b1");
        h.Snapshot("a");
        var w = W(h, "a");
        BarrierWith(h, "b", "stag0201");
        Assert.Equal(ArrivalKind.GuestNew, h.Decision("b").World.Arrival);
        h.Snapshot("b");
        Assert.Same(w, W(h, "b"));
        Assert.Equal(InstanceMode.SoloLed, h.I("b").Mode);
        return (h, w);
    }

    [Fact]
    public void A_removed_guest_that_leads_alone_is_asked_home_its_story_with_the_hosts_key_is_refused_and_a_silent_group_is_detached()
    {
        var (h, w) = GuestApart();
        var notes = new List<string>();
        h.Hub.Audit = (e, _, _, _) => notes.Add(e);
        var group = h.I("b");
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, h.Mem("b").Id).Frame());
        // At once: the notice naming the host, the seat and the story owner, the bar, JOIN_REQ home (the group leads alone).
        var removed = h.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse).Single(n => n.Kind == WorldNoticeKind.Removed);
        Assert.Equal((h.Mem("a").Id, "a"), (removed.Member, removed.Name));
        Assert.Null(h.Mem("b").World);
        Assert.DoesNotContain(h.Mem("b"), w.Members);
        Assert.True(w.Bars.ContainsKey(101));
        Assert.Equal(0u, h.P("b").Last(Msg.InstanceInfo, InstanceInfoMsg.Parse).Wid);
        Assert.Contains("instance_remove", notes);
        var asks = () => h.P("b").All(Msg.JoinReq, JoinReqMsg.Parse).Where(r => r.Kind == JoinReqKind.Home).ToList();
        Assert.Equal((byte)(1 << h.Mem("b").Slot), asks().Single().Slot);
        // A modified client keeps writing the host's story: refused (owner), nothing reaches a's keeper.
        var revision = w.Keeper.Story.Revision;
        h.Send("b", new StoryMsg(group.Id, group.Visit, 0, new StoryDelta { Flags = [67] }, h.Mem("a").Id, 1).Frame());
        Assert.Equal(StoryRefusal.Owner, h.P("b").Last(Msg.StoryAck, StoryAckMsg.Parse).Refused.Single().Reason);
        Assert.Equal(revision, w.Keeper.Story.Revision);
        // It never answers: asked again until Instances:JoinRequestTries (3), then the group is nobody's.
        for (var k = 0; k < 4 && !group.Detached; ++k)
        {
            h.Advance(5100);
            h.Hub.Housekeep(h.Now);
            h.Hub.Housekeep(h.Now);
        }
        Assert.True(group.Detached);
        Assert.Null(group.World);
        Assert.Equal(3, asks().Count);
        Assert.DoesNotContain(group, w.Groups);
        // Its next barrier, even into a field of a's area, is a DISSOLVE home on its own story (the redirect into the lobby).
        BarrierWith(h, "b", "stag0202");
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.Dissolve, StoryMode.Replace, h.Mem("b").Id, "stagml11"),
            (ok.Decision, ok.World.StoryMode, ok.World.OwnerMember, ok.World.RedirectStage.ToLowerInvariant()));
        // a's instance played on.
        Assert.False(w.Ending);
        Assert.Same(w, W(h, "a"));
    }

    [Fact]
    public void The_bar_keeps_the_removed_player_out_of_the_instance_for_ten_minutes_and_then_lets_him_in_again()
    {
        var (h, w) = GuestApart();
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, h.Mem("b").Id).Frame());
        var ask = h.P("b").Last(Msg.JoinReq, JoinReqMsg.Parse);
        h.Log("b", 0, 60);
        h.Send("b", new JoinAtMsg(ask.Inst, ask.Visit, 60).Frame());
        h.Seal(200);
        BarrierWith(h, "b", "stagML11");
        Assert.Equal(BarrierDecision.Dissolve, h.Decision("b").Decision);
        // The snapshot's row names the bar (the website's and the API's lists hide the instance from b).
        h.Hub.Housekeep(h.Now);
        Assert.Equal([101L], h.Hub.CachedSnapshot.Instances.Single(r => r.Wid == w.Wid).Barred);
        // b takes the gate to stag0201 again: a's instance is no candidate, b hosts one of its own.
        BarrierWith(h, "b", "stag0201");
        Assert.NotSame(w, W(h, "b"));
        Assert.Equal(ArrivalKind.NewInstance, h.Decision("b").World.Arrival);
        BarrierWith(h, "b", "stagML11");
        // Ten minutes later the bar is gone (the housekeeping drops it) and b's arrival enters a's instance again.
        h.Advance(10 * 60_000 + 1000);
        h.Hub.Housekeep(h.Now);
        Assert.Empty(w.Bars);
        Assert.Empty(h.Hub.CachedSnapshot.Instances.Single(r => r.Wid == w.Wid).Barred);
        BarrierWith(h, "b", "stag0201");
        Assert.Same(w, W(h, "b"));
    }

    [Fact]
    public void A_hosts_new_block_removes_the_guest_and_no_other_block_removes_anyone()
    {
        var h = SealedPair(out _);
        var audits = new List<(string Event, long? Account, long? Actor, string Detail)>();
        h.Hub.Audit = (e, account, actor, detail) => audits.Add((e, account, actor, System.Text.Json.JsonSerializer.Serialize(detail)));
        var w = W(h, "a");
        h.Seal(300);
        // The guest blocks the host: nobody is removed (19.11: only the blocker's own instance as host).
        h.Hub.Readmitted(h.Mem("b").AccountId, Player(h.Mem("b").AccountId, "b") with { Blocks = [h.Mem("a").AccountId] }, h.Now);
        Assert.Same(w, W(h, "b"));
        Assert.Empty(audits);
        // The host blocks the guest: removed as by INSTANCE_OP (HOME removed, the bar), audited instance_remove with block.
        h.Hub.Readmitted(h.Mem("a").AccountId, Player(h.Mem("a").AccountId, "a") with { Blocks = [h.Mem("b").AccountId] }, h.Now);
        Assert.Null(h.Mem("b").World);
        Assert.True(w.Bars.ContainsKey(h.Mem("b").AccountId));
        var row = audits.Single();
        Assert.Equal(("instance_remove", h.Mem("b").AccountId, h.Mem("a").AccountId), (row.Event, row.Account, row.Actor));
        Assert.Contains("\"block\":true", row.Detail);
        h.Seal(100);
        Assert.Contains(h.P("a").Ticks().SelectMany(t => t.Rec.Events), e => e.Type == Ev.Home && e.HomeMask == 0b10 && e.Payload[1] == (byte)HomeReason.Removed);
        // A re-check with the same block removes nobody again.
        h.Hub.Readmitted(h.Mem("a").AccountId, Player(h.Mem("a").AccountId, "a") with { Blocks = [h.Mem("b").AccountId] }, h.Now);
        Assert.Single(audits);
    }

    [Fact]
    public void Only_the_host_and_seated_staff_remove_never_the_host_and_staff_removals_are_audited_as_staff_remove()
    {
        var h = new Harness();
        var audits = new List<(string Event, long? Account, long? Actor, string Detail)>();
        h.Hub.Audit = (e, account, actor, detail) => audits.Add((e, account, actor, System.Text.Json.JsonSerializer.Serialize(detail)));
        var notes = new List<string>();
        Admit(h, "a", Player(100, "a"));
        Admit(h, "b", Player(101, "b"));
        Admit(h, "c", Player(102, "c"));
        Admit(h, "m", Player(103, "m") with { Staff = true });
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        foreach (var x in new[] { "b", "c", "m" })
        {
            h.Barrier(x, "stag0101");
            Assert.Same(W(h, "a"), W(h, x));
        }
        var w = W(h, "a");
        audits.Clear(); // (staff_join)
        // A guest removes nobody; nobody removes the host; the host does not remove himself.
        h.Send("b", new InstanceOpMsg(InstanceOpKind.Remove, h.Mem("c").Id).Frame());
        h.Send("m", new InstanceOpMsg(InstanceOpKind.Remove, h.Mem("a").Id).Frame());
        h.Advance(1100);
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, h.Mem("a").Id).Frame());
        Assert.Equal(4, w.Seats.Count);
        Assert.Empty(audits);
        // Seated staff remove a guest: staff_remove with the removed member's role, HOME reason staff.
        h.Advance(1100);
        h.Send("m", new InstanceOpMsg(InstanceOpKind.Remove, h.Mem("c").Id).Frame());
        Assert.Null(h.Mem("c").World);
        var row = audits.Single();
        Assert.Equal(("staff_remove", 102L, 103L), (row.Event, row.Account, row.Actor));
        Assert.Contains("\"role\":\"guest\"", row.Detail);
        Assert.Contains($"\"hostId\":100", row.Detail);
        Assert.True(w.Bars.ContainsKey(102));
    }


    /// <summary>The host's notices about <paramref name="about"/> (joined / left), in order.</summary>
    private static List<string> SeatNotices(Harness h, string receiver, string about) => h.P(receiver).All(Msg.WorldNotice, WorldNoticeMsg.Parse)
        .Where(n => n.Member == h.Mem(about).Id && n.Kind is WorldNoticeKind.Joined or WorldNoticeKind.Left).Select(n => n.Kind.ToString()).ToList();

    [Fact]
    public void A_fresh_arrival_removed_while_its_join_waits_goes_home_at_once_and_is_never_seated_again()
    {
        var h = new Harness();
        Admit(h, "a", Player(100, "a"));
        Admit(h, "b", Player(101, "b"));
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a", joinable: false); // a scene: an arrival's JOIN waits (HoldKind.Join)
        h.Barrier("b", "stag0101");
        var w = W(h, "a");
        // b holds its seat while it waits (I1a: the seat at the decision): the host sees "b joined." and removes it.
        Assert.True(w.Seats.ContainsKey(h.Mem("b")));
        Assert.Equal(WaitReason.NotJoinable, h.P("b").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason);
        Assert.False(h.P("b").Has(Msg.BarrierOk));
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, h.Mem("b").Id).Frame());
        // At once: the notice, then the DISSOLVE into the lobby on b's own story; no seat, the bar, no HOME owed any more.
        Assert.Equal(WorldNoticeKind.Removed, h.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse).Last().Kind);
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.Dissolve, h.Mem("b").Id, "stagml11", RoomHub.HomePoint),
            (ok.Decision, ok.World.OwnerMember, ok.World.RedirectStage.ToLowerInvariant(), ok.World.RedirectPoint));
        Assert.False(w.Seats.ContainsKey(h.Mem("b")));
        Assert.True(w.Bars.ContainsKey(101));
        Assert.Null(h.Mem("b").HomeWish);
        Assert.Equal(InstanceMode.Hub, h.I("b").Mode);
        // The scene ends: nothing joins (before the review's fix the hold's JOIN seated b again: "b joined." a second time, the
        // host's story owner, HOME only later).
        h.Joinable("a");
        h.Hub.Housekeep(h.Now);
        h.Hub.Housekeep(h.Now);
        Assert.Single(h.P("b").All(Msg.BarrierOk, BarrierOkMsg.Parse));
        Assert.False(w.Seats.ContainsKey(h.Mem("b")));
        Assert.Null(h.Mem("b").World);
        Assert.Equal(["Joined", "Left"], SeatNotices(h, "a", "b"));
        Assert.False(w.Ending);
    }

    [Fact]
    public void World_TestJoinHoldMs_holds_a_join_like_a_scene_and_a_removal_in_it_sends_the_member_home_as_scenario_W10d_needs()
    {
        var h = new Harness();
        h.Hub.WorldOptions.TestJoinHoldMs = 20_000;
        Admit(h, "a", Player(100, "a"));
        Admit(h, "b", Player(101, "b"));
        Admit(h, "c", Player(102, "c"));
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        // a is joinable, yet c's JOIN waits for the hook's 20 s (BARRIER_WAIT not joinable), then joins.
        h.Barrier("c", "stag0101");
        Assert.Equal(WaitReason.NotJoinable, h.P("c").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason);
        h.Advance(19_000);
        h.Hub.Housekeep(h.Now);
        Assert.False(h.P("c").Has(Msg.BarrierOk));
        h.Advance(1_500);
        h.Hub.Housekeep(h.Now);
        Assert.Equal(BarrierDecision.Join, h.Decision("c").Decision);
        // b waits the same way; the host removes it in the wait: home at once, as without the hook.
        h.Barrier("b", "stag0101");
        Assert.True(W(h, "a").Seats.ContainsKey(h.Mem("b")));
        h.Advance(1_000);
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, h.Mem("b").Id).Frame());
        Assert.Equal(BarrierDecision.Dissolve, h.Decision("b").Decision);
        h.Advance(30_000);
        h.Hub.Housekeep(h.Now);
        Assert.Single(h.P("b").All(Msg.BarrierOk, BarrierOkMsg.Parse));
        Assert.Equal(["Joined", "Left"], SeatNotices(h, "a", "b"));
    }

    /// <summary>a hosts Dry Land and plays alone in dng205b1 (joinable); b and c are a sealed stage group of a's instance at stag0201.</summary>
    private static (Harness H, WorldInstance W) GuestPairApart()
    {
        var h = new Harness();
        Admit(h, "a", Player(100, "a") with { Story = StoryAt(4) });
        Admit(h, "b", Player(101, "b") with { Story = StoryAt(2), Friends = [100] });
        Admit(h, "c", Player(102, "c") with { Story = StoryAt(2), Friends = [100] });
        BarrierWith(h, "a", "stag0202");
        h.Snapshot("a");
        BarrierWith(h, "a", "dng205b1");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        var w = W(h, "a");
        // b takes the gate to stag0201 alone: a guest's NEW (solo-led); c follows and joins b's stage group (JOIN, sealed).
        BarrierWith(h, "b", "stag0201");
        h.Snapshot("b");
        h.Log("b", 0, 120);
        h.Joinable("b");
        BarrierWith(h, "c", "stag0201");
        var okC = h.Decision("c");
        Assert.Equal(BarrierDecision.Join, okC.Decision);
        h.Send("c", new ReadyMsg(okC.NewInst, okC.NewVisit, 120, 0, 5000, [7]).Frame());
        var req = h.P("b").Last(Msg.JoinReq, JoinReqMsg.Parse);
        h.Send("b", new JoinAtMsg(req.Inst, req.Visit, 155).Frame());
        h.Log("b", 120, 35);
        Assert.Same(w, W(h, "b"));
        Assert.Same(w, W(h, "c"));
        Assert.Same(h.I("b"), h.I("c"));
        Assert.Equal(InstanceMode.Sealed, h.I("b").Mode);
        return (h, w);
    }

    [Fact]
    public void A_member_removed_while_its_stage_group_gathers_at_a_barrier_goes_home_alone_and_the_others_join_on()
    {
        var (h, w) = GuestPairApart();
        h.Seal(300);
        // b reports the barrier into a's stage; the host removes c while the group gathers; then c reports it too.
        h.Barrier("b", "dng205b1");
        var decided = h.P("c").All(Msg.BarrierOk, BarrierOkMsg.Parse).Count;
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, h.Mem("c").Id).Frame());
        Assert.Null(h.Mem("c").World);
        Assert.Equal(decided, h.P("c").All(Msg.BarrierOk, BarrierOkMsg.Parse).Count); // (no decision before the barrier's)
        h.Barrier("c", "dng205b1");
        // b joins a's stage group; c goes home alone on its own story (replace: its engine held a's), into the lobby.
        var okB = h.Decision("b");
        var okC = h.Decision("c");
        Assert.Equal((BarrierDecision.Join, h.Mem("a").Id), (okB.Decision, okB.World.OwnerMember));
        Assert.Equal((BarrierDecision.Dissolve, StoryMode.Replace, h.Mem("c").Id, "stagml11"),
            (okC.Decision, okC.World.StoryMode, okC.World.OwnerMember, okC.World.RedirectStage.ToLowerInvariant()));
        Assert.Equal(["a", "b"], w.Seats.Keys.Select(x => x.Name).Order());
        Assert.Null(h.Mem("c").World);
        Assert.Null(h.Mem("c").HomeWish);
        Assert.Equal(["Joined", "Left"], SeatNotices(h, "a", "c"));
        // A STORY with a's key from c's modified client: refused (owner), nothing reaches a's keeper.
        var revision = w.Keeper.Story.Revision;
        var home = h.I("c");
        h.Send("c", new StoryMsg(home.Id, home.Visit, 0, new StoryDelta { Flags = [67] }, h.Mem("a").Id, 1).Frame());
        Assert.Equal(StoryRefusal.Owner, h.P("c").Last(Msg.StoryAck, StoryAckMsg.Parse).Refused.Single().Reason);
        Assert.Equal(revision, w.Keeper.Story.Revision);
    }

    /// <summary>a hosts stag0101 and plays alone (joinable, 120 ticks logged); b arrives from home and is decided a JOIN.</summary>
    private static (Harness H, WorldInstance W, BarrierOkMsg Join) JoiningGuest()
    {
        var h = new Harness();
        Admit(h, "a", Player(100, "a"));
        Admit(h, "b", Player(101, "b"));
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Joinable("a");
        h.Log("a", 0, 120);
        h.Barrier("b", "stag0101");
        var join = h.Decision("b");
        Assert.Equal((BarrierDecision.Join, ArrivalKind.AutoJoin), (join.Decision, join.World.Arrival));
        Assert.NotNull(h.Mem("b").Joining);
        return (h, W(h, "a"), join);
    }

    [Fact]
    public void A_member_removed_while_its_join_runs_is_never_seated_again_and_gets_its_home_in_the_stage_group()
    {
        var (h, w, join) = JoiningGuest();
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, h.Mem("b").Id).Frame());
        Assert.False(w.Seats.ContainsKey(h.Mem("b")));
        // The join runs on (b's engine is catching up): READY, a's JOIN_AT; b is in a's stage group without a seat.
        h.Send("b", new ReadyMsg(join.NewInst, join.NewVisit, 120, 0, 5000, [7]).Frame());
        var req = h.P("a").Last(Msg.JoinReq, JoinReqMsg.Parse);
        h.Send("a", new JoinAtMsg(req.Inst, req.Visit, 155).Frame());
        h.Log("a", 120, 35);
        Assert.Same(h.I("a"), h.Mem("b").Instance);
        for (var k = 0; k < 3; ++k)
        {
            h.Seal(300);
            h.Hub.Housekeep(h.Now);
        }
        // Its owed HOME is stamped there (reason removed); it never got a seat again, the host was told once.
        Assert.Contains(h.P("a").Ticks().SelectMany(t => t.Rec.Events),
            e => e.Type == Ev.Home && e.HomeMask == (byte)(1 << h.Mem("b").Slot) && e.Payload[1] == (byte)HomeReason.Removed);
        Assert.False(w.Seats.ContainsKey(h.Mem("b")));
        Assert.Null(h.Mem("b").World);
        Assert.Equal(["Joined", "Left"], SeatNotices(h, "a", "b"));
    }

    [Fact]
    public void A_member_removed_while_its_join_runs_gets_no_seat_from_the_joins_fallback_and_is_asked_home_at_once()
    {
        var (h, w, _) = JoiningGuest();
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, h.Mem("b").Id).Frame());
        // a walks on before b's READY: b's fallback is a stage group of nobody's instance (fix-019's FallbackHome: BARRIER_OK wid 0, no
        // story; its engine still holds a's), no seat (before the lane's review fix TakeSeat seated it again) and its owed HOME at once
        // (JOIN_REQ home: it leads alone).
        h.Barrier("a", "stag0102", tick: 119);
        var fallback = h.Decision("b");
        Assert.Equal((BarrierDecision.New, 0u), (fallback.Decision, fallback.World.Wid));
        Assert.False(w.Seats.ContainsKey(h.Mem("b")));
        Assert.Null(h.Mem("b").World);
        Assert.Contains(h.P("b").All(Msg.JoinReq, JoinReqMsg.Parse), r => r.Kind == JoinReqKind.Home);
        Assert.Equal(["Joined", "Left"], SeatNotices(h, "a", "b"));
        // A STORY with a's key from b: refused (owner).
        var group = h.I("b");
        h.Send("b", new StoryMsg(group.Id, group.Visit, 0, new StoryDelta { Flags = [67] }, h.Mem("a").Id, 1).Frame());
        Assert.Equal(StoryRefusal.Owner, h.P("b").Last(Msg.StoryAck, StoryAckMsg.Parse).Refused.Single().Reason);
    }

    /// <summary>a hosts Dry Land (dng205b1, joinable); b (stag0201) and c (stag0202) are two solo-led stage groups of a's instance. b, c
    /// and a announce stag0203; b arrives first and holds as a co-arrival base awaiting c and a; c arrives and is b's co-arrival; the
    /// base still awaits a (the I1b review of 2026-11-12, its probes).</summary>
    private static (Harness H, WorldInstance W) CoArrivalOfAGuestBase()
    {
        var h = new Harness();
        Admit(h, "a", Player(100, "a") with { Story = StoryAt(4) });
        Admit(h, "b", Player(101, "b") with { Story = StoryAt(2), Friends = [100] });
        Admit(h, "c", Player(102, "c") with { Story = StoryAt(2), Friends = [100] });
        BarrierWith(h, "a", "stag0202");
        h.Snapshot("a");
        BarrierWith(h, "a", "dng205b1");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        var w = W(h, "a");
        foreach (var (n, stage) in new[] { ("b", "stag0201"), ("c", "stag0202") })
        {
            BarrierWith(h, n, stage);
            Assert.Equal(BarrierDecision.New, h.Decision(n).Decision);
            h.Snapshot(n);
            h.Log(n, 0, 60);
            h.Joinable(n);
            Assert.Same(w, W(h, n));
        }
        Assert.NotSame(h.I("b"), h.I("c"));
        foreach (var n in new[] { "b", "c", "a" }) h.Send(n, new HeadingMsg(h.Inst(n), "stag0203", 0, 5).Frame());
        BarrierWith(h, "b", "stag0203");
        Assert.Equal(WaitReason.CoArrival, h.P("b").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason);
        var decided = h.P("c").All(Msg.BarrierOk, BarrierOkMsg.Parse).Count;
        BarrierWith(h, "c", "stag0203");
        Assert.Equal(decided, h.P("c").All(Msg.BarrierOk, BarrierOkMsg.Parse).Count); // (b's co-arrival; the base waits for a)
        Assert.Same(h.Mem("b").Arrival, h.Mem("c").Arrival);
        return (h, w);
    }

    [Fact]
    public void Removing_the_only_member_of_a_holding_base_lets_its_co_arrival_decide_on_its_own_and_play_on_in_the_instance()
    {
        var (h, w) = CoArrivalOfAGuestBase();
        var c = h.Mem("c");
        var parked = c.Instance!;
        var decided = h.P("c").All(Msg.BarrierOk, BarrierOkMsg.Parse).Count;
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, h.Mem("b").Id).Frame());
        Assert.Equal(BarrierDecision.Dissolve, h.Decision("b").Decision);
        // c parks under its own name again and decides as if it had just arrived: a is still announced, so it holds for him (before the
        // review's fix c waited for a base that was gone: no BARRIER_OK, no BARRIER_WAIT, its client's fallback after 5 s).
        Assert.Equal([c], c.Arrival!.Members.Select(x => x.Member));
        Assert.Equal(WaitReason.CoArrival, h.P("c").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason);
        for (var k = 0; k < 5; ++k)
        {
            h.Advance(1000);
            h.Hub.Housekeep(h.Now);
        }
        // The hold ends: c plays on as a stage group of a's instance at stag0203, its seat kept, the host's story.
        var ok = h.Decision("c");
        Assert.Equal(decided + 1, h.P("c").All(Msg.BarrierOk, BarrierOkMsg.Parse).Count);
        Assert.Equal((BarrierDecision.New, parked.Id, w.Wid, h.Mem("a").Id), (ok.Decision, ok.Inst, ok.World.Wid, ok.World.OwnerMember));
        Assert.True(w.Seats.ContainsKey(c));
        Assert.Same(w, h.I("c").World);
        Assert.Null(c.Arrival);
        Assert.Equal(0, h.Hub.Counters.GetValueOrDefault("faults"));
    }

    [Fact]
    public void A_co_arrival_removed_while_its_base_holds_goes_home_from_its_own_barrier_and_never_co_arrives_into_the_instance()
    {
        var (h, w) = CoArrivalOfAGuestBase();
        var c = h.Mem("c");
        var parked = c.Instance!;
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, c.Id).Frame());
        // At once: the DISSOLVE of the visit c parked in, into the lobby on its own story (its engine held a's), no seat, no HOME owed.
        var home = h.Decision("c");
        Assert.Equal((BarrierDecision.Dissolve, parked.Id, parked.Visit, c.Id, StoryMode.Replace, "stagml11", RoomHub.HomePoint),
            (home.Decision, home.Inst, home.Visit, home.World.OwnerMember, home.World.StoryMode, home.World.RedirectStage.ToLowerInvariant(),
                home.World.RedirectPoint));
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("removed_at_decision"));
        Assert.Null(c.Arrival);
        Assert.Null(c.HomeWish);
        Assert.Equal(InstanceMode.Hub, h.I("c").Mode);
        // The base goes on without it when its hold ends: NEW with no co-arrival; c never gets CO_ARRIVE into a's instance (before the
        // review's fix it did, without a seat, with the host's story and owner key).
        for (var k = 0; k < 5; ++k)
        {
            h.Advance(1000);
            h.Hub.Housekeep(h.Now);
        }
        var b = h.Decision("b");
        Assert.Equal((BarrierDecision.New, 0), (b.Decision, b.CoArrivals.Count));
        Assert.Single(h.P("c").All(Msg.BarrierOk, BarrierOkMsg.Parse), x => x.Decision == BarrierDecision.Dissolve);
        Assert.DoesNotContain(h.P("c").All(Msg.BarrierOk, BarrierOkMsg.Parse), x => x.Decision == BarrierDecision.CoArrive);
        Assert.False(w.Seats.ContainsKey(c));
        Assert.Null(c.World);
        Assert.NotSame(w, h.I("c").World);
        Assert.Equal(["Joined", "Left"], SeatNotices(h, "a", "c"));
        Assert.Equal(0, h.Hub.Counters.GetValueOrDefault("faults"));
    }

    [Fact]
    public void A_NEW_never_places_a_co_arrival_barred_from_the_instance_without_a_seat_whatever_path_led_there()
    {
        var (h, w) = CoArrivalOfAGuestBase();
        var c = h.Mem("c");
        // A path that took c's seat and barred it without sending it home (none is known since the review's fixes): New, when a arrives
        // as the base's last awaited member (no CheckHolds pass between), is the last check.
        Assert.True(w.Seats.Remove(c));
        c.World = null;
        w.Bars[c.AccountId] = h.Now + 600 * System.Diagnostics.Stopwatch.Frequency;
        BarrierWith(h, "a", "stag0203");
        Assert.Equal((BarrierDecision.Dissolve, "stagml11"), (h.Decision("c").Decision, h.Decision("c").World.RedirectStage.ToLowerInvariant()));
        Assert.DoesNotContain(h.P("c").All(Msg.BarrierOk, BarrierOkMsg.Parse), x => x.Decision == BarrierDecision.CoArrive);
        var b = h.Decision("b");
        Assert.Equal((BarrierDecision.New, 1), (b.Decision, b.CoArrivals.Count)); // (a, the last awaited one, came along)
        Assert.Equal(h.Mem("a").Id, b.CoArrivals.Single().Member);
        Assert.Equal(0, h.Hub.Counters.GetValueOrDefault("faults"));
    }

    [Fact]
    public void The_co_arrival_of_a_base_whose_only_member_left_the_world_is_its_own_arrival_for_a_removal()
    {
        var (h, w) = CoArrivalOfAGuestBase();
        var c = h.Mem("c");
        h.Hub.Leave(101, h.Now); // b leaves the world while its base holds (POST /world/leave)
        // c decides on its own (DetachLocked) and holds for a under its own name: its arrival is its group, not b's gone base.
        Assert.Equal([c], c.Arrival!.Members.Select(x => x.Member));
        Assert.Equal(WaitReason.CoArrival, h.P("c").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason);
        // So the host's removal finds it at once (before, only the next housekeeping pass did).
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, c.Id).Frame());
        Assert.Equal((BarrierDecision.Dissolve, "stagml11"), (h.Decision("c").Decision, h.Decision("c").World.RedirectStage.ToLowerInvariant()));
        Assert.Null(c.Arrival);
        for (var k = 0; k < 5; ++k)
        {
            h.Advance(1000);
            h.Hub.Housekeep(h.Now);
        }
        Assert.Single(h.P("c").All(Msg.BarrierOk, BarrierOkMsg.Parse), x => x.Decision == BarrierDecision.Dissolve);
        Assert.False(w.Seats.ContainsKey(c));
        Assert.Equal(0, h.Hub.Counters.GetValueOrDefault("faults"));
    }

    [Fact]
    public void The_co_arrival_of_a_base_whose_only_member_left_the_world_may_leave_too_without_leaving_an_empty_hold()
    {
        var (h, w) = CoArrivalOfAGuestBase();
        h.Hub.Leave(101, h.Now);
        Assert.Equal(WaitReason.CoArrival, h.P("c").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason);
        // c leaves the world too while it holds for a: its hold goes with it (before, c's arrival still named b's gone base, so c's
        // own hold stayed with no member and its end failed in the housekeeping: a NEW of nobody).
        h.Hub.Leave(102, h.Now);
        for (var k = 0; k < 5; ++k)
        {
            h.Advance(1000);
            h.Hub.Housekeep(h.Now);
        }
        Assert.Equal(0, h.Hub.Counters.GetValueOrDefault("faults"));
        Assert.Equal(["a"], w.Seats.Keys.Select(x => x.Name));
        Assert.False(w.Ending);
    }

    [Fact]
    public void A_report_names_the_reported_account_and_the_shared_instance_and_its_answer_reaches_the_reporter()
    {
        var h = SealedPair(out _);
        h.Hub.Housekeep(h.Now); // (INSTANCE_INFO: b's client knows a, the card's member)
        var w = W(h, "a");
        w.HistoryId = 77;
        var reports = new List<ReportRequest>();
        h.Hub.Reports = reports.Add;
        h.Send("b", new ReportMsg(h.Mem("a").Id, 2, w.Wid).Frame());
        var r = reports.Single();
        Assert.Equal((h.Mem("b").AccountId, h.Mem("a").AccountId, h.Mem("a").Id, "a", "cheating", (long?)77),
            (r.ReporterAccount, r.ReportedAccount, r.ReportedMember, r.ReportedName, r.Reason, r.HistoryId));
        h.Hub.ReportAnswered(r.ReporterAccount, r.ReportedMember, r.ReportedName, ReportResult.Sent);
        var answer = h.P("b").Last(Msg.WorldNotice, WorldNoticeMsg.Parse);
        Assert.Equal((WorldNoticeKind.Report, h.Mem("a").Id, "a", (uint)ReportResult.Sent), (answer.Kind, answer.Member, answer.Name, answer.Argument));
        // The own id and an unknown one: "not available" at once without a name, nothing stored.
        h.Send("b", new ReportMsg(h.Mem("b").Id, 1, 0).Frame());
        h.Send("b", new ReportMsg(9999, 1, 0).Frame());
        Assert.Single(reports);
        Assert.Equal([((uint)ReportResult.NotAvailable, ""), ((uint)ReportResult.NotAvailable, "")],
            h.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse).Where(n => n.Kind == WorldNoticeKind.Report).Skip(1).Select(n => (n.Argument, n.Name)));
        // Three at once, then ten a minute as POST /reports (one each 6 s): the fourth within it is dropped without an answer.
        h.Advance(60_000);
        var before = h.P("b").Frames.Count;
        for (var k = 0; k < 4; ++k) h.Send("b", new ReportMsg(h.Mem("a").Id, 1, 0).Frame());
        Assert.Equal(4, reports.Count);
        h.Advance(1500);
        h.Send("b", new ReportMsg(h.Mem("a").Id, 1, 0).Frame());
        Assert.Equal(4, reports.Count);
        h.Advance(6000);
        h.Send("b", new ReportMsg(h.Mem("a").Id, 1, 0).Frame());
        Assert.Equal(5, reports.Count);
        // A wrong reason is a protocol error.
        Assert.Throws<ProtocolException>(() => h.Send("b", new ReportMsg(h.Mem("a").Id, 5, 0).Frame()));
        Assert.Throws<ProtocolException>(() => ReportMsg.Parse(Convert.FromHexString("0100000000" + "00000000")));
        Assert.Equal(before, h.P("b").Frames.Count);
    }

    [Fact]
    public void A_report_resolves_only_members_the_reporter_was_told_about_and_tells_nobodys_name_or_presence()
    {
        var h = new Harness();
        Admit(h, "a", Player(100, "a"));
        Admit(h, "b", Player(101, "b"));
        Admit(h, "z", Player(200, "secretname"));
        var reports = new List<ReportRequest>();
        // A report service whose daily limit was reached (WorldGateway maps report_limit to Limit).
        h.Hub.Reports = r =>
        {
            reports.Add(r);
            h.Hub.ReportAnswered(r.ReporterAccount, r.ReportedMember, r.ReportedName, ReportResult.Limit);
        };
        ReportAnswer Answer(uint member)
        {
            h.Advance(7000);
            h.Send("b", new ReportMsg(member, 4, 0).Frame());
            var n = h.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse).Last(x => x.Kind == WorldNoticeKind.Report);
            return new ReportAnswer(n.Member, (ReportResult)n.Argument, n.Name);
        }
        // b never met anyone: every member id, a live one or none at all, answers the same: "not available", no name, no report.
        foreach (var id in new uint[] { h.Mem("a").Id, h.Mem("z").Id, 77 })
            Assert.Equal(new ReportAnswer(id, ReportResult.NotAvailable, ""), Answer(id));
        Assert.Empty(reports);
        // a hosts, b joins: b's client is told about a (INSTANCE_INFO), not about z.
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        h.Barrier("b", "stag0101");
        Assert.Same(W(h, "a"), W(h, "b"));
        Assert.Equal(new ReportAnswer(h.Mem("a").Id, ReportResult.Limit, "a"), Answer(h.Mem("a").Id));
        Assert.Equal(new ReportAnswer(h.Mem("z").Id, ReportResult.NotAvailable, ""), Answer(h.Mem("z").Id));
        Assert.Equal([100L], reports.Select(r => r.ReportedAccount));
        // a leaves the world; an hour later b can still report him (b's own record, not a's presence): the same answer as before.
        h.Hub.Leave(100, h.Now);
        h.Advance(3_600_000);
        h.Hub.Housekeep(h.Now);
        Assert.Equal(new ReportAnswer(h.Mem("a").Id, ReportResult.Limit, "a"), Answer(h.Mem("a").Id));
        Assert.Equal((100L, "a", (long?)null), (reports.Last().ReportedAccount, reports.Last().ReportedName, reports.Last().HistoryId));
        // Without a report service the answer is "failed".
        h.Hub.Reports = null;
        Assert.Equal(new ReportAnswer(h.Mem("a").Id, ReportResult.Failed, "a"), Answer(h.Mem("a").Id));
    }

    private sealed record ReportAnswer(uint Member, ReportResult Result, string Name);
}
