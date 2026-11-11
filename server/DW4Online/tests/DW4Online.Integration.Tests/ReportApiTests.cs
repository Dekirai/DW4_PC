using System.Net;
using System.Net.Http.Json;
using Dapper;
using DW4Online.Domain.Abstractions;
using Microsoft.Extensions.DependencyInjection;

namespace DW4Online.Integration.Tests;

/// <summary>
/// Report player over the API and the website (round I1b, SEAMLESS.md 19.16, 19.19, 19.20; i1b-contract.md "moderation 16:25"):
/// POST /reports with its fixed reasons, the own account, unknown names, a repeat about the same player (no second row), the daily
/// limit (429 report_limit), the shared instance from the history; the staff list with Done and Reopen and their audit rows; the
/// website's /admin/reports, the admin index's count and the account page's reports.
/// </summary>
public class ReportApiTests
{
    private static Task<HttpResponseMessage> ReportAsync(HttpClient c, string username, string reason) =>
        c.PostAsJsonAsync("/api/v1/reports", new { username, reason });

    [DbFact]
    public async Task A_player_reports_another_with_a_fixed_reason_once_a_day_per_player_and_within_the_daily_limit()
    {
        await using var host = await TestHost.StartAsync(new() { ["Reports:PerDay"] = "2", ["RateLimits:ReportsPerMinute"] = "1000" });
        var (token, _, kimId) = await host.RegisterAsync("Kim");
        var (_, _, leeId) = await host.RegisterAsync("Lee");
        await host.RegisterAsync("Max");
        await host.RegisterAsync("Ann");
        var kim = host.Client(token);
        // An instance Kim and Lee played in yesterday names the report's instance.
        var history = host.App.Services.GetRequiredService<IInstanceHistoryRepository>();
        var now = host.Clock.GetUtcNow().UtcDateTime;
        var shared = await history.StartAsync(new InstanceHistoryStart(leeId, 1, "stag0101", "public", 0, 3, "s", now.AddHours(-2)));
        await history.MemberJoinedAsync(shared, leeId, 1, "host", now.AddHours(-2));
        await history.MemberJoinedAsync(shared, kimId, 1, "guest", now.AddHours(-2));

        var sent = await ReportAsync(kim, "lee", "griefing");
        Assert.Equal(HttpStatusCode.OK, sent.StatusCode);
        var body = await sent.Json();
        Assert.Equal(("sent", "Report sent. Thank you."), (body.GetProperty("result").GetString(), body.GetProperty("message").GetString()));
        Assert.Equal("invalid_reason", await (await ReportAsync(kim, "Lee", "spam")).ProblemCode());
        Assert.Equal("invalid_reason", await (await kim.PostAsJsonAsync("/api/v1/reports", new { username = "Lee" })).ProblemCode());
        var own = await ReportAsync(kim, "Kim", "name");
        Assert.Equal((HttpStatusCode.UnprocessableEntity, "own_account"), (own.StatusCode, await own.ProblemCode()));
        var nobody = await ReportAsync(kim, "Nobody", "name");
        Assert.Equal((HttpStatusCode.NotFound, "player_not_found"), (nobody.StatusCode, await nobody.ProblemCode()));
        // A repeat about Lee answers "sent" and adds nothing; Max is the second of two a day; Ann the third: 429.
        Assert.Equal(HttpStatusCode.OK, (await ReportAsync(kim, "Lee", "cheating")).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await ReportAsync(kim, "Max", "name")).StatusCode);
        var limit = await ReportAsync(kim, "Ann", "other");
        Assert.Equal(HttpStatusCode.TooManyRequests, limit.StatusCode);
        Assert.Equal("report_limit", await limit.ProblemCode());
        // At the limit the service answers without the table for ReportService.LimitKnownMinutes: a repeat about Lee (which the
        // table would answer "sent") is "limit" now; ten minutes later the table speaks again.
        Assert.Equal("report_limit", await (await ReportAsync(kim, "Lee", "griefing")).ProblemCode());
        host.Clock.Advance(TimeSpan.FromMinutes(DW4Online.Domain.Services.ReportService.LimitKnownMinutes + 1));
        Assert.Equal(HttpStatusCode.OK, (await ReportAsync(kim, "Lee", "griefing")).StatusCode);
        Assert.Equal("report_limit", await (await ReportAsync(kim, "Ann", "other")).ProblemCode());
        Assert.Equal(HttpStatusCode.Unauthorized, (await ReportAsync(host.Client(), "Lee", "griefing")).StatusCode);

