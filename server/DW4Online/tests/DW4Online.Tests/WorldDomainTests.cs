using System.Reflection;
using System.Text.Json;
using DW4Online.Domain;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Achievements;
using DW4Online.Domain.Rooms;
using DW4Online.Domain.Services;
using DW4Online.Domain.World;

namespace DW4Online.Tests;

/// <summary>
/// The world's domain rules without a database (round I1a): the online preferences (defaults = RoomSettings.Default's,
/// the PUT body, the randomizer's packing), Visible() for the lists (19.7.2) as a matrix, the protocol floor in code
/// (19.17.3: 46, 47 and 48 refused with Play:MinimumProtocol 46) and the ticket roles.
/// </summary>
public class WorldDomainTests
{
    [Fact]
    public void Online_prefs_default_to_the_room_settings_defaults_and_apply_a_put_body()
    {
        var d = OnlinePrefs.Default;
        Assert.Equal(RoomSettings.Default, d.HostSettings(0));
        Assert.True(d.IsValid);
        Assert.Equal(("public", "anyone", "friends", true, 1), (d.DefaultVisibility, d.Invites, d.Whereabouts, d.FriendToasts, d.LastBox));

        var body = JsonDocument.Parse("""{"defaultVisibility":"friends","whereabouts":"nobody","fastLoading":true,"randomizer":{"enabled":true,"seed":77,"drops":true,"enemies":2,"zones":1}}""").RootElement;
        var p = d.Apply(body);
        Assert.True(p.Ok);
        var v = p.Value!;
        Assert.Equal(("friends", "nobody", true, 77u), (v.DefaultVisibility, v.Whereabouts, v.FastLoading, v.RandomizerSeed!.Value));
        Assert.Equal(1u | 2u << 1 | 1u << 3, v.RandomizerModes);
        Assert.Equal(new RoomSettings(2, 0, 1, 77, true, 2, 1, true), v.HostSettings(2));
        Assert.True(v.HostSettings(2).RandomizerActive);
        Assert.Equal(v, v.Apply(JsonDocument.Parse("{}").RootElement).Value);
        Assert.Null(v.Apply(JsonDocument.Parse("""{"randomizer":{"enabled":false,"seed":5}}""").RootElement).Value!.RandomizerSeed);
        Assert.Equal(0u, v.Apply(JsonDocument.Parse("""{"randomizer":false}""").RootElement).Value!.RandomizerModes);

        foreach (var bad in new[]
                 {
                     """{"defaultVisibility":"secret"}""", """{"cameraMode":2}""", """{"lightMode":"1"}""", """{"lastBox":0}""", """{"invites":"nobody"}""",
                     """{"friendToasts":1}""", """{"colour":"red"}""", """{"randomizer":{"enabled":true,"enemies":3}}""", """[1]""",
                 })
            Assert.False(d.Apply(JsonDocument.Parse(bad).RootElement).Ok, bad);
        Assert.False((d with { RandomizerModes = 6 }).IsValid); // enemies 3
        Assert.False((d with { RandomizerSeed = 0 }).IsValid);
        Assert.Equal(1u | 2u << 1 | 2u << 3, OnlinePrefs.PackModes(true, 9, 9)); // clamped to 2 and 2
    }

    private static WorldInstanceRow Row(string visibility, long host = 10, params long[] guests) =>
        FakeWorldRelay.Instance(1, host, "Host", "dry_land", "stag0201", visibility, guests.Select(g => (g, $"G{g}", "stag0201")));

    private static WorldViewer Viewer(long id, bool staff = false, long[]? friends = null, long[]? blocks = null) =>
        new(id, staff, (friends ?? []).ToHashSet(), (blocks ?? []).ToHashSet());

    /// <summary>Public / Friends / Private x stranger, friend of the host, friend of a member, staff, a member, a block either way.</summary>
    [Fact]
    public void Visible_follows_the_matrix_of_19_7_2()
    {
        string[] vis = [VisibilityNames.Public, VisibilityNames.Friends, VisibilityNames.Private];
        (string Who, WorldViewer Viewer, bool[] Sees)[] cases =
        [
            ("stranger", Viewer(1), [true, false, false]),
            ("friend of the host", Viewer(2, friends: [10]), [true, true, false]),
            ("friend of a member", Viewer(3, friends: [20]), [true, false, false]),
            ("staff", Viewer(4, staff: true), [true, true, true]),
            ("member", Viewer(20), [true, true, true]),
            ("the host", Viewer(10), [true, true, true]),
            ("blocks a member", Viewer(5, friends: [10], blocks: [20]), [false, false, false]),
            ("blocks the host", Viewer(6, blocks: [10]), [false, false, false]),
            ("staff blocking a member", Viewer(7, staff: true, blocks: [20]), [true, true, true]),
            ("signed out", WorldViewer.Anonymous, [false, false, false]),
        ];
        foreach (var (who, viewer, sees) in cases)
            for (var i = 0; i < vis.Length; ++i)
                Assert.True(WorldVisibility.Visible(Row(vis[i], 10, 20), viewer) == sees[i], $"{who} / {vis[i]}");
    }

