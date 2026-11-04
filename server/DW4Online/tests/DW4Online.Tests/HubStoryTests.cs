using System.Diagnostics;
using DW4Online.Domain.Story;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using Microsoft.Extensions.Logging.Abstractions;

namespace DW4Online.Tests;

/// <summary>
/// The room story on the relay (SEAMLESS.md 8.3-8.6, scenario S14 at the hub): the confirmed story after WELCOME, STORY
/// merged with an answer to the sender and STORY_ACK to everyone else, the owner's online story saved on each revision,
/// the same story bytes and dropped set in every BARRIER_OK of a group, the owner-away policies drop / hold / apply, the
/// title refused, the rate limit, and the line of a solo group without the server's answer kept per member.
/// </summary>
public class HubStoryTests
{
    private sealed class Peer : IHubPeer
    {
        public List<(byte Type, byte[] Payload)> Frames { get; } = [];
        public void Send(byte[] frame, bool bulk = false) => Frames.Add((frame[0], frame[Wire.HeaderBytes..]));
        public void Close(CloseReason reason, string text) { }
        public List<StoryAckMsg> Acks => Frames.Where(f => f.Type == Msg.StoryAck).Select(f => StoryAckMsg.Parse(f.Payload)).ToList();
        public StoryAckMsg LastAck => Acks.Last();
        public BarrierOkMsg LastOk => BarrierOkMsg.Parse(Frames.Last(f => f.Type == Msg.BarrierOk).Payload);
    }

    private sealed class Harness
    {
        public RoomHub Hub { get; }
        public long Now { get; private set; } = Stopwatch.Frequency * 1000;
        public Dictionary<string, (HubMember Member, Peer Peer)> M { get; } = [];
        public List<(long Owner, RoomStoryData Story)> Saved { get; } = [];

        // (Without options: no minimum visit age, so a STORY right after a barrier counts; the per-visit tests pass their own.)
        public Harness(StoryPolicy policy, bool ownerPresent = true, RoomStoryData? story = null, InstanceOptions? instances = null, int difficulty = 0,
            bool started = true, RoomSessionState? session = null, Harness? after = null)
        {
            var init = new RoomStoryInit(4242, story ?? new RoomStoryData(), difficulty, policy, ownerPresent, started);
            Hub = new RoomHub(new RoomInfo("ROOM01", 43, 1, 2, 10, init), new RelayOptions(), new SealingOptions(),
                instances ?? new InstanceOptions { StoryMinVisitSeconds = 0 }, NullLogger.Instance, session: session)
            {
                StorySaver = (owner, s, countersOnly) =>
                {
                    Saved.Add((owner, s));
                    CountersOnly.Add(countersOnly);
                },
            };
            if (after is not null) Now = after.Now;
        }

        public List<bool> CountersOnly { get; } = [];

        public void Advance(double ms) => Now += (long)(ms * Stopwatch.Frequency / 1000.0);

        public Peer Connect(string name, int index)
        {
            var peer = new Peer();
            M[name] = (Hub.Connect(index, 100 + index, name, index == 0, peer, Now), peer);
            return peer;
        }

        public void Send(string name, byte[] frame) => Hub.Receive(M[name].Member, frame[0], frame[Wire.HeaderBytes..], Now);
        public Peer P(string name) => M[name].Peer;
        public HubInstance I(string name) => M[name].Member.Instance!;

        public void Barrier(string name, string stage, ulong context = 5)
        {
            var i = I(name);
            Send(name, new BarrierMsg(i.Id, i.Visit, 0, stage, 0, i.PresentMask, i.PresentMask, context, [(byte)name[0]]).Frame());
        }

        public StoryAckMsg Story(string name, StoryDelta d)
        {
            var i = I(name);
            Send(name, new StoryMsg(i.Id, i.Visit, 0, d).Frame());
            var answer = P(name).Frames.Last(f => f.Type == Msg.StoryAck);
            var ack = StoryAckMsg.Parse(answer.Payload);
            Assert.Equal(StoryAckMsg.Answer, ack.Flags);
            return ack;
        }
    }

    private static StoryDelta Flags(params int[] f) => new() { Flags = [.. f] };