        await using var sql = await host.SqlAsync();
        var rows = (await sql.QueryAsync<(string Reported, string Reason, string Source, long? Instance, string State)>(
            "SELECT reported, reason, source, instance_id, state FROM v_player_reports ORDER BY id")).ToList();
        Assert.Equal([("Lee", "griefing", "web", (long?)shared, "open"), ("Max", "name", "web", (long?)null, "open")], rows);
        // A day later the limit is free again.
        host.Clock.Advance(TimeSpan.FromHours(25));
        Assert.Equal(HttpStatusCode.OK, (await ReportAsync(kim, "Ann", "other")).StatusCode);
    }

    [DbFact]
    public async Task Staff_list_reports_and_mark_them_done_or_open_again_with_audit_rows()
    {
        await using var host = await TestHost.StartAsync();
        var (kimToken, _, _) = await host.RegisterAsync("Kim");
        var (_, _, leeId) = await host.RegisterAsync("Lee");
        var (annToken, _, _) = await host.RegisterAsync("Ann");
        await host.RegisterAsync("Moddy");
        await using (var sql = await host.SqlAsync()) await sql.ExecuteAsync("UPDATE accounts SET role = 'moderator' WHERE username = 'Moddy'");
        var mod = host.Client((await host.LoginAsync("Moddy")).Token);
        var kim = host.Client(kimToken);
        Assert.Equal(HttpStatusCode.OK, (await ReportAsync(kim, "Lee", "griefing")).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await ReportAsync(host.Client(annToken), "Lee", "cheating")).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await ReportAsync(kim, "Ann", "name")).StatusCode);

        Assert.Equal(HttpStatusCode.Forbidden, (await kim.GetAsync("/api/v1/admin/reports")).StatusCode);
        var open = await (await mod.GetAsync("/api/v1/admin/reports")).Json();
        Assert.Equal(3, open.GetProperty("total").GetInt32());
        var first = open.GetProperty("reports")[0]; // newest first: Kim about Ann
        Assert.Equal(("Ann", "Kim", "name"), (first.GetProperty("reported").GetString(), first.GetProperty("reporter").GetString(),
            first.GetProperty("reason").GetString()));
        var aboutLee = open.GetProperty("reports")[1];
        Assert.Equal(2, aboutLee.GetProperty("reportsAboutReported").GetInt32());
        var id = aboutLee.GetProperty("id").GetInt64();

        Assert.Equal(HttpStatusCode.NoContent, (await mod.PostAsync($"/api/v1/admin/reports/{id}/done", null)).StatusCode);
        Assert.Equal("report_done", await (await mod.PostAsync($"/api/v1/admin/reports/{id}/done", null)).ProblemCode());
        Assert.Equal("report_not_found", await (await mod.PostAsync("/api/v1/admin/reports/999999/done", null)).ProblemCode());
        Assert.Equal(2, (await (await mod.GetAsync("/api/v1/admin/reports?state=open")).Json()).GetProperty("total").GetInt32());
        var done = (await (await mod.GetAsync("/api/v1/admin/reports?state=done")).Json()).GetProperty("reports")[0];
        Assert.Equal(("done", "Moddy"), (done.GetProperty("state").GetString(), done.GetProperty("handledBy").GetString()));
        Assert.Equal(3, (await (await mod.GetAsync("/api/v1/admin/reports?state=all")).Json()).GetProperty("total").GetInt32());
        Assert.Equal(HttpStatusCode.NoContent, (await mod.PostAsync($"/api/v1/admin/reports/{id}/reopen", null)).StatusCode);
        Assert.Equal("report_open", await (await mod.PostAsync($"/api/v1/admin/reports/{id}/reopen", null)).ProblemCode());
        // A report about the moderator himself: another moderator handles it (403 own_report, nothing audited).
        Assert.Equal(HttpStatusCode.OK, (await ReportAsync(kim, "Moddy", "other")).StatusCode);
        var aboutMod = (await (await mod.GetAsync("/api/v1/admin/reports")).Json()).GetProperty("reports")[0];
        Assert.Equal("Moddy", aboutMod.GetProperty("reported").GetString());
        var ownDone = await mod.PostAsync($"/api/v1/admin/reports/{aboutMod.GetProperty("id").GetInt64()}/done", null);
        Assert.Equal((HttpStatusCode.Forbidden, "own_report"), (ownDone.StatusCode, await ownDone.ProblemCode()));

        await using var check = await host.SqlAsync();
        var audits = (await check.QueryAsync<(string Event, long Account, string Detail)>(
            "SELECT event, CAST(account_id AS SIGNED), detail FROM audit_log WHERE event LIKE 'report%' ORDER BY id")).ToList();
        Assert.Equal(["report_handled", "report_reopened"], audits.Select(a => a.Event));
        Assert.All(audits, a => Assert.Equal(leeId, a.Account));
        Assert.Contains($"\"reportId\":{id}", audits[0].Detail);
        Assert.Equal(2, await check.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM v_staff_audit WHERE staff = 'Moddy' AND event LIKE 'report%'"));
    }

    [DbFact]
    public async Task The_website_shows_staff_the_reports_with_done_and_the_account_page_the_reports_about_a_player()
    {
        await using var host = await TestHost.StartAsync();
        var (kimToken, _, _) = await host.RegisterAsync("Kim");
        await host.RegisterAsync("Lee");
        Assert.Equal(HttpStatusCode.OK, (await ReportAsync(host.Client(kimToken), "Lee", "cheating")).StatusCode);
        using var boss = new WebBrowser(host);
        await boss.RegisterAsync("Boss");
        Assert.Equal(HttpStatusCode.Forbidden, (await boss.GetAsync("/admin/reports")).Status);
        await using (var sql = await host.SqlAsync())
            await sql.ExecuteAsync("UPDATE accounts SET role = 'moderator' WHERE username = 'Boss'");

        Assert.Contains("1 open report.", (await boss.GetAsync("/admin")).Html);
        var list = await boss.GetAsync("/admin/reports");
        Assert.Contains("/admin/account/Lee", list.Html);
        Assert.Contains("Cheating", list.Html);
        await using var sqlId = await host.SqlAsync();
        var id = await sqlId.ExecuteScalarAsync<long>("SELECT CAST(id AS SIGNED) FROM player_reports");
        Assert.Contains($"Report {id} is done.", (await boss.PostAsync("/admin/reports?handler=Done", new() { ["Id"] = id.ToString() }, "/admin/reports")).Html);
        Assert.Contains("No open reports.", (await boss.GetAsync("/admin/reports")).Html);
        Assert.Contains("No open reports.", (await boss.GetAsync("/admin")).Html);
        var account = await boss.GetAsync("/admin/account/Lee");
        Assert.Contains("Reports about this player (1)", account.Html);
        Assert.Contains("done (Boss", account.Html);
        Assert.Contains($"Report {id} is open again.",
            (await boss.PostAsync("/admin/reports?handler=Reopen", new() { ["Id"] = id.ToString() }, "/admin/reports?State=done")).Html);
        // A report about Boss himself: no Done for him; a forged post is refused.
        Assert.Equal(HttpStatusCode.OK, (await ReportAsync(host.Client(kimToken), "Boss", "name")).StatusCode);
        var mine = await sqlId.ExecuteScalarAsync<long>("SELECT CAST(MAX(id) AS SIGNED) FROM player_reports");
        Assert.Contains("About you: another moderator handles it.", (await boss.GetAsync("/admin/reports")).Html);
        Assert.Contains("another moderator handles it",
            (await boss.PostAsync("/admin/reports?handler=Done", new() { ["Id"] = mine.ToString() }, "/admin/reports")).Html);
        Assert.Equal("open", await sqlId.ExecuteScalarAsync<string>("SELECT state FROM player_reports WHERE id = @mine", new { mine }));
    }
}
