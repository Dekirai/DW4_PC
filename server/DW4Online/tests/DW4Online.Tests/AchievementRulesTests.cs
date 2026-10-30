using System.Text.RegularExpressions;
using DW4Online.Domain.Achievements;
using DW4Online.Domain;

namespace DW4Online.Tests;

public partial class AchievementRulesTests
{
    [Fact]
    public void Level_rules_use_the_highest_reached_form()
    {
        var r = Samples.Fresh();
        Assert.False(AchievementRules.StateHolds("lv_10", r, 10));
        r.BaseIsUse[4] = 1;
        r.BaseLevel[4] = 12; // a reached form at 12 while the current form is 5
        Assert.True(AchievementRules.StateHolds("lv_10", r, 10));
        r.BaseIsUse[4] = 0;  // an unreached form's level does not count
        Assert.False(AchievementRules.StateHolds("lv_10", r, 10));
        r.Level = 10;
        Assert.True(AchievementRules.StateHolds("lv_10", r, 10));
    }

    [Fact]
    public void Form_rules()
    {
        var r = Samples.Fresh();
        Assert.False(AchievementRules.StateHolds("form_rookies", r, null));
        for (var s = 0; s < 4; ++s) r.BaseIsUse[s] = 1;
        Assert.True(AchievementRules.StateHolds("form_rookies", r, null));
        Assert.False(AchievementRules.StateHolds("form_eight", r, 8));
        for (var s = 4; s < 8; ++s) r.BaseIsUse[s] = 1;
        Assert.True(AchievementRules.StateHolds("form_eight", r, 8));
        r.BaseIsUse[15] = 1;
        Assert.True(AchievementRules.StateHolds("form_susanoomon", r, null));
        Assert.False(AchievementRules.StateHolds("form_all", r, 16));
        Array.Fill(r.BaseIsUse, (byte)1);
        Assert.True(AchievementRules.StateHolds("form_all", r, 16));
        // Form Master: how many forms stand at level 99 (the goal is the count, 16).
        Assert.False(AchievementRules.StateHolds("form_master", r, 16));
        Array.Fill(r.BaseLevel, 99);
        Assert.True(AchievementRules.StateHolds("form_master", r, 16));
        r.BaseLevel[3] = 98;
        Assert.False(AchievementRules.StateHolds("form_master", r, 16));
        Assert.True(AchievementRules.StateHolds("form_master", r, 15));
    }

    [Fact]
    public void Collection_rules()
    {
        var r = Samples.Fresh();
        Assert.False(AchievementRules.StateHolds("card_first", r, 1));
        for (var i = 0; i < 25; ++i) r.Cards[i] = 1;
        Assert.True(AchievementRules.StateHolds("card_first", r, 1));
        Assert.True(AchievementRules.StateHolds("card_25", r, 25));
        Assert.False(AchievementRules.StateHolds("card_50", r, 50));

        // Held devices: the folder, not the bank (the client's rule).
        Assert.False(AchievementRules.StateHolds("item_rank5", r, 5));
        r.Bank[10] = 5 << 16 | 0x3005;       // rank 5 in the bank
        Assert.False(AchievementRules.StateHolds("item_rank5", r, 5));
        r.Devices[2] = 5 << 16 | 0x3005;     // rank 5 in the folder
        Assert.True(AchievementRules.StateHolds("item_rank5", r, 5));
        Assert.False(AchievementRules.StateHolds("item_pink", r, 1000));
        r.Bank[11] = 1000 << 20 | 0x3010;
        Assert.False(AchievementRules.StateHolds("item_pink", r, 1000));
        r.Devices[3] = 1000 << 20 | 0x3010;  // value 1000
        Assert.True(AchievementRules.StateHolds("item_pink", r, 1000));

        r.Bits = 60_000;
        r.BankBits = 40_000;
        Assert.True(AchievementRules.StateHolds("bits_100k", r, 100_000));
        Assert.False(AchievementRules.StateHolds("bits_1m", r, 1_000_000));
        r.Bits = 600_000;
        r.BankBits = 400_000;
        Assert.True(AchievementRules.StateHolds("bits_1m", r, 1_000_000));
        Assert.False(AchievementRules.StateHolds("bits_max", r, 9_999_999));
        r.BankBits = 9_999_999;               // the bank does not count for the wallet goal
        Assert.False(AchievementRules.StateHolds("bits_max", r, 9_999_999));
        r.Bits = 9_999_999;
        Assert.True(AchievementRules.StateHolds("bits_max", r, 9_999_999));

        Assert.False(AchievementRules.StateHolds("hoarder", r, 36));
        Array.Fill(r.Devices, 0x3001);
        Assert.True(AchievementRules.StateHolds("hoarder", r, 36));
        Assert.False(AchievementRules.StateHolds("no_such_code", r, 1));
    }

