using DW4Online.Api.Auth;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Services;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;

namespace DW4Online.Api.Endpoints;

public sealed record RegisterRequest(string? Username, string? Password, string? Email, string? DeviceName, string? Website);
public sealed record LoginRequest(string? Username, string? Password, string? DeviceName, string? ClientVersion, int? Protocol);
public sealed record ChangePasswordRequest(string? CurrentPassword, string? NewPassword);
public sealed record RecoverRequest(string? Username, string? RecoveryCode, string? NewPassword);
public sealed record PasswordConfirmation(string? CurrentPassword);

public static class AuthEndpoints
{
    public static void Map(RouteGroupBuilder v1)
    {
        var auth = v1.MapGroup("/auth");

        // Registration also logs in (a game session) and returns the one-time recovery code: show it once.
        auth.MapPost("/register", async (RegisterRequest r, AccountService accounts, HttpContext ctx) =>
        {
            if (!string.IsNullOrEmpty(r.Website)) // honeypot field the game and the website never fill
                return ApiProblems.Of(StatusCodes.Status400BadRequest, "invalid_request", "The request could not be processed.");
            var result = await accounts.RegisterAsync(r.Username, r.Password, r.Email, SessionKind.Game, ctx.Caller(r.DeviceName), ctx.RequestAborted);
            return ApiProblems.Ok(result, v => Results.Created(ctx.Request.PathBase + "/api/v1/me", new
            {
                account = v.Account,
                recoveryCode = v.RecoveryCode,
                token = v.Session.Token,
                expiresAt = v.Session.ExpiresAt,
            }));
        }).AllowAnonymous().RequireRateLimiting(RateLimitPolicies.Register);

        auth.MapPost("/login", async (LoginRequest r, AccountService accounts, VersionGate versions, HttpContext ctx) =>
        {
            if ((r.Protocol is not null || r.ClientVersion is not null) && versions.Check(r.Protocol, r.ClientVersion) is { } outdated)
                return ApiProblems.From(outdated);
            var result = await accounts.LoginAsync(r.Username, r.Password, SessionKind.Game, ctx.Caller(r.DeviceName), ctx.RequestAborted);
            return ApiProblems.Ok(result, v => Results.Ok(new { token = v.Session.Token, expiresAt = v.Session.ExpiresAt, account = v.Account }));
        }).AllowAnonymous().RequireRateLimiting(RateLimitPolicies.Login);

        auth.MapPost("/recover", async (RecoverRequest r, AccountService accounts, HttpContext ctx) =>
        {
            var result = await accounts.RecoverAsync(r.Username, r.RecoveryCode, r.NewPassword, ctx.Caller(), ctx.RequestAborted);
            return ApiProblems.Ok(result, code => Results.Ok(new { recoveryCode = code }));
        }).AllowAnonymous().RequireRateLimiting(RateLimitPolicies.Login);

        auth.MapPost("/logout", async (AccountService accounts, HttpContext ctx) =>
        {
            await accounts.LogoutAsync(ctx.Session(), ctx.RequestAborted);
            return Results.NoContent();
        }).RequireAuthorization(Policies.Player);

        auth.MapPost("/logout-all", async (AccountService accounts, HttpContext ctx) =>
        {
            var revoked = await accounts.LogoutEverywhereAsync(ctx.Session(), ctx.Caller(), ctx.RequestAborted);
            return Results.Ok(new { revoked });
        }).RequireAuthorization(Policies.Player);

        // Every other session of the account ends; this one stays.
        auth.MapPost("/password", async (ChangePasswordRequest r, AccountService accounts, HttpContext ctx) =>
        {
            var result = await accounts.ChangePasswordAsync(ctx.Session(), r.CurrentPassword, r.NewPassword, ctx.Caller(), ctx.RequestAborted);
            return ApiProblems.Ok(result, _ => Results.NoContent());
        }).RequireAuthorization(Policies.Player).RequireRateLimiting(RateLimitPolicies.AccountChange);

        auth.MapPost("/recovery-code", async (PasswordConfirmation r, AccountService accounts, HttpContext ctx) =>
        {
            var result = await accounts.RegenerateRecoveryCodeAsync(ctx.Session(), r.CurrentPassword, ctx.Caller(), ctx.RequestAborted);
            return ApiProblems.Ok(result, code => Results.Ok(new { recoveryCode = code }));
        }).RequireAuthorization(Policies.Player).RequireRateLimiting(RateLimitPolicies.AccountChange);
    }
}
