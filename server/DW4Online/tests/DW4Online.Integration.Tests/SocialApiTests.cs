using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Dapper;
using DW4Online.Domain.Services;
using Microsoft.Extensions.DependencyInjection;

namespace DW4Online.Integration.Tests;

/// <summary>
/// Friends and blocks over the REST API (SEAMLESS.md 19.19) on MariaDB: the SQL store behind SocialService, the answers
/// that never reveal a block or a decline, expiry and the purge, the problem details of every refusal, the rate limits
/// and the player card.
/// </summary>
public class SocialApiTests
{
    private static async Task<(HttpClient Kim, HttpClient Lee, HttpClient Max, long KimId, long LeeId, long MaxId)> ThreeAsync(TestHost host)
    {
        var (kt, _, kid) = await host.RegisterAsync("Kim");
        var (lt, _, lid) = await host.RegisterAsync("Lee");
        var (mt, _, mid) = await host.RegisterAsync("Max");
        return (host.Client(kt), host.Client(lt), host.Client(mt), kid, lid, mid);
    }

    private static Task<HttpResponseMessage> Request(HttpClient c, string username) => c.PostAsJsonAsync("/api/v1/friends/requests", new { username });

    private static async Task<JsonElement> Friends(HttpClient c)
    {
        var response = await c.GetAsync("/api/v1/friends");
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        return await response.Json();
    }

    private static List<string> Names(JsonElement overview, string list) =>
        overview.GetProperty(list).EnumerateArray().Select(e => e.GetProperty("username").GetString()!).ToList();

    private static async Task<string> RelationAsync(HttpClient c, string username) =>
        (await (await c.GetAsync($"/api/v1/players/{username}")).Json()).GetProperty("relation").GetString()!;

