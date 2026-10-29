using DW4Online.Api.Auth;
using DW4Online.Domain.Services;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;

namespace DW4Online.Api.Endpoints;

public sealed record EmailRequest(string? CurrentPassword, string? Email);

public static class AccountEndpoints
{
    public static void Map(RouteGroupBuilder v1)
    {
        var me = v1.MapGroup("/me").RequireAuthorization(Policies.Player);

        me.MapGet("", async (AccountService accounts, HttpContext ctx) =>
            await accounts.GetAccountAsync(ctx.Session().AccountId, ctx.RequestAborted) is { } account
                ? Results.Ok(account)
                : ApiProblems.Of(StatusCodes.Status401Unauthorized, "unauthorized", "Not logged in."));

        me.MapPut("/email", async (EmailRequest r, AccountService accounts, HttpContext ctx) =>
            ApiProblems.Ok(await accounts.SetEmailAsync(ctx.Session(), r.CurrentPassword, r.Email, ctx.Caller(), ctx.RequestAborted)))
            .RequireRateLimiting(RateLimitPolicies.AccountChange);

        me.MapGet("/sessions", async (AccountService accounts, HttpContext ctx) =>
        {
            var session = ctx.Session();
            var list = await accounts.ListSessionsAsync(session.AccountId, ctx.RequestAborted);
            return Results.Ok(list.Select(s => new
            {
                s.Id,
                s.Kind,
                s.DeviceName,
                s.CreatedAt,
                s.LastUsedAt,
                s.ExpiresAt,
                current = s.Id == session.SessionId,
            }));
        });

        me.MapDelete("/sessions/{id:long}", async (long id, AccountService accounts, HttpContext ctx) =>
            await accounts.RevokeSessionAsync(ctx.Session().AccountId, id, ctx.RequestAborted)
                ? Results.NoContent()
                : ApiProblems.Of(StatusCodes.Status404NotFound, "session_not_found", "No such active session."));
    }
}
