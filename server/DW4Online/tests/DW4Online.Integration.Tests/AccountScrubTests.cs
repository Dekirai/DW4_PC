using System.Net;
using System.Net.Http.Json;
using Dapper;
using DW4Online.Domain.Services;
using Microsoft.Extensions.DependencyInjection;
using MySqlConnector;

namespace DW4Online.Integration.Tests;

/// <summary>
/// Account deletion (AccountRepository.ScrubAsync) on this release's schema (SEAMLESS.md 19.17.1): the account row
/// stays as "deleted_&lt;id&gt;", so no ON DELETE CASCADE fires and every table that names the account needs its own
/// statement. A deleted account with friends, requests both ways and blocks both ways is nobody's friend, requester or
/// blocker afterwards, and no column that references accounts(id) still names it (except the rooms and instances it
/// hosted). Round I1a's world (migration 0013): its online preferences, its instance memberships and its last-seen time go;
/// the instances it hosted keep their row (the id then names the anonymous account), its guests keep theirs. Round I1b's
/// reports: the account's own reports go, reports about it keep the anonymous id for staff (and a report a staff member handled
/// keeps him as the handler).
/// </summary>
public class AccountScrubTests
{
    /// <summary>Columns that may keep naming a deleted account: rooms and instances keep their record (and their host).</summary>
    private static readonly HashSet<string> Kept = ["rooms.host_account_id", "account_bans.banned_by", "instance_history.host_account_id",
        "player_reports.reported_account_id", "player_reports.handled_by"];

    /// <summary>Every column with a foreign key to accounts(id) that still names <paramref name="id"/>, with the row count.</summary>
    private static async Task<List<string>> LeftoversAsync(MySqlConnection c, long id)
    {
        var columns = (await c.QueryAsync<(string Table, string Column)>(
            "SELECT TABLE_NAME, COLUMN_NAME FROM information_schema.KEY_COLUMN_USAGE " +
            "WHERE TABLE_SCHEMA = DATABASE() AND REFERENCED_TABLE_NAME = 'accounts' AND REFERENCED_COLUMN_NAME = 'id'")).ToList();
        Assert.Contains(("friendships", "account_lo"), columns);
        Assert.Contains(("account_blocks", "blocked_account_id"), columns);
        Assert.Contains(("online_prefs", "account_id"), columns);
        Assert.Contains(("instance_history_members", "account_id"), columns);
        Assert.Contains(("player_reports", "reporter_account_id"), columns);
        var left = new List<string>();
        foreach (var (table, column) in columns)
        {
            var n = await c.ExecuteScalarAsync<long>($"SELECT COUNT(*) FROM `{table}` WHERE `{column}` = @id", new { id });
            if (n > 0 && !Kept.Contains($"{table}.{column}")) left.Add($"{table}.{column} ({n})");
        }
        return left;
    }

    private static async Task<List<string>> NamesAsync(HttpClient c, string list)
    {
        var overview = await (await c.GetAsync("/api/v1/friends")).Json();
        return overview.GetProperty(list).EnumerateArray().Select(e => e.GetProperty("username").GetString()!).ToList();
    }

    private static Task<HttpResponseMessage> RequestAsync(HttpClient c, string username) =>
        c.PostAsJsonAsync("/api/v1/friends/requests", new { username });

