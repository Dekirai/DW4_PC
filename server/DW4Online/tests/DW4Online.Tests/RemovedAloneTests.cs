using DW4Online.Domain.Story;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// fix-d43's review, finding 1 (19.6.6): a guest that went its own way from a stage group of its instance (taken out for lag, its
/// link timed out, its barrier split off or apart) and was removed meanwhile is still on the host's story. Its next barrier was
/// decided as a fresh arrival from that field (the stand-in of a lone barrier named no instance): its own new instance merging the
/// former host's story into the own (the error type fix-019 closed for stage groups), or, with a stranger's instance at the arrival
/// stage, round I1a's automatic join of it without the prompt. Now the stand-in names the instance the member's engine played on
/// (<c>HubInstance.ApartFrom</c>, <c>HubMember.LoneWorld</c>): inside it while the member holds a seat there, else a
/// DISSOLVE home with the replace and the redirect, as a stage group whose members hold no seat. Each test but the control fails
/// without the change.
/// </summary>
public partial class HubLogicTests
{
    /// <summary>a plays on, b sends no pads: b is taken out for lag (its LEAVE) and its engine plays on alone in its stage, its seat
    /// kept (19.6.8).</summary>
    private static void LagOutB(Harness h, uint inst)
    {
        var i = h.I("a");
        h.Seal(12_000); // (nobody is taken out while everyone lags; no housekeeping pass meanwhile)
        for (var k = 0; k < 60; ++k)
        {
            h.Send("a", new InputMsg(inst, i.Visit, i.NextSeal + 3, [new Pad(1, 0, 0, 0, 0), new Pad(1, 0, 0, 0, 0)]).Frame());
            h.Seal(17);
        }
        h.Housekeep();
        Assert.Null(h.Mem("b").Instance);
        Assert.Same(i, h.Mem("a").Instance);
        Assert.Equal("stag0201", h.Mem("b").LoneStage);
    }

    /// <summary>t, a stranger, hosts a running Public instance at <paramref name="stage"/> and stands there (not starting any more).</summary>
    private static WorldInstance StrangerAt(Harness h, string stage)
    {
        Admit(h, "t", Player(500, "t"));
        var i = h.Mem("t").Instance!;
        h.Send("t", new BarrierMsg(i.Id, i.Visit, 0, stage, 0, i.PresentMask, i.PresentMask, 5, [(byte)'t'], 0, BarrierMsg.PlanNewInstance).Frame());
        h.Snapshot("t");
        h.Log("t", 0, 60);
        h.Joinable("t");
        h.Housekeep();
        var wt = W(h, "t");
        Assert.Equal(0, wt.StartingUntil);
        return wt;
    }

    /// <summary>b's BARRIER from the stage it plays alone in, under the instance and visit its engine still names.</summary>
    private static void LoneBarrier(Harness h, uint inst, uint visit, string stage) =>
        h.Send("b", new BarrierMsg(inst, visit, 0, stage, 0, 0b10, 0b10, 5, [(byte)'b']).Frame());