    [Fact]
    public void Event_requirements()
    {
        AchievementCheck Check(long goal, long? igt = null, int? level = null, int? players = null, bool room = false, bool host = false,
            int? roomPlayers = null) =>
            new(goal, new UnlockEvidence { IgtMs = igt, Level = level }, players, room, host, 0, roomPlayers);

        Assert.Null(AchievementRules.CheckEvent("speed_normal", Check(28800, igt: 28_799_000)));
        Assert.NotNull(AchievementRules.CheckEvent("speed_normal", Check(28800, igt: 28_800_001)));
        Assert.NotNull(AchievementRules.CheckEvent("speed_fast", Check(14400)));
        Assert.Null(AchievementRules.CheckEvent("lowlevel_apocalymon", Check(12, level: 12)));
        Assert.NotNull(AchievementRules.CheckEvent("lowlevel_apocalymon", Check(12, level: 13)));
        Assert.Null(AchievementRules.CheckEvent("solo_veryhard", Check(0, players: 1)));
        Assert.NotNull(AchievementRules.CheckEvent("solo_veryhard", Check(0, players: 2)));
        Assert.NotNull(AchievementRules.CheckEvent("solo_veryhard", Check(0, players: 1, room: true, host: true, roomPlayers: 2)));
        Assert.NotNull(AchievementRules.CheckEvent("online_join", Check(0)));
        Assert.Null(AchievementRules.CheckEvent("online_join", Check(0, room: true)));
        Assert.NotNull(AchievementRules.CheckEvent("online_host", Check(0, players: 4, room: true)));
        Assert.Null(AchievementRules.CheckEvent("online_host", Check(0, players: 3, room: true, host: true)));
        // Room Host and Raid Night count the room, not the reporter's instance.
        Assert.Null(AchievementRules.CheckEvent("online_host", Check(0, players: 1, room: true, host: true, roomPlayers: 3)));
        Assert.NotNull(AchievementRules.CheckEvent("online_host", Check(0, players: 4, room: true, host: true, roomPlayers: 2)));
        Assert.Null(AchievementRules.CheckEvent("online_raid", Check(0, players: 1, room: true, roomPlayers: 2)));
        Assert.NotNull(AchievementRules.CheckEvent("online_raid", Check(0, players: 1, room: true, roomPlayers: 1)));
        Assert.Null(AchievementRules.CheckEvent("mp_couch", Check(0, players: 2, room: true, roomPlayers: 5)));
        Assert.NotNull(AchievementRules.CheckEvent("mp_fullhouse", Check(0, players: 3, room: true, roomPlayers: 5)));
        Assert.Null(AchievementRules.CheckEvent("story_apocalymon", Check(0)));
    }