    /// <summary>Gone: a friend of Ann, a request to Ben, requests from Cy (pending) and Dee (declined by Gone), blocks Eve and is blocked by Fay.</summary>
    private static async Task<(long GoneId, string GoneToken, Dictionary<string, HttpClient> Others)> SocialWebAsync(TestHost host)
    {
        var (goneToken, _, goneId) = await host.RegisterAsync("Gone");
        var gone = host.Client(goneToken);
        var others = new Dictionary<string, HttpClient>();
        foreach (var name in new[] { "Ann", "Ben", "Cy1", "Dee", "Eve", "Fay" })
            others[name] = host.Client((await host.RegisterAsync(name)).Token);

        Assert.Equal(HttpStatusCode.OK, (await RequestAsync(gone, "Ann")).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await others["Ann"].PostAsync("/api/v1/friends/requests/Gone/accept", null)).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await RequestAsync(gone, "Ben")).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await RequestAsync(others["Cy1"], "Gone")).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await RequestAsync(others["Dee"], "Gone")).StatusCode);
        Assert.Equal(HttpStatusCode.NoContent, (await gone.PostAsync("/api/v1/friends/requests/Dee/decline", null)).StatusCode);
        Assert.Equal(HttpStatusCode.NoContent, (await gone.PostAsJsonAsync("/api/v1/blocks", new { username = "Eve" })).StatusCode);
        Assert.Equal(HttpStatusCode.NoContent, (await others["Fay"].PostAsJsonAsync("/api/v1/blocks", new { username = "Gone" })).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await RequestAsync(others["Eve"], "Gone")).StatusCode); // silent: Gone blocks Eve

        await using var sql = await host.SqlAsync();
        Assert.Equal(1, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM friendships WHERE @id IN (account_lo, account_hi)", new { id = goneId }));
        Assert.Equal(4, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM friend_requests WHERE @id IN (from_account_id, to_account_id)", new { id = goneId }));
        Assert.Equal(2, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM account_blocks WHERE @id IN (account_id, blocked_account_id)", new { id = goneId }));
        return (goneId, goneToken, others);
    }

    private static async Task AssertNobodysAsync(TestHost host, long goneId, Dictionary<string, HttpClient> others)
    {
        Assert.Empty(await NamesAsync(others["Ann"], "friends"));
        Assert.Empty(await NamesAsync(others["Ben"], "requestsIn"));
        Assert.Empty(await NamesAsync(others["Cy1"], "requestsOut"));
        Assert.Empty(await NamesAsync(others["Dee"], "requestsOut"));
        Assert.Empty(await NamesAsync(others["Eve"], "requestsOut"));
        Assert.Empty(await NamesAsync(others["Fay"], "blocks"));
        foreach (var name in new[] { "Gone", $"deleted_{goneId}" })
            Assert.Equal("player_not_found", await (await RequestAsync(others["Ann"], name)).ProblemCode());
        Assert.Equal("player_not_found", await (await others["Ann"].GetAsync($"/api/v1/players/deleted_{goneId}")).ProblemCode());
        Assert.Equal("player_not_found", await (await others["Fay"].DeleteAsync($"/api/v1/blocks/deleted_{goneId}")).ProblemCode());

        await using var sql = await host.SqlAsync();
        Assert.Empty(await LeftoversAsync(sql, goneId));
        Assert.Equal(0, await sql.ExecuteScalarAsync<int>(
            "SELECT (SELECT COUNT(*) FROM friendships WHERE @id IN (account_lo, account_hi)) + " +
            "(SELECT COUNT(*) FROM friend_requests WHERE @id IN (from_account_id, to_account_id)) + " +
            "(SELECT COUNT(*) FROM account_blocks WHERE @id IN (account_id, blocked_account_id))", new { id = goneId }));
        Assert.Equal("deleted", await sql.ExecuteScalarAsync<string>("SELECT status FROM accounts WHERE id = @goneId", new { goneId }));
    }

    [DbFact]
    public async Task A_player_who_deletes_his_account_is_nobodys_friend_or_blocker_afterwards()
    {
        await using var host = await TestHost.StartAsync();
        var (goneId, goneToken, others) = await SocialWebAsync(host);
        var annId = (await host.LoginAsync("Ann")).Id;
        await host.CreateRoomAsync(goneId, [annId]); // a hosted room keeps its record, its membership rows go
        var instance = await WorldHistoryAsync(host, goneId, annId);
        Assert.Equal(HttpStatusCode.NoContent,
            (await host.Client(goneToken).PostAsJsonAsync("/api/v1/me/delete", new { currentPassword = "a decent password" })).StatusCode);
        await AssertNobodysAsync(host, goneId, others);
        await using var sql = await host.SqlAsync();
        Assert.Equal(1, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM rooms WHERE host_account_id = @goneId", new { goneId }));
        await AssertWorldScrubbedAsync(sql, goneId, annId, instance);
        // The relay heard of it before and after the scrub (a world session started meanwhile ends too).
        Assert.Equal(2, host.World!.Notes.Count(n => n == $"account {goneId}"));
    }

    [DbFact]
    public async Task An_admin_deletion_scrubs_friends_and_blocks_too()
    {
        await using var host = await TestHost.StartAsync();
        var (goneId, _, others) = await SocialWebAsync(host);
        var (adminToken, _, _) = await host.RegisterAsync("Chief");
        Assert.True((await host.App.Services.GetRequiredService<AccountService>()
            .AdminSetRoleAsync(null, "Chief", "admin", new CallerInfo(null, "tests"))).Ok);
        var annId = (await host.LoginAsync("Ann")).Id;
        var instance = await WorldHistoryAsync(host, goneId, annId);
        Assert.Equal(HttpStatusCode.NoContent, (await host.Client(adminToken).DeleteAsync("/api/v1/admin/accounts/Gone")).StatusCode);
        await AssertNobodysAsync(host, goneId, others);
        await using var sql = await host.SqlAsync();
        await AssertWorldScrubbedAsync(sql, goneId, annId, instance);
    }

    /// <summary>
    /// The world's rows of the account (0013): online preferences, last seen, an instance it hosted with Ann as a guest and
    /// one of Ann's it joined, both through the history repository as the relay's queue writes them. Returns its hosted instance.
    /// </summary>
    private static async Task<long> WorldHistoryAsync(TestHost host, long goneId, long annId)
    {
        var prefs = host.App.Services.GetRequiredService<DW4Online.Domain.Abstractions.IWorldRepository>();
        var history = host.App.Services.GetRequiredService<DW4Online.Domain.Abstractions.IInstanceHistoryRepository>();
        var now = host.Clock.GetUtcNow().UtcDateTime;
        Assert.True(await prefs.SaveVisibilityAsync(goneId, "friends", now));
        await prefs.TouchLastSeenAsync([goneId, annId], now);
        var hosted = await history.StartAsync(new DW4Online.Domain.Abstractions.InstanceHistoryStart(goneId, 1, "stag0201", "public", 0, 3, "s", now));
        await history.MemberJoinedAsync(hosted, goneId, 1, "host", now);
        await history.MemberJoinedAsync(hosted, annId, 1, "guest", now);
        var joined = await history.StartAsync(new DW4Online.Domain.Abstractions.InstanceHistoryStart(annId, 1, "stag0101", "public", 0, 3, "s", now));
        await history.MemberJoinedAsync(joined, annId, 1, "host", now);
        await history.MemberJoinedAsync(joined, goneId, 1, "guest", now);
        // Reports both ways (round I1b): Gone about Ann (goes), Ann about Gone (stays, about the anonymous account).
        var reports = host.App.Services.GetRequiredService<ReportService>();
        Assert.True((await reports.ReportAsync(goneId, annId, "griefing", hosted, "game")).Ok);
        Assert.True((await reports.ReportAsync(annId, goneId, "name", null, "web")).Ok);
        await using var sql = await host.SqlAsync();
        Assert.Equal(1, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM online_prefs WHERE account_id = @goneId", new { goneId }));
        Assert.Equal(2, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM instance_history_members WHERE account_id = @goneId", new { goneId }));
        Assert.Equal(2, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM player_reports"));
        return hosted;
    }

    private static async Task AssertWorldScrubbedAsync(MySqlConnection sql, long goneId, long annId, long hosted)
    {
        Assert.Equal(0, await sql.ExecuteScalarAsync<int>(
            "SELECT (SELECT COUNT(*) FROM online_prefs WHERE account_id = @goneId) + " +
            "(SELECT COUNT(*) FROM instance_history_members WHERE account_id = @goneId)", new { goneId }));
        Assert.Null(await sql.ExecuteScalarAsync<DateTime?>("SELECT last_seen_at FROM accounts WHERE id = @goneId", new { goneId }));
        Assert.Equal(goneId, await sql.ExecuteScalarAsync<long>("SELECT CAST(host_account_id AS SIGNED) FROM instance_history WHERE id = @hosted", new { hosted }));
        Assert.Equal(2, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM instance_history_members WHERE account_id = @annId", new { annId }));
        Assert.NotNull(await sql.ExecuteScalarAsync<DateTime?>("SELECT last_seen_at FROM accounts WHERE id = @annId", new { annId }));
        Assert.Equal(0, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM player_reports WHERE reporter_account_id = @goneId", new { goneId }));
        Assert.Equal((annId, "name"), await sql.QuerySingleAsync<(long, string)>(
            "SELECT CAST(reporter_account_id AS SIGNED), reason FROM player_reports WHERE reported_account_id = @goneId", new { goneId }));
    }

    /// <summary>A friend request in flight for an account deleted meanwhile writes nothing (the repository's guard).</summary>
    [DbFact]
    public async Task Nothing_is_written_for_a_deleted_account_afterwards()
    {
        await using var host = await TestHost.StartAsync();
        var (goneToken, _, goneId) = await host.RegisterAsync("Late");
        var (_, _, otherId) = await host.RegisterAsync("Other");
        Assert.Equal(HttpStatusCode.NoContent,
            (await host.Client(goneToken).PostAsJsonAsync("/api/v1/me/delete", new { currentPassword = "a decent password" })).StatusCode);
        var store = host.App.Services.GetRequiredService<DW4Online.Domain.Abstractions.ISocialRepository>();
        var now = host.Clock.GetUtcNow().UtcDateTime;
        Assert.False(await store.PutPendingRequestAsync(otherId, goneId, now));
        Assert.False(await store.PutPendingRequestAsync(goneId, otherId, now));
        Assert.False(await store.AddFriendshipAsync(otherId, goneId, now));
        Assert.False(await store.AddBlockAsync(otherId, goneId, now));
        await using var sql = await host.SqlAsync();
        Assert.Empty(await LeftoversAsync(sql, goneId));
    }
}
