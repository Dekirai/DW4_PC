using DW4Online.Api.Auth;
using DW4Online.Domain.Services;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;

namespace DW4Online.Api.Endpoints;

public sealed record EmailRequest(string? CurrentPassword, string? Email);
public sealed record ProfileRequest(bool Public);

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

        me.MapPut("/profile", async (ProfileRequest r, AccountService accounts, HttpContext ctx) =>
            ApiProblems.Ok(await accounts.SetProfilePublicAsync(ctx.Session(), r.Public, ctx.Caller(), ctx.RequestAborted)))
            .RequireRateLimiting(RateLimitPolicies.AccountChange);

        // Deletes the account and everything personal tied to it (GDPR erasure); needs the current password.
        me.MapPost("/delete", async (PasswordConfirmation r, AccountService accounts, RoomService rooms, HttpContext ctx) =>
        {
            var session = ctx.Session();
            if (!await accounts.CheckPasswordAsync(session.AccountId, r.CurrentPassword, ctx.RequestAborted))
                return ApiProblems.Of(StatusCodes.Status422UnprocessableEntity, "wrong_password", "The current password is wrong.");
            if (session.Role == Domain.Abstractions.Roles.Admin)
                return ApiProblems.Of(StatusCodes.Status403Forbidden, "admin_account", "Admins cannot delete their own account; remove the admin role first.");
            await rooms.RemoveAccountAsync(session.AccountId, ctx.RequestAborted);
            return ApiProblems.Ok(await accounts.DeleteAccountAsync(session, r.CurrentPassword, ctx.RequestAborted), _ => Results.NoContent());
        }).RequireRateLimiting(RateLimitPolicies.AccountChange);

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
