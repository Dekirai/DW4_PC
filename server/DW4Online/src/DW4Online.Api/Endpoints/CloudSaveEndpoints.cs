using DW4Online.Api.Auth;
using DW4Online.Domain;
using DW4Online.Domain.Services;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;

namespace DW4Online.Api.Endpoints;

/// <summary>
/// The private cloud backup of the local save. Body = the save file bytes (application/octet-stream), with
/// X-Sha256 (hex) and If-Match / If-None-Match: *. Optional X-Device-Name and X-Client-Version.
/// </summary>
public static class CloudSaveEndpoints
{
    public static void Map(RouteGroupBuilder v1)
    {
        var group = v1.MapGroup("/cloud-save").RequireAuthorization(Policies.Player).RequirePasswordCurrent();

        group.MapGet("/meta", async (CloudSaveService saves, HttpContext ctx) =>
        {
            var overview = await saves.GetAsync(ctx.Session().AccountId, ctx.RequestAborted);
            if (overview is null) return ApiProblems.Of(StatusCodes.Status404NotFound, "no_cloud_save", "There is no cloud save yet.");
            ctx.Response.Headers.ETag = Revisions.ETag(overview.Latest.Revision);
            return Results.Ok(new
            {
                latest = Meta(overview.Latest),
                boxes = overview.Boxes,
                history = overview.History.Select(Meta),
            });
        });

        // The latest backup, or ?revision=N for a kept older one.
        group.MapGet("", async (int? revision, CloudSaveService saves, HttpContext ctx) =>
        {
            var data = await saves.DownloadAsync(ctx.Session().AccountId, revision, ctx.RequestAborted);
            if (data is null) return ApiProblems.Of(StatusCodes.Status404NotFound, "no_cloud_save", "There is no such cloud save.");
            ctx.Response.Headers.ETag = Revisions.ETag(data.Revision);
            ctx.Response.Headers["X-Sha256"] = Convert.ToHexString(data.Sha256);
            ctx.Response.Headers["X-Save-Format"] = data.Format;
            ctx.Response.Headers.CacheControl = "no-store";
            return Results.File(data.Data, "application/octet-stream", data.Format == "gci" ? "savedata.dat" : "NDMWSaveData.bin");
        });

        group.MapPut("", async (CloudSaveService saves, CloudSaveOptions options, HttpContext ctx) =>
        {
            if (!Revisions.TryIfMatch(ctx.Request, out var expected)) return Revisions.BadPrecondition();
            var data = await Bodies.ReadBoundedAsync(ctx, options.MaxBytes);
            if (data is null)
                return ApiProblems.Of(StatusCodes.Status413PayloadTooLarge, "payload_too_large", $"A cloud save holds at most {options.MaxBytes} bytes.");
            var result = await saves.UploadAsync(ctx.Session().AccountId, data, ctx.Request.Headers["X-Sha256"].ToString(), expected,
                Revisions.IfNoneMatchAny(ctx.Request), Header(ctx, "X-Device-Name"), Header(ctx, "X-Client-Version"), ctx.IpBytes(), ctx.RequestAborted);
            return ApiProblems.Ok(result, meta =>
            {
                ctx.Response.Headers.ETag = Revisions.ETag(meta.Revision);
                return expected is null ? Results.Created(ctx.Request.PathBase + "/api/v1/cloud-save", Meta(meta)) : Results.Ok(Meta(meta));
            });
        }).RequireRateLimiting(RateLimitPolicies.CloudWrite);

        group.MapPost("/restore/{revision:int}", async (int revision, CloudSaveService saves, HttpContext ctx) =>
            ApiProblems.Ok(await saves.RestoreAsync(ctx.Session().AccountId, revision, ctx.IpBytes(), ctx.RequestAborted), meta =>
            {
                ctx.Response.Headers.ETag = Revisions.ETag(meta.Revision);
                return Results.Ok(Meta(meta));
            })).RequireRateLimiting(RateLimitPolicies.CloudWrite);

        // Deletes the backup and every kept revision: like the other destructive actions it needs the current password
        // (a leaked token alone must not destroy the only backup). A wrong password counts as a failed login.
        group.MapPost("/delete", async (PasswordConfirmation r, CloudSaveService saves, AccountService accounts, HttpContext ctx) =>
        {
            var session = ctx.Session();
            if (await accounts.ConfirmPasswordAsync(session.AccountId, r.CurrentPassword, ctx.Caller(), ctx.RequestAborted) is { Ok: false } wrong)
                return ApiProblems.From(wrong.Error!);
            return await saves.DeleteAsync(session.AccountId, ctx.IpBytes(), ctx.RequestAborted)
                ? Results.NoContent()
                : ApiProblems.Of(StatusCodes.Status404NotFound, "no_cloud_save", "There is no cloud save.");
        }).RequireRateLimiting(RateLimitPolicies.AccountChange);
    }

    private static object Meta(Domain.Abstractions.CloudSaveMeta m) => new
    {
        m.Revision,
        m.Format,
        m.SizeBytes,
        sha256 = Convert.ToHexString(m.Sha256),
        m.DeviceName,
        m.ClientVersion,
        m.UploadedAt,
    };

    private static string? Header(HttpContext ctx, string name) =>
        ctx.Request.Headers.TryGetValue(name, out var v) && v.Count > 0 ? v.ToString() : null;
}
