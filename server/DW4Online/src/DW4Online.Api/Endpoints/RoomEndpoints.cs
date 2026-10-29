using System.Text.Json;
using DW4Online.Api.Auth;
using DW4Online.Domain;
using DW4Online.Domain.Rooms;
using DW4Online.Domain.Services;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;

namespace DW4Online.Api.Endpoints;

public sealed record CreateRoomRequest(string? Name, string? Password, int MaxPlayers, int Protocol, ulong DataHash, string? ClientVersion, JsonElement? Settings);
public sealed record JoinRoomRequest(string? Password, int Protocol, ulong DataHash, string? ClientVersion);
public sealed record KickRequest(string? Username);
public sealed record StageRequest(string? Stage, int? Instance);

/// <summary>
/// Rooms: create (name, optional password, 2..10 players), list, join, heartbeat, leave, kick, close, and the stage
/// instances (at most four players each) with their one-time relay tickets. A room keeps running when its owner
/// leaves; the story is frozen until the owner returns (room.storyFrozen).
/// </summary>
public static class RoomEndpoints
{
    public const int DefaultMaxPlayers = 4;

    public static void Map(RouteGroupBuilder v1)
    {
        // Public: the game lists compatible rooms (protocol + data hash, as the HELLO check), the website all of them.
        v1.MapGet("/rooms", async (int? protocol, ulong? dataHash, RoomService rooms, HttpContext ctx) =>
            Results.Ok(await rooms.ListAsync(protocol, dataHash, ctx.RequestAborted))).AllowAnonymous();

        var group = v1.MapGroup("/rooms").RequireAuthorization(Policies.Player).RequirePasswordCurrent();

        group.MapPost("", async (CreateRoomRequest r, RoomService rooms, ServiceOptions service, IRelayGateway relay, HttpContext ctx) =>
        {
            var session = ctx.Session();
            var result = await rooms.CreateAsync(session.AccountId, session.Username,
                new RoomCreate(r.Name, r.Password, r.MaxPlayers == 0 ? DefaultMaxPlayers : r.MaxPlayers, r.Protocol, r.DataHash, r.ClientVersion, r.Settings),
                service.Maintenance, ctx.RequestAborted);
            return ApiProblems.Ok(result, state => Results.Created($"/api/v1/rooms/{state.Room.Code}", Body(state, Relay(ctx, service, relay))));
        }).RequireRateLimiting(RateLimitPolicies.RoomCreate);

        // Members get the full state, everyone else the public summary.
        group.MapGet("/{code}", async (string code, RoomService rooms, ServiceOptions service, IRelayGateway relay, HttpContext ctx) =>
        {
            var (summary, state) = await rooms.GetAsync(code, ctx.Session().AccountId, ctx.RequestAborted);
            if (summary is null) return ApiProblems.Of(StatusCodes.Status404NotFound, "room_not_found", "No such open room.");
            return state is null ? Results.Ok(new { room = summary }) : Results.Ok(Body(state, Relay(ctx, service, relay)));
        });

        group.MapPost("/{code}/join", async (string code, JoinRoomRequest r, RoomService rooms, ServiceOptions service, IRelayGateway relay, HttpContext ctx) =>
        {
            var session = ctx.Session();
            var result = await rooms.JoinAsync(session.AccountId, session.Username, code, r.Password, r.Protocol, r.DataHash, r.ClientVersion, ctx.RequestAborted);
            return ApiProblems.Ok(result, state => Results.Ok(Body(state, Relay(ctx, service, relay))));
        }).RequireRateLimiting(RateLimitPolicies.RoomJoin);

        group.MapPost("/{code}/heartbeat", async (string code, RoomService rooms, ServiceOptions service, IRelayGateway relay, HttpContext ctx) =>
            ApiProblems.Ok(await rooms.HeartbeatAsync(ctx.Session().AccountId, code, ctx.RequestAborted),
                state => Results.Ok(Body(state, Relay(ctx, service, relay)))));

        group.MapPost("/{code}/leave", async (string code, RoomService rooms, HttpContext ctx) =>
            ApiProblems.Ok(await rooms.LeaveAsync(ctx.Session().AccountId, code, ctx.RequestAborted), _ => Results.NoContent()));

        group.MapPost("/{code}/kick", async (string code, KickRequest r, RoomService rooms, HttpContext ctx) =>
            ApiProblems.Ok(await rooms.KickAsync(ctx.Session().AccountId, code, r.Username, ctx.IpBytes(), ctx.RequestAborted), _ => Results.NoContent()));

        // The owner closes the room for everyone (present or away).
        group.MapDelete("/{code}", async (string code, RoomService rooms, HttpContext ctx) =>
            ApiProblems.Ok(await rooms.CloseAsync(ctx.Session().AccountId, false, code, ctx.IpBytes(), ctx.RequestAborted), _ => Results.NoContent()));

        // Entering a stage: an instance of that stage with a free place, or a new one hosted by the caller.
        group.MapPost("/{code}/stage", async (string code, StageRequest r, RoomService rooms, ServiceOptions service, IRelayGateway relay, HttpContext ctx) =>
            ApiProblems.Ok(await rooms.EnterStageAsync(ctx.Session().AccountId, code, r.Stage, r.Instance, ctx.RequestAborted), a => Results.Ok(new
            {
                a.Instance,
                a.Role,
                a.Ticket,
                a.Channel,
                relay = Relay(ctx, service, relay),
            }))).RequireRateLimiting(RateLimitPolicies.RoomStage);

        group.MapPost("/{code}/stage/leave", async (string code, RoomService rooms, HttpContext ctx) =>
            ApiProblems.Ok(await rooms.LeaveStageAsync(ctx.Session().AccountId, code, ctx.RequestAborted), _ => Results.NoContent()))
            .RequireRateLimiting(RateLimitPolicies.RoomStage);
    }

    /// <summary>Where clients reach the relay: Service:RelayHost (or the host name of the request) and its port.</summary>
    public static object? Relay(HttpContext ctx, ServiceOptions service, IRelayGateway relay) => relay.Available
        ? new
        {
            host = service.RelayHost.Length > 0 ? service.RelayHost : ctx.Request.Host.Host,
            port = service.RelayPort > 0 ? service.RelayPort : relay.BoundPort ?? 0,
        }
        : null;

    private static object Body(RoomState s, object? relay) => new
    {
        s.Room,
        s.Settings,
        s.Members,
        s.Instances,
        s.You,
        s.HeartbeatSeconds,
        relay,
    };
}
