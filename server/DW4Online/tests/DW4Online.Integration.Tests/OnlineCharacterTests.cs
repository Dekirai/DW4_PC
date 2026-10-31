using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Dapper;
using DW4Online.Domain.Save;
using DW4Online.Tests;
using Microsoft.Extensions.DependencyInjection;

namespace DW4Online.Integration.Tests;

public class OnlineCharacterTests
{
    private static object Upload(SaveRecord record, long playSeconds, object? context = null) =>
        new { record = Convert.ToBase64String(RecordCodec.Encode(record)), playSeconds, context };

    [DbFact]
    public async Task First_saves_of_many_accounts_at_once_never_conflict()
    {
        // The FOR UPDATE reads of two accounts' first saves lock the same gap and InnoDB made one a deadlock
        // victim, which answered 409 although nobody else had saved that character (a client then stopped its
        // uploads, round S2). A deadlock victim runs again now.
        await using var host = await TestHost.StartAsync();
        var clients = new List<HttpClient>();
        for (var i = 0; i < 12; ++i) clients.Add(host.Client((await host.RegisterAsync("Racer" + i)).Item1));
        var created = await Task.WhenAll(clients.Select((client, i) =>
            client.PutJson("/api/v1/online-character", Upload(Samples.Fresh("R" + i), 600), create: true)));
        Assert.All(created, r => Assert.Equal(HttpStatusCode.Created, r.StatusCode));
        var updated = await Task.WhenAll(clients.Select((client, i) =>
            client.PutJson("/api/v1/online-character", Upload(Samples.Fresh("R" + i), 700), ifMatch: "\"1\"")));
        Assert.All(updated, r => Assert.Equal(HttpStatusCode.OK, r.StatusCode));
    }

    [DbFact]
    public async Task Create_update_conflict_and_history()
    {
        await using var host = await TestHost.StartAsync(new() { ["OnlineCharacter:HistoryRevisions"] = "2" });
        var (token, _, id) = await host.RegisterAsync("Tamer1");
        var client = host.Client(token);

        var none = await client.GetAsync("/api/v1/online-character");
        Assert.Equal(HttpStatusCode.NotFound, none.StatusCode);
        Assert.Equal("no_online_character", await none.ProblemCode());

        var character = Samples.Fresh("Kev");
        var noPrecondition = await client.PutJson("/api/v1/online-character", Upload(character, 600));
        Assert.Equal(HttpStatusCode.PreconditionRequired, noPrecondition.StatusCode);

        var created = await client.PutJson("/api/v1/online-character", Upload(character, 600), create: true);
        Assert.Equal(HttpStatusCode.Created, created.StatusCode);
        Assert.Equal("\"1\"", created.Headers.ETag?.Tag);
        var body = await created.Json();
        Assert.Equal(1, body.GetProperty("revision").GetInt32());
        Assert.Equal("Kev", body.GetProperty("summary").GetProperty("playerName").GetString());
        Assert.Equal(5, body.GetProperty("summary").GetProperty("level").GetInt32());

        var again = await client.PutJson("/api/v1/online-character", Upload(character, 600), create: true);
        Assert.Equal("character_exists", await again.ProblemCode());

        var stored = await (await client.GetAsync("/api/v1/online-character")).Json();
        // The story bytes are the account's own online story (none yet: zero), never the uploaded ones (SEAMLESS.md 8.4).
        Assert.Equal(DW4Online.Domain.Story.StoryRecord.Compose(RecordCodec.Encode(character), new DW4Online.Domain.Story.RoomStoryData()),
            Convert.FromBase64String(stored.GetProperty("record").GetString()!));
        Assert.Equal(600, stored.GetProperty("playSeconds").GetInt64());

        character.Level = 12;
        character.BaseLevel[0] = 12;
        character.Hp = character.MaxHp = 300;
        host.Clock.Advance(TimeSpan.FromHours(1));
        var room = await host.CreateRoomAsync(id, []);
        var stale = await client.PutJson("/api/v1/online-character", Upload(character, 3000), ifMatch: "\"7\"");
        Assert.Equal(HttpStatusCode.Conflict, stale.StatusCode);
        Assert.Equal(1, (await stale.Json()).GetProperty("currentRevision").GetInt32());

        var updated = await client.PutJson("/api/v1/online-character", Upload(character, 3000, new { roomCode = room, players = 1 }), ifMatch: "\"1\"");
        Assert.Equal(HttpStatusCode.OK, updated.StatusCode);
        var result = await updated.Json();
        Assert.Equal(2, result.GetProperty("revision").GetInt32());
        Assert.Contains("lv_10", result.GetProperty("unlocked").EnumerateArray().Select(e => e.GetString()));
        Assert.Empty(result.GetProperty("warnings").EnumerateArray());

        // The summary columns come from the server's decode.
        await using (var c = await host.SqlAsync())
        {
            var row = await c.QuerySingleAsync("SELECT level, highest_level, player_name, digimon, flagged FROM online_characters WHERE account_id = @id", new { id });
            Assert.Equal(12, (int)row.level);
            Assert.Equal("Kev", (string)row.player_name);
            Assert.Equal("p_agumon", (string)row.digimon);
            Assert.False((bool)row.flagged);
        }

        for (var rev = 2; rev < 5; ++rev)
        {
            host.Clock.Advance(TimeSpan.FromMinutes(10));
            var next = await client.PutJson("/api/v1/online-character", Upload(character, 3000 + rev * 600), ifMatch: $"\"{rev}\"");
            Assert.Equal(HttpStatusCode.OK, next.StatusCode);
        }
        var history = await (await client.GetAsync("/api/v1/online-character/history")).Json();
        Assert.Equal([5, 4, 3], history.EnumerateArray().Select(e => e.GetProperty("revision").GetInt32()));
    }

