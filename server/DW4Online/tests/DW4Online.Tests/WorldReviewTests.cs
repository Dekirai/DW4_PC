using DW4Online.Domain.Rooms;
using DW4Online.Domain.Story;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// Round I1a's review of the relay lane: the rules the review found broken or untested, each with a test that fails without it. The
/// story owner key after the host left the world, WELCOME never resumable without its instance, the choice *New instance* through a
/// co-arrival hold, INSTANCE_OP's throttle and the settled audit row, the live re-check's handle and order, M3 for a barrier into a
/// field, a *Leave instance* at a barrier, the starting window, the co-arrival base's visibility and blocks, the data hash filter,
/// the host's grace greying his instance, the presence budget and the staff tag.
/// </summary>
public partial class HubLogicTests
{
    /// <summary>a's instance at <paramref name="stage"/>, joinable and past its starting window; b and c at home.</summary>
    private static Harness RunningInstanceAt(string stage = "stag0101")
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Connect("c", 2);
        h.Barrier("a", stage);
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        h.Advance(11_000);
        h.Hub.Housekeep(h.Now);
        return h;
    }

    [Fact]
    public void No_story_ack_carries_another_story_with_owner_zero_after_the_host_left_the_world()
    {
        // A guest that reconnects while the instance of a host who left the world ends: its own story only (the client reads owner
        // 0 as its own key, client 13:30; the host's story so would become its own confirmed story).
        var h = RunningInstanceAt();
        h.Barrier("b", "stag0101");
        var w = W(h, "a");
        Assert.Same(w, W(h, "b"));
        h.Hub.Disconnect(h.Mem("b"), h.P("b"), h.Now);
        h.Hub.Leave(h.Mem("a").AccountId, h.Now); // POST /world/leave
        Assert.True(w.Ending);
        var b2 = h.Connect("b", 1);
        var acks = b2.All(Msg.StoryAck, StoryAckMsg.Parse);
        Assert.DoesNotContain(acks, k => k.OwnerMember == 0 && k.Story.Length > 0);
        Assert.Equal(h.Mem("b").Id, acks.Single().OwnerMember);

        // A guest's earned value that merges after the host left: the revision names the host's member id, never 0.
        var g = new Harness(new InstanceOptions { StoryMinVisitSeconds = 1, StoryFlagBurst = 1, StoryFlagSeconds = 2 });
        Admit(g, "a", Player(100, "a") with { Story = StoryAt(2) });
        Admit(g, "b", Player(101, "b"));
        g.Barrier("a", "stag0101");
        g.Snapshot("a");
        g.Barrier("a", "stag0102");
        g.Snapshot("a");
        g.Barrier("b", "stag0101");
        g.Snapshot("b");
        var host = g.Mem("a");
        var hostId = host.Id;
        var keeper = g.Hub.KeeperOf(host.AccountId)!;
        g.Advance(1500);
        g.Send("b", StoryOf(g, "b", new StoryDelta { Flags = [505, 506] }, hostId).Frame());
        Assert.Equal(1, keeper.EarnedCount);
        g.Hub.Leave(host.AccountId, g.Now);
        var before = g.P("b").Frames.Count;
        g.Advance(2500);
        g.Hub.Housekeep(g.Now);
        Assert.Equal(0, keeper.EarnedCount);
        var after = g.P("b").Frames.Skip(before).Where(f => f.Type == Msg.StoryAck).Select(f => StoryAckMsg.Parse(f.Payload)).ToList();
        Assert.NotEmpty(after);
        Assert.All(after, k => Assert.Equal(hostId, k.OwnerMember));
    }

    [Fact]
    public void A_welcome_without_the_members_instance_is_never_resumable_and_its_way_home_does_not_stop_the_others()
    {
        // b's link drops in the instance's sealed stage group, staff end the instance, b is back within its reconnect window.
        var h = SealedPair(out _);
        var w = W(h, "a");
        h.Seal(300);
        h.Hub.Disconnect(h.Mem("b"), h.P("b"), h.Now);
        h.Hub.StaffEnd(w.Wid, 999, h.Now);
        h.Advance(500);
        var group = h.I("a");
        var b2 = h.Connect("b", 1);
        var welcome = b2.Last(Msg.Welcome, WelcomeMsg.Parse);
        Assert.Equal((0u, 0), (welcome.Wid, welcome.Flags & WelcomeMsg.Resumable));
        Assert.NotSame(group, h.Mem("b").Instance);
        // b's client takes the local way home (19.6.10): a BARRIER into stagML11 from WELCOME's stage group. a's group seals on.
        Assert.True(group.Sealing);
        h.Send("b", new BarrierMsg(welcome.Inst, welcome.Visit, 50, "stagML11", 3, 0b1, 0b1, 5, [(byte)'b']).Frame());
        Assert.True(group.Sealing);
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.Dissolve, StoryMode.Replace, h.Mem("b").Id), (ok.Decision, ok.World.StoryMode, ok.World.OwnerMember));

        // A member removed while its link was down: its seat went with the removal, so no instance is named and none is resumed.
        var r = SealedPair(out _);
        var wr = W(r, "a");
        r.Seal(300);
        r.Hub.Disconnect(r.Mem("b"), r.P("b"), r.Now);
        Assert.True(r.Hub.StaffRemove(wr.Wid, r.Mem("b").AccountId, 999, r.Now));
        r.Advance(500);
        var rb = r.Connect("b", 1).Last(Msg.Welcome, WelcomeMsg.Parse);
        Assert.Equal((0u, 0), (rb.Wid, rb.Flags & WelcomeMsg.Resumable));
    }

    [Fact]
    public void The_choice_new_instance_holds_through_a_co_arrival_hold_that_nobody_joins()
    {
        var h = RunningInstanceAt();
        // b announces the same stage (HEADING) but never arrives; c chooses New instance (plan 0xFFFF).
        h.Send("b", new HeadingMsg(h.Inst("b"), "stag0101", 0, 5).Frame());
        var i = h.Mem("c").Instance!;
        h.Send("c", new BarrierMsg(i.Id, i.Visit, 0, "stag0101", 0, i.PresentMask, i.PresentMask, 5, [(byte)'c'], 0, BarrierMsg.PlanNewInstance).Frame());
        Assert.Equal(WaitReason.CoArrival, h.P("c").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason);
        h.Advance(3_500);
        h.Hub.Housekeep(h.Now);
        var ok = h.Decision("c");
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance), (ok.Decision, ok.World.Arrival));
        Assert.NotSame(W(h, "a"), W(h, "c"));
    }

    [Fact]
    public void Instance_op_is_throttled_and_a_burst_of_visibility_changes_writes_one_audit_row_once_settled()
    {
        var h = RunningInstanceAt();
        h.Barrier("b", "stag0101");
        var w = W(h, "a");
        Assert.Same(w, W(h, "b"));
        var audits = new List<(string Event, object? Detail)>();
        var remembered = new List<Visibility>();
        h.Hub.Audit = (e, _, _, d) => audits.Add((e, d));
        h.Hub.RememberVisibility = (_, v) => remembered.Add(v);
        // Friends, Public, Friends, ... 200 frames at one instant: the first three pass.
        for (var k = 1; k <= 200; ++k) h.Send("a", new InstanceOpMsg(InstanceOpKind.Visibility, (uint)(k & 1)).Frame());
        var notices = h.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse).Count(n => n.Kind == WorldNoticeKind.VisibilityChanged);
        Assert.Equal(RoomHub.InstanceOpBurst, notices);
        Assert.Equal(200 - RoomHub.InstanceOpBurst, h.Hub.Counters["instance_op_throttled"]);
        Assert.Equal(Visibility.Friends, w.Visibility);
        Assert.Empty(audits);
        Assert.Empty(remembered);
        h.Advance(RoomHub.VisibilitySettleMs + 100);
        h.Hub.Housekeep(h.Now);
        Assert.Equal(["instance_visibility"], audits.Select(x => x.Event));
        Assert.Equal([Visibility.Friends], remembered);
        // One a second after the burst.
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Visibility, 2).Frame());
        Assert.Equal(Visibility.Private, w.Visibility);
        // The end writes a change that has not settled yet.
        h.Hub.StaffEnd(w.Wid, 999, h.Now);
        Assert.Equal(2, audits.Count);
        Assert.Equal([Visibility.Friends, Visibility.Private], remembered);
    }

    [Fact]
    public void A_live_recheck_result_for_an_older_admission_or_overtaken_by_a_newer_one_is_dropped()
    {
        var h = new Harness();
        Admit(h, "p", Player(101, "p") with { Handle = "nonce-1" });
        var first = h.Hub.AdmissionHandles(101).Single();
        var second = h.Hub.AdmissionHandles(101).Single();
        Assert.True(second.Seq > first.Seq);
        // The newer re-check (a block) answers first; the older one (from before the block) after it changes nothing.
        h.Hub.Readmitted(101, Player(101, "p") with { Blocks = [200] }, h.Now, second.Handle, second.Seq);
        h.Hub.Readmitted(101, Player(101, "p"), h.Now, first.Handle, first.Seq);
        Assert.Contains(200L, h.Mem("p").Blocks);
        Assert.Equal(1, h.Hub.Counters["readmit_overtaken"]);
        // A reconnect with a newer ticket: a refusal for the old admission is no refusal of the account.
        var p2 = Admit(h, "p", Player(101, "p") with { Handle = "nonce-2" });
        var third = h.Hub.AdmissionHandles(101).Single();
        Assert.Equal("nonce-2", third.Handle);
        h.Hub.Readmitted(101, null, h.Now, "nonce-1", third.Seq);
        Assert.Null(p2.Closed);
        Assert.NotNull(h.Hub.MemberOf(101));
        Assert.Equal(1, h.Hub.Counters["readmit_stale"]);
        // Refused for the admission it has: CLOSE account.
        h.Hub.Readmitted(101, null, h.Now, "nonce-2", third.Seq);
        Assert.Equal(CloseReason.Account, p2.Closed);
        Assert.Null(h.Hub.MemberOf(101));
    }

    [Fact]
    public void A_hosts_barrier_with_no_living_slot_into_a_field_is_his_defeat_and_goes_home()
    {
        // M3: after a HOME the exit prompt leads the defeated host left alone on matz01 into stag0101 point 2 (alive mask 0).
        var h = new Harness();
        h.Connect("a", 0);
        h.Barrier("a", "dng101b1");
        h.Snapshot("a");
        var w = W(h, "a");
        BarrierWith(h, "a", "stag0101", alive: 0, point: 2);
        Assert.True(w.Ending);
        Assert.Equal(EndReason.HostDefeated, w.EndReason);
        var ok = h.Decision("a");
        Assert.Equal((BarrierDecision.Dissolve, RoomHub.HomeStage, RoomHub.HomePoint, StoryMode.Merge),
            (ok.Decision, ok.World.RedirectStage, ok.World.RedirectPoint, ok.World.StoryMode));
        Assert.Null(h.Mem("a").World);

        // A guest's own barrier with no living slot into a field is travel inside the instance (M3 is the host's).
        var g = new Harness();
        Admit(g, "a", Player(100, "a") with { Story = StoryAt(2) });
        Admit(g, "b", Player(101, "b"));
        g.Barrier("a", "stag0101");
        g.Snapshot("a");
        g.Barrier("a", "stag0102");
        g.Snapshot("a");
        g.Barrier("b", "stag0101");
        g.Snapshot("b");
        var wg = W(g, "a");
        Assert.Same(wg, W(g, "b"));
        BarrierWith(g, "b", "stag0103", alive: 0);
        Assert.False(wg.Ending);
        Assert.Same(wg, W(g, "b"));
        Assert.Equal(BarrierDecision.New, g.Decision("b").Decision);
    }

    [Fact]
    public void A_guests_leave_instance_while_its_stage_group_gathers_at_a_barrier_is_asked_again_in_its_next_stage_group()
    {
        var h = SealedPair(out _);
        var w = W(h, "a");
        h.Seal(500);
        var i = h.I("a");
        // a reached the barrier (b's report still to come); b presses Leave instance meanwhile, then reports the barrier too.
        h.Send("a", new BarrierMsg(i.Id, i.Visit, i.NextSeal - 1, "stag0202", 0, i.PresentMask, i.PresentMask, 5, [(byte)'a']).Frame());
        h.Send("b", new IntentMsg(i.Id, i.Visit, IntentKind.Leave, 0).Frame());
        Assert.Equal(1, h.Hub.Counters["home_at_barrier"]);
        h.Send("b", new BarrierMsg(i.Id, i.Visit, i.SealStop ?? 0, "stag0202", 0, i.PresentMask, i.PresentMask, 5, [(byte)'b']).Frame());
        Assert.Equal(BarrierDecision.New, h.Decision("b").Decision);
        Assert.Same(w, W(h, "b"));
        // The housekeeping asks b's HOME again in the new stage group; it is stamped once that group seals.
        h.Hub.Housekeep(h.Now);
        var next = h.I("b");
        var bit = (byte)(1 << h.Mem("b").Slot);
        Assert.Equal(bit, next.PendingHome?.Mask);
        h.Snapshot("a");
        h.Send("b", new SnapHashMsg(next.Id, next.Visit, 0xAA).Frame());
        Assert.True(next.Sealing);
        h.Seal(200);
        Assert.Contains(h.P("a").Ticks().SelectMany(t => t.Rec.Events),
            e => e.Type == Ev.Home && e.HomeMask == bit && e.Payload[1] == (byte)HomeReason.Leave);
        // Asked once per stage group: the next passes add nothing.
        h.Hub.Housekeep(h.Now);
        Assert.Equal(1, h.Hub.Counters["home_asked_again"]);
    }

    [Fact]
    public void An_arrival_within_the_starting_window_joins_the_starting_instance_with_the_hosts_story()
    {
        // W4b's rule (19.7.6): b 6 s after a, a's instance still starting (not joinable yet): b joins it (arrival 3, replace).
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Advance(6_000);
        h.Hub.Housekeep(h.Now);
        Assert.NotEqual(0, W(h, "a").StartingUntil);
        h.Barrier("b", "stag0101");
        Assert.Same(W(h, "a"), W(h, "b"));
        h.Joinable("a");
        h.Hub.Housekeep(h.Now);
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.Join, ArrivalKind.Starting, StoryMode.Replace, h.Mem("a").Id),
            (ok.Decision, ok.World.Arrival, ok.World.StoryMode, ok.World.OwnerMember));
    }

    [Fact]
    public void A_co_arrival_base_takes_only_arrivals_that_may_see_its_instance()
    {
        // A Private base waits for the staff member it may show itself to; a stranger arriving meanwhile is no co-arrival of it.
        var h = new Harness();
        Admit(h, "a", Player(100, "a") with { DefaultVisibility = Visibility.Private });
        Admit(h, "b", Player(101, "b"));
        Admit(h, "c", Player(102, "c") with { Staff = true });
        foreach (var n in new[] { "a", "b", "c" }) h.Send(n, new HeadingMsg(h.Inst(n), "stag0101", 0, 5).Frame());
        h.Barrier("a", "stag0101");
        Assert.Equal(WaitReason.CoArrival, h.P("a").Last(Msg.BarrierWait, BarrierWaitMsg.Parse).Reason);
        h.Barrier("b", "stag0101");
        h.Barrier("c", "stag0101");
        Assert.Equal(BarrierDecision.CoArrive, h.Decision("c").Decision);
        Assert.Same(W(h, "a"), W(h, "c"));
        Assert.NotSame(W(h, "a"), W(h, "b"));

        // A Public base whose host the arrival blocks: no co-arrival either (19.7.6: "visible to them").
        var g = new Harness();
        Admit(g, "a", Player(100, "a"));
        Admit(g, "b", Player(101, "b") with { Blocks = [100] });
        Admit(g, "c", Player(102, "c"));
        foreach (var n in new[] { "a", "b", "c" }) g.Send(n, new HeadingMsg(g.Inst(n), "stag0101", 0, 5).Frame());
        g.Barrier("a", "stag0101");
        g.Barrier("b", "stag0101");
        g.Barrier("c", "stag0101");
        Assert.Same(W(g, "a"), W(g, "c"));
        Assert.NotSame(W(g, "a"), W(g, "b"));
    }

    [Fact]
    public void Another_data_hash_is_no_candidate_and_no_co_arrival()
    {
        var h = new Harness();
        Admit(h, "a", Player(100, "a") with { DataHash = 1 });
        Admit(h, "b", Player(101, "b") with { DataHash = 2 });
        Admit(h, "c", Player(102, "c") with { DataHash = 1 });
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        h.Advance(11_000);
        h.Hub.Housekeep(h.Now);
        h.Barrier("b", "stag0101");
        Assert.Equal(ArrivalKind.NewInstance, h.Decision("b").World.Arrival);
        Assert.NotSame(W(h, "a"), W(h, "b"));
        h.Barrier("c", "stag0101");
        Assert.Same(W(h, "a"), W(h, "c"));

        var g = new Harness();
        Admit(g, "a", Player(100, "a") with { DataHash = 1 });
        Admit(g, "b", Player(101, "b") with { DataHash = 2 });
        foreach (var n in new[] { "a", "b" }) g.Send(n, new HeadingMsg(g.Inst(n), "stag0101", 0, 5).Frame());
        g.Barrier("a", "stag0101");
        g.Barrier("b", "stag0101");
        g.Advance(3_500);
        g.Hub.Housekeep(g.Now);
        Assert.NotSame(W(g, "a"), W(g, "b"));
    }

    [Fact]
    public void An_instance_whose_host_is_reconnecting_takes_no_automatic_join()
    {
        var h = RunningInstanceAt();
        var w = W(h, "a");
        h.Hub.Disconnect(h.Mem("a"), h.P("a"), h.Now);
        Assert.True(w.HostAway);
        h.Barrier("b", "stag0101");
        Assert.Equal(ArrivalKind.NewInstance, h.Decision("b").World.Arrival);
        Assert.NotSame(w, W(h, "b"));
    }

    [Fact]
    public void A_receivers_presence_beyond_its_byte_budget_is_dropped()
    {
        var h = new Harness();
        var names = Enumerable.Range(0, 9).Select(k => "p" + k).ToArray();
        foreach (var (n, k) in names.Select((n, k) => (n, k))) h.Connect(n, k);
        for (var k = 0; k < names.Length; ++k) h.Send(names[k], Presence2("stagML11", x: k * 5).Frame());
        h.Advance(1100);
        h.Hub.Housekeep(h.Now);
        h.Hub.WorldOptions.PresenceBytesPerSecond = 200; // (a few frames)
        foreach (var n in names.Skip(1)) h.Send(n, Presence2("stagML11", x: 1).Frame());
        var got = h.P("p0").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count;
        Assert.InRange(got, 1, 4);
        Assert.True(h.Hub.Counters["presence_budget_dropped"] >= names.Length - 1 - got);
        // A second later the budget is back.
        h.Advance(1000);
        h.Send("p1", Presence2("stagML11", x: 2).Frame());
        Assert.Equal(got + 1, h.P("p0").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count);
    }

    [Fact]
    public void A_playing_staff_member_is_tagged_in_member_info_and_instance_info()
    {
        var h = new Harness();
        Admit(h, "a", Player(100, "a"));
        Admit(h, "c", Player(102, "c") with { Staff = true });
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Log("a", 0, 60);
        h.Joinable("a");
        h.Advance(11_000);
        h.Hub.Housekeep(h.Now);
        h.Barrier("c", "stag0101");
        Assert.Same(W(h, "a"), W(h, "c"));
        var info = h.P("a").Last(Msg.MemberInfo, MemberInfoMsg.Parse);
        Assert.Equal(h.Mem("c").Id, info.Member);
        Assert.Equal(Relation.Staff | Relation.SameInstance, info.Flags);
        h.Hub.Housekeep(h.Now);
        var row = h.P("a").Last(Msg.InstanceInfo, InstanceInfoMsg.Parse).Members.Single(x => x.Member == h.Mem("c").Id);
        Assert.NotEqual(0, row.Flags & InstanceInfoMember.Staff);
    }
}
