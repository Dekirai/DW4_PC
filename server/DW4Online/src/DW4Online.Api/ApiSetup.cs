using System.Security.Claims;
using System.Threading.RateLimiting;
using DW4Online.Api.Auth;
using DW4Online.Api.Endpoints;
using DW4Online.Domain.Abstractions;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.RateLimiting;
using Microsoft.AspNetCore.Routing;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;

namespace DW4Online.Api;

/// <summary>Rate limits (section "RateLimits"). Logins are CPU heavy (PBKDF2), so these are not optional.</summary>
public sealed class RateLimitOptions
{
    public int GlobalPerMinute { get; set; } = 300;          // per IP, every request
    public int LoginPerMinute { get; set; } = 5;             // per IP: login and recovery
    public int RegisterPerHour { get; set; } = 3;            // per IP
    public int CharacterWritesPerMinute { get; set; } = 12;  // per account
    public int CloudWritesPerHour { get; set; } = 6;         // per account
    public int AchievementsPerMinute { get; set; } = 60;     // per account: unlocks and stats
    public int AccountChangesPerMinute { get; set; } = 10;   // per account: password, e-mail, recovery code
    public int StaffPerMinute { get; set; } = 60;            // per account
    public int RoomCreatesPerMinute { get; set; } = 5;       // per account
    public int RoomJoinsPerMinute { get; set; } = 20;        // per account: room passwords cannot be brute-forced
    // WebLoginPerMinute / WebRegisterPerHour (website forms) are read by DW4Online.Web from the same section.
}

public static class RateLimitPolicies
{
    public const string Login = "login";
    public const string Register = "register";
    public const string CharacterWrite = "character-write";
    public const string CloudWrite = "cloud-write";
    public const string Achievements = "achievements";
    public const string AccountChange = "account-change";
    public const string Staff = "staff";
    public const string RoomCreate = "room-create";
    public const string RoomJoin = "room-join";
}

public static class ApiSetup
{
    /// <summary>Authentication, authorization, rate limits and problem details for the API.</summary>
    public static IServiceCollection AddDw4Api(this IServiceCollection services, RateLimitOptions limits)
    {
        services.AddProblemDetails();
        services.ConfigureHttpJsonOptions(o => o.SerializerOptions.PropertyNamingPolicy = System.Text.Json.JsonNamingPolicy.CamelCase);
        services.AddAuthentication(TokenAuthenticationHandler.SchemeName)
            .AddScheme<AuthenticationSchemeOptions, TokenAuthenticationHandler>(TokenAuthenticationHandler.SchemeName, null);
        services.AddAuthorizationBuilder()
            .AddPolicy(Policies.Player, p => p.AddAuthenticationSchemes(TokenAuthenticationHandler.SchemeName).RequireAuthenticatedUser())
            .AddPolicy(Policies.Staff, p => p.AddAuthenticationSchemes(TokenAuthenticationHandler.SchemeName).RequireRole(Roles.Moderator, Roles.Admin))
            .AddPolicy(Policies.Admin, p => p.AddAuthenticationSchemes(TokenAuthenticationHandler.SchemeName).RequireRole(Roles.Admin));
        services.AddRateLimiter(o =>
        {
            o.GlobalLimiter = PartitionedRateLimiter.Create<HttpContext, string>(ctx =>
                Window(IpKey(ctx), limits.GlobalPerMinute, TimeSpan.FromMinutes(1)));
            o.AddPolicy(RateLimitPolicies.Login, ctx => Window(IpKey(ctx), limits.LoginPerMinute, TimeSpan.FromMinutes(1)));
            o.AddPolicy(RateLimitPolicies.Register, ctx => Window(IpKey(ctx), limits.RegisterPerHour, TimeSpan.FromHours(1)));
            o.AddPolicy(RateLimitPolicies.CharacterWrite, ctx => Window(AccountKey(ctx), limits.CharacterWritesPerMinute, TimeSpan.FromMinutes(1)));
            o.AddPolicy(RateLimitPolicies.CloudWrite, ctx => Window(AccountKey(ctx), limits.CloudWritesPerHour, TimeSpan.FromHours(1)));
            o.AddPolicy(RateLimitPolicies.Achievements, ctx => Window(AccountKey(ctx), limits.AchievementsPerMinute, TimeSpan.FromMinutes(1)));
            o.AddPolicy(RateLimitPolicies.AccountChange, ctx => Window(AccountKey(ctx), limits.AccountChangesPerMinute, TimeSpan.FromMinutes(1)));
            o.AddPolicy(RateLimitPolicies.Staff, ctx => Window(AccountKey(ctx), limits.StaffPerMinute, TimeSpan.FromMinutes(1)));
            o.AddPolicy(RateLimitPolicies.RoomCreate, ctx => Window(AccountKey(ctx), limits.RoomCreatesPerMinute, TimeSpan.FromMinutes(1)));
            o.AddPolicy(RateLimitPolicies.RoomJoin, ctx => Window(AccountKey(ctx), limits.RoomJoinsPerMinute, TimeSpan.FromMinutes(1)));
            o.OnRejected = async (context, ct) =>
            {
                var retry = context.Lease.TryGetMetadata(MetadataName.RetryAfter, out var after) ? (int)Math.Ceiling(after.TotalSeconds) : 60;
                context.HttpContext.Response.Headers.RetryAfter = retry.ToString(System.Globalization.CultureInfo.InvariantCulture);
                await ApiProblems.Write(context.HttpContext, StatusCodes.Status429TooManyRequests, "rate_limited",
                    "Too many requests. Try again later.", new Dictionary<string, object?> { ["retryAfterSeconds"] = retry });
            };
        });
        return services;
    }

