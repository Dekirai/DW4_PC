using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// Presence through the world hub (SEAMLESS.md 12, round 60; 19.13, round I1a): WELCOME announces PRESENCE2; in a hub stage a
/// sender's presence goes to the receivers whose selection chose it (computed once a second off the lock), PRESENCE2 to the
/// members that send it and PRESENCE to the others; the hub bounds the fields (an implausible frame is dropped and counted, HP or
/// MP above the maximum go as the maximum, a later version's checked version 1 part goes on, a larger frame is dropped unread, a
/// malformed one closes) and the rate (a token bucket); a member that leaves a stage is shown leaving to those that drew it.
/// </summary>
public partial class HubLogicTests
{
    private static PresenceMsg Presence(string stage, float x = 10, byte species = 0) => new(stage, 1, x, 0, 20, 0.5f, species, 7, 40, 40, 10, 10);

    private static Presence2Msg Presence2(string stage, float x = 10, byte species = 0, string clip = "runD", byte flags = Presence2Msg.Loop) =>
        new(Presence2Msg.CurrentVersion, Presence(stage, x, species), clip, flags, 0, 1000, "w_a1");

    /// <summary>The members' first presence in a hub stage, then the selection (the housekeeping's, once a second).</summary>
    private static void Meet(Harness h, string stage, params string[] names)
    {
        foreach (var n in names) h.Send(n, Presence2(stage).Frame());
        h.Advance(1100);
        h.Hub.Housekeep(h.Now);
    }

    [Fact]
    public void Welcome_announces_presence2()
    {
        var h = new Harness();
        var a = h.Connect("a", 0);
        var welcome = a.Last(Msg.Welcome, WelcomeMsg.Parse);
        Assert.NotEqual(0, welcome.Flags & WelcomeMsg.PresenceV2);
        Assert.NotEqual(0, welcome.Flags & WelcomeMsg.World);
        Assert.Equal(0, welcome.Flags & WelcomeMsg.Resumable);
        Assert.Equal(h.Mem("a").Id, welcome.Member);
    }

