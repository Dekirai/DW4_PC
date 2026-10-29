using DW4Online.Api.Auth;
using DW4Online.Domain.Achievements;
using DW4Online.Domain.Services;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;

namespace DW4Online.Api.Endpoints;

public sealed record UnlockRequest(string? Code, PlayContext? Context, UnlockEvidence? Evidence);
public sealed record StatsRequest(Dictionary<string, long>? Deltas, PlayContext? Context);

public static class AchievementEndpoints
{
    public static void Map(RouteGroupBuilder v1)
    {
        // Public list; with a token it also carries the caller's unlocks and progress.
        v1.MapGet("/achievements", async (AchievementService achievements, HttpContext ctx) =>
            Results.Ok(await achievements.ListAsync(ctx.SessionOrNull()?.AccountId, ctx.RequestAborted))).AllowAnonymous();

        var group = v1.MapGroup("").RequireAuthorization(Policies.Player).RequirePasswordCurrent();

        group.MapPost("/achievements/unlock", async (UnlockRequest r, AchievementService achievements, HttpContext ctx) =>
            ApiProblems.Ok(await achievements.UnlockAsync(ctx.Session().AccountId, r.Code, r.Context, r.Evidence, ctx.RequestAborted)))
            .RequireRateLimiting(RateLimitPolicies.Achievements);

        // Counter deltas, batched by the game about once a minute.
        group.MapPost("/stats", async (StatsRequest r, AchievementService achievements, HttpContext ctx) =>
            ApiProblems.Ok(await achievements.AddStatsAsync(ctx.Session().AccountId, r.Deltas, r.Context, ctx.IpBytes(), ctx.RequestAborted)))
            .RequireRateLimiting(RateLimitPolicies.Achievements);
    }
}
