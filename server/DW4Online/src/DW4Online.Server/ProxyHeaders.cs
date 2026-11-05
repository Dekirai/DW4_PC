using System.Net;
using DW4Online.Domain;
using Microsoft.Extensions.Primitives;

namespace DW4Online.Server;

/// <summary>
/// The request as the browser sent it when a reverse proxy stands in front of the service (Service:BehindProxy), and the
/// service's own path base (Service:PathBase); deploy/DEPLOY.md section 6.2. Runs first in the pipeline (in place of
/// ASP.NET's ForwardedHeaders and UsePathBase middleware).
/// <para>
/// Forwarded headers count only on a plain-HTTP connection from this machine (the local proxy's hop to the loopback
/// endpoint, 127.0.0.1:5280): never from a direct client, and never on the HTTPS endpoint, so a connection a proxy only
/// passes through (TLS passthrough to the HTTPS port) cannot name an address either. From that local proxy:
/// </para>
/// <list type="bullet">
/// <item><b>X-Forwarded-For</b>: its last entry is the address the local proxy saw. When that address lies in
/// Service:TrustedProxyNetworks (an edge proxy such as Cloudflare), the entry before it (which that edge appended) is the
/// client's. One edge hop only, and an edge is never believed as the connection's own peer: entries further left were
/// written by whoever sent the request to the edge, and a request from such a network straight to the service (an edge
/// that is not behind the local proxy) proves nothing.</item>
/// <item><b>X-Forwarded-Proto</b>: the last value, http or https.</item>
/// <item><b>X-Forwarded-Prefix</b>: the last value (the one the local proxy added, ASP.NET's
/// ForwardedHeaders.XForwardedPrefix reads the same header), a plain path such as /dw4online: the path the proxy
/// stripped. A proxy that keeps the path and still names the prefix has its request path start with it: the prefix is
/// then cut from the path once, so it never doubles.</item>
/// </list>
/// Then Service:PathBase as UsePathBase does: a path that starts with it moves it into the path base (a non-stripping
/// proxy, or a direct request below the path); a path without it keeps working at the root. Both together: a stripping
/// proxy's request has no Service:PathBase in its path any more, so the base is the forwarded prefix alone.
/// </summary>
public sealed class ProxyHeaders
{
    public const string ForwardedFor = "X-Forwarded-For";
    public const string ForwardedProto = "X-Forwarded-Proto";
    public const string ForwardedPrefix = "X-Forwarded-Prefix";

    private readonly PathString _pathBase;

    public ProxyHeaders(bool behindProxy, string pathBase, IReadOnlyList<IPNetwork> edges)
    {
        BehindProxy = behindProxy;
        _pathBase = pathBase.Length > 0 ? new PathString(pathBase) : PathString.Empty;
        Edges = edges;
    }

    public bool BehindProxy { get; }

    /// <summary>Service:TrustedProxyNetworks, parsed.</summary>
    public IReadOnlyList<IPNetwork> Edges { get; }

    /// <summary>
    /// The settings of section "Service", checked: Service:PathBase (<see cref="ServiceOptions.NormalizedPathBase"/>) and
    /// Service:TrustedProxyNetworks (CIDR networks, only with Service:BehindProxy; none wider than /8 for IPv4 or /16 for
    /// IPv6, which would make anyone an edge). Throws an <see cref="InvalidOperationException"/> that names the setting.
    /// </summary>
    public static ProxyHeaders From(ServiceOptions service)
    {
        var edges = new List<IPNetwork>();
        foreach (var text in service.TrustedProxyNetworks ?? [])
        {
            if (string.IsNullOrWhiteSpace(text)) continue;
            if (!IPNetwork.TryParse(text.Trim(), out var network))
                throw new InvalidOperationException(
                    $"Service:TrustedProxyNetworks: '{text}' is not a network in CIDR form such as 173.245.48.0/20 or 2400:cb00::/32.");
            var minimum = network.BaseAddress.AddressFamily == System.Net.Sockets.AddressFamily.InterNetwork ? 8 : 16;
            if (network.PrefixLength < minimum)
                throw new InvalidOperationException(
                    $"Service:TrustedProxyNetworks: '{text}' is wider than /{minimum}; list the proxy's own networks (DEPLOY.md section 6.2).");
            edges.Add(network);
        }
        if (edges.Count > 0 && !service.BehindProxy)
            throw new InvalidOperationException(
                "Service:TrustedProxyNetworks is set but Service:BehindProxy is false: the networks count only behind the local proxy (DEPLOY.md section 6.2).");
        return new ProxyHeaders(service.BehindProxy, service.NormalizedPathBase(), edges);
    }