    /// <summary>Round I1b (19.6.6): a player under the instance's removal bar does not see it in the lists; staff and its members do.</summary>
    [Fact]
    public void A_removal_bar_hides_the_instance_from_the_barred_player_only()
    {
        var row = Row(VisibilityNames.Public, 10, 20) with { Barred = [30, 4] };
        Assert.False(WorldVisibility.Visible(row, Viewer(30)));
        Assert.False(WorldVisibility.Visible(row, Viewer(30, friends: [10])));
        Assert.True(WorldVisibility.Visible(row, Viewer(31)));
        Assert.True(WorldVisibility.Visible(row, Viewer(4, staff: true))); // (staff see every row in the lists, 19.14)
        Assert.True(WorldVisibility.Visible(row with { Barred = [20] }, Viewer(20))); // (a member always sees his own)
    }

    [Theory]
    [InlineData(0)]
    [InlineData(46)]
    [InlineData(48)]
    public void The_protocol_floor_is_in_code(int configured)
    {
        var gate = new VersionGate(new PlayOptions { MinimumProtocol = configured });
        Assert.Equal(WorldProtocol.Version, gate.MinimumProtocol);
        foreach (var old in new int?[] { null, 1, 46, 47, 48, 49 })
            Assert.Equal(ErrorKind.Outdated, gate.Check(old, "0.60.0")?.Kind);
        Assert.Null(gate.Check(50, "0.60.0"));
        Assert.Null(gate.Check(new PlayContext { Protocol = 50 }));
        Assert.Equal(50, gate.Check(49, null)!.Extra!["minimumProtocol"]);
        Assert.Equal(60, new VersionGate(new PlayOptions { MinimumProtocol = 60 }).MinimumProtocol);
    }

    [Fact]
    public void Ticket_roles_come_from_the_account_role()
    {
        Assert.Equal(WorldRoles.Member, WorldRoles.Of(Roles.Player));
        Assert.Equal(WorldRoles.Staff, WorldRoles.Of(Roles.Moderator));
        Assert.Equal(WorldRoles.Staff, WorldRoles.Of(Roles.Admin));
    }

    /// <summary>The rules of the online achievements in an instance (19.8.9): story challenges only for the host, online_join for guests.</summary>
    [Fact]
    public void Instance_achievement_rules()
    {
        AchievementCheck Check(bool inInstance, bool host, int players) => new(0, null, 1, inInstance, host, 0, players);
        Assert.Null(AchievementRules.CheckEvent("online_join", Check(true, false, 2)));
        Assert.NotNull(AchievementRules.CheckEvent("online_join", Check(true, true, 2)));
        Assert.NotNull(AchievementRules.CheckEvent("online_join", Check(false, true, 1)));
        Assert.Null(AchievementRules.CheckEvent("online_host", Check(true, true, 3)));
        Assert.NotNull(AchievementRules.CheckEvent("online_host", Check(true, true, 2)));
        Assert.NotNull(AchievementRules.CheckEvent("deathless_clear", Check(true, false, 2)));
        Assert.Null(AchievementRules.CheckEvent("deathless_clear", Check(false, true, 1)));
        Assert.Null(AchievementRules.CheckEvent("solo_veryhard", Check(false, true, 1)));
        Assert.NotNull(AchievementRules.CheckEvent("solo_veryhard", Check(true, true, 2)));
        Assert.Equal(("invalid_players", "The instance's player count is outside 1..4."), RankedRules.Check(new PlayContext { InstancePlayers = 5 }));
    }

    [Fact]
    public void The_relays_world_protocol_is_the_floor_in_code()
    {
        // One constant per side (lane contract, web 12:50): the relay's handshake floor (Wire.WorldProtocol, the relay lane's)
        // must be the version gate's (WorldProtocol.Version). A relay without that constant predates the world: its newest
        // protocol is below the floor, so the version gate refuses every client it could serve.
        var field = typeof(DW4Online.Relay.Protocol.Wire).GetField("WorldProtocol", BindingFlags.Public | BindingFlags.Static);
        if (field is not null)
            Assert.Equal(WorldProtocol.Version, Convert.ToInt32(field.GetValue(null)));
        else
            Assert.True(DW4Online.Relay.Protocol.Wire.ReadyCheckProtocol < WorldProtocol.Version);
    }
}
