using System.Net;
using System.Net.Http.Json;
using Dapper;

namespace DW4Online.Integration.Tests;

public class AchievementTests
{
    [DbFact]
    public async Task Public_list_masks_hidden_achievements()
    {
        await using var host = await TestHost.StartAsync();
        var list = await (await host.Client().GetAsync("/api/v1/achievements")).Json();
        Assert.Equal(72, list.GetArrayLength());
        var lucemon = list.EnumerateArray().Single(a => a.GetProperty("code").GetString() == "boss_lucemon");
        Assert.Equal("Hidden achievement", lucemon.GetProperty("name").GetString());
        var first = list[0];
        Assert.Equal("New Recruit", first.GetProperty("name").GetString());
    }

    [DbFact]
    public async Task Unlocks_follow_the_ranked_rules()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, id) = await host.RegisterAsync("Hunter");
        var (_, _, guest) = await host.RegisterAsync("Buddy");
        var client = host.Client(token);

        var noRoom = await client.PostAsJsonAsync("/api/v1/achievements/unlock", new { code = "story_briefing" });
        Assert.Equal(HttpStatusCode.Forbidden, noRoom.StatusCode);
        Assert.Equal("room_required", await noRoom.ProblemCode());

        var room = await host.CreateRoomAsync(id, [guest]);
        var notMine = await host.CreateRoomAsync(guest, []);
        Assert.Equal("not_in_room", await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "story_briefing", context = new { roomCode = notMine } })).ProblemCode());

        var ok = await client.PostAsJsonAsync("/api/v1/achievements/unlock", new { code = "story_briefing", context = new { roomCode = room, players = 2 } });
        Assert.Equal(HttpStatusCode.OK, ok.StatusCode);
        Assert.True((await ok.Json()).GetProperty("newlyUnlocked").GetBoolean());
        var repeat = await (await client.PostAsJsonAsync("/api/v1/achievements/unlock", new { code = "story_briefing", context = new { roomCode = room } })).Json();
        Assert.False(repeat.GetProperty("newlyUnlocked").GetBoolean());

        var randomizer = await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "story_briefing", context = new { roomCode = room, randomizer = true } })).Json();
        Assert.Equal("randomizer", randomizer.GetProperty("ruleset").GetString());
        Assert.True(randomizer.GetProperty("newlyUnlocked").GetBoolean());

        Assert.Equal("unranked_save_state", await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "story_restore", context = new { roomCode = room, saveStateLoaded = true } })).ProblemCode());
        Assert.Equal("server_tracked", await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "lv_999", context = new { roomCode = room } })).ProblemCode());
        Assert.Equal("unknown_achievement", await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "nope", context = new { roomCode = room } })).ProblemCode());
        Assert.Equal("requirement_not_met", await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "speed_3h", context = new { roomCode = room }, evidence = new { igtMs = 4 * 3600 * 1000L } })).ProblemCode());
        // Ten players claimed, but the room has two members: the count is capped by the room.
        Assert.Equal("requirement_not_met", await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "online_ten", context = new { roomCode = room, players = 10 } })).ProblemCode());

        var clear = await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "clear_normal", context = new { roomCode = room, players = 2 }, evidence = new { igtMs = 5 * 3600 * 1000L } })).Json();
        Assert.True(clear.GetProperty("newBestTime").GetBoolean());
        var slower = await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "clear_normal", context = new { roomCode = room }, evidence = new { igtMs = 6 * 3600 * 1000L } })).Json();
        Assert.False(slower.GetProperty("newBestTime").GetBoolean());

        var unranked = await host.CreateRoomAsync(id, [], ranked: false);
        Assert.Equal("unranked_room", await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "story_plugs", context = new { roomCode = unranked } })).ProblemCode());

        var mine = await (await client.GetAsync("/api/v1/achievements")).Json();
        var briefing = mine.EnumerateArray().Single(a => a.GetProperty("code").GetString() == "story_briefing");
        Assert.Equal(2, briefing.GetProperty("unlocks").GetArrayLength());

        await using var c = await host.SqlAsync();
        Assert.Equal(5L * 3600 * 1000, await c.ExecuteScalarAsync<long>(
            "SELECT igt_ms FROM speedrun_times WHERE account_id = @id AND board = 'story_normal' AND ruleset = 'standard'", new { id }));
        Assert.Equal(2, await c.ExecuteScalarAsync<int>(
            "SELECT players FROM achievement_unlocks u JOIN achievements a ON a.id = u.achievement_id WHERE u.account_id = @id AND a.code = 'clear_normal'", new { id }));
    }

    [DbFact]
    public async Task Impossible_speedrun_times_are_not_kept()
    {
        await using var host = await TestHost.StartAsync(new() { ["Play:SpeedrunFloorSeconds:story_hard"] = "3600" });
        var (token, _, id) = await host.RegisterAsync("Glitcher");
        var client = host.Client(token);
        var room = await host.CreateRoomAsync(id, []);

        // 1 ms: the clear counts but waits for review, no best time, an audit row.
        var fake = await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "clear_normal", context = new { roomCode = room }, evidence = new { igtMs = 1L } })).Json();
        Assert.True(fake.GetProperty("newlyUnlocked").GetBoolean());
        Assert.False(fake.GetProperty("newBestTime").GetBoolean());
        Assert.True(fake.GetProperty("timeRejected").GetBoolean());
        Assert.Equal("requirement_not_met", await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "speed_3h", context = new { roomCode = room }, evidence = new { igtMs = 1L } })).ProblemCode());

        // A plausible time afterwards is kept (the unlock stays as it is).
        var real = await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "clear_normal", context = new { roomCode = room }, evidence = new { igtMs = 2L * 3600 * 1000 } })).Json();
        Assert.True(real.GetProperty("newBestTime").GetBoolean());
        Assert.False(real.GetProperty("timeRejected").GetBoolean());
        var speed = await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "speed_3h", context = new { roomCode = room }, evidence = new { igtMs = 2L * 3600 * 1000 } });
        Assert.Equal(HttpStatusCode.OK, speed.StatusCode);

        // A per-board override: 50 minutes is below Hard's configured hour.
        var hard = await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "clear_hard", context = new { roomCode = room }, evidence = new { igtMs = 50L * 60 * 1000 } })).Json();
        Assert.True(hard.GetProperty("timeRejected").GetBoolean());

        await using var c = await host.SqlAsync();
        Assert.Equal(2L * 3600 * 1000, await c.ExecuteScalarAsync<long>(
            "SELECT igt_ms FROM speedrun_times WHERE account_id = @id AND board = 'story_normal'", new { id }));
        Assert.Equal(0, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM speedrun_times WHERE account_id = @id AND board = 'story_hard'", new { id }));
        Assert.Equal(2, await c.ExecuteScalarAsync<int>(
            "SELECT COUNT(*) FROM achievement_unlocks u JOIN achievements a ON a.id = u.achievement_id " +
            "WHERE u.account_id = @id AND a.code IN ('clear_normal', 'clear_hard') AND u.flagged = 1", new { id }));
        Assert.Equal(2, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM audit_log WHERE event = 'speedrun_implausible' AND account_id = @id", new { id }));
    }

    [DbFact]
    public async Task Stats_drive_progress_achievements()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, id) = await host.RegisterAsync("Grinder");
        var client = host.Client(token);
        var room = await host.CreateRoomAsync(id, []);

        Assert.Equal("unknown_stat", await (await client.PostAsJsonAsync("/api/v1/stats",
            new { deltas = new Dictionary<string, long> { ["gold"] = 1 }, context = new { roomCode = room } })).ProblemCode());
        Assert.Equal("negative_stat", await (await client.PostAsJsonAsync("/api/v1/stats",
            new { deltas = new Dictionary<string, long> { ["enemies_defeated"] = -1 }, context = new { roomCode = room } })).ProblemCode());

        // Right after registration nothing is saved up yet.
        Assert.Equal("stats_implausible", await (await client.PostAsJsonAsync("/api/v1/stats",
            new { deltas = new Dictionary<string, long> { ["enemies_defeated"] = 120 }, context = new { roomCode = room } })).ProblemCode());

        host.Clock.Advance(TimeSpan.FromMinutes(15));
        var first = await (await client.PostAsJsonAsync("/api/v1/stats",
            new { deltas = new Dictionary<string, long> { ["enemies_defeated"] = 120, ["play_seconds"] = 900 }, context = new { roomCode = room } })).Json();
        Assert.Equal(120, first.GetProperty("totals").GetProperty("enemies_defeated").GetInt64());
        Assert.Equal(["kills_100"], first.GetProperty("unlocked").EnumerateArray().Select(e => e.GetString()));

        // 15 minutes at 400 a minute were saved up: 5880 are left, not one more.
        var implausible = await client.PostAsJsonAsync("/api/v1/stats",
            new { deltas = new Dictionary<string, long> { ["enemies_defeated"] = 5881 }, context = new { roomCode = room } });
        Assert.Equal("stats_implausible", await implausible.ProblemCode());

        // Two batches at the same time cannot both spend the same budget.
        var parallel = await Task.WhenAll(Enumerable.Range(0, 2).Select(_ => client.PostAsJsonAsync("/api/v1/stats",
            new { deltas = new Dictionary<string, long> { ["enemies_defeated"] = 3000 }, context = new { roomCode = room } })));
        Assert.Equal(1, parallel.Count(r => r.StatusCode == HttpStatusCode.OK));

        // The review's case: a fresh account sending 400 every 1.1 seconds only gets what 1.1 seconds allow.
        var (rushToken, _, rushId) = await host.RegisterAsync("Rusher");
        var rushRoom = await host.CreateRoomAsync(rushId, []);
        var rusher = host.Client(rushToken);
        var accepted = 0;
        for (var i = 0; i < 10; ++i)
        {
            host.Clock.Advance(TimeSpan.FromSeconds(1.1));
            var batch = await rusher.PostAsJsonAsync("/api/v1/stats",
                new { deltas = new Dictionary<string, long> { ["enemies_defeated"] = 400 }, context = new { roomCode = rushRoom } });
            if (batch.StatusCode == HttpStatusCode.OK) ++accepted;
        }
        Assert.Equal(0, accepted); // 11 seconds allow about 73

        // At the game's pace (400 a minute) every batch is fine.
        for (var i = 0; i < 3; ++i)
        {
            host.Clock.Advance(TimeSpan.FromMinutes(1));
            var paced = await client.PostAsJsonAsync("/api/v1/stats",
                new { deltas = new Dictionary<string, long> { ["enemies_defeated"] = 400, ["play_seconds"] = 60 }, context = new { roomCode = room } });
            Assert.Equal(HttpStatusCode.OK, paced.StatusCode);
        }

        var list = await (await client.GetAsync("/api/v1/achievements")).Json();
        var kills10000 = list.EnumerateArray().Single(a => a.GetProperty("code").GetString() == "kills_10000");
        Assert.Equal(120 + 3000 + 1200, kills10000.GetProperty("progress").GetInt64());
        Assert.Empty(kills10000.GetProperty("unlocks").EnumerateArray());
    }

    /// <summary>Stat unlocks go to the room's ruleset (a randomizer room's into the randomizer list).</summary>
    [DbFact]
    public async Task Stat_unlocks_take_the_rooms_ruleset()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, id) = await host.RegisterAsync("Randomer");
        var client = host.Client(token);
        var room = await host.CreateRoomAsync(id, [], randomizer: true);
        host.Clock.Advance(TimeSpan.FromMinutes(5));
        var batch = await (await client.PostAsJsonAsync("/api/v1/stats",
            new { deltas = new Dictionary<string, long> { ["enemies_defeated"] = 150 }, context = new { roomCode = room } })).Json();
        Assert.Equal(["kills_100"], batch.GetProperty("unlocked").EnumerateArray().Select(e => e.GetString()));
        await using var c = await host.SqlAsync();
        Assert.Equal(["randomizer"], (await c.QueryAsync<string>(
            "SELECT u.ruleset FROM achievement_unlocks u JOIN achievements a ON a.id = u.achievement_id WHERE u.account_id = @id AND a.code = 'kills_100'", new { id })).ToList());
    }
}
