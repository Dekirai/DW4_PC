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

        // Admins only: returns a temporary password once; the user must change it at the next login. All sessions end.
        admin.MapPost("/accounts/{username}/reset-password", async (string username, AccountService accounts, HttpContext ctx) =>
            ApiProblems.Ok(await accounts.AdminResetPasswordAsync(ctx.Session(), username, ctx.Caller(), ctx.RequestAborted),
                temporary => Results.Ok(new { temporaryPassword = temporary })))
            .RequireAuthorization(Policies.Admin);

        // A locked or banned account also leaves the world at once (the relay re-checks it: IWorldNotifier.AccountChanged).
        admin.MapPost("/accounts/{username}/status", async (string username, StatusRequest r, AccountService accounts, WorldService world, HttpContext ctx) =>
        {
            var result = await accounts.AdminSetStatusAsync(ctx.Session(), username, r.Status ?? "", r.Reason, ctx.Caller(), ctx.RequestAborted);
            if (result.Ok) world.AccountChanged(result.Value!.Id);
            return ApiProblems.Ok(result);
        });

        // GDPR erasure on request (admins only): the account leaves the world, then everything personal is deleted.
        admin.MapDelete("/accounts/{username}", async (string username, AccountService accounts, WorldService world, HttpContext ctx) =>
        {
            if (await accounts.FindAccountAsync(username, ctx.RequestAborted) is not { } account) return NoAccount();
            world.AccountChanged(account.Id);
            var deleted = await accounts.AdminDeleteAccountAsync(ctx.Session(), username, ctx.RequestAborted);
            if (deleted.Ok) world.AccountChanged(account.Id); // (a world session that started meanwhile ends)
            return ApiProblems.Ok(deleted, _ => Results.NoContent());
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

        // The world's instances (SEAMLESS.md 19.19): every one, Private included, with members and spectators; End and
        // Remove are audited (staff_end_instance, staff_remove). The id is the relay's run-local wid.
        admin.MapGet("/instances", (WorldService world) => Results.Ok(new { instances = world.AdminInstances().Select(WorldEndpoints.AdminRow) }));

        admin.MapPost("/instances/{wid:long}/end", async (long wid, WorldService world, HttpContext ctx) =>
            wid is < 1 or > uint.MaxValue
                ? ApiProblems.Of(StatusCodes.Status404NotFound, "instance_not_found", "No such instance (it may have ended).")
                : ApiProblems.Ok(await world.EndInstanceAsync(ctx.Session().AccountId, (uint)wid, ctx.IpBytes(), ctx.RequestAborted), _ => Results.NoContent()));

        admin.MapPost("/instances/{wid:long}/remove", async (long wid, UsernameRequest r, WorldService world, HttpContext ctx) =>
            wid is < 1 or > uint.MaxValue
                ? ApiProblems.Of(StatusCodes.Status404NotFound, "instance_not_found", "No such instance (it may have ended).")
                : ApiProblems.Ok(await world.RemoveMemberAsync(ctx.Session().AccountId, (uint)wid, r.Username, ctx.IpBytes(), ctx.RequestAborted),
                    _ => Results.NoContent()));

        // A demotion counts in the world at once (the relay re-reads the role, 19.14).
        admin.MapPost("/accounts/{username}/role", async (string username, RoleRequest r, AccountService accounts, WorldService world, HttpContext ctx) =>
        {
            var result = await accounts.AdminSetRoleAsync(ctx.Session(), username, r.Role ?? "", ctx.Caller(), ctx.RequestAborted);
            if (result.Ok) world.AccountChanged(result.Value!.Id);
            return ApiProblems.Ok(result);
        }).RequireAuthorization(Policies.Admin);

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
            OnlineCharacterService characters, WorldService world, HttpContext ctx) =>
        {
            if (await accounts.FindAccountAsync(username, ctx.RequestAborted) is not { } account) return NoAccount();
            if (!await characters.HasResetWorkAsync(account.Id, ctx.RequestAborted))
                return ApiProblems.Of(StatusCodes.Status404NotFound, "no_online_character", "There is no online character and no online story.");
            var reset = await characters.ResetAsync(account.Id, ctx.Session().AccountId, ctx.IpBytes(), ctx.RequestAborted);
            // The relay's keeper reloads the emptied story and a hosted instance ends (19.8.1).
            if (reset) world.StoryReplaced(account.Id);
            return reset
                ? Results.NoContent()
                : ApiProblems.Of(StatusCodes.Status404NotFound, "no_online_character", "There is no online character and no online story.");
        });

        // The account's online story: its kept revisions (each with its phase, key items and flag count), and putting one
        // back as the next revision (damage from a forged or faulty merge; the relay's keeper reloads it, a hosted instance ends).
        admin.MapGet("/accounts/{username}/online-story/history", async (string username, AccountService accounts,
            OnlineCharacterService characters, HttpContext ctx) =>
            await accounts.FindAccountAsync(username, ctx.RequestAborted) is { } account
                ? Results.Ok(await characters.StoryHistoryAsync(account.Id, ctx.RequestAborted))
                : NoAccount());

        admin.MapPost("/accounts/{username}/online-story/restore/{revision:long}", async (string username, long revision,
            AccountService accounts, OnlineCharacterService characters, WorldService world, HttpContext ctx) =>
        {
            if (await accounts.FindAccountAsync(username, ctx.RequestAborted) is not { } account) return NoAccount();
            // The revision first: a wrong number ended the owner's instance for nothing (round 59 review).
            if (!await characters.HasStoryRevisionAsync(account.Id, revision, ctx.RequestAborted))
                return ApiProblems.Of(StatusCodes.Status404NotFound, "revision_not_found", "No such kept revision of the online story.");
            var restored = await characters.RestoreStoryAsync(account.Id, revision, ctx.Session().AccountId, ctx.IpBytes(), ctx.RequestAborted);
            if (restored.Ok) world.StoryReplaced(account.Id);
            return ApiProblems.Ok(restored, rev => Results.Ok(new { revision = rev }));
        });
    }

    private static IResult NoAccount() => ApiProblems.Of(StatusCodes.Status404NotFound, "account_not_found", "No such account.");
}
