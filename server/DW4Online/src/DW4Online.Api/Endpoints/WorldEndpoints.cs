using System.Text.Json;
using DW4Online.Api.Auth;
using DW4Online.Domain;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Services;
using DW4Online.Domain.World;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;

namespace DW4Online.Api.Endpoints;

/// <summary>The body of POST /world/enter and /world/ticket: the client's protocol, game data hash and version.</summary>
public sealed record WorldEnterRequest(int? Protocol, ulong? DataHash, string? ClientVersion);

/// <summary>
/// The world (SEAMLESS.md 19.19, round I1a): POST /world/enter (the world session's one-time relay ticket, the relay
/// endpoint, the account's role, box, preferences and host settings), /world/ticket (a new ticket for a reconnect),
/// /world/leave, the public counts (GET /world), the instances the caller may see (GET /instances) and the online
/// preferences (GET / PUT /me/online-prefs). The staff's /admin/instances live in <see cref="AdminEndpoints"/>.
/// </summary>
public static class WorldEndpoints
{
    public static void Map(RouteGroupBuilder v1)
    {
        // Public: players online and instances per area, no names.
        v1.MapGet("/world", (WorldService world) =>
        {
            var counts = world.Counts();
            return Results.Ok(new
            {
                playersOnline = counts.PlayersOnline,
                instances = counts.Instances,
                areas = counts.Areas.Select(a => new { area = a.Area, name = a.Name, instances = a.Instances, players = a.Players }),
            });
        }).AllowAnonymous();

        var group = v1.MapGroup("/world").RequireAuthorization(Policies.Player).RequirePasswordCurrent();

        group.MapPost("/enter", async (WorldEnterRequest? r, WorldService world, ServiceOptions service, IWorldRelay relay, HttpContext ctx) =>
            ApiProblems.Ok(await world.EnterAsync(ctx.Session(), r?.Protocol, r?.DataHash, r?.ClientVersion, ctx.RequestAborted),
                entry => Results.Ok(Body(entry, Relay(ctx, service, relay)))))
            .RequireRateLimiting(RateLimitPolicies.WorldEnter);

        group.MapPost("/ticket", async (WorldEnterRequest? r, WorldService world, ServiceOptions service, IWorldRelay relay, HttpContext ctx) =>
            ApiProblems.Ok(await world.TicketAsync(ctx.Session(), r?.Protocol, r?.DataHash, r?.ClientVersion, ctx.RequestAborted),
                entry => Results.Ok(Body(entry, Relay(ctx, service, relay)))))
            .RequireRateLimiting(RateLimitPolicies.WorldEnter);

        group.MapPost("/leave", async (WorldService world, HttpContext ctx) =>
        {
            await world.LeaveAsync(ctx.Session().AccountId, ctx.RequestAborted);
            return Results.NoContent();
        }).RequireRateLimiting(RateLimitPolicies.Social);

        // The instances visible to the caller (Visible(), 19.7.2), optionally of one area; staff see every one with its visibility.
        v1.MapGroup("/instances").RequireAuthorization(Policies.Player).RequirePasswordCurrent().MapGet("", async (string? area, WorldService world, HttpContext ctx) =>
        {
            if (!string.IsNullOrEmpty(area) && AreaTable.Areas.All(a => a.Key != area))
                return ApiProblems.Of(StatusCodes.Status422UnprocessableEntity, "unknown_area", "Areas are death_valley, dry_land, venom_jungle and machine_pit.");
            var session = ctx.Session();
            var viewer = await world.ViewerAsync(session.AccountId, session.Role, ctx.RequestAborted);
            return Results.Ok(new
            {
                instances = world.Instances(viewer, area).Select(i => new
                {
                    wid = i.Wid,
                    host = i.Host,
                    area = i.Area,
                    areaName = i.AreaName,
                    stage = i.Stage,
                    stageName = i.StageName,
                    players = i.Players,
                    maxPlayers = i.MaxPlayers,
                    visibility = i.Visibility,
                    difficulty = i.Difficulty,
                    randomizer = i.Randomizer,
                    storyLabel = i.StoryLabel,
                    ageSeconds = i.AgeSeconds,
                    yours = i.Yours,
                }),
            });
        }).RequireRateLimiting(RateLimitPolicies.Social);

        var me = v1.MapGroup("/me").RequireAuthorization(Policies.Player).RequirePasswordCurrent();

        me.MapGet("/online-prefs", async (WorldService world, HttpContext ctx) =>
            Results.Ok((await world.GetPrefsAsync(ctx.Session().AccountId, ctx.RequestAborted)).ToJson()));

        me.MapPut("/online-prefs", async (JsonElement body, WorldService world, HttpContext ctx) =>
            ApiProblems.Ok(await world.UpdatePrefsAsync(ctx.Session().AccountId, body, ctx.RequestAborted), p => Results.Ok(p.ToJson())))
            .RequireRateLimiting(RateLimitPolicies.AccountChange);
    }

    /// <summary>
    /// Where clients reach the relay: Service:RelayHost (or the host name of the request) and Service:RelayPort, which by
    /// default (0) is the port the relay listens on.
    /// </summary>
    public static object? Relay(HttpContext ctx, ServiceOptions service, IWorldRelay relay) => relay.Available
        ? new
        {
            host = service.RelayHost.Length > 0 ? service.RelayHost : ctx.Request.Host.Host,
            port = service.RelayPort > 0 ? service.RelayPort : relay.BoundPort ?? 0,
            protocol = "DW4R2",
        }
        : null;

    private static object Body(WorldEntry e, object? relay)
    {
        var settings = e.Prefs.HostSettings(0);
        return new
        {
            ticket = e.Ticket,
            relay,
            protocol = e.Protocol,
            role = e.Role,
            box = e.Box,
            seat = (object?)null, // round I5
            prefs = new
            {
                defaultVisibility = e.Prefs.DefaultVisibility,
                invites = e.Prefs.Invites,
                whereabouts = e.Prefs.Whereabouts,
                friendToasts = e.Prefs.FriendToasts,
            },
            settings = new
            {
                cameraMode = settings.CameraMode,
                lightMode = settings.LightMode,
                fastLoading = settings.FastLoading,
                randomizer = new
                {
                    enabled = e.Prefs.RandomizerSeed is not null,
                    seed = settings.RandomizerSeed,
                    drops = settings.RandomizerDrops,
                    enemies = settings.RandomizerEnemies,
                    zones = settings.RandomizerZones,
                },
            },
        };
    }

    /// <summary>An instance as the staff's GET /admin/instances shows it: everything, Private ones and spectators included.</summary>
    public static object AdminRow(WorldInstanceRow i) => new
    {
        wid = i.Wid,
        historyId = i.HistoryId,
        host = i.HostName,
        hostBox = i.HostBox,
        area = i.Area,
        stage = i.HostStage,
        stageName = AreaTable.Default.DisplayName(i.HostStage),
        visibility = i.Visibility,
        difficulty = i.Difficulty,
        settings = i.Settings,
        randomizer = i.Randomizer,
        startedAt = i.StartedAtUtc,
        starting = i.Starting,
        ending = i.Ending,
        hostAway = i.HostAway,
        members = i.Members.Select(Member),
        spectators = i.Spectators.Select(Member),
    };

    private static object Member(WorldMemberRow m) => new
    {
        name = m.Name,
        member = m.MemberId,
        role = m.Role,
        stage = m.Stage,
        stageName = AreaTable.Default.DisplayName(m.Stage),
        connected = m.Connected,
        lagging = m.Lagging,
    };
}
