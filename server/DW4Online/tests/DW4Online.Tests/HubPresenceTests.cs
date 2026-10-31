using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;

namespace DW4Online.Tests;

/// <summary>
/// Presence through the hub (SEAMLESS.md 12, round 60): WELCOME announces PRESENCE2; PRESENCE2 goes to the members that
/// send it and PRESENCE to the others; the hub bounds the new fields (an implausible or later-version frame is dropped and
/// counted, a malformed one closes) and the rate (a token bucket); a member that leaves a stage is shown leaving to
/// those still there.
/// </summary>
public partial class HubLogicTests
{
    private static PresenceMsg Presence(string stage, float x = 10, byte species = 0) => new(stage, 1, x, 0, 20, 0.5f, species, 7, 40, 40, 10, 10);

    private static Presence2Msg Presence2(string stage, float x = 10, byte species = 0, string clip = "runD", byte flags = Presence2Msg.Loop) =>
        new(Presence2Msg.CurrentVersion, Presence(stage, x, species), clip, flags, 0, 1000, "w_a1");

    [Fact]
    public void Welcome_announces_presence2()
    {
        var h = new Harness();
        var a = h.Connect("a", 0);
        var welcome = a.Last(Msg.Welcome, WelcomeMsg.Parse);
        Assert.NotEqual(0, welcome.Flags & WelcomeMsg.PresenceV2);
        Assert.Equal(0, welcome.Flags & WelcomeMsg.Resumable);
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
        h.Advance(200);
        h.Send("a", Presence2("stagML11", x: 12).Frame());
        var toB = h.P("b").All(Msg.PresenceOut, PresenceOutMsg.Parse);
        Assert.Equal(12f, toB.Last().Presence.X);
        Assert.False(h.P("b").Has(Msg.Presence2Out));
        Assert.False(h.P("c").Has(Msg.PresenceOut) || h.P("c").Has(Msg.Presence2Out)); // no presence of its own: no stage to match
        // b's PRESENCE reaches a as PRESENCE.
        Assert.Single(h.P("a").All(Msg.PresenceOut, PresenceOutMsg.Parse));
        // Once b sends PRESENCE2 it gets a's PRESENCE2, with the figure's fields.
        h.Advance(200);
        h.Send("b", Presence2("stagml11").Frame());
        h.Advance(100);
        h.Send("a", Presence2("stagML11", x: 14, clip: "waitS").Frame());
        var ghost = h.P("b").Last(Msg.Presence2Out, Presence2OutMsg.Parse);
        Assert.Equal(((byte)0, 14f, "waitS", "w_a1"), (ghost.Member, ghost.Presence.Presence.X, ghost.Presence.Clip, ghost.Presence.Weapon));
        Assert.Single(h.P("a").All(Msg.Presence2Out, Presence2OutMsg.Parse));
        // Another stage gets nothing.
        h.Send("c", Presence2("stag0101").Frame());
        h.Advance(100);
        h.Send("a", Presence2("stagML11", x: 16).Frame());
        Assert.False(h.P("c").Has(Msg.Presence2Out));
        Assert.Null(h.P("a").Closed);
    }

    [Fact]
    public void Implausible_and_later_version_presence2_is_dropped_and_a_malformed_one_closes()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Send("b", Presence2("stagML11").Frame());
        var sent = 0;
        void Try(Presence2Msg m)
        {
            h.Advance(200);
            h.Send("a", m.Frame());
        }
        Try(Presence2("stagML11", species: 16));
        Try(Presence2("stagML11", x: 2e6f));
        Try(Presence2("stagML11") with { Presence = Presence("stagML11") with { Yaw = 20 } });
        Try(Presence2("stagML11") with { Presence = Presence("stagML11") with { Hp = 41 } });
        Try(Presence2("stagML11") with { Presence = Presence("stagML11") with { Level = 1000 } });
        Try(Presence2("stagML11", flags: 4));
        Assert.Equal(6, h.Hub.Counters["presence_implausible"]);
        Assert.Equal(1, h.Hub.Counters["presence_implausible_species"]);
        Assert.Equal(1, h.Hub.Counters["presence_implausible_flags"]);
        Assert.False(h.P("b").Has(Msg.Presence2Out));
        // A later version (appended bytes): read, not forwarded, the connection stays.
        h.Advance(200);
        var later = Presence2("stagML11").Frame();
        later[Wire.HeaderBytes] = 2;
        h.Hub.Receive(h.Mem("a"), later[0], later[Wire.HeaderBytes..].Concat(new byte[] { 1, 2 }).ToArray(), h.Now);
        Assert.Equal(1, h.Hub.Counters["presence_version"]);
        Assert.False(h.P("b").Has(Msg.Presence2Out));
        // A plausible one goes through.
        Try(Presence2("stagML11", species: 15));
        sent++;
        Assert.Equal(sent, h.P("b").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count);
        // Malformed: the connection layer closes on the exception.
        var bad = Presence2("stagML11", clip: "runD").Frame();
        bad[^(1 + 4 + 4 + 1 + 1 + 4)] = (byte)'/';
        Assert.Throws<ProtocolException>(() => h.Send("a", bad));
        Assert.Null(h.P("a").Closed);
    }

    [Fact]
    public void Presence2_is_rate_limited_by_a_token_bucket()
    {
        var h = new Harness();
        h.Connect("a", 0);
        h.Connect("b", 1);
        h.Send("b", Presence2("stagML11").Frame());
        for (var k = 0; k < 10; k++) h.Send("a", Presence2("stagML11", x: k).Frame());
        Assert.Equal((int)RoomHub.PresenceBurst, h.P("b").All(Msg.Presence2Out, Presence2OutMsg.Parse).Count);
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
        h.Send("a", Presence2("stagML11").Frame());
        h.Send("b", Presence2("stagML11").Frame());
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
        g.Advance(200);
        g.Send("a", Presence("stagML12").Frame());
        Assert.Equal("stagML12", g.P("b").Last(Msg.PresenceOut, PresenceOutMsg.Parse).Presence.Stage);
    }
}
