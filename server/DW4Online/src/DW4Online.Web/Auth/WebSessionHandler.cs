using System.Globalization;
using System.Security.Claims;
using System.Text.Encodings.Web;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Services;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace DW4Online.Web.Auth;

/// <summary>
/// Website sign-in: the cookie holds an opaque "web" session token (the same sessions table as the game, kind web,
/// 14 days sliding). Revoking the session (log out, log out everywhere, password change, staff lock) ends the cookie
/// at once. HttpOnly, SameSite=Lax, Secure on https ("__Host-" name there).
/// </summary>
public sealed class WebSessionHandler(
    IOptionsMonitor<AuthenticationSchemeOptions> options,
    ILoggerFactory logger,
    UrlEncoder encoder,
    AccountService accounts) : AuthenticationHandler<AuthenticationSchemeOptions>(options, logger, encoder)
{
    public const string SchemeName = "WebSession";
    internal const string SessionItem = "dw4.session";

    protected override async Task<AuthenticateResult> HandleAuthenticateAsync()
    {
        var token = Request.Cookies[WebSessions.CookieName(Request)];
        if (string.IsNullOrEmpty(token)) return AuthenticateResult.NoResult();
        var session = await accounts.AuthenticateAsync(token, Context.RequestAborted);
        if (session is null || session.Kind != SessionKind.Web)
        {
            if (!Response.HasStarted) WebSessions.SignOut(Context); // a stale cookie: drop it
            return AuthenticateResult.Fail("invalid or expired session");
        }
        Context.Items[SessionItem] = session;
        var identity = new ClaimsIdentity(
        [
            new Claim(ClaimTypes.NameIdentifier, session.AccountId.ToString(CultureInfo.InvariantCulture)),
            new Claim(ClaimTypes.Name, session.Username),
            new Claim(ClaimTypes.Role, session.Role),
            new Claim("dw4:session", session.SessionId.ToString(CultureInfo.InvariantCulture)),
            new Claim("dw4:kind", session.Kind),
        ], SchemeName);
        return AuthenticateResult.Success(new AuthenticationTicket(new ClaimsPrincipal(identity), SchemeName));
    }

    protected override Task HandleChallengeAsync(AuthenticationProperties properties)
    {
        var back = Request.PathBase + Request.Path + Request.QueryString;
        Response.Redirect("/login?returnUrl=" + Uri.EscapeDataString(back));
        return Task.CompletedTask;
    }

    protected override Task HandleForbiddenAsync(AuthenticationProperties properties)
    {
        Response.StatusCode = StatusCodes.Status403Forbidden;
        return Task.CompletedTask;
    }
}

public static class WebSessions
{
    private const string PlainName = "dw4_session";
    private const string HostName = "__Host-dw4_session"; // https only: Secure, Path=/, no Domain

    public static string CookieName(HttpRequest request) => request.IsHttps ? HostName : PlainName;

    /// <summary>The signed-in website session of the request, or null.</summary>
    public static AuthenticatedSession? WebSession(this HttpContext context) =>
        context.User.Identity?.AuthenticationType == WebSessionHandler.SchemeName
            ? context.Items[WebSessionHandler.SessionItem] as AuthenticatedSession
            : null;

    public static bool IsStaff(this AuthenticatedSession? session) => session?.Role is Roles.Moderator or Roles.Admin;

    /// <summary>Sets the session cookie; <paramref name="persistent"/> keeps it after the browser closes (until the session's expiry).</summary>
    public static void SignIn(HttpContext context, SessionTicket ticket, bool persistent)
    {
        var options = new CookieOptions
        {
            HttpOnly = true,
            Secure = context.Request.IsHttps,
            SameSite = SameSiteMode.Lax,
            Path = "/",
            IsEssential = true,
        };
        if (persistent) options.Expires = new DateTimeOffset(DateTime.SpecifyKind(ticket.ExpiresAt, DateTimeKind.Utc));
        context.Response.Cookies.Append(CookieName(context.Request), ticket.Token, options);
    }

    public static void SignOut(HttpContext context) =>
        context.Response.Cookies.Delete(CookieName(context.Request), new CookieOptions
        {
            HttpOnly = true,
            Secure = context.Request.IsHttps,
            SameSite = SameSiteMode.Lax,
            Path = "/",
        });

    /// <summary>The caller's address as 4 or 16 bytes (IPv4-mapped addresses as 4) and a device label for new sessions.</summary>
    public static CallerInfo Caller(this HttpContext context, string? deviceName = null)
    {
        var ip = context.Connection.RemoteIpAddress;
        if (ip is not null && ip.IsIPv4MappedToIPv6) ip = ip.MapToIPv4();
        return new CallerInfo(ip?.GetAddressBytes(), deviceName);
    }

    /// <summary>"Website (Firefox)" style device label from the user agent.</summary>
    public static string DeviceLabel(HttpRequest request)
    {
        var agent = request.Headers.UserAgent.ToString();
        string[] known = ["Edg", "OPR", "Firefox", "Chrome", "Safari"];
        var browser = known.FirstOrDefault(k => agent.Contains(k, StringComparison.Ordinal)) switch
        {
            "Edg" => "Edge",
            "OPR" => "Opera",
            null => "browser",
            var k => k,
        };
        return $"Website ({browser})";
    }
}
