using DW4Online.Api.Auth;
using DW4Online.Domain.Services;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;

namespace DW4Online.Api.Endpoints;

public sealed record ReportRequest(string? Username, string? Reason);

/// <summary>
/// Report player (round I1b, SEAMLESS.md 19.16, 19.19): POST /reports {username, reason} for players (the game's player card
/// goes through the relay's REPORT into the same service), and the staff list with Done and Reopen (audited). The reasons are
/// a fixed list (griefing, cheating, name, other); there is no free text. Players are limited by Reports:PerDay (429
/// report_limit) and RateLimits:ReportsPerMinute.
/// </summary>
public static class ReportEndpoints
{
    public static void Map(RouteGroupBuilder v1)
    {
        v1.MapGroup("/reports").RequireAuthorization(Policies.Player).RequirePasswordCurrent()
            .MapPost("", async (ReportRequest r, ReportService reports, HttpContext ctx) =>
                ApiProblems.Ok(await reports.ReportByNameAsync(ctx.Session().AccountId, r.Username, r.Reason, ctx.RequestAborted),
                    o => Results.Ok(new { result = o.Result, message = o.Message })))
            .RequireRateLimiting(RateLimitPolicies.Reports);

        var admin = v1.MapGroup("/admin/reports").RequireAuthorization(Policies.Staff).RequirePasswordCurrent()
            .RequireRateLimiting(RateLimitPolicies.Staff);

        // state: open (default), done or all; page from 1.
        admin.MapGet("", async (string? state, int? page, ReportService reports, HttpContext ctx) =>
        {
            var p = await reports.ListAsync(state is null ? "open" : state == "all" ? null : state, page ?? 1, ctx.RequestAborted);
            return Results.Ok(new
            {
                reports = p.Reports.Select(x => new
                {
                    x.Id,
                    reporter = x.Reporter,
                    reporterId = x.ReporterAccountId,
                    reported = x.Reported,
                    reportedId = x.ReportedAccountId,
                    x.Reason,
                    x.Source,
                    x.InstanceId,
                    x.InstanceHost,
                    x.InstanceStage,
                    x.InstanceStarted,
                    x.CreatedAt,
                    x.State,
                    handledBy = x.HandledByName,
                    x.HandledAt,
                    reportsAboutReported = x.OpenAboutReported,
                }),
                total = p.Total,
                page = p.Page,
                pageSize = p.PageSize,
            });
        });

        admin.MapPost("/{id:long}/done", async (long id, ReportService reports, HttpContext ctx) =>
            ApiProblems.Ok(await reports.DoneAsync(id, ctx.Session().AccountId, ctx.IpBytes(), ctx.RequestAborted), _ => Results.NoContent()));

        admin.MapPost("/{id:long}/reopen", async (long id, ReportService reports, HttpContext ctx) =>
            ApiProblems.Ok(await reports.ReopenAsync(id, ctx.Session().AccountId, ctx.IpBytes(), ctx.RequestAborted), _ => Results.NoContent()));
    }
}
