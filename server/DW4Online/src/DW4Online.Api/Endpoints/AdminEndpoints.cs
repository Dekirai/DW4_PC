using DW4Online.Api.Auth;
using DW4Online.Domain.Services;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;

namespace DW4Online.Api.Endpoints;

public sealed record StatusRequest(string? Status, string? Reason);
public sealed record RoleRequest(string? Role);
public sealed record FlagRequest(string? Ruleset, bool Flagged);

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

        // A locked or banned account also leaves its room at once.
        admin.MapPost("/accounts/{username}/status", async (string username, StatusRequest r, AccountService accounts, RoomService rooms, HttpContext ctx) =>
        {
            var result = await accounts.AdminSetStatusAsync(ctx.Session(), username, r.Status ?? "", r.Reason, ctx.Caller(), ctx.RequestAborted);
            if (result.Ok && result.Value!.Status != Domain.Abstractions.AccountStatus.Active)
                await rooms.RemoveAccountAsync(result.Value.Id, ctx.RequestAborted);
            return ApiProblems.Ok(result);
        });

        // GDPR erasure on request (admins only): the account leaves its room, then everything personal is deleted.
        admin.MapDelete("/accounts/{username}", async (string username, AccountService accounts, RoomService rooms, HttpContext ctx) =>
        {
            if (await accounts.FindAccountAsync(username, ctx.RequestAborted) is not { } account) return NoAccount();
            await rooms.RemoveAccountAsync(account.Id, ctx.RequestAborted);
            return ApiProblems.Ok(await accounts.AdminDeleteAccountAsync(ctx.Session(), username, ctx.RequestAborted), _ => Results.NoContent());
        }).RequireAuthorization(Policies.Admin);

        // Flag (hide from rankings until reviewed) or clear the flag of one unlock; ruleset standard (default) or randomizer.
        admin.MapPost("/accounts/{username}/achievements/{code}/flag", async (string username, string code, FlagRequest r,
            AccountService accounts, AchievementService achievements, HttpContext ctx) =>
        {
            if (await accounts.FindAccountAsync(username, ctx.RequestAborted) is not { } account) return NoAccount();
            return ApiProblems.Ok(await achievements.AdminSetFlagAsync(ctx.Session().AccountId, account.Id, code, r.Ruleset, r.Flagged,
                ctx.IpBytes(), ctx.RequestAborted), _ => Results.NoContent());
        });

        admin.MapDelete("/accounts/{username}/achievements/{code}", async (string username, string code, string? ruleset,
            AccountService accounts, AchievementService achievements, HttpContext ctx) =>
        {
            if (await accounts.FindAccountAsync(username, ctx.RequestAborted) is not { } account) return NoAccount();
            return ApiProblems.Ok(await achievements.AdminRevokeAsync(ctx.Session().AccountId, account.Id, code, ruleset,
                ctx.IpBytes(), ctx.RequestAborted), _ => Results.NoContent());
        });

        admin.MapPost("/rooms/{code}/close", async (string code, RoomService rooms, HttpContext ctx) =>
            ApiProblems.Ok(await rooms.CloseAsync(ctx.Session().AccountId, true, code, ctx.IpBytes(), ctx.RequestAborted), _ => Results.NoContent()));

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
