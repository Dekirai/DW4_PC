using System.Net;
using System.Net.Http.Json;
using Dapper;
using DW4Online.Domain.Story;
using Microsoft.Extensions.DependencyInjection;

namespace DW4Online.Integration.Tests;

/// <summary>
/// Achievements in the world (SEAMLESS.md 19.8.9, round I1a): online play is proven by the relay's play context (here the
/// stand-in <see cref="DW4Online.Tests.FakeWorldRelay"/>: in the world within the last minute, the instance's history id,
/// its players, whether the reporter hosts it, its randomizer and data hash), never by the client's report; a story clear
/// needs the reporter's own story to hold its flag; new unlocks and times record the instance's history id.
/// </summary>
public class AchievementTests
{
    private const int P = TestHost.Protocol;

    /// <summary>Puts the clear flags of the given difficulties into the account's own online story (as the relay's queue would).</summary>
    private static async Task ClearedAsync(TestHost host, long account, params int[] difficulties)
    {
        var story = new RoomStoryData { Revision = 1 };
        foreach (var d in difficulties) story.Flags[RoomStoryKeeper.FirstClearFlag + d] = 1;
        await host.App.Services.GetRequiredService<OnlineStoryService>().SaveAsync(account, story);
    }

    [DbFact]
    public async Task Public_list_masks_hidden_achievements()
    {
        await using var host = await TestHost.StartAsync();
        var list = await (await host.Client().GetAsync("/api/v1/achievements")).Json();
        Assert.Equal(72, list.GetArrayLength());
        var lucemon = list.EnumerateArray().Single(a => a.GetProperty("code").GetString() == "story_lucemon");
        Assert.Equal("Hidden achievement", lucemon.GetProperty("name").GetString());
        var first = list[0];
        Assert.Equal("Tower Toppler", first.GetProperty("name").GetString());
    }