    private static void AssertHomeOnOwnStory(Harness h, HubMember b, WorldInstance w)
    {
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.Dissolve, StoryMode.Replace, b.Id, "stagml11"),
            (ok.Decision, ok.World.StoryMode, ok.World.OwnerMember, ok.World.RedirectStage.ToLowerInvariant()));
        Assert.Equal(0u, ok.World.Wid);
        Assert.Null(b.World);
        Assert.Null(b.HomeWish);
        Assert.Equal(InstanceMode.Hub, b.Instance!.Mode);
        Assert.False(w.Seats.ContainsKey(b));
        Assert.False(h.P("b").Has(Msg.Options));
        Assert.DoesNotContain(h.Hub.Worlds, x => x.Host == b);
        Assert.Equal(0, h.Hub.Counters.GetValueOrDefault("auto_join_refused_remembered"));
    }

    [Theory]
    [InlineData(false, Visibility.Public)]
    [InlineData(true, Visibility.Public)]
    [InlineData(true, Visibility.Private)]
    public void A_guest_taken_out_for_lag_and_removed_while_it_plays_on_alone_goes_home_at_its_next_barrier(bool stranger, Visibility remembered)
    {
        var h = SealedPair(out var inst, new RelayOptions { LagLeaveSeconds = 10 });
        var i = h.I("a");
        var visit = i.Visit;
        var w = W(h, "a");
        var b = h.Mem("b");
        b.DefaultVisibility = remembered;
        var wt = stranger ? StrangerAt(h, "stag0202") : null;
        LagOutB(h, inst);
        Assert.True(w.Seats.ContainsKey(b));
        // a removes b while its engine plays on alone in stag0201 on a's story: the seat goes, the HOME is owed.
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, b.Id).Frame());
        Assert.False(w.Seats.ContainsKey(b));
        Assert.Null(b.World);
        Assert.NotNull(b.HomeWish);
        // b's next barrier, to stag0202 (where t's Public instance runs with t there, when there is one): home on its own story with
        // the redirect (before: its own new instance whose merge took a's story, or the automatic join of t's instance without a prompt;
        // with Private remembered, a new instance after auto_join_refused).
        LoneBarrier(h, inst, visit, "stag0202");
        AssertHomeOnOwnStory(h, b, w);
        if (wt is not null) Assert.False(wt.Seats.ContainsKey(b));
        Assert.Equal(stranger ? 2 : 1, h.Hub.Worlds.Count);
    }

    [Fact]
    public void A_guest_taken_out_for_lag_that_keeps_its_seat_is_decided_inside_its_instance_as_before()
    {
        // (The control: only the seat decides. b plays on alone with its seat; its next barrier is a stage group of a's instance.)
        var h = SealedPair(out var inst, new RelayOptions { LagLeaveSeconds = 10 });
        var visit = h.I("a").Visit;
        var w = W(h, "a");
        var b = h.Mem("b");
        LagOutB(h, inst);
        LoneBarrier(h, inst, visit, "stag0202");
        var ok = h.Decision("b");
        Assert.Equal((BarrierDecision.New, w.Wid), (ok.Decision, ok.World.Wid));
        Assert.Same(w, b.World);
        Assert.Same(w, b.Instance!.World);
        Assert.Single(h.Hub.Worlds);
    }

    [Fact]
    public void A_guest_whose_link_timed_out_back_without_its_slot_and_removed_goes_home_at_its_next_barrier()
    {
        // b's link goes; after Relay:ReconnectSeconds its slot is taken (LEAVE). It comes back without a resume (WELCOME still names
        // a's instance, its seat is kept), files its field (RESUME refused, plays on alone) and is removed. Its barrier comes from the
        // empty hub stage of its new connection: before, a fresh arrival from home with the prompt (a row of another instance to take
        // with a's story in its engine, or New instance merging it).
        var h = SealedPair(out var inst, new RelayOptions { LagLeaveSeconds = 600, ReconnectSeconds = 10 });
        var i = h.I("a");
        var visit = i.Visit;
        var w = W(h, "a");
        var b = h.Mem("b");
        StrangerAt(h, "stag0202");
        h.Hub.Disconnect(b, h.P("b"), h.Now);
        for (var k = 0; k < 650; ++k)
        {
            h.Send("a", new InputMsg(inst, visit, i.NextSeal + 3, [new Pad(1, 0, 0, 0, 0), new Pad(1, 0, 0, 0, 0)]).Frame());
            h.Seal(17);
        }
        h.Housekeep();
        Assert.Null(b.Instance);
        var peer = h.NewPeer();
        h.Hub.Connect(1, b.AccountId, "b", false, peer, h.Now);
        h.M["b"] = (b, peer);
        Assert.Equal((w.Wid, false), (peer.Last(Msg.Welcome, WelcomeMsg.Parse).Wid, b.ResumeAllowed));
        h.Send("b", new ResumeMsg(inst, visit, 1, 0, "stag0201").Frame());
        Assert.Same(w, b.World);
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, b.Id).Frame());
        Assert.False(w.Seats.ContainsKey(b));
        LoneBarrier(h, inst, visit, "stag0202");
        AssertHomeOnOwnStory(h, b, w);
    }

    [Fact]
    public void A_guest_removed_while_its_stage_group_gathers_goes_home_when_its_barrier_splits_off()
    {
        // b reaches the exit to stag0202, a plays on (a desync): a removes b while the barrier waits for a. After
        // Instances:BarrierReportSeconds the barrier splits off b's report (6.9). Before: a fresh arrival from stag0201, its own
        // new instance merging a's story.
        var h = SealedPair(out var inst);
        var i = h.I("a");
        var w = W(h, "a");
        var b = h.Mem("b");
        h.Seal(500);
        h.Barrier("b", "stag0202", tick: i.NextSeal + 5);
        Assert.NotNull(i.Barrier);
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, b.Id).Frame());
        Assert.False(w.Seats.ContainsKey(b));
        h.Advance(5_100);
        h.Housekeep();
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("barrier_split"));
        AssertHomeOnOwnStory(h, b, w);
        Assert.Same(w, h.Mem("a").World);
    }

    [Fact]
    public void A_removed_guests_barrier_past_every_sealed_tick_goes_home_when_it_is_decided_apart()
    {
        // b is removed (its HOME stamped) and its engine sends a barrier past every tick a's stage group sealed (another timeline, W7e):
        // decided apart at once (barrier_apart). Before: a fresh arrival from stag0201, its own new instance merging a's story.
        var h = SealedPair(out var inst);
        var i = h.I("a");
        var w = W(h, "a");
        var b = h.Mem("b");
        h.Seal(500);
        h.Send("a", new InstanceOpMsg(InstanceOpKind.Remove, b.Id).Frame());
        Assert.False(w.Seats.ContainsKey(b));
        h.Barrier("b", "stag0202", tick: i.NextSeal + RoomHub.StaleBarrierTicks + 50);
        Assert.Equal(1, h.Hub.Counters.GetValueOrDefault("barrier_apart"));
        AssertHomeOnOwnStory(h, b, w);
    }
}
