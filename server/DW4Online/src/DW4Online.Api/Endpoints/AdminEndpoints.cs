using DW4Online.Api.Auth;
using DW4Online.Domain.Services;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;

namespace DW4Online.Api.Endpoints;

public sealed record StatusRequest(string? Status, string? Reason);
public sealed record RoleRequest(string? Role);

/// <summary>Staff actions (moderators and admins). Every action is written to the audit log.</summary>
public static class AdminEndpoints
{
    public static void Map(RouteGroupBuilder v1)
    {
        var admin = v1.MapGroup("/admin").RequireAuthorization(Policies.Staff).RequirePasswordCurrent()
            .RequireRateLimiting(RateLimitPolicies.Staff);

        admin.MapGet("/accounts", async (string? query, int? limit, AccountService accounts, HttpContext ctx) =>
            Results.Ok(await accounts.SearchAsync(query, limit ?? 50, ctx.RequestAborted)));

        // Returns a temporary password once; the user must change it at the next login. All sessions end.
        admin.MapPost("/accounts/{username}/reset-password", async (string username, AccountService accounts, HttpContext ctx) =>
            ApiProblems.Ok(await accounts.AdminResetPasswordAsync(ctx.Session(), username, ctx.Caller(), ctx.RequestAborted),
                temporary => Results.Ok(new { temporaryPassword = temporary })));

        admin.MapPost("/accounts/{username}/status", async (string username, StatusRequest r, AccountService accounts, HttpContext ctx) =>
            ApiProblems.Ok(await accounts.AdminSetStatusAsync(ctx.Session(), username, r.Status ?? "", r.Reason, ctx.Caller(), ctx.RequestAborted)));

        admin.MapPost("/accounts/{username}/role", async (string username, RoleRequest r, AccountService accounts, HttpContext ctx) =>
            ApiProblems.Ok(await accounts.AdminSetRoleAsync(ctx.Session(), username, r.Role ?? "", ctx.Caller(), ctx.RequestAborted)))
            .RequireAuthorization(Policies.Admin);

        admin.MapGet("/accounts/{username}/online-character/history", async (string username, AccountService accounts,
            OnlineCharacterService characters, HttpContext ctx) =>
            await accounts.FindAccountAsync(username, ctx.RequestAborted) is { } account
                ? Results.Ok(await characters.HistoryAsync(account.Id, ctx.RequestAborted))
                : NoAccount());

        admin.MapPost("/accounts/{username}/online-character/restore/{revision:int}", async (string username, int revision,
            AccountService accounts, OnlineCharacterService characters, HttpContext ctx) =>
        {
            if (await accounts.FindAccountAsync(username, ctx.RequestAborted) is not { } account) return NoAccount();
            return ApiProblems.Ok(await characters.RestoreAsync(account.Id, revision, ctx.Session().AccountId, ctx.IpBytes(), ctx.RequestAborted),
                rev => Results.Ok(new { revision = rev }));
        });

        admin.MapPost("/accounts/{username}/online-character/reset", async (string username, AccountService accounts,
            OnlineCharacterService characters, HttpContext ctx) =>
        {
            if (await accounts.FindAccountAsync(username, ctx.RequestAborted) is not { } account) return NoAccount();
            return await characters.ResetAsync(account.Id, ctx.Session().AccountId, ctx.IpBytes(), ctx.RequestAborted)
                ? Results.NoContent()
                : ApiProblems.Of(StatusCodes.Status404NotFound, "no_online_character", "There is no online character.");
        });
    }

    private static IResult NoAccount() => ApiProblems.Of(StatusCodes.Status404NotFound, "account_not_found", "No such account.");
}
