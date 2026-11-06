using DW4Online.Api.Auth;
using DW4Online.Domain.Achievements;
using DW4Online.Domain.Services;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;

namespace DW4Online.Api.Endpoints;

/// <summary>An upload: the 3316-byte record as base64, the port's play time without loading, and the session context.</summary>
public sealed record CharacterUpload(string? Record, long PlaySeconds, PlayContext? Context);

public static class OnlineCharacterEndpoints
{
    public static void Map(RouteGroupBuilder v1)
    {
        var group = v1.MapGroup("/online-character").RequireAuthorization(Policies.Player).RequirePasswordCurrent();

        // 404 means: no online character yet, start from zero.
        group.MapGet("", async (OnlineCharacterService characters, HttpContext ctx) =>
        {
            var view = await characters.GetAsync(ctx.Session().AccountId, ctx.RequestAborted);
            if (view is null) return ApiProblems.Of(StatusCodes.Status404NotFound, "no_online_character", "There is no online character yet.");
            ctx.Response.Headers.ETag = Revisions.ETag(view.Revision);
            return Results.Ok(new
            {
                view.Revision,
                record = Convert.ToBase64String(view.Record),
                view.Summary,
                view.PlaySeconds,
                view.Flagged,
                view.CreatedAt,
                view.UpdatedAt,
            });
        });

        // Create with "If-None-Match: *", update with "If-Match: <revision>". 409 carries currentRevision.
        group.MapPut("", async (CharacterUpload body, OnlineCharacterService characters, HttpContext ctx) =>
        {
            if (!Revisions.TryIfMatch(ctx.Request, out var expected)) return Revisions.BadPrecondition();
            var create = Revisions.IfNoneMatchAny(ctx.Request);
            if (expected is null && !create)
                return ApiProblems.Of(StatusCodes.Status428PreconditionRequired, "precondition_required",
                    "Send If-Match with the current revision, or If-None-Match: * to create the character.");
            byte[] record;
            try
            {
                record = Convert.FromBase64String(body.Record ?? "");
            }
            catch (FormatException)
            {
                return ApiProblems.Of(StatusCodes.Status422UnprocessableEntity, "invalid_record", "record is not base64.");
            }
            var result = await characters.SaveAsync(ctx.Session().AccountId, record, body.PlaySeconds, expected, body.Context, ctx.IpBytes(), ctx.RequestAborted);
            return ApiProblems.Ok(result, v =>
            {
                ctx.Response.Headers.ETag = Revisions.ETag(v.Revision);
                var payload = new { v.Revision, v.Summary, v.Warnings, v.Unlocked };
                return expected is null ? Results.Created(ctx.Request.PathBase + "/api/v1/online-character", payload) : Results.Ok(payload);
            });
        }).RequireRateLimiting(RateLimitPolicies.CharacterWrite);

        group.MapGet("/history", async (OnlineCharacterService characters, HttpContext ctx) =>
            Results.Ok(await characters.HistoryAsync(ctx.Session().AccountId, ctx.RequestAborted)));

        // Ends the current online character and empties the account's online story (also an account that has a story but no
        // character, a host whose guests played); the next upload with If-None-Match: * starts a new one. The relay's keeper
        // reloads the emptied story and the account's hosted instance ends (19.8.1).
        group.MapPost("/reset", async (PasswordConfirmation r, OnlineCharacterService characters, AccountService accounts, WorldService world,
            HttpContext ctx) =>
        {
            var session = ctx.Session();
            if (await accounts.ConfirmPasswordAsync(session.AccountId, r.CurrentPassword, ctx.Caller(), ctx.RequestAborted) is { Ok: false } wrong)
                return ApiProblems.From(wrong.Error!);
            if (!await characters.HasResetWorkAsync(session.AccountId, ctx.RequestAborted))
                return ApiProblems.Of(StatusCodes.Status404NotFound, "no_online_character", "There is no online character and no online story.");
            var reset = await characters.ResetAsync(session.AccountId, session.AccountId, ctx.IpBytes(), ctx.RequestAborted);
            if (reset) world.StoryReplaced(session.AccountId);
            return reset
                ? Results.NoContent()
                : ApiProblems.Of(StatusCodes.Status404NotFound, "no_online_character", "There is no online character and no online story.");
        }).RequireRateLimiting(RateLimitPolicies.AccountChange);
    }
}