    [Fact]
    public void The_story_reaches_everyone_and_the_owners_online_story()
    {
        var owner = new RoomStoryData { Revision = 5 };
        owner.Flags[392] = 1;
        var h = new Harness(StoryPolicy.Drop, story: owner);
        var a = h.Connect("a", 0);
        var b = h.Connect("b", 1);
        var first = a.Acks.Single();
        Assert.Equal((5u, (byte)0), (first.Revision, first.Flags));
        Assert.Equal(1, RoomStoryData.FromBlob(first.Story, first.Revision).Flags[392]);
        // From the title: nothing counts.
        Assert.Equal(StoryRefusal.NotMember, h.Story("b", Flags(393)).Refused.Single().Reason);
        // b in its lobby (stagML12) makes a lobby flag: merged, answered, broadcast, saved as the owner's story.
        h.Barrier("b", "stagml12");
        Assert.Equal(BarrierDecision.Dissolve, b.LastOk.Decision);
        var acksOfA = a.Acks.Count;
        var ack = h.Story("b", Flags(393));
        Assert.Empty(ack.Refused);
        Assert.Equal(6u, ack.Revision);
        Assert.Equal(acksOfA + 1, a.Acks.Count);
        Assert.Equal((6u, (byte)0), (a.LastAck.Revision, a.LastAck.Flags));
        Assert.Equal(4242, h.Saved.Single().Owner);
        Assert.Equal(1, h.Saved.Single().Story.Flags[393]);
        // A flag no script of stagML12 sets is refused.
        Assert.Equal(StoryRefusal.Stage, h.Story("b", Flags(19)).Refused.Single().Reason);
        // The next barrier brings the story to a's engine.
        h.Barrier("a", "stagml11");
        var ok = a.LastOk;
        Assert.Equal(6u, ok.StoryRevision);
        Assert.Equal(1, RoomStoryData.FromBlob(ok.Story, ok.StoryRevision).Flags[393]);
        Assert.Empty(ok.Dropped);
        Assert.Equal(6u, h.Hub.StoryState().Revision);
    }

    [Fact]
    public void Drop_refuses_and_every_barrier_of_the_group_carries_the_same_story_and_dropped_set()
    {
        var h = new Harness(StoryPolicy.Drop, ownerPresent: false);
        var a = h.Connect("a", 0);
        var b = h.Connect("b", 1);
        h.Barrier("b", "stagml13");
        var ack = h.Story("b", new StoryDelta { Flags = [392], Counters = [new CounterGrowth(1, h.I("b").Id, h.I("b").Visit, 200)] });
        Assert.All(ack.Refused, r => Assert.Equal(StoryRefusal.OwnerAway, r.Reason));
        Assert.Equal(2, ack.Refused.Count);
        Assert.Equal(0u, ack.Revision);
        Assert.Empty(h.Saved);
        Assert.Equal([392], h.Hub.StoryState().DroppedFlags);
        // The owner returns: the value still in b's engine line does not count; a later visit that sets it anew does.
        h.Hub.OwnerPresence(true);
        Assert.Equal(StoryRefusal.OwnerAway, h.Story("b", Flags(392)).Refused.Single().Reason);
        h.Barrier("b", "stagml12");
        h.Barrier("b", "stagml13");
        Assert.Empty(h.Story("b", Flags(392)).Refused);
        Assert.Contains(392, h.Hub.StoryState().Flags);
        Assert.Single(h.Saved);
        // Away again: 393 is dropped. Both go to stag0201 together (base NEW and co-arrival): the same story bytes and
        // dropped set in both answers.
        h.Hub.OwnerPresence(false);
        Assert.Equal(StoryRefusal.OwnerAway, h.Story("b", Flags(393)).Refused.Single().Reason);
        h.Barrier("a", "stagml11");
        h.Send("a", new HeadingMsg(h.I("a").Id, "stag0201", 0, 5).Frame());
        h.Send("b", new HeadingMsg(h.I("b").Id, "stag0201", 0, 5).Frame());
        h.Barrier("a", "stag0201");
        h.Barrier("b", "stag0201");
        var okA = a.LastOk;
        var okB = b.LastOk;
        Assert.Equal((BarrierDecision.New, BarrierDecision.CoArrive), (okA.Decision, okB.Decision));
        Assert.Equal(okA.Story, okB.Story);
        Assert.Equal(okA.Dropped, okB.Dropped);
        var dropped = StoryKeys.FromBlob(okA.Dropped);
        Assert.Equal([392, 393], dropped.Flags);
        Assert.Equal([1], dropped.Counters);
        Assert.Equal(1, RoomStoryData.FromBlob(okA.Story, okA.StoryRevision).Flags[392]); // (in the room: the engines keep it)
    }

