using System.Net;
using DW4Online.Domain;
using DW4Online.Server;
using Microsoft.AspNetCore.Http;

namespace DW4Online.Tests;

/// <summary>
/// The forwarded headers behind a reverse proxy and the path base (ProxyHeaders, DEPLOY.md section 6.2): who may name the
/// client address (the local proxy, and behind it a trusted edge's CF-Connecting-IP), the scheme and the path prefix, and
/// how a forwarded prefix and Service:PathBase combine. The whole service with these settings: PathBaseTests and
/// ReverseProxyTests (integration).
/// </summary>
public class ProxyHeadersTests
{
    // Documentation addresses: 198.51.100.0/24 and 2001:db8:edge::/48 stand in for the edge (Cloudflare's ranges),
    // 203.0.113.0/24 and 2001:db8:c::/48 for clients, 192.0.2.0/24 for addresses a client made up.
    private static readonly string[] EdgeNetworks = ["198.51.100.0/24", "2001:db8:ed9e::/48"];

    private static ProxyHeaders Proxy(bool behindProxy = true, string pathBase = "", string[]? edges = null) =>
        ProxyHeaders.From(new ServiceOptions { BehindProxy = behindProxy, PathBase = pathBase, TrustedProxyNetworks = edges ?? [] });

    private static DefaultHttpContext Request(string peer, string path, params (string Name, string Value)[] headers)
    {
        var context = new DefaultHttpContext();
        context.Connection.RemoteIpAddress = IPAddress.Parse(peer);
        context.Connection.RemotePort = 50000;
        context.Request.Scheme = "http";
        context.Request.Path = new PathString(path);
        foreach (var (name, value) in headers) context.Request.Headers.Append(name, value);
        return context;
    }

    private static string Ip(DefaultHttpContext context) => context.Connection.RemoteIpAddress!.ToString();

    // ---- (b) the client address --------------------------------------------------------------------------------------

    [Theory]
    // Traefik saw the client itself (a request straight to Traefik): a CF-Connecting-IP it carries is the client's own.
    [InlineData("203.0.113.9", null, "203.0.113.9")]
    [InlineData("203.0.113.9", "192.0.2.66", "203.0.113.9")]
    // Through Cloudflare, Traefik at its default (it replaced X-Forwarded-For with the edge it saw): Cloudflare's own
    // CF-Connecting-IP is the browser.
    [InlineData("198.51.100.7", "203.0.113.10", "203.0.113.10")]
    [InlineData("2001:db8:ed9e::1", "2001:db8:c::5", "2001:db8:c::5")]
    [InlineData("198.51.100.7:443", "203.0.113.14", "203.0.113.14")]
    [InlineData("::ffff:198.51.100.7", "::ffff:203.0.113.15", "203.0.113.15")]
    // A local proxy that keeps the incoming X-Forwarded-For (Traefik with forwardedHeaders.trustedIPs, nginx's
    // $proxy_add_x_forwarded_for): its last entry is still the edge it saw; the entries before it never count.
    [InlineData("192.0.2.66, 203.0.113.11, 198.51.100.7", "203.0.113.11", "203.0.113.11")]
    [InlineData("203.0.113.12, 198.51.100.7", null, "198.51.100.7")]
    [InlineData("192.0.2.69, 203.0.113.13", "192.0.2.70", "203.0.113.13")]
    // No usable CF-Connecting-IP behind an edge: the edge stays the address (as without the setting).
    [InlineData("198.51.100.8", null, "198.51.100.8")]
    [InlineData("198.51.100.8", "", "198.51.100.8")]
    [InlineData("198.51.100.8", "not-an-address", "198.51.100.8")]
    [InlineData("198.51.100.8", "203.0.113.16, 203.0.113.17", "198.51.100.8")] // Cloudflare sends one address
    [InlineData("198.51.100.8", "203.0.113.18:80", "198.51.100.8")]
    // Garbage in X-Forwarded-For: an unusable last entry ignores the headers (the proxy's own address stays).
    [InlineData("203.0.113.19, unknown", "203.0.113.20", "127.0.0.1")]
    [InlineData("", "203.0.113.21", "127.0.0.1")]
    public void The_client_address_is_the_local_proxys_view_or_behind_an_edge_its_CF_Connecting_IP(string forwardedFor, string? connectingIp,
        string expected)
    {
        var context = connectingIp is null
            ? Request("127.0.0.1", "/", (ProxyHeaders.ForwardedFor, forwardedFor))
            : Request("127.0.0.1", "/", (ProxyHeaders.ForwardedFor, forwardedFor), (ProxyHeaders.ConnectingIp, connectingIp));
        Proxy(edges: EdgeNetworks).Apply(context);
        Assert.Equal(expected, Ip(context));
    }

