using System.Net;
using System.Net.Sockets;
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
/// <item><b>X-Forwarded-For</b>: its last entry is the address the local proxy saw (every proxy appends it or writes
/// the header anew; entries further left were written by whoever sent the request, and never count). When that address
/// lies in Service:TrustedProxyNetworks (Cloudflare's edges), the client's address is the edge's
/// <b>CF-Connecting-IP</b>: Cloudflare sets that header itself on every request through the site's zone, so the local
/// proxy keeps its default (it trusts no peer's X-Forwarded-* headers; Traefik then replaces them for every site it
/// serves). A CF-Connecting-IP that is no single public address (loopback, private, link-local, an edge itself) keeps the
/// edge's address; an edge connecting to the service directly proves nothing (not behind the local proxy).</item>
/// <item><b>X-Forwarded-Proto</b>: the last value, http or https.</item>
/// <item><b>X-Forwarded-Prefix</b>: only Service:PathBase itself (letter case aside): a stripping proxy (Traefik's
/// stripPrefix) names the prefix it removed. Any other value, or the header without Service:PathBase, counts for nothing:
/// it could be a client's own header that a proxy passed on.</item>
/// </list>
/// Then Service:PathBase as UsePathBase does: a path that starts with it moves it into the path base (a non-stripping
/// proxy, or a direct request below the path); a path without it keeps working at the root, unless the local proxy named
/// it as its stripped prefix. The base is never doubled.
/// </summary>
public sealed class ProxyHeaders
{
    public const string ForwardedFor = "X-Forwarded-For";
    public const string ForwardedProto = "X-Forwarded-Proto";
    public const string ForwardedPrefix = "X-Forwarded-Prefix";
    /// <summary>Cloudflare's header with the address that connected to its edge (one address, set by Cloudflare).</summary>
    public const string ConnectingIp = "CF-Connecting-IP";

