using System.Text.RegularExpressions;
using DW4Online.Domain.Achievements;

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
        Assert.False(AchievementRules.StateHolds("form_mega", r, null));
        for (var s = 0; s < 4; ++s) r.BaseIsUse[s] = 1;
        Assert.True(AchievementRules.StateHolds("form_rookies", r, null));
        r.BaseIsUse[15] = 1;
        Assert.True(AchievementRules.StateHolds("form_mega", r, null));
        Assert.True(AchievementRules.StateHolds("form_susanoomon", r, null));
        Assert.False(AchievementRules.StateHolds("form_all16", r, null));
        Array.Fill(r.BaseIsUse, (byte)1);
        Assert.True(AchievementRules.StateHolds("form_all16", r, null));
        Assert.False(AchievementRules.StateHolds("form_master", r, 99));
        Array.Fill(r.BaseLevel, 99);
        Assert.True(AchievementRules.StateHolds("form_master", r, 99));
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

        Assert.False(AchievementRules.StateHolds("item_rank5", r, 5));
        r.Bank[10] = 5 << 16 | 0x3005;       // rank 5 in the bank
        Assert.True(AchievementRules.StateHolds("item_rank5", r, 5));
        Assert.False(AchievementRules.StateHolds("item_pink", r, 1000));
        r.Devices[3] = 1000 << 20 | 0x3010;  // value 1000
        Assert.True(AchievementRules.StateHolds("item_pink", r, 1000));

        r.Bits = 600_000;
        r.BankBits = 400_000;
        Assert.True(AchievementRules.StateHolds("bits_1m", r, 1_000_000));
        Assert.False(AchievementRules.StateHolds("bits_max", r, 9_999_999));
        r.BankBits = 9_999_999;
        Assert.True(AchievementRules.StateHolds("bits_max", r, 9_999_999));

        Assert.False(AchievementRules.StateHolds("hoarder", r, 36));
        Array.Fill(r.Devices, 0x3001);
        Assert.True(AchievementRules.StateHolds("hoarder", r, 36));
        Assert.False(AchievementRules.StateHolds("no_such_code", r, 1));
    }

    [Fact]
    public void Event_requirements()
    {
        AchievementCheck Check(long goal, long? igt = null, int? level = null, int? players = null, bool room = false, bool host = false) =>
            new(goal, new UnlockEvidence { IgtMs = igt, Level = level }, players, room, host);

        Assert.Null(AchievementRules.CheckEvent("speed_6h", Check(21600, igt: 21_599_000)));
        Assert.NotNull(AchievementRules.CheckEvent("speed_6h", Check(21600, igt: 21_600_001)));
        Assert.NotNull(AchievementRules.CheckEvent("speed_3h", Check(10800)));
        Assert.Null(AchievementRules.CheckEvent("lowlevel_apocalymon", Check(8, level: 8)));
        Assert.NotNull(AchievementRules.CheckEvent("lowlevel_apocalymon", Check(8, level: 9)));
        Assert.Null(AchievementRules.CheckEvent("solo_veryhard", Check(0, players: 1)));
        Assert.NotNull(AchievementRules.CheckEvent("solo_veryhard", Check(0, players: 2)));
        Assert.NotNull(AchievementRules.CheckEvent("online_join", Check(0)));
        Assert.Null(AchievementRules.CheckEvent("online_join", Check(0, room: true)));
        Assert.NotNull(AchievementRules.CheckEvent("online_host", Check(0, players: 4, room: true)));
        Assert.Null(AchievementRules.CheckEvent("online_host", Check(0, players: 4, room: true, host: true)));
        Assert.Null(AchievementRules.CheckEvent("story_briefing", Check(0)));
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

    [Fact]
    public void Seed_and_rules_agree()
    {
        var seed = LoadSeed();
        var state = seed.Where(s => s.Kind == AchievementKinds.State).Select(s => s.Code).ToHashSet();
        Assert.Equal(state.OrderBy(c => c), AchievementRules.StateCodes.OrderBy(c => c));
        foreach (var s in seed.Where(s => s.Kind == AchievementKinds.Stat))
        {
            Assert.NotNull(s.StatKey);
            Assert.True(AchievementRules.StatKeys.ContainsKey(s.StatKey!), s.StatKey);
            Assert.NotNull(s.Goal);
        }
        foreach (var code in AchievementRules.CheckedEventCodes.Concat(AchievementRules.SpeedrunBoards.Keys))
            Assert.Equal(AchievementKinds.Event, seed.Single(s => s.Code == code).Kind);
        Assert.All(seed.Where(s => s.Kind != AchievementKinds.Stat), s => Assert.Null(s.StatKey));
    }
}