    /// <summary>
    /// An address no browser has on the internet never comes from an edge's CF-Connecting-IP (a request that reaches the
    /// local proxy from an edge's network without passing through the site's zone, DEPLOY.md 6.2, could name one): the
    /// edge stays. Loopback in its odd spellings too (IPAddress.Parse reads "2130706433" and "0x7f.1" as 127.0.0.1).
    /// </summary>
    [Theory]
    [InlineData("127.0.0.1")]
    [InlineData("127.9.9.9")]
    [InlineData("2130706433")]
    [InlineData("0x7f.1")]
    [InlineData("::1")]
    [InlineData("::ffff:127.0.0.1")]
    [InlineData("0.0.0.0")]
    [InlineData("::")]
    [InlineData("10.1.2.3")]
    [InlineData("172.16.5.5")]
    [InlineData("192.168.1.1")]
    [InlineData("100.64.0.1")]
    [InlineData("169.254.1.1")]
    [InlineData("fe80::1")]
    [InlineData("fec0::1")]
    [InlineData("fd00::1")]
    [InlineData("224.0.0.1")]
    [InlineData("255.255.255.255")]
    [InlineData("ff02::1")]
    [InlineData("198.51.100.9")]      // an edge itself
    [InlineData("2001:db8:ed9e::42")]
    public void An_edge_never_names_an_address_no_browser_has(string connectingIp)
    {
        var context = Request("127.0.0.1", "/", (ProxyHeaders.ForwardedFor, "198.51.100.7"), (ProxyHeaders.ConnectingIp, connectingIp));
        Proxy(edges: EdgeNetworks).Apply(context);
        Assert.Equal("198.51.100.7", Ip(context));
    }

    [Fact]
    public void Several_header_lines_are_one_list()
    {
        var context = Request("127.0.0.1", "/", (ProxyHeaders.ForwardedFor, "192.0.2.70, 203.0.113.17"), (ProxyHeaders.ForwardedFor, "198.51.100.7"),
            (ProxyHeaders.ConnectingIp, "203.0.113.22"));
        Proxy(edges: EdgeNetworks).Apply(context);
        Assert.Equal("203.0.113.22", Ip(context));
        // Two CF-Connecting-IP lines are two addresses: not Cloudflare's single one.
        var twice = Request("127.0.0.1", "/", (ProxyHeaders.ForwardedFor, "198.51.100.7"), (ProxyHeaders.ConnectingIp, "203.0.113.23"),
            (ProxyHeaders.ConnectingIp, "203.0.113.24"));
        Proxy(edges: EdgeNetworks).Apply(twice);
        Assert.Equal("198.51.100.7", Ip(twice));
    }

    [Fact]
    public void Without_trusted_networks_the_local_proxys_view_counts()
    {
        var context = Request("127.0.0.1", "/", (ProxyHeaders.ForwardedFor, "203.0.113.18, 198.51.100.7"), (ProxyHeaders.ConnectingIp, "203.0.113.18"));
        Proxy().Apply(context);
        Assert.Equal("198.51.100.7", Ip(context)); // (what BehindProxy did before: the edge, never further)
        var ipv6 = Request("::1", "/", (ProxyHeaders.ForwardedFor, "203.0.113.19"));
        Proxy().Apply(ipv6);
        Assert.Equal("203.0.113.19", Ip(ipv6));
    }

    [Theory]
    // A direct client (the game at https://88.198.52.221:8443, or anyone at Kestrel's port) names nothing.
    [InlineData("203.0.113.50")]
    // An edge connecting to the service itself (a Cloudflare address, not behind the local proxy) is not trusted either.
    [InlineData("198.51.100.7")]
    [InlineData("2001:db8:ed9e::9")]
    public void Nobody_but_the_local_proxy_names_an_address_a_scheme_or_a_prefix(string peer)
    {
        var context = Request(peer, "/account", (ProxyHeaders.ForwardedFor, "192.0.2.77"), (ProxyHeaders.ForwardedProto, "https"),
            (ProxyHeaders.ForwardedPrefix, "/dw4online"), (ProxyHeaders.ConnectingIp, "192.0.2.78"));
        Proxy(pathBase: "/dw4online", edges: EdgeNetworks).Apply(context);
        Assert.Equal(peer, Ip(context));
        Assert.Equal("http", context.Request.Scheme);
        Assert.Equal("", context.Request.PathBase.Value ?? "");
        Assert.Equal("/account", context.Request.Path.Value);
    }