    [Fact]
    public void Story_challenges_count_only_for_the_room_owner()
    {
        AchievementCheck Check(bool room, bool host) => new(14400, new UnlockEvidence { IgtMs = 3600_000 }, 1, room, host, 0, 1);
        foreach (var code in AchievementRules.OwnerOnlyCodes)
        {
            Assert.NotNull(AchievementRules.CheckEvent(code, Check(room: true, host: false)));
            Assert.Null(AchievementRules.CheckEvent(code, Check(room: true, host: true)));
            Assert.Null(AchievementRules.CheckEvent(code, Check(room: false, host: false))); // Play:RequireRoom off
        }
        // The plain clear counts for everybody who was there.
        Assert.Null(AchievementRules.CheckEvent("clear_normal", Check(room: true, host: false)));
    }

    [Fact]
    public void Speed_goals_respect_the_story_floor()
    {
        var floor = new PlayOptions().StoryFloorMs;
        Assert.Equal(45 * 60 * 1000L, floor);
        AchievementCheck Check(long goal, long igt) => new(goal, new UnlockEvidence { IgtMs = igt }, 1, true, true, floor);

        Assert.NotNull(AchievementRules.CheckEvent("speed_fast", Check(14400, 1)));          // a 1 ms "speedrun"
        Assert.NotNull(AchievementRules.CheckEvent("speed_fast", Check(14400, floor - 1)));
        Assert.Null(AchievementRules.CheckEvent("speed_fast", Check(14400, floor)));
        Assert.Null(AchievementRules.CheckEvent("speed_normal", Check(28800, 5 * 3600 * 1000L)));
        Assert.NotNull(AchievementRules.CheckEvent("speed_normal", Check(28800, 0)));
    }

    [Fact]
    public void Speedrun_floors_cover_every_board_and_can_be_overridden()
    {
        var play = new PlayOptions();
        Assert.Equal(AchievementRules.SpeedrunBoards.Values.Order(), play.SpeedrunFloorSeconds.Keys.Order());
        Assert.All(AchievementRules.SpeedrunBoards.Values, board => Assert.Equal(2_700_000, play.SpeedrunFloorMs(board)));
        play.SpeedrunFloorSeconds["story_veryhard"] = 3600;
        play.SpeedrunFloorSeconds["story_normal"] = 1800;
        Assert.Equal(1_800_000, play.StoryFloorMs);
        Assert.Equal(3_600_000, play.SpeedrunFloorMs("story_veryhard"));
        Assert.Equal(0, play.SpeedrunFloorMs("unknown_board"));
    }

    [Fact]
    public void Ranked_rules()
    {
        Assert.Null(RankedRules.Check(null));
        Assert.Null(RankedRules.Check(new PlayContext { Players = 4, Difficulty = 2, Randomizer = true }));
        Assert.Equal("unranked_cheats", RankedRules.Check(new PlayContext { Cheats = true })?.Code);
        Assert.Equal("unranked_rebalanced", RankedRules.Check(new PlayContext { Rebalanced = true })?.Code);
        Assert.Equal("unranked_save_state", RankedRules.Check(new PlayContext { SaveStateLoaded = true })?.Code);
        Assert.Equal("invalid_players", RankedRules.Check(new PlayContext { Players = 11 })?.Code);
        Assert.Equal("invalid_players", RankedRules.Check(new PlayContext { Players = 1, RoomPlayers = 0 })?.Code);
        Assert.Null(RankedRules.Check(new PlayContext { Players = 1, RoomPlayers = 10 }));
        Assert.Equal(Rulesets.Randomizer, Rulesets.Of(new PlayContext { Randomizer = true }));
        Assert.Equal(Rulesets.Standard, Rulesets.Of(null));
    }

    // The seed migration and the rules must agree: every state / stat definition has a rule, ids and codes are stable.
    [GeneratedRegex(@"^\(\s*(\d+),\s*'([a-z0-9_]+)',\s*'((?:[^']|'')*)',\s*'((?:[^']|'')*)',\s*'(\w+)',\s*'(\w+)',\s*(\d+),\s*'(\w+)',\s*(NULL|\d+),\s*(NULL|'\w+'),\s*([01]),\s*(\d+)\)[,;]", RegexOptions.Multiline)]
    private static partial Regex SeedRow();

