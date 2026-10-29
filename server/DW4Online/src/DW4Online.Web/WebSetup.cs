using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Routing;
using Microsoft.Extensions.DependencyInjection;

namespace DW4Online.Web;

/// <summary>Wires the Razor Pages website into the service host.</summary>
public static class WebSetup
{
    public static IServiceCollection AddDw4Web(this IServiceCollection services)
    {
        services.AddRazorPages().AddApplicationPart(typeof(WebSetup).Assembly);
        return services;
    }

    public static IEndpointRouteBuilder MapDw4Web(this IEndpointRouteBuilder routes)
    {
        routes.MapRazorPages();
        return routes;
    }
}
