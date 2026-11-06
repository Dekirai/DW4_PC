using DW4Online.Api.Auth;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Services;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;

namespace DW4Online.Api.Endpoints;

public sealed record UsernameRequest(string? Username);

/// <summary>
/// Friends and blocks (SEAMLESS.md 19.11, 19.19): the lists, requests by exact user name, accept / decline / cancel,
/// remove, block / unblock, and a player's public card with the relation to the caller. A block is never revealed: a
/// blocked sender's request answers like any other ("Request sent."), and the card never says that the other player
/// blocks the caller. GET /friends is limited to one call per RateLimits:FriendListSeconds (2) per account, every other
/// call here to RateLimits:SocialPerMinute (60).
/// </summary>
public static class SocialEndpoints
{
    public static void Map(RouteGroupBuilder v1)
    {
        var friends = v1.MapGroup("/friends").RequireAuthorization(Policies.Player).RequirePasswordCurrent();

        // Friends with where they are (round I1a: by each friend's whereabouts preference; null when he shows nothing),
        // requests in and out, blocks, the limits.
        friends.MapGet("", async (SocialService social, WorldService world, HttpContext ctx) =>
        {
            var o = await social.OverviewAsync(ctx.Session().AccountId, ctx.RequestAborted);
            var where = await world.PresenceAsync(o.Friends.Select(f => f.AccountId).ToList(), ctx.RequestAborted);
            return Results.Ok(new
            {
                friends = o.Friends.Select(f => new
                {
                    f.Username,
                    f.Since,
                    presence = where.GetValueOrDefault(f.AccountId) is { } p
                        ? new { online = p.Online, status = p.Status, area = p.Area, stage = p.Stage, lastSeenAt = p.LastSeenAt }
                        : null,
                }),
                requestsIn = o.RequestsIn,
                requestsOut = o.RequestsOut,
                blocks = o.Blocks,
                limits = o.Limits,
            });
        }).RequireRateLimiting(RateLimitPolicies.FriendList);

        friends.MapPost("/requests", async (UsernameRequest r, SocialService social, HttpContext ctx) =>
            ApiProblems.Ok(await social.SendRequestAsync(ctx.Session().AccountId, r.Username, ctx.Caller(), ctx.RequestAborted)))
            .RequireRateLimiting(RateLimitPolicies.Social);

        friends.MapPost("/requests/{username}/accept", async (string username, SocialService social, HttpContext ctx) =>
            ApiProblems.Ok(await social.AcceptAsync(ctx.Session().AccountId, username, ctx.Caller(), ctx.RequestAborted)))
            .RequireRateLimiting(RateLimitPolicies.Social);

        friends.MapPost("/requests/{username}/decline", async (string username, SocialService social, HttpContext ctx) =>
            ApiProblems.Ok(await social.DeclineAsync(ctx.Session().AccountId, username, ctx.RequestAborted), _ => Results.NoContent()))
            .RequireRateLimiting(RateLimitPolicies.Social);

        // The caller's own request (cancel).
        friends.MapDelete("/requests/{username}", async (string username, SocialService social, HttpContext ctx) =>
            ApiProblems.Ok(await social.CancelAsync(ctx.Session().AccountId, username, ctx.RequestAborted), _ => Results.NoContent()))
            .RequireRateLimiting(RateLimitPolicies.Social);

        friends.MapDelete("/{username}", async (string username, SocialService social, HttpContext ctx) =>
            ApiProblems.Ok(await social.RemoveFriendAsync(ctx.Session().AccountId, username, ctx.RequestAborted), _ => Results.NoContent()))
            .RequireRateLimiting(RateLimitPolicies.Social);

        var blocks = v1.MapGroup("/blocks").RequireAuthorization(Policies.Player).RequirePasswordCurrent();

        blocks.MapGet("", async (SocialService social, HttpContext ctx) =>
            Results.Ok(await social.BlocksAsync(ctx.Session().AccountId, ctx.RequestAborted)))
            .RequireRateLimiting(RateLimitPolicies.Social);

        blocks.MapPost("", async (UsernameRequest r, SocialService social, HttpContext ctx) =>
            ApiProblems.Ok(await social.BlockAsync(ctx.Session().AccountId, r.Username, ctx.Caller(), ctx.RequestAborted), _ => Results.NoContent()))
            .RequireRateLimiting(RateLimitPolicies.Social);

        blocks.MapDelete("/{username}", async (string username, SocialService social, HttpContext ctx) =>
            ApiProblems.Ok(await social.UnblockAsync(ctx.Session().AccountId, username, ctx.RequestAborted), _ => Results.NoContent()))
            .RequireRateLimiting(RateLimitPolicies.Social);

        // A player's public card (exact name only, as the website's /u/{name}) and how the caller stands to him.
        v1.MapGroup("/players").RequireAuthorization(Policies.Player).RequirePasswordCurrent()
            .MapGet("/{username}", async (string username, AccountService accounts, OnlineCharacterService characters, SocialService social,
                HttpContext ctx) =>
            {
                var session = ctx.Session();
                var name = Domain.Accounts.UsernamePolicy.Canonical(username);
                var account = name is null ? null : await accounts.FindAccountAsync(name, ctx.RequestAborted);
                if (account is null || account.Status == AccountStatus.Deleted)
                    return ApiProblems.Of(StatusCodes.Status404NotFound, "player_not_found", "No player has this exact name.");
                var own = account.Id == session.AccountId;
                var staff = session.Role is Roles.Moderator or Roles.Admin;
                // As the profile page: a private (or banned) profile shows nothing but the name (not even since when the
                // account exists), plus the caller's own relation to it.
                var visible = own || staff || (account.ProfilePublic && account.Status != AccountStatus.Banned);
                var character = visible ? await characters.GetAsync(account.Id, ctx.RequestAborted) : null;
                return Results.Ok(new
                {
                    account.Username,
                    memberSince = visible ? account.CreatedAt : (DateTime?)null,
                    hidden = !visible,
                    character = character is null ? null : new
                    {
                        digimon = character.Summary.Digimon,
                        playerName = character.Summary.PlayerName,
                        level = character.Summary.Level,
                    },
                    relation = await social.RelationAsync(session.AccountId, account.Id, ctx.RequestAborted),
                });
            }).RequireRateLimiting(RateLimitPolicies.Social);
    }
}
