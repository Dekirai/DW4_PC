using System.Threading.RateLimiting;
using DW4Online.Domain.Abstractions;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.DataProtection;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.Filters;
using Microsoft.AspNetCore.RateLimiting;
using Microsoft.AspNetCore.Routing;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;

namespace DW4Online.Web;

/// <summary>
/// Wires the Razor Pages website into the service host: cookie sign-in on web sessions, anti-forgery (Razor Pages
/// validate every POST), Data Protection keys, form rate limits, security headers and HTML error pages. Paths under
/// /api, /health and /.well-known belong to the API and keep its problem-details behaviour.
/// </summary>
public static class WebSetup
{
    private static readonly string Css = LoadCss();

    public static IServiceCollection AddDw4Web(this IServiceCollection services, IConfiguration config, IHostEnvironment env)
    {
        var options = config.GetSection("Web").Get<WebOptions>() ?? new WebOptions();
        var limits = config.GetSection("RateLimits").Get<WebRateLimits>() ?? new WebRateLimits();
        services.AddSingleton(options);

        var protection = services.AddDataProtection().SetApplicationName("DW4Online");
        if (options.DataProtectionKeysDirectory == "memory")
        {
            protection.UseEphemeralDataProtectionProvider();
        }
        else if (options.DataProtectionKeysDirectory.Length > 0)
        {
            var dir = Path.IsPathRooted(options.DataProtectionKeysDirectory)
                ? options.DataProtectionKeysDirectory
                : Path.Combine(env.ContentRootPath, options.DataProtectionKeysDirectory);
            protection.PersistKeysToFileSystem(new DirectoryInfo(dir));
            if (OperatingSystem.IsWindows()) protection.ProtectKeysWithDpapi();
        }

        services.AddAntiforgery(o =>
        {
            o.Cookie.Name = "dw4_antiforgery";
            o.Cookie.HttpOnly = true;
            o.Cookie.SameSite = SameSiteMode.Strict;
            o.Cookie.SecurePolicy = options.RequireHttps ? CookieSecurePolicy.Always : CookieSecurePolicy.SameAsRequest;
            o.FormFieldName = "__RequestVerificationToken";
        });

        services.AddAuthentication().AddScheme<AuthenticationSchemeOptions, WebSessionHandler>(WebSessionHandler.SchemeName, null);
        services.AddAuthorizationBuilder()
            .AddPolicy(WebPolicies.User, p => p.AddAuthenticationSchemes(WebSessionHandler.SchemeName).RequireAuthenticatedUser())
            .AddPolicy(WebPolicies.Staff, p => p.AddAuthenticationSchemes(WebSessionHandler.SchemeName).RequireRole(Roles.Moderator, Roles.Admin))
            .AddPolicy(WebPolicies.Admin, p => p.AddAuthenticationSchemes(WebSessionHandler.SchemeName).RequireRole(Roles.Admin));

        services.AddRazorPages(o =>
            {
                o.Conventions.AuthorizeFolder("/Account", WebPolicies.User);
                o.Conventions.AuthorizeFolder("/Admin", WebPolicies.Staff);
            })
            .AddApplicationPart(typeof(WebSetup).Assembly)
            .AddMvcOptions(o => o.Filters.Add(new PasswordChangePageFilter()));

        // Only the POSTs count: looking at the login form is free, trying passwords is not.
        services.Configure<RateLimiterOptions>(o =>
        {
            o.AddPolicy(WebPolicies.LoginLimit, ctx => PostWindow(ctx, "login", limits.WebLoginPerMinute, TimeSpan.FromMinutes(1)));
            o.AddPolicy(WebPolicies.RegisterLimit, ctx => PostWindow(ctx, "register", limits.WebRegisterPerHour, TimeSpan.FromHours(1)));
            // The account page's forms confirm with the current password (PBKDF2): per account, like the API's
            // account changes, so a signed-in browser cannot guess passwords or burn CPU at the global rate.
            o.AddPolicy(WebPolicies.AccountChangeLimit, ctx => PostWindow(ctx, "account", limits.WebAccountChangesPerMinute, TimeSpan.FromMinutes(1),
                ctx.User.FindFirst(System.Security.Claims.ClaimTypes.NameIdentifier)?.Value));
            // Friend and block buttons (the friends page, profiles): per account; the friend service has its own limits too.
            o.AddPolicy(WebPolicies.SocialLimit, ctx => PostWindow(ctx, "social", limits.WebSocialPerMinute, TimeSpan.FromMinutes(1),
                ctx.User.FindFirst(System.Security.Claims.ClaimTypes.NameIdentifier)?.Value));
            var api = o.OnRejected;
            o.OnRejected = async (context, ct) =>
            {
                if (!IsWebPath(context.HttpContext.Request.Path))
                {
                    if (api is not null) await api(context, ct);
                    return;
                }
                var retry = context.Lease.TryGetMetadata(MetadataName.RetryAfter, out var after) ? (int)Math.Ceiling(after.TotalSeconds) : 60;
                var response = context.HttpContext.Response;
                response.StatusCode = StatusCodes.Status429TooManyRequests;
                response.Headers.RetryAfter = retry.ToString(System.Globalization.CultureInfo.InvariantCulture);
                response.ContentType = "text/html; charset=utf-8";
                var root = System.Net.WebUtility.HtmlEncode(context.HttpContext.Request.PathBase.Value ?? "");
                await response.WriteAsync(
                    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\"><title>Too many attempts</title>" +
                    $"<link rel=\"stylesheet\" href=\"{root}/css/site.css\"></head><body><main class=\"page\"><h1>Too many attempts</h1>" +
                    $"<p>Please wait {retry} seconds and try again.</p><p><a href=\"{root}/\">Back to the home page</a></p></main></body></html>", ct);
            };
        });
        return services;
    }

