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

        public Harness(StoryPolicy policy, bool ownerPresent = true, RoomStoryData? story = null, InstanceOptions? instances = null)
        {
            var init = new RoomStoryInit(4242, story ?? new RoomStoryData(), 0, policy, ownerPresent);
            Hub = new RoomHub(new RoomInfo("ROOM01", 43, 1, 2, 10, init), new RelayOptions(), new SealingOptions(), instances ?? new InstanceOptions(),
                NullLogger.Instance) { StorySaver = (owner, s) => Saved.Add((owner, s)) };
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
        var h = new Harness(StoryPolicy.Apply, instances: new InstanceOptions { StoryBurst = 2, StoryMessagesPerMinute = 6 });
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Barrier("a", "stag0001");
        h.Barrier("b", "stag0001", context: 6); // (another quest context: its own instance at once)
        Assert.Empty(h.Story("a", new StoryDelta { Counters = [new CounterGrowth(7, 0, 3, 1)] }).Refused);
        Assert.Empty(h.Story("a", new StoryDelta { Counters = [new CounterGrowth(7, 0, 3, 1)] }).Refused);
        Assert.Equal(StoryRefusal.Rate, h.Story("a", new StoryDelta { Counters = [new CounterGrowth(7, 0, 3, 2)] }).Refused.Single().Reason);
        h.Advance(10_000); // one token per ten seconds
        Assert.Empty(h.Story("a", new StoryDelta { Counters = [new CounterGrowth(7, 0, 3, 2)] }).Refused);
        // b's instance 0 visit 3 is another engine line than a's.
        Assert.Empty(h.Story("b", new StoryDelta { Counters = [new CounterGrowth(7, 0, 3, 1)] }).Refused);
        Assert.Equal(3, h.Hub.StoryState().Counters[7]);
    }
}