    /// <summary>Applies the forwarded headers and the path base to the request (before every other middleware).</summary>
    public void Apply(HttpContext context)
    {
        var request = context.Request;
        if (BehindProxy && !request.IsHttps && Normal(context.Connection.RemoteIpAddress) is { } peer && IPAddress.IsLoopback(peer))
        {
            var headers = request.Headers;
            if (Client(headers[ForwardedFor], Edges) is { } client)
            {
                context.Connection.RemoteIpAddress = client.Address;
                context.Connection.RemotePort = client.Port;
            }
            if (Last(headers[ForwardedProto]) is { } proto &&
                (proto.Equals("https", StringComparison.OrdinalIgnoreCase) || proto.Equals("http", StringComparison.OrdinalIgnoreCase)))
                request.Scheme = proto.ToLowerInvariant();
            if (Prefix(Last(headers[ForwardedPrefix])) is { } prefix)
            {
                // A proxy that kept the path and named the prefix as well: cut it once, or the base would double.
                if (request.Path.StartsWithSegments(prefix, StringComparison.OrdinalIgnoreCase, out var kept)) request.Path = kept;
                request.PathBase = request.PathBase.Add(prefix);
            }
        }
        if (_pathBase.HasValue && request.Path.StartsWithSegments(_pathBase, StringComparison.OrdinalIgnoreCase, out var matched, out var rest))
        {
            request.PathBase = request.PathBase.Add(matched);
            request.Path = rest;
        }
    }

    /// <summary>
    /// The client's address from the local proxy's X-Forwarded-For: its last entry, or, when that entry lies in one of
    /// <paramref name="edges"/>, the entry before it. Null when the last entry is no address (the header is then ignored;
    /// an edge with no usable entry before it stays the address).
    /// </summary>
    public static IPEndPoint? Client(StringValues forwardedFor, IReadOnlyList<IPNetwork> edges)
    {
        var entries = Entries(forwardedFor);
        if (entries.Count == 0 || Parse(entries[^1]) is not { } last) return null;
        if (entries.Count >= 2 && edges.Any(n => n.Contains(last.Address)) && Parse(entries[^2]) is { } client) return client;
        return last;
    }

    /// <summary>
    /// The X-Forwarded-Prefix value as a path base: a plain path of letters, digits and - _ . ~ in non-empty segments (no
    /// "." or ".." segment), "/dw4online" for "/dw4online/" or "dw4online"; null for anything else (the header is ignored).
    /// </summary>
    public static PathString? Prefix(string? value)
    {
        if (value is null) return null;
        var trimmed = value.Trim().Trim('/');
        if (trimmed.Length == 0 || trimmed.Length > 200) return null;
        foreach (var segment in trimmed.Split('/'))
        {
            if (segment.Length == 0 || segment is "." or "..") return null;
            if (!segment.All(c => char.IsAsciiLetterOrDigit(c) || c is '-' or '_' or '.' or '~')) return null;
        }
        return new PathString("/" + trimmed);
    }

    private static List<string> Entries(StringValues values)
    {
        var list = new List<string>();
        foreach (var value in values)
        {
            if (value is null) continue;
            foreach (var part in value.Split(','))
                if (part.Trim() is { Length: > 0 } entry) list.Add(entry);
        }
        return list;
    }

    private static string? Last(StringValues values) => Entries(values) is { Count: > 0 } list ? list[^1] : null;

    private static IPEndPoint? Parse(string entry)
    {
        if (!IPEndPoint.TryParse(entry, out var endpoint)) return null;
        return new IPEndPoint(Normal(endpoint.Address)!, endpoint.Port);
    }

    private static IPAddress? Normal(IPAddress? address) =>
        address is { IsIPv4MappedToIPv6: true } ? address.MapToIPv4() : address;
}