    [Fact]
    public void Presence2_goes_to_the_members_that_send_it_and_presence_to_the_others()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Connect("c", 2);
        // a speaks PRESENCE2, b PRESENCE (an older client), c not at all yet.
        h.Send("a", Presence2("stagML11").Frame());
        h.Send("b", Presence("stagml11").Frame());
        h.Advance(1100);
        h.Hub.Housekeep(h.Now);
        h.Send("a", Presence2("stagML11", x: 12).Frame());
        var toB = h.P("b").All(Msg.PresenceOut, PresenceOutMsg.Parse);
        Assert.Equal(12f, toB.Last().Presence.X);
        Assert.Equal(h.Mem("a").Id, toB.Last().Member);
        Assert.False(h.P("b").Has(Msg.Presence2Out));
        // MEMBER_INFO came first: the receiver knows the name before the figure.
        Assert.Equal("a", h.P("b").Last(Msg.MemberInfo, MemberInfoMsg.Parse).Name);
        Assert.False(h.P("c").Has(Msg.PresenceOut) || h.P("c").Has(Msg.Presence2Out)); // no presence of its own: no stage to match
        // b's PRESENCE reaches a as PRESENCE.
        h.Advance(200);
        h.Send("b", Presence("stagml11").Frame());
        Assert.Single(h.P("a").All(Msg.PresenceOut, PresenceOutMsg.Parse));
        // Once b sends PRESENCE2 it gets a's PRESENCE2, with the figure's fields.
        h.Advance(200);
        h.Send("b", Presence2("stagml11").Frame());
        h.Advance(100);
        h.Send("a", Presence2("stagML11", x: 14, clip: "waitS").Frame());
        var ghost = h.P("b").Last(Msg.Presence2Out, Presence2OutMsg.Parse);
        Assert.Equal((h.Mem("a").Id, 14f, "waitS", "w_a1"), (ghost.Member, ghost.Presence.Presence.X, ghost.Presence.Clip, ghost.Presence.Weapon));
        Assert.Single(h.P("a").All(Msg.Presence2Out, Presence2OutMsg.Parse));
        // Another stage gets nothing.
        h.Send("c", Presence2("stag0101").Frame());
        h.Advance(100);
        h.Send("a", Presence2("stagML11", x: 16).Frame());
        Assert.False(h.P("c").Has(Msg.Presence2Out));
        Assert.Null(h.P("a").Closed);
    }

    [Fact]
    public void Implausible_presence2_is_dropped_a_later_version_goes_on_as_version_1_and_a_malformed_one_closes()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        Meet(h, "stagML11", "a", "b");
        var sent = h.P("b").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count;
        void Try(Presence2Msg m)
        {
            h.Advance(200);
            h.Send("a", m.Frame());
        }
        Try(Presence2("stagML11", species: 16));
        Try(Presence2("stagML11", x: 2e6f));
        Try(Presence2("stagML11") with { Presence = Presence("stagML11") with { Yaw = 20 } });
        Try(Presence2("stagML11") with { Presence = Presence("stagML11") with { Level = 1000 } });
        Try(Presence2("stagML11", flags: 4));
        Assert.Equal(5, h.Hub.Counters["presence_implausible"]);
        Assert.Equal(1, h.Hub.Counters["presence_implausible_species"]);
        Assert.Equal(1, h.Hub.Counters["presence_implausible_flags"]);
        Assert.Equal(sent, h.P("b").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count);
        // HP and MP above the maximum (a script may raise them: MDLaddHp) go on as the maximum, the tag and figure stay.
        Try(Presence2("stagML11") with { Presence = Presence("stagML11") with { Hp = 41, Mp = 60 } });
        sent++;
        var full = h.P("b").Last(Msg.Presence2Out, Presence2OutMsg.Parse).Presence.Presence;
        Assert.Equal(((ushort)40, (ushort)40, (ushort)10, (ushort)10), (full.Hp, full.HpMax, full.Mp, full.MpMax));
        // A later version (appended bytes, a flag version 1 does not know): its version 1 part goes on, written at
        // version 1 without the unknown flag and the appended bytes; the connection stays.
        h.Advance(200);
        var later = Presence2("stagML11", x: 33, flags: Presence2Msg.Loop | 8).Frame();
        later[Wire.HeaderBytes] = 2;
        h.Hub.Receive(h.Mem("a"), later[0], later[Wire.HeaderBytes..].Concat(new byte[] { 1, 2 }).ToArray(), h.Now);
        Assert.Equal(1, h.Hub.Counters["presence_version"]);
        sent++;
        var forwarded = h.P("b").Last(Msg.Presence2Out, Presence2OutMsg.Parse);
        Assert.Equal((Presence2Msg.CurrentVersion, 33f, Presence2Msg.Loop, "w_a1"),
            (forwarded.Presence.Version, forwarded.Presence.Presence.X, forwarded.Presence.Flags, forwarded.Presence.Weapon));
        Assert.Null(h.P("a").Closed);
        // Its version 1 part is checked like any other.
        h.Advance(200);
        var laterBad = Presence2("stagML11", species: 16).Frame();
        laterBad[Wire.HeaderBytes] = 2;
        h.Hub.Receive(h.Mem("a"), laterBad[0], laterBad[Wire.HeaderBytes..], h.Now);
        Assert.Equal(2, h.Hub.Counters["presence_version"]);
        Assert.Equal(6, h.Hub.Counters["presence_implausible"]);
        Assert.Equal(sent, h.P("b").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count);
        // A plausible one goes through.
        Try(Presence2("stagML11", species: 15));
        sent++;
        Assert.Equal(sent, h.P("b").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count);
        // Any version stays within MaxBytes: a larger frame is dropped unread (counted), the connection stays.
        h.Advance(200);
        var huge = Presence2("stagML11", x: 44).Frame();
        huge[Wire.HeaderBytes] = 2;
        var payload = huge[Wire.HeaderBytes..].Concat(new byte[Presence2Msg.MaxBytes]).ToArray();
        h.Hub.Receive(h.Mem("a"), huge[0], payload, h.Now);
        Assert.Equal(1, h.Hub.Counters["presence_oversize"]);
        Assert.Equal(sent, h.P("b").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count);
        Assert.True(Presence2("stagML11").Frame().Length - Wire.HeaderBytes <= Presence2Msg.MaxBytes);
        Assert.Null(h.P("a").Closed);
        // Malformed: the connection layer closes on the exception.
        var bad = Presence2("stagML11", clip: "runD").Frame();
        bad[^(1 + 4 + 4 + 1 + 1 + 4)] = (byte)'/';
        Assert.Throws<ProtocolException>(() => h.Send("a", bad));
        Assert.Null(h.P("a").Closed);
    }

    [Fact]
    public void Presence_of_an_older_client_is_bounded_like_presence2()
    {
        // A PRESENCE drives a ghost figure as well (round 60): the same bounds, HP above the maximum as the maximum.
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Send("a", Presence("stagML11").Frame());
        h.Send("b", Presence2("stagML11").Frame());
        h.Advance(1100);
        h.Hub.Housekeep(h.Now);
        void Try(PresenceMsg m)
        {
            h.Advance(200);
            h.Send("a", m.Frame());
        }
        Try(Presence("stagML11", species: 255));
        Try(Presence("stagML11", x: 1e30f));
        Try(Presence("stagML11") with { Yaw = -30 });
        Try(Presence("stagML11") with { Level = 5000 });
        Assert.Equal(4, h.Hub.Counters["presence_implausible"]);
        Assert.False(h.P("b").Has(Msg.PresenceOut) || h.P("b").Has(Msg.Presence2Out));
        Try(Presence("stagML11", x: 7) with { Hp = 900, Mp = 11 });
        var out1 = h.P("b").Last(Msg.PresenceOut, PresenceOutMsg.Parse).Presence;
        Assert.Equal((7f, (ushort)40, (ushort)10), (out1.X, out1.Hp, out1.Mp));
        // A maximum of 0 (stats not known yet) keeps the HP as sent.
        Try(Presence("stagML11") with { Hp = 5, HpMax = 0 });
        Assert.Equal((ushort)5, h.P("b").Last(Msg.PresenceOut, PresenceOutMsg.Parse).Presence.Hp);
        Assert.Null(h.P("a").Closed);
    }

    [Fact]
    public void Presence2_is_rate_limited_by_a_token_bucket()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        Meet(h, "stagML11", "a", "b");
        var start = h.P("b").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count;
        h.Advance(1000);
        for (var k = 0; k < 10; k++) h.Send("a", Presence2("stagML11", x: k).Frame());
        Assert.Equal(start + (int)RoomHub.PresenceBurst, h.P("b").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count);
        Assert.Equal(10 - (long)RoomHub.PresenceBurst, h.Hub.Counters["presence_dropped"]);
        // Ten a second (a moving figure) all go through.
        h.Advance(1000);
        var before = h.P("b").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count;
        for (var k = 0; k < 50; k++)
        {
            h.Advance(100);
            h.Send("a", Presence2("stagML11", x: k).Frame());
        }
        Assert.Equal(before + 50, h.P("b").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count);
        // Thirty a second do not: about the bucket's rate.
        before = h.P("b").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count;
        for (var k = 0; k < 90; k++)
        {
            h.Advance(1000.0 / 30);
            h.Send("a", Presence2("stagML11", x: k).Frame());
        }
        var passed = h.P("b").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count - before;
        Assert.InRange(passed, (int)(3 * RoomHub.PresenceRate) - 1, (int)(3 * RoomHub.PresenceRate + RoomHub.PresenceBurst) + 1);
    }

    [Fact]
    public void A_member_leaving_a_stage_is_shown_leaving_to_those_still_there()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        Meet(h, "stagML11", "a", "b");
        h.Advance(200);
        // a loads (hidden: an empty stage): b, still in the lobby, hears it at once.
        h.Send("a", Presence2("").Frame());
        Assert.Equal("", h.P("b").Last(Msg.Presence2Out, Presence2OutMsg.Parse).Presence.Presence.Stage);
        var count = h.P("b").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count;
        // a arrives in a field: nothing more for b (the stage a left was the empty one).
        h.Advance(200);
        h.Send("a", Presence2("stag0101").Frame());
        Assert.Equal(count, h.P("b").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count);
        // A PRESENCE member leaving straight to another stage is shown leaving too (as PRESENCE to a PRESENCE member).
        var g = new Harness();
        g.Connect("a", 0);
        g.Connect("b", 1);
        g.Send("a", Presence("stagML11").Frame());
        g.Send("b", Presence("stagML11").Frame());
        g.Advance(1100);
        g.Hub.Housekeep(g.Now);
        g.Advance(200);
        g.Send("a", Presence("stagML12").Frame());
        Assert.Equal("stagML12", g.P("b").Last(Msg.PresenceOut, PresenceOutMsg.Parse).Presence.Stage);
    }

    // Round I1a (SEAMLESS.md 19.13): the lobby as a meeting place --------------------------------------------------------------

    [Fact]
    public void The_selection_draws_at_most_the_cap_nearest_first_never_a_blocked_pair_and_keeps_its_figures()
    {
        var b1 = new HubMember(1, 1, "r");
        var others = Enumerable.Range(2, 35).Select(k => new HubMember((uint)k, k, "m" + k)).ToList();
        var blocked = others[0]; // the nearest of all
        PresenceSample S(HubMember m, float x, IReadOnlySet<long>? blocks = null) =>
            new(m, m.AccountId, "stagml11", x, 0, new HashSet<long>(), blocks ?? new HashSet<long>(), new HashSet<long>());
        var samples = new List<PresenceSample> { S(b1, 0, new HashSet<long> { blocked.AccountId }) };
        for (var k = 0; k < others.Count; ++k) samples.Add(S(others[k], k + 1));
        var choice = PresenceSelection.Compute(samples, new Dictionary<HubMember, PresenceChoice>(), 30, 10);
        var mine = choice[b1];
        Assert.Equal(30, mine.All.Count);
        Assert.DoesNotContain(blocked, mine.All); // r blocks it: never drawn
        Assert.DoesNotContain(b1, choice[blocked].All); // nor the other way round
        Assert.Equal(others.Skip(1).Take(30).ToHashSet(), mine.All.ToHashSet());
        Assert.Equal(others.Skip(1).Take(10).ToHashSet(), mine.Near.ToHashSet());
        // A figure moves a little farther than the 30th candidate: it stays (within 20 %); much farther: it goes.
        samples[31] = S(others[30], 33); // was at 31, the 30th at 31 is now others[31] at 32
        var again = PresenceSelection.Compute(samples, choice, 30, 10)[b1];
        Assert.Contains(others[30], again.All);
        samples[31] = S(others[30], 100);
        Assert.DoesNotContain(others[30], PresenceSelection.Compute(samples, choice, 30, 10)[b1].All);
    }

    [Fact]
    public void A_lobby_of_strangers_server_wide_is_drawn_with_the_far_figures_at_half_rate()
    {
        var h = new Harness();
        var names = Enumerable.Range(0, 14).Select(k => "p" + k).ToArray();
        foreach (var (n, k) in names.Select((n, k) => (n, k))) h.Connect(n, k);
        for (var k = 0; k < names.Length; ++k) h.Send(names[k], Presence2("stagML11", x: k * 5).Frame());
        h.Advance(1100);
        h.Hub.Housekeep(h.Now);
        // p0 is drawn by everyone; its nearest receivers (p1..p10) get every packet, the farther ones (p11..p13) every second one.
        for (var k = 0; k < 4; ++k)
        {
            h.Advance(200);
            h.Send("p0", Presence2("stagML11", x: 0).Frame());
        }
        var near = h.P("p1").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count(m => m.Member == h.Mem("p0").Id);
        var far = h.P("p13").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count(m => m.Member == h.Mem("p0").Id);
        Assert.Equal(4, near);
        Assert.Equal(2, far);
        Assert.True(h.Hub.Counters["presence_tier_skipped"] > 0);
    }

    [Fact]
    public void In_a_field_presence_goes_only_to_the_own_instances_members_of_other_stage_groups()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Connect("c", 2);
        // a hosts in stag0101; b, at home, plays in stag0101 too (a's instance's stage group is not joinable: b makes its own? No:
        // the automatic join puts b into a's instance; it waits for a's group, so it gets a stage group of its own here through a
        // second stage: simplest, b is in a's instance in another stage group of stag0101 after a fork-free NEW of its own group).
        h.Barrier("a", "stag0101");
        h.Snapshot("a");
        h.Barrier("c", "stag0201");
        h.Snapshot("c");
        var wa = h.Mem("a").World!;
        var wc = h.Mem("c").World!;
        Assert.NotSame(wa, wc);
        h.Send("a", Presence2("stag0101").Frame());
        h.Send("c", Presence2("stag0101").Frame()); // (c claims a's stage: still another instance)
        h.Advance(200);
        h.Send("a", Presence2("stag0101", x: 3).Frame());
        Assert.False(h.P("c").Has(Msg.Presence2Out));
    }
}
