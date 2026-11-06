using System.Reflection;
using DW4Online.Domain;
using DW4Online.Domain.Abstractions;
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
        // The minimum protocol announced is the one the version gate applies: max(Play:MinimumProtocol, the world's protocol).
        v1.MapGet("/status", (ServiceOptions service, PlayOptions play, AccountOptions accounts, IWorldRelay relay, HttpContext ctx, TimeProvider time) => Results.Ok(new
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
                minimumProtocol = play.EffectiveMinimumProtocol,
                currentProtocol = Math.Max(play.CurrentProtocol, play.EffectiveMinimumProtocol),
            },
            relay = WorldEndpoints.Relay(ctx, service, relay),
        })).AllowAnonymous();
    }
}
