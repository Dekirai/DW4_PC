using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Dapper;
using DW4Online.Domain.Save;
using DW4Online.Tests;

namespace DW4Online.Integration.Tests;

public class OnlineCharacterTests
{
    private static object Upload(SaveRecord record, long playSeconds, object? context = null) =>
        new { record = Convert.ToBase64String(RecordCodec.Encode(record)), playSeconds, context };

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
        Assert.Equal(RecordCodec.Encode(character), Convert.FromBase64String(stored.GetProperty("record").GetString()!));
        Assert.Equal(600, stored.GetProperty("playSeconds").GetInt64());

        character.Level = 12;
        character.BaseLevel[0] = 12;
        character.Hp = character.MaxHp = 300;
        host.Clock.Advance(TimeSpan.FromHours(1));
        var stale = await client.PutJson("/api/v1/online-character", Upload(character, 3000), ifMatch: "\"7\"");
        Assert.Equal(HttpStatusCode.Conflict, stale.StatusCode);
        Assert.Equal(1, (await stale.Json()).GetProperty("currentRevision").GetInt32());

        var updated = await client.PutJson("/api/v1/online-character", Upload(character, 3000), ifMatch: "\"1\"");
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
        var created = await client.PutJson("/api/v1/online-character", Upload(maxed, 120), create: true);
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
}