    /// <summary>
    /// Problem details for API paths (a 500 never shows a stack trace), then authentication before the rate limiter so
    /// per-account limits know the account. The website adds its own HTML error pages for its paths.
    /// </summary>
    public static WebApplication UseDw4Api(this WebApplication app)
    {
        var development = app.Environment.IsDevelopment();
        app.UseWhen(ctx => IsApiPath(ctx.Request.Path), api =>
        {
            if (!development) api.UseExceptionHandler();
            api.UseStatusCodePages();
        });
        // Explicit routing after the error handlers (the website's included), so a re-executed error page is routed again.
        app.UseRouting();
        app.UseAuthentication();
        app.UseRateLimiter();
        app.UseAuthorization();
        return app;
    }

    public static IEndpointRouteBuilder MapDw4Api(this IEndpointRouteBuilder routes)
    {
        var v1 = routes.MapGroup("/api/v1");
        StatusEndpoints.Map(v1);
        AuthEndpoints.Map(v1);
        AccountEndpoints.Map(v1);
        OnlineCharacterEndpoints.Map(v1);
        CloudSaveEndpoints.Map(v1);
        AchievementEndpoints.Map(v1);
        RoomEndpoints.Map(v1);
        AdminEndpoints.Map(v1);
        return routes;
    }

    /// <summary>The API, the health check and ACME challenges (everything else is the website).</summary>
    public static bool IsApiPath(PathString path) =>
        path.StartsWithSegments("/api") || path.StartsWithSegments("/health") || path.StartsWithSegments("/.well-known");

    public static RateLimitPartition<string> Window(string key, int permits, TimeSpan window) =>
        RateLimitPartition.GetFixedWindowLimiter(key, _ => new FixedWindowRateLimiterOptions
        {
            PermitLimit = Math.Max(1, permits),
            Window = window,
            QueueLimit = 0,
        });

    public static string IpKey(HttpContext ctx) => "ip:" + (ctx.Connection.RemoteIpAddress?.ToString() ?? "unknown");

    private static string AccountKey(HttpContext ctx) =>
        ctx.User.FindFirstValue(ClaimTypes.NameIdentifier) is { } id ? "acc:" + id : IpKey(ctx);
}
