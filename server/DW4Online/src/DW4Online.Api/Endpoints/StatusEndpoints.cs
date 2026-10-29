using System.Reflection;
using DW4Online.Domain;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;

namespace DW4Online.Api.Endpoints;

/// <summary>Service-wide settings announced to clients (section "Service").</summary>
public sealed class ServiceOptions
{
    public string Name { get; set; } = "DW4Online";
    /// <summary>Message of the day shown in the game's online menu.</summary>
    public string Motd { get; set; } = "";
    /// <summary>Maintenance mode: announced in the status; the room service stops opening rooms.</summary>
    public bool Maintenance { get; set; }
    /// <summary>Public host name or IP clients use for the relay (announced in the status).</summary>
    public string RelayHost { get; set; } = "";
    public int RelayPort { get; set; } = 52041;
}

public static class StatusEndpoints
{
    private static readonly string Version =
        typeof(StatusEndpoints).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion.Split('+')[0] ?? "0.0.0";

    public static void Map(RouteGroupBuilder v1)
    {
        v1.MapGet("/status", (ServiceOptions service, PlayOptions play, AccountOptions accounts, TimeProvider time) => Results.Ok(new
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
            relay = string.IsNullOrEmpty(service.RelayHost) ? null : new { host = service.RelayHost, port = service.RelayPort },
        })).AllowAnonymous();
    }
}
