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
        Admit(h, "b", Player(101, "b") with { Story = StoryAt(2) });
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

    [Fact]
    public void A_report_names_the_reported_account_and_the_shared_instance_and_its_answer_reaches_the_reporter()
    {
        var h = SealedPair(out _);
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
        // The own id and an unknown one: "not available" at once, nothing stored.
        h.Send("b", new ReportMsg(h.Mem("b").Id, 1, 0).Frame());
        h.Send("b", new ReportMsg(9999, 1, 0).Frame());
        Assert.Single(reports);
        Assert.Equal([(uint)ReportResult.NotAvailable, (uint)ReportResult.NotAvailable],
            h.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse).Where(n => n.Kind == WorldNoticeKind.Report).Skip(1).Select(n => n.Argument));
        // Three at once, then one a second: the fourth within the second is dropped without an answer.
        h.Advance(5000);
        var before = h.P("b").Frames.Count;
        for (var k = 0; k < 4; ++k) h.Send("b", new ReportMsg(h.Mem("a").Id, 1, 0).Frame());
        Assert.Equal(4, reports.Count);
        // A wrong reason is a protocol error.
        Assert.Throws<ProtocolException>(() => h.Send("b", new ReportMsg(h.Mem("a").Id, 5, 0).Frame()));
        Assert.Throws<ProtocolException>(() => ReportMsg.Parse(Convert.FromHexString("0100000000" + "00000000")));
        Assert.Equal(before, h.P("b").Frames.Count);
    }

    [Fact]
    public void A_player_who_left_the_world_can_still_be_reported_and_a_stranger_report_names_no_instance()
    {
        var h = new Harness();
        Admit(h, "a", Player(100, "a"));
        Admit(h, "b", Player(101, "b"));
        var reports = new List<ReportRequest>();
        h.Hub.Reports = reports.Add;
        var aId = h.Mem("a").Id;
        h.Hub.Leave(100, h.Now);
        h.Advance(60_000);
        h.Hub.Housekeep(h.Now);
        h.Send("b", new ReportMsg(aId, 3, 0).Frame());
        var r = reports.Single();
        Assert.Equal((100L, "a", "name", (long?)null), (r.ReportedAccount, r.ReportedName, r.Reason, r.HistoryId));
        // After 30 minutes the departed member is forgotten.
        h.Advance(RoomHub.DepartedKeepMs + 1000);
        h.Hub.Housekeep(h.Now);
        h.Send("b", new ReportMsg(aId, 3, 0).Frame());
        Assert.Single(reports);
        Assert.Equal((uint)ReportResult.NotAvailable, h.P("b").Last(Msg.WorldNotice, WorldNoticeMsg.Parse).Argument);
        // Without a report service the answer is "failed".
        h.Hub.Reports = null;
        Admit(h, "c", Player(102, "c"));
        h.Send("b", new ReportMsg(h.Mem("c").Id, 4, 0).Frame());
        Assert.Equal((uint)ReportResult.Failed, h.P("b").Last(Msg.WorldNotice, WorldNoticeMsg.Parse).Argument);
    }
}
