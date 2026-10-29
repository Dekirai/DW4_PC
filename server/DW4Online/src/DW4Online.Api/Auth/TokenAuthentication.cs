using System.Security.Claims;
using System.Text.Encodings.Web;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Services;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace DW4Online.Api.Auth;

/// <summary>
/// Bearer authentication with opaque session tokens ("Authorization: Bearer dw4_..."). The token's SHA-256 is
/// looked up in the sessions table; a valid session puts its <see cref="AuthenticatedSession"/> into the request.
/// </summary>
public sealed class TokenAuthenticationHandler(
    IOptionsMonitor<AuthenticationSchemeOptions> options,
    ILoggerFactory logger,
    UrlEncoder encoder,
    AccountService accounts) : AuthenticationHandler<AuthenticationSchemeOptions>(options, logger, encoder)
{
    public const string SchemeName = "Token";
    internal const string SessionItem = "dw4.session";

    protected override async Task<AuthenticateResult> HandleAuthenticateAsync()
    {
        var header = Request.Headers.Authorization.ToString();
        if (!header.StartsWith("Bearer ", StringComparison.Ordinal)) return AuthenticateResult.NoResult();
        var session = await accounts.AuthenticateAsync(header[7..].Trim(), Context.RequestAborted);
        if (session is null) return AuthenticateResult.Fail("invalid or expired token");
        Context.Items[SessionItem] = session;
        var identity = new ClaimsIdentity(
        [
            new Claim(ClaimTypes.NameIdentifier, session.AccountId.ToString(System.Globalization.CultureInfo.InvariantCulture)),
            new Claim(ClaimTypes.Name, session.Username),
            new Claim(ClaimTypes.Role, session.Role),
            new Claim("dw4:session", session.SessionId.ToString(System.Globalization.CultureInfo.InvariantCulture)),
            new Claim("dw4:kind", session.Kind),
        ], SchemeName);
        return AuthenticateResult.Success(new AuthenticationTicket(new ClaimsPrincipal(identity), SchemeName));
    }

    protected override Task HandleChallengeAsync(AuthenticationProperties properties) =>
        ApiProblems.Write(Context, StatusCodes.Status401Unauthorized, "unauthorized", "Log in first (Authorization: Bearer <token>).");

    protected override Task HandleForbiddenAsync(AuthenticationProperties properties) =>
        ApiProblems.Write(Context, StatusCodes.Status403Forbidden, "forbidden", "Your account may not do this.");
}

public static class SessionExtensions
{
    /// <summary>The authenticated session of the request (only on endpoints that require authorization).</summary>
    public static AuthenticatedSession Session(this HttpContext context) =>
        context.Items[TokenAuthenticationHandler.SessionItem] as AuthenticatedSession
        ?? throw new InvalidOperationException("The endpoint needs RequireAuthorization().");

    public static AuthenticatedSession? SessionOrNull(this HttpContext context) =>
        context.Items[TokenAuthenticationHandler.SessionItem] as AuthenticatedSession;

    /// <summary>The caller's address as 4 or 16 bytes (IPv4-mapped addresses as 4).</summary>
    public static byte[]? IpBytes(this HttpContext context)
    {
        var ip = context.Connection.RemoteIpAddress;
        if (ip is null) return null;
        if (ip.IsIPv4MappedToIPv6) ip = ip.MapToIPv4();
        return ip.GetAddressBytes();
    }

    public static CallerInfo Caller(this HttpContext context, string? deviceName = null) => new(context.IpBytes(), deviceName);
}

public static class Policies
{
    public const string Player = "player";
    public const string Staff = "staff";
    public const string Admin = "admin";
}
