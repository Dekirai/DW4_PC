using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
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
        // retryAfterSeconds: when the oldest report of the 24 hours leaves them (round I1b's integration review: it said 3600 while
        // the count runs over 24 hours, so a client that kept to it was refused for up to a day).
        static async Task<(HttpStatusCode Status, string Code, int RetryAfter)> Refused(HttpResponseMessage r)
        {
            var body = await r.Json();
            return (r.StatusCode, body.GetProperty("code").GetString()!, body.GetProperty("retryAfterSeconds").GetInt32());
        }
        var limit = await Refused(await ReportAsync(kim, "Ann", "other"));
        Assert.Equal((HttpStatusCode.TooManyRequests, "report_limit"), (limit.Status, limit.Code));
        Assert.InRange(limit.RetryAfter, 86_400 - 5, 86_400);
        // At the limit the service answers without the table for ReportService.LimitKnownMinutes: a repeat about Lee (which the
        // table would answer "sent") is "limit" now, with the same time; ten minutes later the table speaks again.
        var known = await Refused(await ReportAsync(kim, "Lee", "griefing"));
        Assert.Equal("report_limit", known.Code);
        Assert.InRange(known.RetryAfter, 86_400 - 5, 86_400);
        host.Clock.Advance(TimeSpan.FromMinutes(DW4Online.Domain.Services.ReportService.LimitKnownMinutes + 1));
        Assert.Equal(HttpStatusCode.OK, (await ReportAsync(kim, "Lee", "griefing")).StatusCode);
        var later = await Refused(await ReportAsync(kim, "Ann", "other"));
        Assert.Equal("report_limit", later.Code);
        var left = 86_400 - (DW4Online.Domain.Services.ReportService.LimitKnownMinutes + 1) * 60;
        Assert.InRange(later.RetryAfter, left - 5, left);
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
        var (kimToken, _, kimId) = await host.RegisterAsync("Kim");
        var (_, _, leeId) = await host.RegisterAsync("Lee");
        var (annToken, _, _) = await host.RegisterAsync("Ann");
        var (_, _, moddyId) = await host.RegisterAsync("Moddy");
        await host.RegisterAsync("Mona");
        await using (var sql = await host.SqlAsync())
            await sql.ExecuteAsync("UPDATE accounts SET role = 'moderator' WHERE username IN ('Moddy', 'Mona')");
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
        // A report about the moderator himself (from an instance Kim hosted and Moddy played in): another moderator handles it (403
        // own_report, nothing audited), and Moddy never learns who reported him: no reporter, no shared instance (its host is Kim).
        var history = host.App.Services.GetRequiredService<IInstanceHistoryRepository>();
        var now = host.Clock.GetUtcNow().UtcDateTime;
        var shared = await history.StartAsync(new InstanceHistoryStart(kimId, 1, "stag0101", "public", 0, 3, "s", now.AddHours(-1)));
        await history.MemberJoinedAsync(shared, kimId, 1, "host", now.AddHours(-1));
        await history.MemberJoinedAsync(shared, moddyId, 1, "guest", now.AddHours(-1));
        Assert.Equal(HttpStatusCode.OK, (await ReportAsync(kim, "Moddy", "other")).StatusCode);
        var aboutMod = (await (await mod.GetAsync("/api/v1/admin/reports")).Json()).GetProperty("reports")[0];
        Assert.Equal("Moddy", aboutMod.GetProperty("reported").GetString());
        Assert.All(new[] { "reporter", "reporterId", "instanceId", "instanceHost", "instanceStage", "instanceStarted" },
            p => Assert.Equal(JsonValueKind.Null, aboutMod.GetProperty(p).ValueKind));
        var ownDone = await mod.PostAsync($"/api/v1/admin/reports/{aboutMod.GetProperty("id").GetInt64()}/done", null);
        Assert.Equal((HttpStatusCode.Forbidden, "own_report"), (ownDone.StatusCode, await ownDone.ProblemCode()));
        // Another moderator sees all of it (and Moddy the reporters of the reports about others, as before).
        var mona = host.Client((await host.LoginAsync("Mona")).Token);
        var seen = (await (await mona.GetAsync("/api/v1/admin/reports")).Json()).GetProperty("reports")[0];
        Assert.Equal(("Moddy", "Kim", kimId, shared, "Kim"), (seen.GetProperty("reported").GetString(), seen.GetProperty("reporter").GetString(),
            seen.GetProperty("reporterId").GetInt64(), seen.GetProperty("instanceId").GetInt64(), seen.GetProperty("instanceHost").GetString()));
        var aboutAnn = (await (await mod.GetAsync("/api/v1/admin/reports")).Json()).GetProperty("reports")[1];
        Assert.Equal(("Ann", "Kim"), (aboutAnn.GetProperty("reported").GetString(), aboutAnn.GetProperty("reporter").GetString()));

        await using var check = await host.SqlAsync();
        var audits = (await check.QueryAsync<(string Event, long Account, string Detail)>(
            "SELECT event, CAST(account_id AS SIGNED), detail FROM audit_log WHERE event LIKE 'report%' ORDER BY id")).ToList();
        Assert.Equal(["report_handled", "report_reopened"], audits.Select(a => a.Event));
        Assert.All(audits, a => Assert.Equal(leeId, a.Account));
        Assert.Contains($"\"reportId\":{id}", audits[0].Detail);
        // (Never the reporter: an admin reads every audit row with its detail, 19.16.)
        Assert.All(audits, a => Assert.DoesNotContain("reporter", a.Detail, StringComparison.OrdinalIgnoreCase));
        // (v_staff_audit is 0013's view: report_handled is in it, report_reopened only in audit_log and the website's list; 0014 could
        // not replace the view without the right DROP.)
        Assert.Equal(1, await check.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM v_staff_audit WHERE staff = 'Moddy' AND event LIKE 'report%'"));
    }

    /// <summary>A reported admin reads the whole staff audit (every row with its detail, also the ones about his own account): the
    /// report_handled row of a report about him never names who reported him (19.16; round I1b's integration review: its detail had
    /// reporterId, which the account search turns into a name).</summary>
    [DbFact]
    public async Task A_reported_admin_never_learns_his_reporter_from_the_staff_audit()
    {
        await using var host = await TestHost.StartAsync();
        var (kimToken, _, kimId) = await host.RegisterAsync("Kim");
        await host.RegisterAsync("Boss");
        await host.RegisterAsync("Moddy");
        await using (var sql = await host.SqlAsync())
        {
            await sql.ExecuteAsync("UPDATE accounts SET role = 'admin' WHERE username = 'Boss'");
            await sql.ExecuteAsync("UPDATE accounts SET role = 'moderator' WHERE username = 'Moddy'");
        }
        Assert.Equal(HttpStatusCode.OK, (await ReportAsync(host.Client(kimToken), "Boss", "griefing")).StatusCode);
        var mod = host.Client((await host.LoginAsync("Moddy")).Token);
        var report = (await (await mod.GetAsync("/api/v1/admin/reports")).Json()).GetProperty("reports")[0];
        Assert.Equal(("Boss", "Kim"), (report.GetProperty("reported").GetString(), report.GetProperty("reporter").GetString()));
        Assert.Equal(HttpStatusCode.NoContent, (await mod.PostAsync($"/api/v1/admin/reports/{report.GetProperty("id").GetInt64()}/done", null)).StatusCode);

        var boss = host.Client((await host.LoginAsync("Boss")).Token);
        var rows = (await (await boss.GetAsync("/api/v1/admin/audit")).Json()).GetProperty("rows").EnumerateArray().ToList();
        var handled = Assert.Single(rows, r => r.GetProperty("event").GetString() == "report_handled");
        Assert.Equal("Boss", handled.GetProperty("player").GetString());
        var detail = handled.GetProperty("detail").GetString() ?? "";
        Assert.DoesNotContain("reporter", detail, StringComparison.OrdinalIgnoreCase);
        using (var parsed = JsonDocument.Parse(detail))
        {
            Assert.Equal(["reason", "reportId", "reportedId"], parsed.RootElement.EnumerateObject().Select(p => p.Name).Order(StringComparer.Ordinal));
            Assert.DoesNotContain(parsed.RootElement.EnumerateObject(), p => p.Value.ValueKind == JsonValueKind.Number && p.Value.GetInt64() == kimId &&
                                                                           p.Name != "reportId");
        }
        Assert.All(rows, r => Assert.NotEqual("Kim", r.GetProperty("player").GetString()));
        using var web = new WebBrowser(host);
        await web.LoginAsync("Boss");
        var page = (await web.GetAsync("/admin/audit")).Html;
        Assert.Contains("report_handled", page);
        Assert.DoesNotContain("reporterId", page);
    }

    /// <summary>Done reports go Reports:DoneDays after they were handled (the hourly housekeeping; the privacy page names the period);
    /// open ones stay until staff handle them.</summary>
    [DbFact]
    public async Task Done_reports_go_after_reports_done_days_and_open_ones_stay()
    {
        await using var host = await TestHost.StartAsync(new() { ["Reports:DoneDays"] = "30" });
        var (kimToken, _, _) = await host.RegisterAsync("Kim");
        await host.RegisterAsync("Lee");
        await host.RegisterAsync("Max");
        await host.RegisterAsync("Moddy");
        await using (var sql = await host.SqlAsync())
            await sql.ExecuteAsync("UPDATE accounts SET role = 'moderator' WHERE username = 'Moddy'");
        var kim = host.Client(kimToken);
        Assert.Equal(HttpStatusCode.OK, (await ReportAsync(kim, "Lee", "griefing")).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await ReportAsync(kim, "Max", "name")).StatusCode);
        var mod = host.Client((await host.LoginAsync("Moddy")).Token);
        var aboutLee = (await (await mod.GetAsync("/api/v1/admin/reports")).Json()).GetProperty("reports").EnumerateArray()
            .Single(r => r.GetProperty("reported").GetString() == "Lee");
        Assert.Equal(HttpStatusCode.NoContent, (await mod.PostAsync($"/api/v1/admin/reports/{aboutLee.GetProperty("id").GetInt64()}/done", null)).StatusCode);
        var service = host.App.Services.GetRequiredService<DW4Online.Domain.Services.ReportService>();
        host.Clock.Advance(TimeSpan.FromDays(29));
        Assert.Equal(0, await service.PruneDoneAsync());
        host.Clock.Advance(TimeSpan.FromDays(2));
        Assert.Equal(1, await service.PruneDoneAsync());
        await using var check = await host.SqlAsync();
        Assert.Equal(["Max"], await check.QueryAsync<string>("SELECT reported FROM v_player_reports"));
        Assert.Equal(0, await service.PruneDoneAsync());
        // The privacy page says what a report keeps, who sees it and this period (round I1b's integration review).
        using var web = new WebBrowser(host);
        var privacy = System.Text.RegularExpressions.Regex.Replace(System.Net.WebUtility.HtmlDecode((await web.GetAsync("/privacy")).Html), @"\s+", " ");
        Assert.Contains("Reports: who reported whom", privacy);
        Assert.Contains("the reported player never sees who reported him", privacy);
        Assert.Contains("Until 30 days after a moderator handled it", privacy);
        Assert.Contains("the user names, levels and Digimon of up to four players in it", privacy);
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
        // A report about Boss himself: no Done for him, nor who reported him (here and on his own account page: "a player"); a forged
        // post is refused.
        Assert.Equal(HttpStatusCode.OK, (await ReportAsync(host.Client(kimToken), "Boss", "name")).StatusCode);
        var mine = await sqlId.ExecuteScalarAsync<long>("SELECT CAST(MAX(id) AS SIGNED) FROM player_reports");
        var withMine = (await boss.GetAsync("/admin/reports")).Html;
        Assert.Contains("About you: another moderator handles it.", withMine);
        Assert.Contains("<span class=\"muted\">a player</span>", withMine);
        Assert.Single(System.Text.RegularExpressions.Regex.Matches(withMine, "/admin/account/Kim\"")); // (only the report about Lee names Kim)
        var own = (await boss.GetAsync("/admin/account/Boss")).Html;
        Assert.Contains("Reports about this player (1)", own);
        Assert.Contains("<span class=\"muted\">a player</span>", own);
        Assert.DoesNotContain("/admin/account/Kim", own);
        Assert.Contains("another moderator handles it",
            (await boss.PostAsync("/admin/reports?handler=Done", new() { ["Id"] = mine.ToString() }, "/admin/reports")).Html);
        Assert.Equal("open", await sqlId.ExecuteScalarAsync<string>("SELECT state FROM player_reports WHERE id = @mine", new { mine }));
    }
}