    [DbFact]
    public async Task Request_accept_and_remove_over_the_api()
    {
        await using var host = await TestHost.StartAsync();
        var (kim, lee, _, kimId, leeId, _) = await ThreeAsync(host);

        var sent = await Request(kim, " lee ");
        Assert.Equal(HttpStatusCode.OK, sent.StatusCode);
        var body = await sent.Json();
        Assert.Equal(("sent", "Request sent."), (body.GetProperty("result").GetString(), body.GetProperty("message").GetString()));
        var kimView = await Friends(kim);
        Assert.Equal(["Lee"], Names(kimView, "requestsOut"));
        var expires = kimView.GetProperty("requestsOut")[0].GetProperty("expiresAt").GetDateTime();
        Assert.Equal(host.Clock.GetUtcNow().UtcDateTime.AddDays(30), expires);
        Assert.Equal(200, kimView.GetProperty("limits").GetProperty("maxFriends").GetInt32());
        Assert.Equal(["Kim"], Names(await Friends(lee), "requestsIn"));
        Assert.Equal("request_sent", await RelationAsync(kim, "Lee"));
        Assert.Equal("request_received", await RelationAsync(lee, "kim"));

        var accepted = await lee.PostAsync("/api/v1/friends/requests/Kim/accept", null);
        Assert.Equal(HttpStatusCode.OK, accepted.StatusCode);
        Assert.Equal("You are now friends with Kim.", (await accepted.Json()).GetProperty("message").GetString());
        var leeView = await Friends(lee);
        Assert.Equal(["Kim"], Names(leeView, "friends"));
        // Round I1 fills it: Kim is offline (never in the world) and shows his whereabouts to friends by default.
        Assert.False(leeView.GetProperty("friends")[0].GetProperty("presence").GetProperty("online").GetBoolean());
        Assert.Empty(Names(leeView, "requestsIn"));
        Assert.Equal("friends", await RelationAsync(kim, "Lee"));
        await using (var sql = await host.SqlAsync())
        {
            var (lo, hi) = (Math.Min(kimId, leeId), Math.Max(kimId, leeId));
            Assert.Equal(1, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM friendships WHERE account_lo = @lo AND account_hi = @hi", new { lo, hi }));
            Assert.Equal(0, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM friend_requests"));
            Assert.Equal(0, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM audit_log WHERE event LIKE 'social%'")); // not audited
        }

        Assert.Equal("already_friends", await (await Request(lee, "Kim")).ProblemCode());
        Assert.Equal(HttpStatusCode.NoContent, (await kim.DeleteAsync("/api/v1/friends/LEE")).StatusCode);
        Assert.Empty(Names(await Friends(lee), "friends")); // one-sided, untold
        Assert.Equal("not_friends", await (await kim.DeleteAsync("/api/v1/friends/Lee")).ProblemCode());
    }

    [DbFact]
    public async Task Blocks_and_declines_are_never_revealed_to_the_sender()
    {
        await using var host = await TestHost.StartAsync();
        var (kim, lee, max, kimId, leeId, maxId) = await ThreeAsync(host);
        Assert.Equal(HttpStatusCode.NoContent, (await lee.PostAsJsonAsync("/api/v1/blocks", new { username = "Kim" })).StatusCode);
        Assert.Equal(HttpStatusCode.NoContent, (await lee.PostAsJsonAsync("/api/v1/blocks", new { username = "kim" })).StatusCode); // again: no change

        // Kim's request to Lee (who blocks him) and to Max (who does not): the same status and the same bytes.
        var toLee = await Request(kim, "Lee");
        var toMax = await Request(kim, "Max");
        Assert.Equal((toMax.StatusCode, await toMax.Content.ReadAsStringAsync()), (toLee.StatusCode, await toLee.Content.ReadAsStringAsync()));
        Assert.Equal(["Lee", "Max"], Names(await Friends(kim), "requestsOut").Order());
        Assert.Equal("request_sent", await RelationAsync(kim, "Lee"));
        Assert.Empty(Names(await Friends(lee), "requestsIn"));
        Assert.Equal("blocked", await RelationAsync(lee, "Kim"));
        Assert.Equal(["Kim"], Names(await Friends(lee), "blocks"));
        var blocks = await (await lee.GetAsync("/api/v1/blocks")).Json();
        Assert.Equal("Kim", blocks[0].GetProperty("username").GetString());
        Assert.Equal("request_not_found", await (await lee.PostAsync("/api/v1/friends/requests/Kim/accept", null)).ProblemCode());
        Assert.Equal("you_blocked", await (await Request(lee, "Kim")).ProblemCode());
        await using (var sql = await host.SqlAsync())
        {
            var rows = (await sql.QueryAsync<(string State, bool Delivered)>(
                "SELECT state, delivered FROM friend_requests WHERE from_account_id = @kimId ORDER BY to_account_id", new { kimId })).ToList();
            Assert.Equal([("pending", false), ("pending", true)], leeId < maxId ? rows : [rows[1], rows[0]]);
        }

        // Max declines: Kim's list still says "sent", a new request changes nothing, the row says declined.
        Assert.Equal(HttpStatusCode.NoContent, (await max.PostAsync("/api/v1/friends/requests/Kim/decline", null)).StatusCode);
        Assert.Empty(Names(await Friends(max), "requestsIn"));
        host.Clock.Advance(TimeSpan.FromHours(2));
        var again = await Request(kim, "Max");
        Assert.Equal((HttpStatusCode.OK, await toMax.Content.ReadAsStringAsync()), (again.StatusCode, await again.Content.ReadAsStringAsync()));
        Assert.Equal("request_sent", await RelationAsync(kim, "Max"));
        Assert.Equal("none", await RelationAsync(max, "Kim"));
        await using (var sql = await host.SqlAsync())
        {
            var row = await sql.QuerySingleAsync<(string State, DateTime Created, DateTime? Decided)>(
                "SELECT state, created_at, decided_at FROM friend_requests WHERE from_account_id = @kimId AND to_account_id = @maxId", new { kimId, maxId });
            Assert.Equal("declined", row.State);
            Assert.Equal(host.Clock.GetUtcNow().UtcDateTime.AddHours(-2), row.Created);
            Assert.NotNull(row.Decided);
        }
        // Kim cancels the declined one: hidden from him, the decline holds; sending again shows it as sent, not delivered.
        Assert.Equal(HttpStatusCode.NoContent, (await kim.DeleteAsync("/api/v1/friends/requests/Max")).StatusCode);
        Assert.Equal(["Lee"], Names(await Friends(kim), "requestsOut"));
        Assert.Equal(HttpStatusCode.OK, (await Request(kim, "Max")).StatusCode);
        Assert.Equal(["Lee", "Max"], Names(await Friends(kim), "requestsOut").Order());
        Assert.Empty(Names(await Friends(max), "requestsIn"));

        // Lee unblocks Kim: Kim's pending request reaches Lee now.
        Assert.Equal(HttpStatusCode.NoContent, (await lee.DeleteAsync("/api/v1/blocks/Kim")).StatusCode);
        Assert.Equal(["Kim"], Names(await Friends(lee), "requestsIn"));
        Assert.Equal("not_blocked", await (await lee.DeleteAsync("/api/v1/blocks/Kim")).ProblemCode());

        // A block ends a friendship and the blocker's own request; a delivered request from the blocked player turns
        // silent and stays "sent" to him.
        Assert.Equal(HttpStatusCode.OK, (await lee.PostAsync("/api/v1/friends/requests/Kim/accept", null)).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await Request(lee, "Max")).StatusCode);
        Assert.Equal(HttpStatusCode.NoContent, (await lee.PostAsJsonAsync("/api/v1/blocks", new { username = "Kim" })).StatusCode);
        Assert.Empty(Names(await Friends(kim), "friends"));
        Assert.Empty(Names(await Friends(lee), "friends"));
        Assert.Equal(HttpStatusCode.NoContent, (await lee.PostAsJsonAsync("/api/v1/blocks", new { username = "Max" })).StatusCode);
        Assert.Empty(Names(await Friends(lee), "requestsOut"));  // the blocker's own request went
        Assert.Empty(Names(await Friends(max), "requestsIn"));
        var (annToken, _, _) = await host.RegisterAsync("Ann");
        var ann = host.Client(annToken);
        Assert.Equal(HttpStatusCode.OK, (await Request(ann, "Lee")).StatusCode);
        Assert.Equal(["Ann"], Names(await Friends(lee), "requestsIn"));
        Assert.Equal(HttpStatusCode.NoContent, (await lee.PostAsJsonAsync("/api/v1/blocks", new { username = "Ann" })).StatusCode);
        Assert.Empty(Names(await Friends(lee), "requestsIn"));
        Assert.Equal(["Lee"], Names(await Friends(ann), "requestsOut"));
        Assert.Equal("request_sent", await RelationAsync(ann, "Lee"));
    }

    [DbFact]
    public async Task Crossing_requests_make_friends_and_expired_requests_are_purged()
    {
        await using var host = await TestHost.StartAsync(new() { ["Accounts:GameSessionDays"] = "90" }); // the sessions outlive the requests
        var (kim, lee, max, _, _, _) = await ThreeAsync(host);
        Assert.Equal(HttpStatusCode.OK, (await Request(kim, "Lee")).StatusCode);
        var crossing = await Request(lee, "Kim");
        Assert.Equal("friends", (await crossing.Json()).GetProperty("result").GetString());
        Assert.Equal(["Lee"], Names(await Friends(kim), "friends"));

        Assert.Equal(HttpStatusCode.OK, (await Request(kim, "Max")).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await Request(lee, "Max")).StatusCode);
        Assert.Equal(HttpStatusCode.NoContent, (await max.PostAsync("/api/v1/friends/requests/Lee/decline", null)).StatusCode);
        host.Clock.Advance(TimeSpan.FromDays(30).Add(TimeSpan.FromSeconds(1)));
        Assert.Empty(Names(await Friends(kim), "requestsOut"));
        Assert.Empty(Names(await Friends(lee), "requestsOut"));
        Assert.Empty(Names(await Friends(max), "requestsIn"));
        Assert.Equal("request_not_found", await (await max.PostAsync("/api/v1/friends/requests/Kim/accept", null)).ProblemCode());
        var purged = await host.App.Services.GetRequiredService<SocialService>().PurgeExpiredAsync();
        Assert.Equal(2, purged);
        await using var sql = await host.SqlAsync();
        Assert.Equal(0, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM friend_requests"));
        // After the expiry a new request reaches Max again (also Lee's, whose earlier one Max declined).
        Assert.Equal(HttpStatusCode.OK, (await Request(lee, "Max")).StatusCode);
        Assert.Equal(["Lee"], Names(await Friends(max), "requestsIn"));
    }

    [DbFact]
    public async Task Refusals_are_problem_details_and_the_limits_are_logged()
    {
        await using var host = await TestHost.StartAsync(new()
        {
            ["Friends:MaxPendingIn"] = "1",
            ["Friends:RequestsPerHour"] = "2",
            ["Friends:MaxBlocks"] = "1",
        });
        var (kim, lee, max, kimId, _, _) = await ThreeAsync(host);
        Assert.Equal((HttpStatusCode.NotFound, "player_not_found"), await Problem(Request(kim, "Nobody")));
        Assert.Equal((HttpStatusCode.NotFound, "player_not_found"), await Problem(Request(kim, "Le")));          // no partial names
        Assert.Equal((HttpStatusCode.NotFound, "player_not_found"), await Problem(Request(kim, "Le%")));
        Assert.Equal((HttpStatusCode.UnprocessableEntity, "own_account"), await Problem(Request(kim, "KIM")));
        Assert.Equal((HttpStatusCode.NotFound, "request_not_found"), await Problem(kim.PostAsync("/api/v1/friends/requests/Lee/accept", null)));
        Assert.Equal((HttpStatusCode.NotFound, "request_not_found"), await Problem(kim.DeleteAsync("/api/v1/friends/requests/Lee")));
        Assert.Equal((HttpStatusCode.NotFound, "player_not_found"), await Problem(kim.GetAsync("/api/v1/players/Nobody")));

        // Lee's one place (Friends:MaxPendingIn = 1) is taken: Max's request answers the same bytes and waits.
        var first = await Request(kim, "Lee");
        Assert.Equal(HttpStatusCode.OK, first.StatusCode);
        var waits = await Request(max, "Lee");
        Assert.Equal((HttpStatusCode.OK, await first.Content.ReadAsStringAsync()), (waits.StatusCode, await waits.Content.ReadAsStringAsync()));
        Assert.Equal(["Kim"], Names(await Friends(lee), "requestsIn"));
        Assert.Equal(["Lee"], Names(await Friends(max), "requestsOut"));

        Assert.Equal(HttpStatusCode.OK, (await Request(kim, "Max")).StatusCode); // Kim's second request this hour
        await host.RegisterAsync("Zed");
        var tooMany = await Request(kim, "Zed");
        Assert.Equal(HttpStatusCode.TooManyRequests, tooMany.StatusCode);
        var tooManyBody = await tooMany.Json();
        Assert.Equal("friend_request_rate", tooManyBody.GetProperty("code").GetString());
        Assert.Equal(3600, tooManyBody.GetProperty("retryAfterSeconds").GetInt32());

        Assert.Equal(HttpStatusCode.NoContent, (await kim.PostAsJsonAsync("/api/v1/blocks", new { username = "Zed" })).StatusCode);
        // Refused three times, logged once (per account and limit an hour: a retrying client cannot grow the log).
        for (var i = 0; i < 3; ++i)
            Assert.Equal((HttpStatusCode.Conflict, "block_limit"), await Problem(kim.PostAsJsonAsync("/api/v1/blocks", new { username = "Lee" })));

        await using var sql = await host.SqlAsync();
        var limits = (await sql.QueryAsync<string>(
            "SELECT JSON_VALUE(detail, '$.limit') FROM audit_log WHERE event = 'social_limit' ORDER BY id")).ToList();
        Assert.Equal(["requests_per_time", "blocks"], limits);
        // Lee answers Kim's request: Max's waiting one takes the place.
        Assert.Equal(HttpStatusCode.NoContent, (await lee.PostAsync("/api/v1/friends/requests/Kim/decline", null)).StatusCode);
        Assert.Equal(["Max"], Names(await Friends(lee), "requestsIn"));
        Assert.Equal(1, await sql.ExecuteScalarAsync<int>(
            "SELECT COUNT(*) FROM audit_log WHERE event = 'social_limit' AND account_id = @kimId AND ip IS NOT NULL AND detail LIKE '%blocks%'", new { kimId }));
    }

    private static async Task<(HttpStatusCode, string)> Problem(Task<HttpResponseMessage> call)
    {
        var response = await call;
        return (response.StatusCode, await response.ProblemCode());
    }

    [DbFact]
    public async Task The_friend_list_is_limited_to_one_call_per_two_seconds()
    {
        await using var host = await TestHost.StartAsync(new() { ["RateLimits:FriendListSeconds"] = "2" });
        var (token, _, _) = await host.RegisterAsync("Poller");
        var client = host.Client(token);
        Assert.Equal(HttpStatusCode.OK, (await client.GetAsync("/api/v1/friends")).StatusCode);
        var second = await client.GetAsync("/api/v1/friends");
        Assert.Equal(HttpStatusCode.TooManyRequests, second.StatusCode);
        Assert.Equal("rate_limited", await second.ProblemCode());
        // Other social calls have their own limit.
        Assert.Equal(HttpStatusCode.OK, (await client.GetAsync("/api/v1/blocks")).StatusCode);
        Assert.Equal(HttpStatusCode.Unauthorized, (await host.Client().GetAsync("/api/v1/friends")).StatusCode);
    }

    [DbFact]
    public async Task The_player_card_shows_the_public_card_and_the_relation()
    {
        await using var host = await TestHost.StartAsync();
        var (kim, lee, _, _, _, _) = await ThreeAsync(host);
        Assert.Equal(HttpStatusCode.OK, (await lee.PutAsJsonAsync("/api/v1/me/profile", new { @public = false })).StatusCode);
        var card = await (await kim.GetAsync("/api/v1/players/lee")).Json();
        Assert.Equal("Lee", card.GetProperty("username").GetString());
        Assert.True(card.GetProperty("hidden").GetBoolean());
        Assert.Equal(JsonValueKind.Null, card.GetProperty("character").ValueKind);
        Assert.Equal(JsonValueKind.Null, card.GetProperty("memberSince").ValueKind); // as the website: nothing but the name
        Assert.Equal("none", card.GetProperty("relation").GetString());
        var own = await (await lee.GetAsync("/api/v1/players/Lee")).Json();
        Assert.False(own.GetProperty("hidden").GetBoolean());
        Assert.Equal(host.Clock.GetUtcNow().UtcDateTime, own.GetProperty("memberSince").GetDateTime());
        Assert.Equal("self", own.GetProperty("relation").GetString());
        var visible = await (await lee.GetAsync("/api/v1/players/Kim")).Json();
        Assert.False(visible.GetProperty("hidden").GetBoolean());
        Assert.Equal(JsonValueKind.String, visible.GetProperty("memberSince").ValueKind);
    }

    /// <summary>
    /// The SQL behind waiting requests and expiry: a request beyond Friends:MaxPendingIn waits and moves up when a place
    /// frees; a request sent again over an expired row that the housekeeping has not purged yet (pending or declined)
    /// is a fresh pending one; the hourly pass delivers what waits for places an expiry freed.
    /// </summary>
    [DbFact]
    public async Task Waiting_requests_and_requests_over_expired_rows_in_sql()
    {
        await using var host = await TestHost.StartAsync(new()
        {
            ["Friends:MaxPendingIn"] = "1",
            ["Accounts:GameSessionDays"] = "90", // the sessions outlive the requests
        });
        var (kim, lee, max, kimId, leeId, maxId) = await ThreeAsync(host);
        async Task<(string State, bool Delivered, DateTime Created, DateTime? Decided, DateTime? Cancelled)> RowAsync(long from)
        {
            await using var sql = await host.SqlAsync();
            return await sql.QuerySingleAsync<(string, bool, DateTime, DateTime?, DateTime?)>(
                "SELECT state, delivered, created_at, decided_at, cancelled_at FROM friend_requests WHERE from_account_id = @from AND to_account_id = @leeId",
                new { from, leeId });
        }

        Assert.Equal(HttpStatusCode.OK, (await Request(kim, "Lee")).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await Request(max, "Lee")).StatusCode);
        Assert.False((await RowAsync(maxId)).Delivered);
        Assert.Equal(["Kim"], Names(await Friends(lee), "requestsIn"));
        Assert.Equal(HttpStatusCode.NoContent, (await lee.PostAsync("/api/v1/friends/requests/Kim/decline", null)).StatusCode);
        Assert.Equal(["Max"], Names(await Friends(lee), "requestsIn"));
        Assert.Equal("declined", (await RowAsync(kimId)).State);

        // 31 days later, before any purge: both rows are expired but still there. Sending again writes fresh rows.
        host.Clock.Advance(TimeSpan.FromDays(31));
        await using (var sql = await host.SqlAsync())
            Assert.Equal(2, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM friend_requests"));
        Assert.Equal(HttpStatusCode.OK, (await Request(kim, "Lee")).StatusCode);
        var kimRow = await RowAsync(kimId);
        Assert.Equal(("pending", true, host.Clock.GetUtcNow().UtcDateTime, (DateTime?)null, (DateTime?)null), kimRow);
        Assert.Equal(["Kim"], Names(await Friends(lee), "requestsIn")); // the expired decline no longer holds
        host.Clock.Advance(TimeSpan.FromHours(1));
        Assert.Equal(HttpStatusCode.OK, (await Request(max, "Lee")).StatusCode);
        var maxRow = await RowAsync(maxId);
        Assert.Equal(("pending", false, host.Clock.GetUtcNow().UtcDateTime), (maxRow.State, maxRow.Delivered, maxRow.Created)); // waits

        // Kim's request expires 30 days after it was sent, Max's an hour later: the hourly pass purges Kim's and
        // gives Lee's free place to Max's.
        host.Clock.Advance(TimeSpan.FromDays(30).Subtract(TimeSpan.FromMinutes(30)));
        Assert.Empty(Names(await Friends(lee), "requestsIn"));
        Assert.Equal(1, await host.App.Services.GetRequiredService<SocialService>().PurgeExpiredAsync());
        Assert.Equal(["Max"], Names(await Friends(lee), "requestsIn"));
        Assert.True((await RowAsync(maxId)).Delivered);
        Assert.Equal(HttpStatusCode.OK, (await lee.PostAsync("/api/v1/friends/requests/Max/accept", null)).StatusCode);
    }
}
