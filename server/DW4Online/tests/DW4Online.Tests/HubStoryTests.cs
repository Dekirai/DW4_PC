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
        public Harness(StoryPolicy policy, bool ownerPresent = true, RoomStoryData? story = null, InstanceOptions? instances = null, int difficulty = 0)
        {
            var init = new RoomStoryInit(4242, story ?? new RoomStoryData(), difficulty, policy, ownerPresent);
            Hub = new RoomHub(new RoomInfo("ROOM01", 43, 1, 2, 10, init), new RelayOptions(), new SealingOptions(),
                instances ?? new InstanceOptions { StoryMinVisitSeconds = 0 }, NullLogger.Instance) { StorySaver = (owner, s) => Saved.Add((owner, s)) };
        }

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
        var h = new Harness(StoryPolicy.Apply, instances: new InstanceOptions { StoryBurst = 1000 });
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
        // Phase 7 (the game cleared) is five steps away; the clear flags only of the room's difficulty (normal: 406).
        h.Barrier("b", "stagbs06");
        h.Advance(11_000);
        var ack = h.Story("b", new StoryDelta { Phase = 7, Flags = [406, 407, 408] });
        Assert.Contains(new StoryRefused(StoryKeyKind.Phase, 7, StoryRefusal.PhaseJump), ack.Refused);
        Assert.Contains(new StoryRefused(StoryKeyKind.Flag, 407, StoryRefusal.Stage), ack.Refused);
        Assert.Contains(new StoryRefused(StoryKeyKind.Flag, 408, StoryRefusal.Stage), ack.Refused);
        Assert.Contains(406, h.Hub.StoryState().Flags);
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
}
