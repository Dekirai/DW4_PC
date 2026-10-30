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

/// <summary>
/// Rooms: create (name, optional password, 2..10 players, settings), list, join, heartbeat, leave, kick, close, and a
/// fresh relay ticket (/ticket). Create and join answer with the member's one-time relay ticket (relayTicket) and the
/// room's settings hash for the DW4R2 handshake; the relay groups the members into instances (instances). A room keeps
/// running when its owner leaves; the story is frozen until the owner returns (room.storyFrozen).
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
            return ApiProblems.Ok(result, state => Results.Created($"{ctx.Request.PathBase}/api/v1/rooms/{state.Room.Code}", Body(state, Relay(ctx, service, relay))));
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

        // The room story (members only; SEAMLESS.md 8): revision, phase, flags, keys, counters, dropped set.
        group.MapGet("/{code}/story", async (string code, RoomService rooms, HttpContext ctx) =>
            ApiProblems.Ok(await rooms.StoryAsync(ctx.Session().AccountId, code, ctx.RequestAborted), story => Results.Ok(story)));

        // A fresh one-time relay ticket (a reconnect).
        group.MapPost("/{code}/ticket", async (string code, RoomService rooms, ServiceOptions service, IRelayGateway relay, HttpContext ctx) =>
            ApiProblems.Ok(await rooms.TicketAsync(ctx.Session().AccountId, code, ctx.RequestAborted),
                state => Results.Ok(Body(state, Relay(ctx, service, relay))))).RequireRateLimiting(RateLimitPolicies.RoomStage);
    }

    /// <summary>
    /// Where clients reach the relay: Service:RelayHost (or the host name of the request) and Service:RelayPort, which by
    /// default (0) is the port the relay listens on.
    /// </summary>
    public static object? Relay(HttpContext ctx, ServiceOptions service, IRelayGateway relay) => relay.Available
        ? new
        {
            host = service.RelayHost.Length > 0 ? service.RelayHost : ctx.Request.Host.Host,
            port = service.RelayPort > 0 ? service.RelayPort : relay.BoundPort ?? 0,
            protocol = "DW4R2",
        }
        : null;

    private static object Body(RoomState s, object? relay) => new
    {
        s.Room,
        s.Settings,
        s.SettingsHash,
        s.Members,
        s.Instances,
        s.You,
        s.HeartbeatSeconds,
        relay,
        s.RelayTicket,
    };
}