    /// <summary>Website-only middleware: HTML error pages, https redirect, HSTS and security headers.</summary>
    public static IApplicationBuilder UseDw4Web(this IApplicationBuilder app, WebOptions options, bool development)
    {
        app.UseWhen(ctx => IsWebPath(ctx.Request.Path), web =>
        {
            if (!development) web.UseExceptionHandler("/error");
            web.UseStatusCodePagesWithReExecute("/error", "?code={0}");
            web.Use(async (ctx, next) =>
            {
                if (options.RequireHttps && !ctx.Request.IsHttps)
                {
                    if (HttpMethods.IsGet(ctx.Request.Method) || HttpMethods.IsHead(ctx.Request.Method))
                    {
                        ctx.Response.Redirect("https://" + ctx.Request.Host.Host + ctx.Request.PathBase + ctx.Request.Path + ctx.Request.QueryString);
                        return;
                    }
                    ctx.Response.StatusCode = StatusCodes.Status403Forbidden;
                    return;
                }
                var headers = ctx.Response.Headers;
                headers.ContentSecurityPolicy =
                    "default-src 'none'; style-src 'self'; img-src 'self' data:; form-action 'self'; frame-ancestors 'none'; base-uri 'none'";
                headers.XContentTypeOptions = "nosniff";
                headers.XFrameOptions = "DENY";
                headers["Referrer-Policy"] = "same-origin";
                headers["Cross-Origin-Opener-Policy"] = "same-origin";
                headers["Permissions-Policy"] = "camera=(), microphone=(), geolocation=()";
                if (options.Hsts && ctx.Request.IsHttps) headers.StrictTransportSecurity = "max-age=31536000";
                if (!ctx.Request.Path.StartsWithSegments("/css")) headers.CacheControl = "no-store";
                await next();
            });
        });
        return app;
    }

    public static IEndpointRouteBuilder MapDw4Web(this IEndpointRouteBuilder routes)
    {
        routes.MapRazorPages();
        routes.MapGet("/css/site.css", (HttpContext ctx) =>
        {
            ctx.Response.Headers.CacheControl = "public, max-age=3600";
            return Results.Text(Css, "text/css; charset=utf-8");
        }).AllowAnonymous();
        return routes;
    }

    /// <summary>Everything but the API, the health check and ACME challenges is a website path.</summary>
    public static bool IsWebPath(PathString path) =>
        !path.StartsWithSegments("/api") && !path.StartsWithSegments("/health") && !path.StartsWithSegments("/.well-known");

    /// <summary>A fixed window for POSTs, keyed by the account when one is given, else by the IP address.</summary>
    private static RateLimitPartition<string> PostWindow(HttpContext ctx, string name, int permits, TimeSpan window, string? account = null)
    {
        if (!HttpMethods.IsPost(ctx.Request.Method)) return RateLimitPartition.GetNoLimiter("get");
        // By address: IPv4 as a whole, IPv6 by its /64 network (one client, as for the login throttle).
        var key = name + ":" + (account is { Length: > 0 } ? "acc:" + account : DW4Online.Domain.Accounts.LoginThrottle.AddressKey(ctx.Connection.RemoteIpAddress));
        return RateLimitPartition.GetFixedWindowLimiter(key, _ => new FixedWindowRateLimiterOptions
        {
            PermitLimit = Math.Max(1, permits),
            Window = window,
            QueueLimit = 0,
        });
    }

    private static string LoadCss()
    {
        using var stream = typeof(WebSetup).Assembly.GetManifestResourceStream("DW4Online.Web.site.css")
            ?? throw new InvalidOperationException("site.css is not embedded");
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }
}

/// <summary>After an admin reset the temporary password must be changed before any other page works.</summary>
internal sealed class PasswordChangePageFilter : IAsyncPageFilter
{
    private static readonly HashSet<string> Allowed = new(StringComparer.OrdinalIgnoreCase) { "/Account/Index", "/Logout", "/Privacy", "/Error" };

    public Task OnPageHandlerSelectionAsync(PageHandlerSelectedContext context) => Task.CompletedTask;

    public async Task OnPageHandlerExecutionAsync(PageHandlerExecutingContext context, PageHandlerExecutionDelegate next)
    {
        if (context.HttpContext.WebSession() is { MustChangePassword: true } && !Allowed.Contains(context.ActionDescriptor.ViewEnginePath))
        {
            context.Result = new RedirectResult("~/account"); // "~/": below Service:PathBase
            return;
        }
        await next();
    }
}