    // Addresses no browser has on the internet (beside loopback, unspecified and the IPv6 scopes .NET names). Not 240.0.0.0/4:
    // Cloudflare's "Pseudo IPv4: Overwrite Headers" names IPv6 visitors by such addresses.
    private static readonly IPNetwork[] NotPublic =
    [
        IPNetwork.Parse("0.0.0.0/8"), IPNetwork.Parse("10.0.0.0/8"), IPNetwork.Parse("100.64.0.0/10"), IPNetwork.Parse("127.0.0.0/8"),
        IPNetwork.Parse("169.254.0.0/16"), IPNetwork.Parse("172.16.0.0/12"), IPNetwork.Parse("192.168.0.0/16"),
        IPNetwork.Parse("224.0.0.0/4"), IPNetwork.Parse("255.255.255.255/32"), IPNetwork.Parse("fc00::/7"),
    ];

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
    /// Service:TrustedProxyNetworks (CIDR networks in their plain form, only with Service:BehindProxy; none wider than /8
    /// for IPv4 or /16 for IPv6, which would make anyone an edge). Throws an <see cref="InvalidOperationException"/> that
    /// names the setting.
    /// </summary>
    public static ProxyHeaders From(ServiceOptions service)
    {
        var edges = new List<IPNetwork>();
        foreach (var raw in service.TrustedProxyNetworks ?? [])
        {
            if (string.IsNullOrWhiteSpace(raw)) continue;
            var text = raw.Trim();
            var slash = text.IndexOf('/');
            if (slash <= 0 || !IPAddress.TryParse(text[..slash], out var address) || !IPNetwork.TryParse(text, out var network))
                throw new InvalidOperationException(
                    $"Service:TrustedProxyNetworks: '{raw}' is not a network in CIDR form such as 173.245.48.0/20 or 2400:cb00::/32.");
            // An IPv4-mapped IPv6 network never matches: addresses are compared as IPv4 (Normal).
            if (address.IsIPv4MappedToIPv6)
                throw new InvalidOperationException(
                    $"Service:TrustedProxyNetworks: '{raw}' is an IPv4-mapped IPv6 network, which never matches; write it as IPv4 " +
                    $"({address.MapToIPv4()}/{Math.Max(0, network.PrefixLength - 96)}).");
            // Host bits set (198.51.100.1/24): a typo for another network, or a single address meant as /32.
            if (!network.BaseAddress.Equals(address))
                throw new InvalidOperationException(
                    $"Service:TrustedProxyNetworks: '{raw}' has bits set after its prefix length; the network is {network.BaseAddress}/" +
                    $"{network.PrefixLength} (a single address: /{(address.AddressFamily == AddressFamily.InterNetwork ? 32 : 128)}).");
            var minimum = network.BaseAddress.AddressFamily == AddressFamily.InterNetwork ? 8 : 16;
            if (network.PrefixLength < minimum)
                throw new InvalidOperationException(
                    $"Service:TrustedProxyNetworks: '{raw}' is wider than /{minimum}; list the proxy's own networks (DEPLOY.md section 6.2).");
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
        var strippedBase = false;
        if (BehindProxy && !request.IsHttps && Normal(context.Connection.RemoteIpAddress) is { } peer && IPAddress.IsLoopback(peer))
        {
            var headers = request.Headers;
            if (Client(headers[ForwardedFor], headers[ConnectingIp], Edges) is { } client)
            {
                context.Connection.RemoteIpAddress = client.Address;
                context.Connection.RemotePort = client.Port;
            }
            if (Last(headers[ForwardedProto]) is { } proto &&
                (proto.Equals("https", StringComparison.OrdinalIgnoreCase) || proto.Equals("http", StringComparison.OrdinalIgnoreCase)))
                request.Scheme = proto.ToLowerInvariant();
            strippedBase = _pathBase.HasValue && Prefix(Last(headers[ForwardedPrefix])) is { } prefix &&
                           string.Equals(prefix.Value, _pathBase.Value, StringComparison.OrdinalIgnoreCase);
        }
        if (_pathBase.HasValue && request.Path.StartsWithSegments(_pathBase, StringComparison.OrdinalIgnoreCase, out var matched, out var rest))
        {
            // Below Service:PathBase (a direct request, a non-stripping proxy, or one that kept the path and named it too).
            request.PathBase = request.PathBase.Add(matched);
            request.Path = rest;
        }
        else if (strippedBase)
        {
            // A stripping proxy removed Service:PathBase from the path and named it: links, redirects and cookies get it back.
            request.PathBase = request.PathBase.Add(_pathBase);
        }
    }

    /// <summary>
    /// The client's address from the local proxy's X-Forwarded-For (its last entry, the address the proxy saw) and, when
    /// that address lies in one of <paramref name="edges"/>, the edge's <paramref name="connectingIp"/> (CF-Connecting-IP)
    /// if it is one public address (port 0); otherwise the edge's address stays. Null when the last X-Forwarded-For entry is
    /// no address (the headers are then ignored).
    /// </summary>
    public static IPEndPoint? Client(StringValues forwardedFor, StringValues connectingIp, IReadOnlyList<IPNetwork> edges)
    {
        var entries = Entries(forwardedFor);
        if (entries.Count == 0 || Parse(entries[^1]) is not { } last) return null;
        if (edges.Any(n => n.Contains(last.Address)) && EdgeClient(connectingIp, edges) is { } client) return new IPEndPoint(client, 0);
        return last;
    }

    /// <summary>The address in an edge's CF-Connecting-IP: exactly one public address, not an edge's own; else null.</summary>
    private static IPAddress? EdgeClient(StringValues connectingIp, IReadOnlyList<IPNetwork> edges)
    {
        var entries = Entries(connectingIp);
        if (entries.Count != 1 || !IPAddress.TryParse(entries[0], out var parsed)) return null;
        var address = Normal(parsed)!;
        if (IPAddress.IsLoopback(address) || address.Equals(IPAddress.IPv6Any) || address.IsIPv6LinkLocal || address.IsIPv6SiteLocal ||
            address.IsIPv6Multicast || NotPublic.Any(n => n.Contains(address)) || edges.Any(n => n.Contains(address)))
            return null;
        return address;
    }

    /// <summary>
    /// The X-Forwarded-Prefix value as a path: a plain path of letters, digits and - _ . ~ in non-empty segments (no "." or
    /// ".." segment), "/dw4online" for "/dw4online/" or "dw4online"; null for anything else. It counts only when it is
    /// Service:PathBase (<see cref="Apply"/>).
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