    [DbFact]
    public async Task Unlocks_follow_the_ranked_rules_and_the_worlds_play_context()
    {
        await using var host = await TestHost.StartAsync(new() { ["Play:ClearStoryWaitSeconds"] = "0" });
        var (token, _, id) = await host.RegisterAsync("Hunter");
        var client = host.Client(token);

        // A report without a protocol (or an old one) is refused by the version gate's floor in code (49).
        Assert.Equal("client_outdated", await (await client.PostAsJsonAsync("/api/v1/achievements/unlock", new { code = "story_apocalymon" })).ProblemCode());
        Assert.Equal("client_outdated", await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "story_apocalymon", context = new { protocol = 48 } })).ProblemCode());
        // Not in the world: nothing counts (Play:RequireWorld).
        var outside = await client.PostAsJsonAsync("/api/v1/achievements/unlock", new { code = "story_apocalymon", context = new { protocol = P } });
        Assert.Equal(HttpStatusCode.Forbidden, outside.StatusCode);
        Assert.Equal("world_required", await outside.ProblemCode());

        host.InWorld(id, wid: 7, host: true, players: 2, historyId: 70);
        var ok = await client.PostAsJsonAsync("/api/v1/achievements/unlock", new { code = "story_apocalymon", context = new { protocol = P, players = 2 } });
        Assert.Equal(HttpStatusCode.OK, ok.StatusCode);
        Assert.True((await ok.Json()).GetProperty("newlyUnlocked").GetBoolean());
        var repeat = await (await client.PostAsJsonAsync("/api/v1/achievements/unlock", new { code = "story_apocalymon", context = new { protocol = P } })).Json();
        Assert.False(repeat.GetProperty("newlyUnlocked").GetBoolean());

        // The client's randomizer flag counts for nothing: the instance's settings decide the ruleset.
        var claimed = await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "story_apocalymon", context = new { protocol = P, randomizer = true } })).Json();
        Assert.Equal("standard", claimed.GetProperty("ruleset").GetString());
        host.InWorld(id, wid: 8, host: true, players: 2, randomizer: true, historyId: 80);
        var randomizer = await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "story_apocalymon", context = new { protocol = P } })).Json();
        Assert.Equal("randomizer", randomizer.GetProperty("ruleset").GetString());
        Assert.True(randomizer.GetProperty("newlyUnlocked").GetBoolean());
        host.InWorld(id, wid: 7, host: true, players: 2, historyId: 70);

        Assert.Equal("unranked_save_state", await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "story_belphemon", context = new { protocol = P, saveStateLoaded = true } })).ProblemCode());
        Assert.Equal("server_tracked", await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "lv_999", context = new { protocol = P } })).ProblemCode());
        Assert.Equal("unknown_achievement", await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "nope", context = new { protocol = P } })).ProblemCode());
        Assert.Equal("requirement_not_met", await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "speed_fast", context = new { protocol = P }, evidence = new { igtMs = 5 * 3600 * 1000L } })).ProblemCode());
        // Four players claimed, but the relay saw two in the instance: the count is the relay's.
        Assert.Equal("requirement_not_met", await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "online_host", context = new { protocol = P, players = 4, instancePlayers = 4 } })).ProblemCode());

        // A clear needs the own story to hold its flag (no wait here: Play:ClearStoryWaitSeconds 0).
        Assert.Equal("requirement_not_met", await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "clear_normal", context = new { protocol = P, players = 2 }, evidence = new { igtMs = 5 * 3600 * 1000L } })).ProblemCode());
        await ClearedAsync(host, id, 0);
        var clear = await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "clear_normal", context = new { protocol = P, players = 2 }, evidence = new { igtMs = 5 * 3600 * 1000L } })).Json();
        Assert.True(clear.GetProperty("newBestTime").GetBoolean());
        var slower = await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "clear_normal", context = new { protocol = P }, evidence = new { igtMs = 6 * 3600 * 1000L } })).Json();
        Assert.False(slower.GetProperty("newBestTime").GetBoolean());

        var mine = await (await client.GetAsync("/api/v1/achievements")).Json();
        var briefing = mine.EnumerateArray().Single(a => a.GetProperty("code").GetString() == "story_apocalymon");
        Assert.Equal(2, briefing.GetProperty("unlocks").GetArrayLength());

        await using var c = await host.SqlAsync();
        Assert.Equal((5L * 3600 * 1000, 70L), await c.QuerySingleAsync<(long, long)>(
            "SELECT igt_ms, CAST(instance_id AS SIGNED) FROM speedrun_times WHERE account_id = @id AND board = 'story_normal' AND ruleset = 'standard'", new { id }));
        Assert.Equal((2, 70L), await c.QuerySingleAsync<(int, long)>(
            "SELECT players, CAST(instance_id AS SIGNED) FROM achievement_unlocks u JOIN achievements a ON a.id = u.achievement_id " +
            "WHERE u.account_id = @id AND a.code = 'clear_normal'", new { id }));
        Assert.Equal(0, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM achievement_unlocks WHERE account_id = @id AND room_id IS NOT NULL", new { id }));
    }

    /// <summary>World:RankedDataHashes: an instance with other game data counts for nothing.</summary>
    [DbFact]
    public async Task Unranked_game_data_counts_for_nothing()
    {
        await using var host = await TestHost.StartAsync(new() { ["World:RankedDataHashes:0"] = "0x2" });
        var (token, _, id) = await host.RegisterAsync("Modder");
        var client = host.Client(token);
        host.InWorld(id, dataHash: 1);
        Assert.Equal("unranked_data", await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "story_daemon", context = new { protocol = P } })).ProblemCode());
        host.InWorld(id, dataHash: 2);
        Assert.Equal(HttpStatusCode.OK, (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "story_daemon", context = new { protocol = P } })).StatusCode);
    }

    /// <summary>The room era's name of the switch is read for one release: Play:RequireRoom false lets reports from outside count.</summary>
    [DbFact]
    public async Task Play_RequireRoom_is_read_as_RequireWorld()
    {
        var overrides = new Dictionary<string, string?> { ["Play:RequireWorld"] = null, ["Play:RequireRoom"] = "false" };
        await using var host = await TestHost.StartAsync(overrides);
        var (token, _, _) = await host.RegisterAsync("Offline");
        Assert.Equal(HttpStatusCode.OK, (await host.Client(token).PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "story_daemon", context = new { protocol = P } })).StatusCode);
    }

    [DbFact]
    public async Task Impossible_speedrun_times_are_not_kept()
    {
        await using var host = await TestHost.StartAsync(new() { ["Play:SpeedrunFloorSeconds:story_hard"] = "3600", ["Play:ClearStoryWaitSeconds"] = "0" });
        var (token, _, id) = await host.RegisterAsync("Glitcher");
        var client = host.Client(token);
        host.InWorld(id);
        await ClearedAsync(host, id, 0, 1);

        // 1 ms: the clear counts but waits for review, no best time, an audit row.
        var fake = await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "clear_normal", context = new { protocol = P }, evidence = new { igtMs = 1L } })).Json();
        Assert.True(fake.GetProperty("newlyUnlocked").GetBoolean());
        Assert.False(fake.GetProperty("newBestTime").GetBoolean());
        Assert.True(fake.GetProperty("timeRejected").GetBoolean());
        Assert.Equal("requirement_not_met", await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "speed_fast", context = new { protocol = P }, evidence = new { igtMs = 1L } })).ProblemCode());

        // A plausible time afterwards is kept (the unlock stays as it is).
        var real = await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "clear_normal", context = new { protocol = P }, evidence = new { igtMs = 2L * 3600 * 1000 } })).Json();
        Assert.True(real.GetProperty("newBestTime").GetBoolean());
        Assert.False(real.GetProperty("timeRejected").GetBoolean());
        var speed = await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "speed_fast", context = new { protocol = P }, evidence = new { igtMs = 2L * 3600 * 1000 } });
        Assert.Equal(HttpStatusCode.OK, speed.StatusCode);

        // A per-board override: 50 minutes is below Hard's configured hour.
        var hard = await (await client.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "clear_hard", context = new { protocol = P }, evidence = new { igtMs = 50L * 60 * 1000 } })).Json();
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
        host.InWorld(id);
        var context = new { protocol = P };

        Assert.Equal("unknown_stat", await (await client.PostAsJsonAsync("/api/v1/stats",
            new { deltas = new Dictionary<string, long> { ["gold"] = 1 }, context })).ProblemCode());
        Assert.Equal("negative_stat", await (await client.PostAsJsonAsync("/api/v1/stats",
            new { deltas = new Dictionary<string, long> { ["enemies_defeated"] = -1 }, context })).ProblemCode());

        // Right after registration nothing is saved up yet.
        Assert.Equal("stats_implausible", await (await client.PostAsJsonAsync("/api/v1/stats",
            new { deltas = new Dictionary<string, long> { ["enemies_defeated"] = 120 }, context })).ProblemCode());

        host.Clock.Advance(TimeSpan.FromMinutes(15));
        var first = await (await client.PostAsJsonAsync("/api/v1/stats",
            new { deltas = new Dictionary<string, long> { ["enemies_defeated"] = 120, ["play_seconds"] = 900 }, context })).Json();
        Assert.Equal(120, first.GetProperty("totals").GetProperty("enemies_defeated").GetInt64());
        Assert.Equal(["kills_100"], first.GetProperty("unlocked").EnumerateArray().Select(e => e.GetString()));

        // 15 minutes at 400 a minute were saved up: 5880 are left, not one more.
        var implausible = await client.PostAsJsonAsync("/api/v1/stats",
            new { deltas = new Dictionary<string, long> { ["enemies_defeated"] = 5881 }, context });
        Assert.Equal("stats_implausible", await implausible.ProblemCode());

        // Two batches at the same time cannot both spend the same budget.
        var parallel = await Task.WhenAll(Enumerable.Range(0, 2).Select(_ => client.PostAsJsonAsync("/api/v1/stats",
            new { deltas = new Dictionary<string, long> { ["enemies_defeated"] = 3000 }, context })));
        Assert.Equal(1, parallel.Count(r => r.StatusCode == HttpStatusCode.OK));

        // The review's case: a fresh account sending 400 every 1.1 seconds only gets what 1.1 seconds allow.
        var (rushToken, _, rushId) = await host.RegisterAsync("Rusher");
        host.InWorld(rushId);
        var rusher = host.Client(rushToken);
        var accepted = 0;
        for (var i = 0; i < 10; ++i)
        {
            host.Clock.Advance(TimeSpan.FromSeconds(1.1));
            var batch = await rusher.PostAsJsonAsync("/api/v1/stats",
                new { deltas = new Dictionary<string, long> { ["enemies_defeated"] = 400 }, context });
            if (batch.StatusCode == HttpStatusCode.OK) ++accepted;
        }
        Assert.Equal(0, accepted); // 11 seconds allow about 73

        // At the game's pace (400 a minute) every batch is fine.
        for (var i = 0; i < 3; ++i)
        {
            host.Clock.Advance(TimeSpan.FromMinutes(1));
            var paced = await client.PostAsJsonAsync("/api/v1/stats",
                new { deltas = new Dictionary<string, long> { ["enemies_defeated"] = 400, ["play_seconds"] = 60 }, context });
            Assert.Equal(HttpStatusCode.OK, paced.StatusCode);
        }

        var list = await (await client.GetAsync("/api/v1/achievements")).Json();
        var kills10000 = list.EnumerateArray().Single(a => a.GetProperty("code").GetString() == "kills_10000");
        Assert.Equal(120 + 3000 + 1200, kills10000.GetProperty("progress").GetInt64());
        Assert.Empty(kills10000.GetProperty("unlocks").EnumerateArray());
    }

    /// <summary>Stat unlocks go to the instance's ruleset (a randomizer instance's into the randomizer list).</summary>
    [DbFact]
    public async Task Stat_unlocks_take_the_instances_ruleset()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, id) = await host.RegisterAsync("Randomer");
        var client = host.Client(token);
        host.InWorld(id, wid: 3, host: false, players: 2, randomizer: true);
        host.Clock.Advance(TimeSpan.FromMinutes(5));
        var batch = await (await client.PostAsJsonAsync("/api/v1/stats",
            new { deltas = new Dictionary<string, long> { ["enemies_defeated"] = 150 }, context = new { protocol = P } })).Json();
        Assert.Equal(["kills_100"], batch.GetProperty("unlocked").EnumerateArray().Select(e => e.GetString()));
        await using var c = await host.SqlAsync();
        Assert.Equal(["randomizer"], (await c.QueryAsync<string>(
            "SELECT u.ruleset FROM achievement_unlocks u JOIN achievements a ON a.id = u.achievement_id WHERE u.account_id = @id AND a.code = 'kills_100'", new { id })).ToList());
    }

    /// <summary>
    /// The catalogue is the game client's (data/online/achievements.json, SEAMLESS.md 11): its ids in its order, the retired
    /// codes of 0002 unknown. In the world (19.8.9): story challenges only for the instance's host; a guest's clear counts when
    /// his own story holds it (an in-step guest got it mirrored) and not when it does not (a not-in-step guest); Lone Tamer
    /// alone at home; online_host for a host with three players, online_join for a guest, online_raid with two; the couch
    /// goals read the reporter's stage group (players).
    /// </summary>
    [DbFact]
    public async Task The_catalogue_is_the_clients_and_the_host_gets_the_story_challenges()
    {
        await using var host = await TestHost.StartAsync(new() { ["Play:ClearStoryWaitSeconds"] = "0" });
        var list = await (await host.Client().GetAsync("/api/v1/achievements")).Json();
        using var json = System.Text.Json.JsonDocument.Parse(await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "data", "achievements.json")));
        var ids = json.RootElement.GetProperty("achievements").EnumerateArray().Select(a => a.GetProperty("id").GetString()).ToList();
        Assert.Equal(ids, list.EnumerateArray().Select(a => a.GetProperty("code").GetString()));

        var (ownerToken, _, owner) = await host.RegisterAsync("Leader");
        var (guestToken, _, guest) = await host.RegisterAsync("Follower");
        var (lateToken, _, late) = await host.RegisterAsync("Latecomer");
        host.InWorld(owner, wid: 5, host: true, players: 3);
        host.InWorld(guest, wid: 5, host: false, players: 3);
        host.InWorld(late, wid: 5, host: false, players: 3);
        await ClearedAsync(host, owner, 0);
        await ClearedAsync(host, guest, 0); // in step: the host's clear was mirrored into his story
        var ownerClient = host.Client(ownerToken);
        var guestClient = host.Client(guestToken);
        Assert.Equal("unknown_achievement", await (await ownerClient.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "story_briefing", context = new { protocol = P } })).ProblemCode());

        var evidence = new { igtMs = 3L * 3600 * 1000 };
        Assert.Equal("requirement_not_met", await (await guestClient.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "speed_fast", context = new { protocol = P, players = 2 }, evidence })).ProblemCode());
        Assert.Equal(HttpStatusCode.OK, (await guestClient.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "clear_normal", context = new { protocol = P, players = 2 } })).StatusCode);
        Assert.Equal("requirement_not_met", await (await host.Client(lateToken).PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "clear_normal", context = new { protocol = P, players = 2 } })).ProblemCode());
        Assert.Equal(HttpStatusCode.OK, (await ownerClient.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "speed_fast", context = new { protocol = P, players = 2 }, evidence })).StatusCode);

        // Room Host's successor: the host, three in the instance, alone in its own stage group.
        Assert.Equal(HttpStatusCode.OK, (await ownerClient.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "online_host", context = new { protocol = P, players = 1 } })).StatusCode);
        Assert.Equal("requirement_not_met", await (await guestClient.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "online_host", context = new { protocol = P, players = 1 } })).ProblemCode());
        Assert.Equal(HttpStatusCode.OK, (await guestClient.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "online_join", context = new { protocol = P, players = 1 } })).StatusCode);
        Assert.Equal("requirement_not_met", await (await ownerClient.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "online_join", context = new { protocol = P, players = 1 } })).ProblemCode());
        Assert.Equal(HttpStatusCode.OK, (await guestClient.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "online_raid", context = new { protocol = P, players = 1 } })).StatusCode);
        Assert.Equal("requirement_not_met", await (await guestClient.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "mp_fullhouse", context = new { protocol = P, players = 3 } })).ProblemCode());

        // Lone Tamer: alone at home (wid 0, the own story), not in an instance of three.
        Assert.Equal("requirement_not_met", await (await ownerClient.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "solo_veryhard", context = new { protocol = P, players = 1 } })).ProblemCode());
        host.InWorld(owner);
        Assert.Equal(HttpStatusCode.OK, (await ownerClient.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "solo_veryhard", context = new { protocol = P, players = 1 } })).StatusCode);
    }
}