    [DbFact]
    public async Task Invalid_unranked_and_foreign_records_are_rejected()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, _) = await host.RegisterAsync("Cheater");
        var client = host.Client(token);

        var broken = Samples.Fresh();
        broken.Level = 0;
        broken.Bits = 50_000_000;
        var invalid = await client.PutJson("/api/v1/online-character", Upload(broken, 60), create: true);
        Assert.Equal(HttpStatusCode.UnprocessableEntity, invalid.StatusCode);
        var problem = await invalid.Json();
        Assert.Equal("invalid_record", problem.GetProperty("code").GetString());
        var issues = problem.GetProperty("issues").EnumerateArray().Select(i => i.GetProperty("code").GetString()).ToList();
        Assert.Contains("level_range", issues);
        Assert.Contains("bits_range", issues);

        var notBase64 = await client.PutJson("/api/v1/online-character", new { record = "%%%", playSeconds = 1 }, create: true);
        Assert.Equal("invalid_record", await notBase64.ProblemCode());
        var shortRecord = await client.PutJson("/api/v1/online-character", new { record = Convert.ToBase64String(new byte[100]), playSeconds = 1 }, create: true);
        Assert.Equal("invalid_record", await shortRecord.ProblemCode());

        var cheats = await client.PutJson("/api/v1/online-character", Upload(Samples.Fresh(), 60, new { cheats = true }), create: true);
        Assert.Equal("unranked_cheats", await cheats.ProblemCode());

        Assert.Equal(HttpStatusCode.Created, (await client.PutJson("/api/v1/online-character", Upload(Samples.Fresh(), 60), create: true)).StatusCode);
        var other = Samples.Fresh();
        other.Unique = 0x0BADF00D;
        var foreign = await client.PutJson("/api/v1/online-character", Upload(other, 120), ifMatch: "1");
        Assert.Equal(HttpStatusCode.UnprocessableEntity, foreign.StatusCode);
        Assert.Contains("unique_changed", (await foreign.Json()).GetProperty("issues").EnumerateArray().Select(i => i.GetProperty("code").GetString()));
    }

    [DbFact]
    public async Task Implausible_uploads_are_flagged_not_rejected()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, id) = await host.RegisterAsync("Speedy");
        var client = host.Client(token);
        var maxed = Samples.Fresh();
        maxed.Level = 400;
        maxed.BaseLevel[0] = 400;
        maxed.Hp = maxed.MaxHp = 9000;
        var room = await host.CreateRoomAsync(id, []);
        var created = await client.PutJson("/api/v1/online-character", Upload(maxed, 120, new { roomCode = room, players = 1 }), create: true);
        Assert.Equal(HttpStatusCode.Created, created.StatusCode);
        Assert.Contains("level_rate", (await created.Json()).GetProperty("warnings").EnumerateArray().Select(w => w.GetProperty("code").GetString()));
        await using var c = await host.SqlAsync();
        Assert.True(await c.ExecuteScalarAsync<bool>("SELECT flagged FROM online_characters WHERE account_id = @id", new { id }));
        Assert.Equal(1, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM audit_log WHERE account_id = @id AND event = 'character_flagged'", new { id }));
        Assert.True(await c.ExecuteScalarAsync<bool>(
            "SELECT u.flagged FROM achievement_unlocks u JOIN achievements a ON a.id = u.achievement_id WHERE u.account_id = @id AND a.code = 'lv_200'", new { id }));
    }

    [DbFact]
    public async Task Reset_and_admin_restore()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, _) = await host.RegisterAsync("Restarter");
        var client = host.Client(token);
        var first = Samples.Fresh();
        Assert.Equal(HttpStatusCode.Created, (await client.PutJson("/api/v1/online-character", Upload(first, 60), create: true)).StatusCode);
        first.Bits = 5000;
        Assert.Equal(HttpStatusCode.OK, (await client.PutJson("/api/v1/online-character", Upload(first, 120), ifMatch: "1")).StatusCode);

        var wrong = await client.PostAsJsonAsync("/api/v1/online-character/reset", new { currentPassword = "wrong password" });
        Assert.Equal("wrong_password", await wrong.ProblemCode());
        Assert.Equal(HttpStatusCode.NoContent,
            (await client.PostAsJsonAsync("/api/v1/online-character/reset", new { currentPassword = "a decent password" })).StatusCode);
        Assert.Equal(HttpStatusCode.NotFound, (await client.GetAsync("/api/v1/online-character")).StatusCode);

        var second = Samples.Fresh("New");
        second.Unique = 0x22222222;
        var recreated = await client.PutJson("/api/v1/online-character", Upload(second, 30), create: true);
        Assert.Equal(3, (await recreated.Json()).GetProperty("revision").GetInt32()); // revisions keep counting

        var (adminToken, _, _) = await host.RegisterAsync("Admin1");
        await using (var c = await host.SqlAsync())
            await c.ExecuteAsync("UPDATE accounts SET role = 'moderator' WHERE username_norm = 'admin1'");
        var restored = await host.Client(adminToken).PostAsync("/api/v1/admin/accounts/Restarter/online-character/restore/2", null);
        Assert.Equal(HttpStatusCode.OK, restored.StatusCode);
        Assert.Equal(4, (await restored.Json()).GetProperty("revision").GetInt32());
        var now = await (await client.GetAsync("/api/v1/online-character")).Json();
        Assert.Equal(5000, now.GetProperty("summary").GetProperty("bits").GetInt32());
        Assert.Equal("Kevin", now.GetProperty("summary").GetProperty("playerName").GetString());
    }

    /// <summary>What the privacy page says: a reset character's revisions stay 30 days, then they go; the flag is noted.</summary>
    [DbFact]
    public async Task A_reset_characters_history_goes_after_30_days()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, id) = await host.RegisterAsync("Wiper");
        var client = host.Client(token);
        host.Clock.Advance(TimeSpan.FromMinutes(2));
        var maxed = Samples.Fresh();
        maxed.Level = 300;
        maxed.BaseLevel[0] = 300;
        maxed.Hp = maxed.MaxHp = 9000;
        Assert.Equal(HttpStatusCode.Created, (await client.PutJson("/api/v1/online-character", Upload(maxed, 60), create: true)).StatusCode);
        host.Clock.Advance(TimeSpan.FromMinutes(1));
        Assert.Equal(HttpStatusCode.NoContent,
            (await client.PostAsJsonAsync("/api/v1/online-character/reset", new { currentPassword = "a decent password" })).StatusCode);
        host.Clock.Advance(TimeSpan.FromMinutes(1));
        var next = Samples.Fresh("Two");
        next.Unique = 0x33333333;
        Assert.Equal(HttpStatusCode.Created, (await client.PutJson("/api/v1/online-character", Upload(next, 30), create: true)).StatusCode);

        var maintenance = host.App.Services.GetRequiredService<DW4Online.Data.Repositories.MaintenanceRepository>();
        await using var c = await host.SqlAsync();
        Assert.Equal(2, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM online_character_history WHERE account_id = @id", new { id }));
        var detail = await c.ExecuteScalarAsync<string>("SELECT detail FROM audit_log WHERE event = 'character_reset' AND account_id = @id", new { id });
        Assert.Contains("\"flagged\":true", detail);
        Assert.False(await c.ExecuteScalarAsync<bool>("SELECT flagged FROM online_characters WHERE account_id = @id", new { id }));

        // 29 days later the old revision is still there for the staff; after 30 it is gone, the new character's stays.
        host.Clock.Advance(TimeSpan.FromDays(29));
        Assert.Equal(0, (await maintenance.PruneAsync(host.Clock.GetUtcNow().UtcDateTime, 180, 30)).ResetRevisions);
        host.Clock.Advance(TimeSpan.FromDays(2));
        Assert.Equal(1, (await maintenance.PruneAsync(host.Clock.GetUtcNow().UtcDateTime, 180, 30)).ResetRevisions);
        var left = (await c.QueryAsync<int>("SELECT revision FROM online_character_history WHERE account_id = @id", new { id })).ToList();
        Assert.Equal([2], left);
    }

    /// <summary>
    /// Two resets within 30 days: each character's revisions go 30 days after its own reset (the second reset does not
    /// keep the first character's revisions longer), and a staff restore after a reset starts a live character again.
    /// </summary>
    [DbFact]
    public async Task Each_reset_characters_history_goes_30_days_after_its_own_reset()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, id) = await host.RegisterAsync("Wiper2");
        var client = host.Client(token);
        async Task Upload(string name, uint unique)
        {
            var record = Samples.Fresh(name);
            record.Unique = unique;
            Assert.Equal(HttpStatusCode.Created, (await client.PutJson("/api/v1/online-character", OnlineCharacterTests.Upload(record, 30), create: true)).StatusCode);
        }
        async Task Reset() => Assert.Equal(HttpStatusCode.NoContent,
            (await client.PostAsJsonAsync("/api/v1/online-character/reset", new { currentPassword = "a decent password" })).StatusCode);
        var maintenance = host.App.Services.GetRequiredService<DW4Online.Data.Repositories.MaintenanceRepository>();
        await using var c = await host.SqlAsync();
        async Task<List<int>> Kept() =>
            (await c.QueryAsync<int>("SELECT revision FROM online_character_history WHERE account_id = @id ORDER BY revision", new { id })).ToList();
        async Task<int> Prune() => (await maintenance.PruneAsync(host.Clock.GetUtcNow().UtcDateTime, 180, 30)).ResetRevisions;

        host.Clock.Advance(TimeSpan.FromHours(1));
        await Upload("One", 0x11111111);             // revision 1
        await Reset();                               // day 0: character one ends
        host.Clock.Advance(TimeSpan.FromDays(20));
        await Upload("Two", 0x22222222);             // revision 2
        await Reset();                               // day 20: character two ends
        host.Clock.Advance(TimeSpan.FromDays(1));
        await Upload("Three", 0x33333333);           // revision 3, live

        host.Clock.Advance(TimeSpan.FromDays(10));   // day 31: one goes, two (11 days ago) stays
        Assert.Equal(1, await Prune());
        Assert.Equal([2, 3], await Kept());
        host.Clock.Advance(TimeSpan.FromDays(19));   // day 50: two is 30 days old
        Assert.Equal(0, await Prune());
        host.Clock.Advance(TimeSpan.FromDays(1));    // day 51: two goes, the live character stays
        Assert.Equal(1, await Prune());
        Assert.Equal([3], await Kept());

        // A staff reset and its undo (a restore of the reset character's revision) make the character live again: its
        // revisions are no longer deleted 30 days after that reset (the player's token ran out after 30 idle days, so
        // staff act here).
        var staff = host.App.Services.GetRequiredService<DW4Online.Domain.Services.OnlineCharacterService>();
        Assert.True(await staff.ResetAsync(id, id, null));
        host.Clock.Advance(TimeSpan.FromDays(1));
        Assert.True((await staff.RestoreAsync(id, 3, id, null)).Ok);
        host.Clock.Advance(TimeSpan.FromDays(31));
        Assert.Equal(0, await Prune());
        Assert.Equal([3, 4], await Kept());           // the restored character's revision 3 and its restored copy
        Assert.Equal(0, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM online_character_history WHERE account_id = @id AND ended_at IS NOT NULL", new { id }));
    }

    /// <summary>
    /// The review's restore probe: an honest level 60 character with 100 hours on a 200-day-old account, reset by
    /// mistake and restored by staff, keeps its play time, and its next upload is not flagged; a flagged character
    /// (level 90 with 200 hours on a fresh account) stays flagged through reset and restore. Undoing a reset over a
    /// newer character ends that one like a reset.
    /// </summary>
    [DbFact]
    public async Task A_restore_after_a_reset_keeps_the_revisions_play_time_and_flag()
    {
        await using var host = await TestHost.StartAsync();
        var staff = host.App.Services.GetRequiredService<DW4Online.Domain.Services.OnlineCharacterService>();
        await using var c = await host.SqlAsync();
        const long hundredHours = 100L * 3600;

        var (_, _, id) = await host.RegisterAsync("Honest");
        host.Clock.Advance(TimeSpan.FromDays(200));
        var client = host.Client((await host.LoginAsync("Honest")).Token); // the registration's token ran out long ago
        var honest = Samples.Fresh("Hon");
        honest.Level = 60;
        honest.BaseLevel[0] = 60;
        Assert.Equal(HttpStatusCode.Created, (await client.PutJson("/api/v1/online-character", Upload(honest, hundredHours - 600), create: true)).StatusCode);
        host.Clock.Advance(TimeSpan.FromMinutes(10));
        var second = await client.PutJson("/api/v1/online-character", Upload(honest, hundredHours), ifMatch: "1");
        Assert.Empty((await second.Json()).GetProperty("warnings").EnumerateArray());
        Assert.True(await staff.ResetAsync(id, id, null));
        var resetAt = await c.ExecuteScalarAsync<DateTime>("SELECT online_reset_at FROM accounts WHERE id = @id", new { id });
        host.Clock.Advance(TimeSpan.FromHours(2));
        Assert.Equal(3, (await staff.RestoreAsync(id, 2, id, null)).Value);

        // Before the fix: play time 0, so the next upload was "too fast" and flagged the character for good.
        var restored = await (await client.GetAsync("/api/v1/online-character")).Json();
        Assert.Equal(hundredHours, restored.GetProperty("playSeconds").GetInt64());
        Assert.False(restored.GetProperty("flagged").GetBoolean());
        Assert.Equal(0, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM online_character_history WHERE account_id = @id AND ended_at IS NOT NULL", new { id }));
        Assert.Null(await c.ExecuteScalarAsync<DateTime?>("SELECT online_reset_at FROM accounts WHERE id = @id", new { id })); // no earlier reset
        host.Clock.Advance(TimeSpan.FromMinutes(10));
        var next = await client.PutJson("/api/v1/online-character", Upload(honest, hundredHours + 600), ifMatch: "3");
        Assert.Equal(HttpStatusCode.OK, next.StatusCode);
        Assert.Empty((await next.Json()).GetProperty("warnings").EnumerateArray());
        Assert.False(await c.ExecuteScalarAsync<bool>("SELECT flagged FROM online_characters WHERE account_id = @id", new { id }));

        // Undoing a reset after a new character started: the new one ends now (kept like a reset character's revisions).
        Assert.True(await staff.ResetAsync(id, id, null));
        host.Clock.Advance(TimeSpan.FromMinutes(5));
        var fresh = Samples.Fresh("New");
        fresh.Unique = 0x22222222;
        Assert.Equal(5, (await (await client.PutJson("/api/v1/online-character", Upload(fresh, 60), create: true)).Json()).GetProperty("revision").GetInt32());
        host.Clock.Advance(TimeSpan.FromMinutes(5));
        Assert.Equal(6, (await staff.RestoreAsync(id, 4, id, null)).Value);
        var ended = (await c.QueryAsync<int>("SELECT revision FROM online_character_history WHERE account_id = @id AND ended_at IS NOT NULL", new { id })).ToList();
        Assert.Equal([5], ended);
        Assert.Equal(resetAt, await c.ExecuteScalarAsync<DateTime?>("SELECT online_reset_at FROM accounts WHERE id = @id", new { id })); // the reset before
        Assert.Equal(hundredHours + 600, (await (await client.GetAsync("/api/v1/online-character")).Json()).GetProperty("playSeconds").GetInt64());
        var audit = await c.ExecuteScalarAsync<string>("SELECT detail FROM audit_log WHERE event = 'character_restore' AND account_id = @id ORDER BY id DESC LIMIT 1", new { id });
        Assert.Contains("\"replaced\":5", audit);

        // A flagged character stays flagged through a reset and a restore (before the fix it came back unflagged).
        var (token2, _, id2) = await host.RegisterAsync("Cheater");
        var maxed = Samples.Fresh("Max");
        maxed.Unique = 0x33333333;
        maxed.Level = 90;
        maxed.BaseLevel[0] = 90;
        var flagged = await host.Client(token2).PutJson("/api/v1/online-character", Upload(maxed, 200L * 3600), create: true);
        Assert.Contains("play_time_too_fast", (await flagged.Json()).GetProperty("warnings").EnumerateArray().Select(w => w.GetProperty("code").GetString()));
        Assert.True(await staff.ResetAsync(id2, id2, null));
        Assert.True((await staff.RestoreAsync(id2, 1, id2, null)).Ok);
        Assert.True(await c.ExecuteScalarAsync<bool>("SELECT flagged FROM online_characters WHERE account_id = @id2", new { id2 }));
        Assert.True((await (await host.Client(token2).GetAsync("/api/v1/online-character")).Json()).GetProperty("flagged").GetBoolean());
    }

    /// <summary>The review's case: a fresh account uploads a maxed record claiming 400 hours of play.</summary>
    [DbFact]
    public async Task A_maxed_record_on_a_fresh_account_is_flagged_and_unlocks_nothing_outside_a_room()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, id) = await host.RegisterAsync("Hexer");
        var client = host.Client(token);
        var maxed = Samples.Fresh();
        maxed.Level = 999;
        for (var s = 0; s < maxed.BaseLevel.Length; ++s)
        {
            maxed.BaseLevel[s] = 999;
            maxed.BaseIsUse[s] = 1;
        }
        maxed.Bits = 9_999_999;
        var created = await client.PutJson("/api/v1/online-character", Upload(maxed, 400L * 3600), create: true);
        Assert.Equal(HttpStatusCode.Created, created.StatusCode);
        var body = await created.Json();
        var warnings = body.GetProperty("warnings").EnumerateArray().Select(w => w.GetProperty("code").GetString()).ToList();
        Assert.Contains("play_time_too_fast", warnings); // 400 h on an account a few seconds old
        Assert.Contains("level_rate", warnings);
        Assert.Empty(body.GetProperty("unlocked").EnumerateArray()); // no room: nothing counts
        await using var c = await host.SqlAsync();
        Assert.True(await c.ExecuteScalarAsync<bool>("SELECT flagged FROM online_characters WHERE account_id = @id", new { id }));
        Assert.Equal(0, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM achievement_unlocks WHERE account_id = @id", new { id }));

        // In a room the state unlocks come, flagged (hidden from rankings until reviewed).
        var room = await host.CreateRoomAsync(id, []);
        maxed.Bits = 9_999_998;
        var update = await client.PutJson("/api/v1/online-character", Upload(maxed, 400L * 3600 + 60, new { roomCode = room, players = 1 }), ifMatch: "1");
        Assert.Equal(HttpStatusCode.OK, update.StatusCode);
        Assert.NotEmpty((await update.Json()).GetProperty("unlocked").EnumerateArray());
        Assert.Equal(0, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM achievement_unlocks WHERE account_id = @id AND flagged = 0", new { id }));

        // A record that fits the account's age is not flagged: a new account after a day with a day of play.
        var (token2, _, id2) = await host.RegisterAsync("Honest");
        host.Clock.Advance(TimeSpan.FromHours(30));
        var modest = Samples.Fresh("Hon");
        modest.Unique = 0x12340001;
        var ok = await host.Client(token2).PutJson("/api/v1/online-character", Upload(modest, 24 * 3600), create: true);
        Assert.Equal(HttpStatusCode.Created, ok.StatusCode);
        Assert.Empty((await ok.Json()).GetProperty("warnings").EnumerateArray());
        // After a reset the clock starts again: the next new character cannot bring the old play time.
        Assert.Equal(HttpStatusCode.NoContent,
            (await host.Client(token2).PostAsJsonAsync("/api/v1/online-character/reset", new { currentPassword = "a decent password" })).StatusCode);
        var again = await host.Client(token2).PutJson("/api/v1/online-character", Upload(modest, 24 * 3600), create: true);
        Assert.Contains("play_time_too_fast", (await again.Json()).GetProperty("warnings").EnumerateArray().Select(w => w.GetProperty("code").GetString()));
        Assert.True(await c.ExecuteScalarAsync<bool>("SELECT online_reset_at IS NOT NULL FROM accounts WHERE id = @id2", new { id2 }));
    }

    /// <summary>State unlocks go to the room's ruleset: a randomizer room's into the randomizer list.</summary>
    [DbFact]
    public async Task State_unlocks_take_the_rooms_ruleset()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, id) = await host.RegisterAsync("Shuffler");
        var client = host.Client(token);
        host.Clock.Advance(TimeSpan.FromHours(2));
        var room = await host.CreateRoomAsync(id, [], randomizer: true);
        var character = Samples.Fresh();
        character.Level = 12;
        character.BaseLevel[0] = 12;
        character.Hp = character.MaxHp = 300;
        // The client even claims a standard run: the room decides.
        var created = await client.PutJson("/api/v1/online-character", Upload(character, 3600, new { roomCode = room, players = 1, randomizer = false }), create: true);
        Assert.Equal(HttpStatusCode.Created, created.StatusCode);
        Assert.Contains("lv_10", (await created.Json()).GetProperty("unlocked").EnumerateArray().Select(e => e.GetString()));
        await using var c = await host.SqlAsync();
        Assert.Equal(["randomizer"], (await c.QueryAsync<string>(
            "SELECT u.ruleset FROM achievement_unlocks u JOIN achievements a ON a.id = u.achievement_id WHERE u.account_id = @id AND a.code = 'lv_10'", new { id })).ToList());
        // A room the account is not in counts for nothing.
        var (otherToken, _, otherId) = await host.RegisterAsync("Outsider");
        var foreignRoom = await host.CreateRoomAsync(otherId, []);
        var outside = await client.PutJson("/api/v1/online-character", Upload(character, 3700, new { roomCode = foreignRoom, players = 1 }), ifMatch: "1");
        Assert.Equal(HttpStatusCode.OK, outside.StatusCode);
        Assert.Empty((await outside.Json()).GetProperty("unlocked").EnumerateArray());
        _ = otherToken;
    }
}