    [Fact]
    public void Hold_keeps_until_the_owner_returns_and_apply_merges_at_once()
    {
        var hold = new Harness(StoryPolicy.Hold, ownerPresent: false);
        var a = hold.Connect("a", 0);
        hold.Connect("b", 1);
        hold.Barrier("b", "stagml12");
        Assert.Equal(StoryRefusal.Held, hold.Story("b", Flags(392)).Refused.Single().Reason);
        Assert.Equal(1, hold.Hub.StoryState().Held);
        Assert.Empty(hold.Saved);
        var acks = a.Acks.Count;
        hold.Hub.OwnerPresence(true);
        Assert.Equal(acks + 1, a.Acks.Count);
        Assert.Equal(1u, a.LastAck.Revision);
        Assert.Equal(1, hold.Saved.Single().Story.Flags[392]);

        var apply = new Harness(StoryPolicy.Apply, ownerPresent: false);
        apply.Connect("a", 0);
        apply.Barrier("a", "stagml12");
        Assert.Empty(apply.Story("a", Flags(392)).Refused);
        Assert.Equal(1u, apply.Hub.StoryState().Revision);
    }

    [Fact]
    public void The_rate_limit_refuses_bursts_and_lines_of_instance_zero_are_the_members_own()
    {
        var h = new Harness(StoryPolicy.Apply, instances: new InstanceOptions { StoryBurst = 2, StoryMessagesPerMinute = 6, StoryMinVisitSeconds = 0 });
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("a", "stag0001");
        h.Barrier("b", "stag0001", context: 6); // (another quest context: its own instance at once)
        Assert.Empty(h.Story("a", new StoryDelta { Counters = [new CounterGrowth(7, 0, 3, 1)] }).Refused);
        Assert.Empty(h.Story("a", new StoryDelta { Counters = [new CounterGrowth(7, 0, 3, 1)] }).Refused);
        var rated = h.Story("a", new StoryDelta { Counters = [new CounterGrowth(7, 0, 3, 2)] });
        Assert.Equal(StoryRefusal.Rate, rated.Refused.Single().Reason);
        Assert.Empty(rated.Story); // (it changed nothing: the answer carries no story blob)
        h.Advance(10_000); // one token per ten seconds
        Assert.Empty(h.Story("a", new StoryDelta { Counters = [new CounterGrowth(7, 0, 3, 2)] }).Refused);
        // b's instance 0 visit 3 is another engine line than a's.
        Assert.Empty(h.Story("b", new StoryDelta { Counters = [new CounterGrowth(7, 0, 3, 1)] }).Refused);
        Assert.Equal(3, h.Hub.StoryState().Counters[7]);
    }