    [Fact]
    public void The_https_endpoint_never_takes_forwarded_headers()
    {
        // A proxy that only passes TLS through (a TCP router) connects from 127.0.0.1 to the HTTPS port: the headers are
        // the client's own.
        var context = Request("127.0.0.1", "/", (ProxyHeaders.ForwardedFor, "198.51.100.7"), (ProxyHeaders.ConnectingIp, "192.0.2.78"),
            (ProxyHeaders.ForwardedPrefix, "/dw4online"));
        context.Request.Scheme = "https";
        Proxy(pathBase: "/dw4online", edges: EdgeNetworks).Apply(context);
        Assert.Equal("127.0.0.1", Ip(context));
        Assert.Equal("", context.Request.PathBase.Value ?? "");
    }

    [Fact]
    public void Without_BehindProxy_the_headers_count_for_nothing()
    {
        var context = Request("127.0.0.1", "/", (ProxyHeaders.ForwardedFor, "192.0.2.79"), (ProxyHeaders.ForwardedProto, "https"),
            (ProxyHeaders.ForwardedPrefix, "/dw4online"), (ProxyHeaders.ConnectingIp, "192.0.2.80"));
        Proxy(behindProxy: false, pathBase: "/dw4online").Apply(context);
        Assert.Equal("127.0.0.1", Ip(context));
        Assert.Equal("http", context.Request.Scheme);
        Assert.Equal("", context.Request.PathBase.Value ?? "");
    }

    [Theory]
    [InlineData("https", "https")]
    [InlineData("HTTPS", "https")]
    [InlineData("http, https", "https")] // the last value: the local proxy's
    [InlineData("https, http", "http")]
    [InlineData("wss", "http")]          // anything else is ignored
    [InlineData("javascript", "http")]
    public void The_scheme_is_the_local_proxys_last_value(string proto, string expected)
    {
        var context = Request("127.0.0.1", "/", (ProxyHeaders.ForwardedProto, proto));
        Proxy().Apply(context);
        Assert.Equal(expected, context.Request.Scheme);
    }

    [Theory]
    [InlineData("173.245.48.0/20")]
    [InlineData("2400:cb00::/32")]
    [InlineData(" 104.16.0.0/13 ")]
    public void Trusted_networks_are_cidr(string network) => Assert.Single(Proxy(edges: [network]).Edges);

    [Theory]
    [InlineData("173.245.48.0", "not a network")]      // no prefix length
    [InlineData("cloudflare", "not a network")]
    [InlineData("/20", "not a network")]
    [InlineData("173.245.48.0/33", "not a network")]
    [InlineData("0.0.0.0/0", "wider than /8")]         // everyone an edge
    [InlineData("10.0.0.0/7", "wider than /8")]
    [InlineData("::/0", "wider than /16")]
    [InlineData("2000::/3", "wider than /16")]
    // Never matching (addresses are compared as IPv4) or not the network written (host bits set): refused, naming the
    // network meant (the review's probe found both taken silently).
    [InlineData("::ffff:198.51.100.0/120", "write it as IPv4 (198.51.100.0/24)")]
    [InlineData("::ffff:c633:6400/120", "write it as IPv4 (198.51.100.0/24)")]
    [InlineData("198.51.100.1/24", "the network is 198.51.100.0/24")]
    [InlineData("2400:cb00::1/32", "the network is 2400:cb00::/32")]
    [InlineData("104.16.0.1/13", "the network is 104.16.0.0/13")]
    public void Wrong_or_too_wide_networks_stop_the_start(string network, string why)
    {
        var e = Assert.Throws<InvalidOperationException>(() => Proxy(edges: [network]));
        Assert.Contains("Service:TrustedProxyNetworks", e.Message);
        Assert.Contains(why, e.Message);
    }

    [Fact]
    public void Trusted_networks_need_BehindProxy()
    {
        var e = Assert.Throws<InvalidOperationException>(() => Proxy(behindProxy: false, edges: ["173.245.48.0/20"]));
        Assert.Contains("Service:BehindProxy", e.Message);
        Assert.Empty(Proxy(behindProxy: false, edges: ["", " "]).Edges); // (empty entries are no networks)
    }

    // ---- (a) the path prefix -----------------------------------------------------------------------------------------