    private sealed record Seed(int Id, string Code, string Name, string Category, string Tier, int Points, string Kind, long? Goal, string? StatKey, bool Hidden);

    private static List<Seed> LoadSeed()
    {
        var sql = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "sql", "0002_achievements.sql"));
        return SeedRow().Matches(sql).Select(m => new Seed(
            int.Parse(m.Groups[1].Value), m.Groups[2].Value, m.Groups[3].Value, m.Groups[5].Value, m.Groups[6].Value,
            int.Parse(m.Groups[7].Value), m.Groups[8].Value, m.Groups[9].Value == "NULL" ? null : long.Parse(m.Groups[9].Value),
            m.Groups[10].Value == "NULL" ? null : m.Groups[10].Value.Trim('\''), m.Groups[11].Value == "1")).ToList();
    }

    [Fact]
    public void Seed_has_72_stable_definitions()
    {
        var seed = LoadSeed();
        Assert.Equal(72, seed.Count);
        Assert.Equal(Enumerable.Range(1, 72), seed.Select(s => s.Id));
        Assert.Equal(seed.Count, seed.Select(s => s.Code).Distinct().Count());
        var points = new Dictionary<string, int> { ["bronze"] = 10, ["silver"] = 25, ["gold"] = 50, ["platinum"] = 100 };
        Assert.All(seed, s => Assert.Equal(points[s.Tier], s.Points));
        Assert.Equal("story_briefing", seed[0].Code);
        Assert.Equal("online_raid", seed[71].Code);
    }

    // The catalogue after 0008: the game client's (data/online/achievements.json, exported by the client's
    // dw4_achievements_tests; its ctest compares the same file with the client's table).
    [GeneratedRegex(@"^\(\s*(\d+),\s*'([a-z0-9_]+)',\s*'((?:[^']|'')*)',\s*'((?:[^']|'')*)',\s*'(\w+)',\s*'(\w+)',\s*(\d+),\s*'(\w+)',\s*(NULL|\d+),\s*(NULL|'\w+'),\s*([01]),\s*(\d+)\)[,;]?\r?$", RegexOptions.Multiline)]
    private static partial Regex CatalogueRow();

    private sealed record CatalogueEntry(int Id, string Code, string Name, string Description, string Category, string Tier, int Points,
        string Kind, long? Goal, string? StatKey, bool Hidden, int SortOrder);

    private static string Sql0008() => File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "sql", "0008_achievement_catalogue.sql"));

    private static List<CatalogueEntry> LoadCatalogue()
    {
        static string Unquote(string s) => s.Replace("''", "'");
        return CatalogueRow().Matches(Sql0008()).Select(m => new CatalogueEntry(
            int.Parse(m.Groups[1].Value), m.Groups[2].Value, Unquote(m.Groups[3].Value), Unquote(m.Groups[4].Value), m.Groups[5].Value,
            m.Groups[6].Value, int.Parse(m.Groups[7].Value), m.Groups[8].Value,
            m.Groups[9].Value == "NULL" ? null : long.Parse(m.Groups[9].Value),
            m.Groups[10].Value == "NULL" ? null : m.Groups[10].Value.Trim('\''), m.Groups[11].Value == "1", int.Parse(m.Groups[12].Value))).ToList();
    }

    private static List<System.Text.Json.JsonElement> LoadClientCatalogue()
    {
        using var doc = System.Text.Json.JsonDocument.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "data", "achievements.json")));
        Assert.Equal("dw4-achievements/1", doc.RootElement.GetProperty("format").GetString());
        return doc.RootElement.GetProperty("achievements").EnumerateArray().Select(e => e.Clone()).ToList();
    }

    [Fact]
    public void Catalogue_is_the_game_clients()
    {
        var catalogue = LoadCatalogue();
        var client = LoadClientCatalogue();
        Assert.Equal(72, client.Count);
        Assert.Equal(client.Select(c => c.GetProperty("id").GetString()), catalogue.Select(c => c.Code));
        for (var i = 0; i < client.Count; ++i)
        {
            var c = client[i];
            var s = catalogue[i];
            Assert.Equal(c.GetProperty("title").GetString(), s.Name);
            Assert.Equal(c.GetProperty("description").GetString(), s.Description);
            Assert.Equal(c.GetProperty("category").GetString(), s.Category);
            Assert.Equal(c.GetProperty("kind").GetString(), s.Kind);
            var goal = c.GetProperty("goal");
            Assert.Equal(goal.ValueKind == System.Text.Json.JsonValueKind.Null ? null : goal.GetInt64(), s.Goal);
            var stat = c.GetProperty("stat");
            Assert.Equal(stat.ValueKind == System.Text.Json.JsonValueKind.Null ? null : stat.GetString(), s.StatKey);
            Assert.Equal(c.GetProperty("ownerOnly").GetBoolean(), AchievementRules.OwnerOnlyCodes.Contains(s.Code));
            Assert.Equal(10 * (i + 1), s.SortOrder);
        }
    }

    [Fact]
    public void Catalogue_keeps_ids_and_retires_the_rest()
    {
        var seed = LoadSeed().ToDictionary(s => s.Code);
        var catalogue = LoadCatalogue();
        var points = new Dictionary<string, int> { ["bronze"] = 10, ["silver"] = 25, ["gold"] = 50, ["platinum"] = 100 };
        Assert.All(catalogue, s => Assert.Equal(points[s.Tier], s.Points));
        Assert.Equal(catalogue.Count, catalogue.Select(s => s.Id).Distinct().Count());
        foreach (var s in catalogue)
        {
            if (seed.TryGetValue(s.Code, out var old))
                Assert.Equal(old.Id, s.Id);                         // a code keeps its id
            else
                Assert.InRange(s.Id, 73, 92);                       // the client's other 20 are new
        }
        Assert.Equal(52, catalogue.Count(s => seed.ContainsKey(s.Code)));
        // Every 0002 code the client does not have is retired by name (never reused).
        var sql = Sql0008();
        var retire = sql[sql.IndexOf("UPDATE achievements SET active = 0", StringComparison.Ordinal)..];
        var retired = Regex.Matches(retire, "'([a-z0-9_]+)'").Select(m => m.Groups[1].Value).ToHashSet();
        Assert.Equal(seed.Keys.Where(c => catalogue.All(s => s.Code != c)).Order(), retired.Order());
    }

    [Fact]
    public void Catalogue_and_rules_agree()
    {
        var catalogue = LoadCatalogue();
        var state = catalogue.Where(s => s.Kind == AchievementKinds.State).Select(s => s.Code).ToHashSet();
        Assert.Equal(state.OrderBy(c => c), AchievementRules.StateCodes.OrderBy(c => c));
        foreach (var s in catalogue.Where(s => s.Kind == AchievementKinds.Stat))
        {
            Assert.NotNull(s.StatKey);
            Assert.True(AchievementRules.StatKeys.ContainsKey(s.StatKey!), s.StatKey);
            Assert.NotNull(s.Goal);
        }
        var codes = catalogue.Select(s => s.Code).ToHashSet();
        foreach (var code in AchievementRules.CheckedEventCodes.Concat(AchievementRules.SpeedrunBoards.Keys).Concat(AchievementRules.OwnerOnlyCodes))
        {
            Assert.Contains(code, codes);
            Assert.Equal(AchievementKinds.Event, catalogue.Single(s => s.Code == code).Kind);
        }
        Assert.All(catalogue.Where(s => s.Kind != AchievementKinds.Stat), s => Assert.Null(s.StatKey));
    }
}