    /// <summary>
    /// The forgery of the round 56 review (8.7): a guest client that BARRIERs from stage to stage at once and sends the phase
    /// step by step, every key item and a counter growth on 32 invented lines got revision 10 in 50 ms. Now a visit gives
    /// nothing before it is ten seconds old, one phase step and one key item at most, the clear flag only of the room's
    /// difficulty, and counter growth only on the member's own lines.
    /// </summary>
    [Fact]
    public void A_client_that_hops_from_stage_to_stage_gets_one_phase_step_and_one_key_per_visit_and_only_its_own_lines()
    {
        // (The per-member intervals of round 59 apart: A_patient_forger_... checks those.)
        var h = new Harness(StoryPolicy.Apply, instances: new InstanceOptions { StoryBurst = 1000, StoryPhaseStepSeconds = 0, StoryKeySeconds = 0 });
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("b", "stagml02");
        Assert.Equal(new StoryRefused(StoryKeyKind.Phase, 1, StoryRefusal.Wait), h.Story("b", new StoryDelta { Phase = 1 }).Refused.Single());
        h.Advance(11_000);
        for (byte p = 1; p <= 6; ++p) h.Story("b", new StoryDelta { Phase = p });
        Assert.Equal(1, h.Hub.StoryState().Phase);
        // The next step needs another visit of a stage that sets it.
        h.Barrier("b", "stagml12");
        h.Barrier("b", "stagml02");
        Assert.Equal(StoryRefusal.Wait, h.Story("b", new StoryDelta { Phase = 2 }).Refused.Single().Reason);
        h.Advance(11_000);
        for (byte p = 2; p <= 6; ++p) h.Story("b", new StoryDelta { Phase = p });
        Assert.Equal(2, h.Hub.StoryState().Phase);
        // Phase 7 (the game cleared) is five steps away; the clear flags only of the room's difficulty (normal: 406), and
        // that one only after the last briefing (phase 6; round 59: the review's guest gave a phase 2 story the clear).
        h.Barrier("b", "stagbs06");
        h.Advance(11_000);
        var ack = h.Story("b", new StoryDelta { Phase = 7, Flags = [406, 407, 408] });
        Assert.Contains(new StoryRefused(StoryKeyKind.Phase, 7, StoryRefusal.PhaseJump), ack.Refused);
        Assert.Contains(new StoryRefused(StoryKeyKind.Flag, 406, StoryRefusal.PhaseJump), ack.Refused);
        Assert.Contains(new StoryRefused(StoryKeyKind.Flag, 407, StoryRefusal.Stage), ack.Refused);
        Assert.Contains(new StoryRefused(StoryKeyKind.Flag, 408, StoryRefusal.Stage), ack.Refused);
        Assert.DoesNotContain(406, h.Hub.StoryState().Flags);
        Assert.DoesNotContain(407, h.Hub.StoryState().Flags);
        // Every key item at once: one counts, the others wait for later visits.
        h.Barrier("b", "stag0101");
        h.Advance(11_000);
        ack = h.Story("b", new StoryDelta { Folder = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10] });
        Assert.Equal(10, ack.Refused.Count(r => r is { Kind: StoryKeyKind.Folder, Reason: StoryRefusal.Wait }));
        Assert.Equal([0], h.Hub.StoryState().Folder);
        // COUNTER 7 on 32 invented lines: refused; the member's own line counts.
        h.Barrier("b", "stag0001");
        h.Advance(11_000);
        var own = h.I("b");
        var lines = Enumerable.Range(0, 31).Select(n => new CounterGrowth(7, 5000u + (uint)n, 1, 100_000)).Append(new CounterGrowth(7, own.Id, own.Visit, 10));
        ack = h.Story("b", new StoryDelta { Counters = [.. lines] });
        Assert.Equal(31, ack.Refused.Count(r => r is { Kind: StoryKeyKind.Counter, Reason: StoryRefusal.Limit }));
        Assert.Equal(10, h.Hub.StoryState().Counters[7]);
        // Instance 0 lines (a group that went on alone) are the member's own, a few per session.
        for (uint v = 1; v <= HubMember.LoneLineLimit + 2; ++v) h.Story("b", new StoryDelta { Counters = [new CounterGrowth(7, 0, v, 1)] });
        Assert.Equal(10 + HubMember.LoneLineLimit, h.Hub.StoryState().Counters[7]);
    }

    /// <summary>
    /// A member the server has no instance with a stage for (its barrier parked while its client went on as NEW after the
    /// five seconds; its connection lost in a lobby and back without resuming): its STORY counts against the stage its
    /// client plays in. Before, it was refused (not_member) and lost for the room once the client moved on.
    /// </summary>
    [Fact]
    public void A_member_alone_counts_against_the_stage_its_client_plays_in()
    {
        var h = new Harness(StoryPolicy.Apply, instances: new InstanceOptions());
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("a", "stagml11");
        h.Barrier("b", "stagml13");
        h.Advance(11_000);
        // Both head for stag0101: a's barrier waits for b (a co-arrival hold); a's client goes on alone after five seconds.
        h.Send("a", new HeadingMsg(h.I("a").Id, "stag0101", 0, 5).Frame());
        h.Send("b", new HeadingMsg(h.I("b").Id, "stag0101", 0, 5).Frame());
        h.Barrier("a", "stag0101");
        Assert.NotNull(h.M["a"].Member.Arrival);
        Assert.Equal(StoryRefusal.Wait, h.Story("a", Flags(505)).Refused.Single().Reason);
        h.Advance(11_000);
        Assert.Empty(h.Story("a", Flags(505)).Refused);
        Assert.Contains(505, h.Hub.StoryState().Flags);

        // b's connection drops in its lobby (stagML13) and comes back without resuming: its lobby flag still counts there.
        var peer = h.P("b");
        h.Hub.Disconnect(h.M["b"].Member, peer, h.Now);
        h.Connect("b", 1);
        Assert.Empty(h.Story("b", Flags(394)).Refused);
        Assert.Contains(394, h.Hub.StoryState().Flags);
        Assert.Equal(StoryRefusal.Stage, h.Story("b", Flags(426)).Refused.Single().Reason); // (stagML11's, not stagML13's)
    }

    /// <summary>
    /// The user's decision of 31.10 (SEAMLESS.md 8.6): a room whose owner has no online story starts at zero. What a guest
    /// does before the owner's own game enters a stage is dropped (not_started) whatever the policy, the next barrier
    /// removes it from the guest's engine, and it counts again only when set anew in a later visit; the owner's title does
    /// not start the story, its first stage does.
    /// </summary>
    [Fact]
    public void A_room_whose_owner_has_no_story_starts_at_zero_when_the_owners_game_enters_a_stage()
    {
        var h = new Harness(StoryPolicy.Apply, started: false);
        h.Connect("a", 0); // the owner, at the title
        var b = h.Connect("b", 1);
        h.Barrier("b", "stagml12");
        var ack = h.Story("b", new StoryDelta { Flags = [392] });
        Assert.Equal(new StoryRefused(StoryKeyKind.Flag, 392, StoryRefusal.NotStarted), ack.Refused.Single());
        Assert.Equal(0u, h.Hub.StoryState().Revision);
        Assert.False(h.Hub.StoryState().Started);
        Assert.Equal([392], h.Hub.StoryState().DroppedFlags);
        Assert.Empty(h.Saved);
        // The owner's title does not start it (stagIS*); its game entering a stage does.
        h.Barrier("a", "stagis10");
        Assert.False(h.Hub.StoryState().Started);
        h.Barrier("a", "stagml11");
        Assert.True(h.Hub.StoryState().Started);
        // The guest's old visit still holds 392: it does not count; its barrier removes it (the dropped set).
        Assert.Equal(StoryRefusal.NotStarted, h.Story("b", Flags(392)).Refused.Single().Reason);
        h.Barrier("b", "stagml13");
        Assert.Equal([392], StoryKeys.FromBlob(b.LastOk.Dropped).Flags);
        Assert.Equal(0, RoomStoryData.FromBlob(b.LastOk.Story, b.LastOk.StoryRevision).Flags[392]);
        // Set anew in a later visit of a stage that sets it, it counts; so does the owner's own progress.
        h.Barrier("b", "stagml12");
        Assert.Empty(h.Story("b", Flags(392)).Refused);
        Assert.Empty(h.Story("a", Flags(426)).Refused);
        Assert.Equal(2u, h.Hub.StoryState().Revision);
        Assert.Equal(2, h.Saved.Count);
    }

    /// <summary>
    /// Round 59's verification (OZ at the hub): the guest's lobby (phase 1) and first briefing (phase 2) before the owner's
    /// game starts. Every claim is dropped, phase 2 included (it was refused phase_jump and kept before), and the barrier out
    /// of stagML02 removes it from the guest's engine. The owner's own lobby phase 1 merges although the guest's old lobby
    /// offered the same value after the start; the guest's old stagML02 visit, offering phase 2 again once it is one step
    /// above the room, and a value it offers only after the start, are refused and dropped. The room story stays at the
    /// owner's phase 1.
    /// </summary>
    [Fact]
    public void A_guests_briefing_from_before_the_start_never_becomes_the_new_owners_phase_2()
    {
        var h = new Harness(StoryPolicy.Apply, instances: new InstanceOptions { StoryBurst = 1000 }, started: false);
        var a = h.Connect("a", 0); // the owner, at the title
        var b = h.Connect("b", 1);
        h.Barrier("b", "stagml11");
        h.Advance(12_000);
        Assert.All(h.Story("b", new StoryDelta { Phase = 1, Flags = [501] }).Refused, r => Assert.Equal(StoryRefusal.NotStarted, r.Reason));
        h.Barrier("b", "stagml02");
        Assert.Equal([1, 501], StoryKeys.FromBlob(b.LastOk.Dropped).Flags);
        h.Advance(12_000);
        var ack = h.Story("b", new StoryDelta { Phase = 2, Flags = [320] });
        Assert.Equal([new(StoryKeyKind.Phase, 2, StoryRefusal.NotStarted), new StoryRefused(StoryKeyKind.Flag, 320, StoryRefusal.NotStarted)], ack.Refused);
        Assert.Equal([1, 2, 320, 501], h.Hub.StoryState().DroppedFlags);
        // The owner's game enters its lobby: the story starts.
        h.Barrier("a", "stagml11");
        Assert.True(h.Hub.StoryState().Started);
        h.Advance(1_000);
        // The guest's old stagML02 visit offers flag 502 only now (set a moment before the start): dropped as well.
        Assert.Equal(StoryRefusal.NotStarted, h.Story("b", Flags(502)).Refused.Single().Reason);
        h.Advance(10_500);
        Assert.Empty(h.Story("a", new StoryDelta { Phase = 1, Flags = [501] }).Refused);
        Assert.Equal((1u, 1), (h.Hub.StoryState().Revision, h.Hub.StoryState().Phase));
        // Phase 2 is one step above the room now, from stagML02, in an old visit, the guest's first step: refused all the same,
        // in the visit and at its barrier, which removes it from the guest's engine.
        h.Advance(300_000);
        Assert.All(h.Story("b", new StoryDelta { Phase = 2, Flags = [320, 502] }).Refused, r => Assert.Equal(StoryRefusal.NotStarted, r.Reason));
        h.Hub.Housekeep(h.Now);
        h.Barrier("b", "stagml11");
        var dropped = StoryKeys.FromBlob(b.LastOk.Dropped).Flags;
        Assert.Contains(2, dropped);
        Assert.Contains(502, dropped);
        Assert.Equal(1, StoryMerge.Phase(RoomStoryData.FromBlob(b.LastOk.Story, b.LastOk.StoryRevision)));
        Assert.Equal((1u, 1), (h.Hub.StoryState().Revision, h.Hub.StoryState().Phase));
        Assert.Single(h.Saved);
        Assert.Equal(1, StoryMerge.Phase(h.Saved.Single().Story));
        // The guest's visits from now on count: the lobby talk of its new visit.
        h.Advance(12_000);
        Assert.Empty(h.Story("b", Flags(426)).Refused);
        Assert.Equal(2u, h.Hub.StoryState().Revision);
        Assert.Equal(2u, a.LastAck.Revision);
    }

    /// <summary>
    /// The round 61 design's finding on round 59 (C1 on real clients showed it too): a new owner's phase 2, the first
    /// briefing a minute after the lobby's phase 1, waited for the five-minute spacing, was refused for the stage once the
    /// owner left stagML02, and the room story stayed at phase 1 for the room session. The owner's own steps are not spaced;
    /// a guest's step that waits for the spacing is earned and merged by the relay when due, from whatever stage the guest
    /// plays in then, and everyone gets the new revision.
    /// </summary>
    [Fact]
    public void A_new_owners_phase_2_counts_and_a_guests_waiting_step_is_merged_when_due()
    {
        var h = new Harness(StoryPolicy.Drop, instances: new InstanceOptions { StoryBurst = 1000 }, started: false);
        var a = h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("a", "stagml11");
        h.Advance(11_000);
        Assert.Empty(h.Story("a", new StoryDelta { Phase = 1 }).Refused);
        h.Barrier("a", "stagml02");
        h.Advance(60_000);
        Assert.Empty(h.Story("a", new StoryDelta { Phase = 2 }).Refused);
        Assert.Equal(2, h.Hub.StoryState().Phase);
        // The guest plays ahead: its first step counts, its next one within five minutes waits ...
        h.Barrier("b", "stagml02");
        h.Advance(11_000);
        Assert.Empty(h.Story("b", new StoryDelta { Phase = 3 }).Refused);
        h.Barrier("b", "stagml12");
        h.Barrier("b", "stagml02");
        h.Advance(11_000);
        Assert.Equal(StoryRefusal.Wait, h.Story("b", new StoryDelta { Phase = 4 }).Refused.Single().Reason);
        // ... is earned, and the guest leaves stagML02, where a resend would be refused for the stage.
        h.Barrier("b", "stag0101");
        Assert.Equal(StoryRefusal.Stage, h.Story("b", new StoryDelta { Phase = 4 }).Refused.Single().Reason);
        h.Advance(60_000);
        h.Hub.Housekeep(h.Now);
        Assert.Equal(3, h.Hub.StoryState().Phase);
        h.Advance(230_000); // five minutes after its last step
        var acks = a.Acks.Count;
        var saved = h.Saved.Count;
        h.Hub.Housekeep(h.Now);
        Assert.Equal(4, h.Hub.StoryState().Phase);
        Assert.Equal(acks + 1, a.Acks.Count);
        Assert.Equal(saved + 1, h.Saved.Count);
        Assert.Equal(4, StoryMerge.Phase(h.Saved[^1].Story));
        h.Hub.Housekeep(h.Now);
        Assert.Equal(saved + 1, h.Saved.Count);
    }

    /// <summary>
    /// The patient forgery of the round 59 review: a guest that BARRIERs into stagML02, waits a little over ten seconds and
    /// sends the next phase and a key item, again and again, climbed to the clear in under two minutes. A guest's phase
    /// steps now count five minutes apart and its key items a minute apart (Instances:StoryPhaseStepSeconds,
    /// StoryKeySeconds; the rest waits, earned, and the relay merges it when due, no sooner).
    /// </summary>
    [Fact]
    public void A_patient_forger_gets_one_phase_step_per_five_minutes_and_one_key_item_per_minute()
    {
        var h = new Harness(StoryPolicy.Apply, instances: new InstanceOptions { StoryBurst = 1000 });
        h.Connect("a", 0);
        h.Connect("b", 1);
        for (var n = 0; n < 12; ++n) // about two minutes
        {
            h.Barrier("b", "stagml12");
            h.Barrier("b", "stagml02");
            h.Advance(10_300);
            h.Story("b", new StoryDelta { Phase = (byte)(h.Hub.StoryState().Phase + 1), Folder = [n % 11] });
            h.Hub.Housekeep(h.Now);
        }
        Assert.Equal(1, h.Hub.StoryState().Phase);
        Assert.Equal(2, h.Hub.StoryState().Folder.Count); // at 10.3 s and at 72.1 s
        h.Advance(300_000);
        h.Barrier("b", "stagml12");
        h.Barrier("b", "stagml02");
        h.Advance(10_300);
        Assert.Empty(h.Story("b", new StoryDelta { Phase = 2 }).Refused);
        Assert.Equal(2, h.Hub.StoryState().Phase);
        // Another member counts on its own (N accounts, N steps: the story history repairs what they do).
        h.Barrier("a", "stagml02");
        h.Advance(10_300);
        Assert.Empty(h.Story("a", new StoryDelta { Phase = 3 }).Refused);
    }

    /// <summary>
    /// The round 59 review's line flood: a guest looping barriers in stag0001 and reporting its last 16 lines with growth 1
    /// made about a thousand lines a minute that the keeper kept for as long as the room lived. Counters now wait for a visit
    /// ten seconds old, a member opens at most Instances:StoryLinesPerMember lines a session (then limit), and the lines no
    /// member can report any more are forgotten.
    /// </summary>
    [Fact]
    public void Counter_lines_wait_for_old_visits_are_capped_per_member_and_forgotten_once_nobody_can_report_them()
    {
        var h = new Harness(StoryPolicy.Apply, instances: new InstanceOptions { StoryBurst = 100_000, StoryMessagesPerMinute = 100_000, StoryLinesPerMember = 100 });
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("b", "stag0001");
        var i = h.I("b");
        Assert.Equal(StoryRefusal.Wait, h.Story("b", new StoryDelta { Counters = [new CounterGrowth(7, i.Id, i.Visit, 1)] }).Refused.Single().Reason);
        var limited = 0;
        for (var n = 0; n < 200; ++n)
        {
            h.Barrier("b", "stag0001");
            h.Advance(11_000);
            var own = h.M["b"].Member.StoryLines.Select(l => new CounterGrowth(7, l.Inst, l.Visit, 1)).ToList();
            limited += h.Story("b", new StoryDelta { Counters = own }).Refused.Count(r => r.Reason == StoryRefusal.Limit);
        }
        Assert.Equal(100, h.Hub.StoryState().Counters[7]);
        Assert.True(limited > 0);
        Assert.True(h.Hub.Story.LineCount <= 64 + HubMember.StoryLineLimit, $"{h.Hub.Story.LineCount} lines kept");
        Assert.True(h.Hub.Counters.GetValueOrDefault("story_lines_pruned") > 0);
    }

    /// <summary>
    /// A reconnect without RESUME made the member's lone play a new visit (its key was the visit's start), so it gave one
    /// more phase step and key item (round 59 review); the lone play now keeps the visit's key.
    /// </summary>
    [Fact]
    public void A_reconnect_without_resuming_is_no_new_visit()
    {
        var h = new Harness(StoryPolicy.Apply, instances: new InstanceOptions { StoryPhaseStepSeconds = 0, StoryKeySeconds = 0 });
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("b", "stagml02");
        h.Advance(11_000);
        Assert.Empty(h.Story("b", new StoryDelta { Phase = 1, Folder = [0] }).Refused);
        Assert.Equal(2, h.Story("b", new StoryDelta { Phase = 2, Folder = [1] }).Refused.Count(r => r.Reason == StoryRefusal.Wait));
        h.Hub.Disconnect(h.M["b"].Member, h.P("b"), h.Now);
        h.Connect("b", 1);
        Assert.Equal(2, h.Story("b", new StoryDelta { Phase = 2, Folder = [1] }).Refused.Count(r => r.Reason == StoryRefusal.Wait));
        Assert.Equal(1, h.Hub.StoryState().Phase);
    }

    /// <summary>
    /// A reconnect without resuming keeps the last visit's epoch: the client plays on alone in that visit, which no barrier
    /// has cleaned since (before, the reconnect's stageless instance gave the member the current epoch, and a value dropped
    /// in that visit, before the owner's start or while the owner was away, counted again from the same engine).
    /// </summary>
    [Fact]
    public void A_reconnect_without_resuming_keeps_its_visits_drops()
    {
        var h = new Harness(StoryPolicy.Drop, started: false);
        h.Connect("a", 0);
        var b = h.Connect("b", 1);
        h.Barrier("b", "stagml12");
        Assert.Equal(StoryRefusal.NotStarted, h.Story("b", Flags(392)).Refused.Single().Reason);
        h.Hub.Disconnect(h.M["b"].Member, b, h.Now);
        h.Barrier("a", "stagml11");
        Assert.True(h.Hub.StoryState().Started);
        b = h.Connect("b", 1);
        Assert.Equal(StoryRefusal.NotStarted, h.Story("b", Flags(392)).Refused.Single().Reason);
        // A value that visit offers only now is from before the start as well.
        Assert.Equal(StoryRefusal.NotStarted, h.Story("b", Flags(393)).Refused.Single().Reason);
        Assert.Equal(0u, h.Hub.StoryState().Revision);
        // Owner away (drop): the same.
        h.Barrier("b", "stagml13");
        h.Hub.OwnerPresence(false);
        Assert.Equal(StoryRefusal.OwnerAway, h.Story("b", Flags(394)).Refused.Single().Reason);
        h.Hub.Disconnect(h.M["b"].Member, b, h.Now);
        h.Hub.OwnerPresence(true);
        h.Connect("b", 1);
        Assert.Equal(StoryRefusal.OwnerAway, h.Story("b", Flags(394)).Refused.Single().Reason);
        Assert.Empty(h.Story("b", Flags(392)).Refused); // (set anew in this visit, after the start)
        Assert.Equal(1u, h.Hub.StoryState().Revision);
    }

    /// <summary>
    /// The relay drops a hub whose connections are all gone while its room stays open; the room story (dropped set, epochs,
    /// held values, per-member rules) and the instance ids now stay with the room (round 59 review: the next keeper started
    /// without them, so values dropped while the owner was away counted again).
    /// </summary>
    [Fact]
    public void The_room_story_outlives_a_rebuilt_hub()
    {
        var session = new RoomSessionState();
        var h = new Harness(StoryPolicy.Drop, ownerPresent: false, session: session);
        h.Connect("b", 1);
        h.Barrier("b", "stagml12");
        Assert.Equal(StoryRefusal.OwnerAway, h.Story("b", Flags(392)).Refused.Single().Reason);
        var last = h.I("b").Id;
        var again = new Harness(StoryPolicy.Drop, ownerPresent: false, session: session, after: h);
        var b = again.Connect("b", 1);
        Assert.Same(h.Hub.Story, again.Hub.Story);
        Assert.Equal([392], again.Hub.StoryState().DroppedFlags);
        Assert.True(again.I("b").Id > last);
        again.Barrier("b", "stagml13");
        Assert.Equal([392], StoryKeys.FromBlob(b.LastOk.Dropped).Flags);
    }

    /// <summary>A revision that changed only counters goes to the saver as such (it writes those at most once a minute).</summary>
    [Fact]
    public void The_saver_learns_which_revisions_changed_only_counters()
    {
        var h = new Harness(StoryPolicy.Apply);
        h.Connect("a", 0);
        h.Barrier("a", "stagml13");
        var i = h.I("a");
        h.Story("a", Flags(392));
        h.Story("a", new StoryDelta { Counters = [new CounterGrowth(1, i.Id, i.Visit, 100)] });
        h.Story("a", new StoryDelta { Counters = [new CounterGrowth(1, i.Id, i.Visit, 200)] });
        h.Story("a", Flags(394));
        Assert.Equal([false, true, true, false], h.CountersOnly);
    }
}