    /// <summary>
    /// Every way the service is reached: (path base, forwarded prefix or none, the path the service receives) gives the
    /// path base and path the pages build their links, redirects and cookie paths from. X-Forwarded-Prefix counts only as
    /// Service:PathBase itself (the review's case: Traefik without stripPrefix that trusts Cloudflare passed a browser's
    /// own X-Forwarded-Prefix on, and its links, cookie paths and redirects went below "/evil/dw4online").
    /// </summary>
    [Theory]
    // No proxy, no path base.
    [InlineData("", null, "/account", "", "/account")]
    // Service:PathBase only (a direct request below it, or a non-stripping proxy without X-Forwarded-Prefix); the root
    // keeps working.
    [InlineData("/dw4online", null, "/dw4online/account", "/dw4online", "/account")]
    [InlineData("/dw4online", null, "/dw4online", "/dw4online", "")]
    [InlineData("/dw4online", null, "/account", "", "/account")]
    // Service:PathBase and a stripping proxy (Traefik stripPrefix, X-Forwarded-Prefix: /dw4online).
    [InlineData("/dw4online", "/dw4online", "/account", "/dw4online", "/account")]
    [InlineData("/dw4online", "/dw4online", "/", "/dw4online", "/")]
    [InlineData("/dw4online", "/dw4online", "", "/dw4online", "")]
    [InlineData("/dw4online", "/DW4Online", "/account", "/dw4online", "/account")]
    [InlineData("/dw4online", "/dw4online/", "/account", "/dw4online", "/account")]
    [InlineData("/dw4online", "dw4online", "/account", "/dw4online", "/account")]
    // A non-stripping proxy that names its prefix as well: cut once, never doubled.
    [InlineData("/dw4online", "/dw4online", "/dw4online/account", "/dw4online", "/account")]
    [InlineData("/dw4online", "/DW4Online", "/dw4online/account", "/dw4online", "/account")]
    // Any other prefix counts for nothing (a browser's own header that a proxy passed on).
    [InlineData("/dw4online", "/evil", "/dw4online/css/site.css", "/dw4online", "/css/site.css")]
    [InlineData("/dw4online", "/evil", "/account", "", "/account")]
    [InlineData("/dw4online", "/games", "/dw4online/account", "/dw4online", "/account")]
    [InlineData("/dw4online", "/evil/dw4online", "/account", "", "/account")]
    [InlineData("/dw4online", "/dw4online/evil", "/account", "", "/account")]
    // Without Service:PathBase the header counts for nothing at all (DEPLOY.md 6.2: PathBase in both Traefik variants).
    [InlineData("", "/dw4online", "/account", "", "/account")]
    [InlineData("", "/dw4online", "/dw4online/account", "", "/dw4online/account")]
    [InlineData("", "/evil", "/account", "", "/account")]
    public void A_forwarded_prefix_and_the_path_base_combine_without_doubling(string pathBase, string? prefix, string path,
        string expectedBase, string expectedPath)
    {
        var context = prefix is null ? Request("127.0.0.1", path) : Request("127.0.0.1", path, (ProxyHeaders.ForwardedPrefix, prefix));
        Proxy(pathBase: pathBase).Apply(context);
        Assert.Equal(expectedBase, context.Request.PathBase.Value ?? "");
        Assert.Equal(expectedPath, context.Request.Path.Value ?? "");
    }

    [Theory]
    [InlineData("/injected, /dw4online", "/dw4online")] // Traefik's stripPrefix adds its value after one the client sent
    [InlineData("/dw4online, /injected", null)]         // the last value counts
    [InlineData("//dw4online", "/dw4online")]
    [InlineData("//evil.example", null)]                // never a scheme-relative URL
    [InlineData("/a/../dw4online", null)]
    [InlineData("/./dw4online", null)]
    [InlineData("/dw4online/..", null)]
    [InlineData("/dw4 online", null)]
    [InlineData("/dw4%2fonline", null)]
    [InlineData("/dw4online?x=1", null)]
    [InlineData("/dw4online#x", null)]
    [InlineData("/\"><script>", null)]
    [InlineData("https://evil.example/dw4online", null)]
    [InlineData("/", null)]
    [InlineData("", null)]
    public void Only_Service_PathBase_is_taken_from_the_header(string header, string? expected)
    {
        var context = Request("127.0.0.1", "/account", (ProxyHeaders.ForwardedPrefix, header));
        Proxy(pathBase: "/dw4online").Apply(context);
        Assert.Equal(expected ?? "", context.Request.PathBase.Value ?? "");
        Assert.Equal("/account", context.Request.Path.Value);
    }

    [Theory]
    [InlineData("/dw4online/", "/dw4online")]
    [InlineData("dw4online", "/dw4online")]
    [InlineData("/games/dw4", "/games/dw4")]
    [InlineData("/dw4_online-2.x~", "/dw4_online-2.x~")]
    [InlineData("//evil.example", "/evil.example")]
    [InlineData("/a/../b", null)]
    [InlineData("/a//b", null)]
    [InlineData("", null)]
    public void A_prefix_is_a_plain_path(string header, string? expected) => Assert.Equal(expected, ProxyHeaders.Prefix(header)?.Value);
}
