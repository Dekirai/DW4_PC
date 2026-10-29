using System.Reflection;
using DW4Online.Domain;
using DW4Online.Domain.Rooms;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;

namespace DW4Online.Api.Endpoints;

public static class StatusEndpoints
{
    private static readonly string Version =
        typeof(StatusEndpoints).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion.Split('+')[0] ?? "0.0.0";

    public static void Map(RouteGroupBuilder v1)
    {
        v1.MapGet("/status", (ServiceOptions service, PlayOptions play, AccountOptions accounts, IRelayGateway relay, HttpContext ctx, TimeProvider time) => Results.Ok(new
        {
            service = service.Name,
            version = Version,
            api = 1,
            serverTime = time.GetUtcNow().UtcDateTime,
            maintenance = service.Maintenance,
            motd = service.Motd,
            registration = accounts.Registration,
            client = new
            {
                minimumVersion = play.MinimumClientVersion,
                currentVersion = play.CurrentClientVersion,
                minimumProtocol = play.MinimumProtocol,
                currentProtocol = play.CurrentProtocol,
            },
            relay = RoomEndpoints.Relay(ctx, service, relay),
        })).AllowAnonymous();
    }
}
