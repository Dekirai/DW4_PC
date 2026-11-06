using System.Net;
using System.Net.Http.Json;
using System.Text.RegularExpressions;
using Dapper;
using Microsoft.AspNetCore.TestHost;

namespace DW4Online.Integration.Tests;

/// <summary>
/// The service behind a reverse proxy on the same machine (DEPLOY.md section 6.2, case 2: Cloudflare, then Traefik at its
/// default, then http://127.0.0.1:5280), in every way it can be reached: links, forms, redirects (the login challenge, the
/// logout, the RequireHttps redirect), Location headers and the session and anti-forgery cookies stay below the address the
/// browser used; the client address the rate limits and the records see is the browser's (Cloudflare's CF-Connecting-IP
/// behind a trusted edge), and only the local proxy (and a trusted edge behind it) can name it. The rules one by one:
/// ProxyHeadersTests (unit).
/// </summary>
public partial class ReverseProxyTests
{
    private const string Client = "203.0.113.20";   // the browser
    private const string Edge = "198.51.100.7";     // a Cloudflare edge (Service:TrustedProxyNetworks in the tests)
    private const string EdgeNetwork = "198.51.100.0/24";

    /// <summary>
    /// The local proxy in front of the test server, as Traefik forwards: the browser's https URL arrives as plain http from
    /// 127.0.0.1, the prefix stripped (stripPrefix, which adds X-Forwarded-Prefix) or kept, with X-Forwarded-For (Traefik at
    /// its default: the address it saw, an edge's or a direct browser's), X-Forwarded-Proto and the CF-Connecting-IP that
    /// came in (Cloudflare sets it; Traefik passes it on). <paramref name="prefix"/>: the X-Forwarded-Prefix value when
    /// <paramref name="namePrefix"/> (a stripping proxy names what it stripped; a proxy that trusts its peer may pass a
    /// browser's own value on).
    /// </summary>
    private sealed class LocalProxy(string? strip, bool namePrefix, string forwardedFor, string proto = "https", string? connectingIp = null,
        string? prefix = null) : DelegatingHandler
    {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken ct)
        {
            var uri = request.RequestUri!;
            var path = uri.AbsolutePath;
            if (strip is not null && path.StartsWith(strip, StringComparison.Ordinal))
            {
                path = path[strip.Length..];
                if (!path.StartsWith('/')) path = "/" + path;
            }
            request.RequestUri = new UriBuilder("http", uri.Host, 80, path) { Query = uri.Query.TrimStart('?') }.Uri;
            request.Headers.Remove(TestClientAddress.Header);
            request.Headers.Add(TestClientAddress.Header, "127.0.0.1");
            request.Headers.TryAddWithoutValidation("X-Forwarded-For", forwardedFor);
            request.Headers.TryAddWithoutValidation("X-Forwarded-Proto", proto);
            if (connectingIp is not null) request.Headers.TryAddWithoutValidation("CF-Connecting-IP", connectingIp);
            if (namePrefix) request.Headers.TryAddWithoutValidation("X-Forwarded-Prefix", prefix ?? strip ?? "/dw4online");
            return base.SendAsync(request, ct);
        }
    }

    /// <summary>A browser's direct connection (no proxy): https or http as the URL says, from the client's address.</summary>
    private class Direct(string ip) : DelegatingHandler
    {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken ct)
        {
            request.Headers.Remove(TestClientAddress.Header);
            request.Headers.Add(TestClientAddress.Header, ip);
            return base.SendAsync(request, ct);
        }
    }

    // How the service is reached: its settings, the path the browser sees it at, and the hop in front of it.
    public enum Way
    {
        NoProxy,                   // https://host/ straight to Kestrel
        PathBaseOnly,              // https://host/dw4online/ straight to Kestrel with Service:PathBase
        PathBaseStripping,         // Traefik with stripPrefix and Service:PathBase (the user's case, DEPLOY.md 6.2)
        PathBaseNonStripping,      // Traefik without stripPrefix, Service:PathBase
        PathBaseNonStrippingNamed, // a proxy that keeps the path and sends X-Forwarded-Prefix too, Service:PathBase
        PathBaseForeignPrefix,     // a proxy that keeps the path and passes a browser's X-Forwarded-Prefix "/evil" on (the review's case 3)
    }

    private static (Dictionary<string, string?> Settings, string BrowserBase, Func<string, string, HttpMessageHandler> Hop) Setup(Way way)
    {
        var settings = new Dictionary<string, string?> { ["Web:RequireHttps"] = "true" };
        var proxied = way is not (Way.NoProxy or Way.PathBaseOnly);
        if (proxied)
        {
            settings["Service:BehindProxy"] = "true";
            settings["Service:TrustedProxyNetworks:0"] = EdgeNetwork;
        }
        if (way != Way.NoProxy) settings["Service:PathBase"] = "/dw4online";
        // (proto, the browser's address): through Cloudflare's edge, Traefik at its default.
        Func<string, string, HttpMessageHandler> hop = way switch
        {
            Way.NoProxy or Way.PathBaseOnly => (_, ip) => new Direct(ip),
            Way.PathBaseStripping => (proto, ip) => new LocalProxy("/dw4online", true, Edge, proto, ip),
            Way.PathBaseNonStripping => (proto, ip) => new LocalProxy(null, false, Edge, proto, ip),
            Way.PathBaseNonStrippingNamed => (proto, ip) => new LocalProxy(null, true, Edge, proto, ip),
            _ => (proto, ip) => new LocalProxy(null, true, Edge, proto, ip, "/evil"),
        };
        return (settings, way == Way.NoProxy ? "" : "/dw4online", hop);
    }

    private static HttpMessageHandler Through(TestHost host, HttpMessageHandler hop)
    {
        ((DelegatingHandler)hop).InnerHandler = host.App.GetTestServer().CreateHandler();
        return hop;
    }

    // A root-relative link, form or stylesheet in a page.
    [GeneratedRegex("(?:href|action|src)=\"(/[^\"]*)\"")]
    private static partial Regex RootLinks();

    private static void AllBelow(WebBrowser.Page page, string expected)
    {
        Assert.True(page.Status == HttpStatusCode.OK, $"{page.Status}: {page.Html[..Math.Min(300, page.Html.Length)]}");
        var links = RootLinks().Matches(page.Html).Select(m => m.Groups[1].Value).ToList();
        Assert.NotEmpty(links);
        if (expected.Length > 0)
            Assert.All(links, l => Assert.True(l.StartsWith(expected + "/", StringComparison.Ordinal), $"a link outside {expected}: {l}"));
        Assert.DoesNotContain(links, l => l.StartsWith("/dw4online/dw4online", StringComparison.OrdinalIgnoreCase));
        Assert.DoesNotContain(links, l => l.Contains("evil", StringComparison.OrdinalIgnoreCase));
        Assert.Contains($"href=\"{expected}/css/site.css\"", page.Html);
    }

    [DbTheory]
    [InlineData(Way.NoProxy)]
    [InlineData(Way.PathBaseOnly)]
    [InlineData(Way.PathBaseStripping)]
    [InlineData(Way.PathBaseNonStripping)]
    [InlineData(Way.PathBaseNonStrippingNamed)]
    [InlineData(Way.PathBaseForeignPrefix)]
    public async Task Links_redirects_cookies_and_the_client_address_in_every_setup(Way way)
    {
        var (settings, at, hop) = Setup(way);
        await using var host = await TestHost.StartAsync(settings);
        using var web = new WebBrowser(Through(host, hop("https", Client)), new Uri("https://dekirai.example/"), at);

        AllBelow(await web.GetAsync("/"), at);
        AllBelow(await web.GetAsync("/login"), at);
        AllBelow(await web.GetAsync("/register"), at);
        // The bare base without a slash is the home page too.
        if (at.Length > 0)
        {
            using var bare = new HttpClient(Through(host, hop("https", Client))) { BaseAddress = new Uri("https://dekirai.example/") };
            var response = await bare.GetAsync(at);
            AllBelow(new WebBrowser.Page(response.StatusCode, await response.Content.ReadAsStringAsync(), null, response), at);
        }

        // Signed out, the account page sends the browser to the login and back (WebSessionHandler's challenge).
        var challenge = await web.GetAsync("/account");
        Assert.Equal(HttpStatusCode.Redirect, challenge.Status);
        Assert.Equal($"{at}/login?returnUrl={Uri.EscapeDataString(at + "/account")}", challenge.Location);

        // Registering signs in: the session and anti-forgery cookies live below the base, Secure, with the https names.
        await web.RegisterAsync("Proxy" + (int)way);
        var cookieName = at.Length == 0 ? "__Host-dw4_session=" : "__Secure-dw4_session=";
        var session = Assert.Single(web.SetCookieLines, l => l.StartsWith(cookieName, StringComparison.Ordinal));
        Assert.Contains($"path={(at.Length == 0 ? "/" : at)};", session + ";", StringComparison.OrdinalIgnoreCase);
        Assert.Contains("secure", session, StringComparison.OrdinalIgnoreCase);
        var antiforgery = Assert.Single(web.SetCookieLines.Where(l => l.StartsWith("dw4_antiforgery=", StringComparison.Ordinal)).Distinct());
        Assert.Contains($"path={(at.Length == 0 ? "/" : at)};", antiforgery + ";", StringComparison.OrdinalIgnoreCase);

        var account = await web.GetAsync("/account");
        AllBelow(account, at);
        Assert.Contains($"action=\"{at}/account?handler=Password\"", account.Html);
        Assert.Contains($"action=\"{at}/logout\"", account.Html);
        foreach (var page in new[] { "/account/friends", "/achievements", "/rooms", "/privacy", "/u/Proxy" + (int)way })
            AllBelow(await web.GetAsync(page), at);

        // The session's address is the browser's, through the proxy and its edge too.
        await using (var sql = await host.SqlAsync())
        {
            var ip = await sql.ExecuteScalarAsync<byte[]>("SELECT created_ip FROM sessions ORDER BY id DESC LIMIT 1");
            Assert.Equal(Client, new IPAddress(ip!).ToString());
        }

        // Logging out and in again with a return address stays below the base.
        Assert.Equal($"{at}/", (await web.PostAsync("/logout", new(), "/account")).Location);
        var login = await web.LoginAsync("Proxy" + (int)way, returnUrl: $"{at}/u/Proxy{(int)way}");
        Assert.Equal($"{at}/u/Proxy{(int)way}", login.Location);
        Assert.Equal($"{at}/account", (await web.GetAsync("/login")).Location);

        // The API's Location header.
        using var api = new HttpClient(Through(host, hop("https", Client))) { BaseAddress = new Uri("https://dekirai.example/") };
        var created = await api.PostAsJsonAsync($"{at}/api/v1/auth/register", new { username = "ProxyApi" + (int)way, password = "a decent password" });
        Assert.Equal(HttpStatusCode.Created, created.StatusCode);
        Assert.Equal($"{at}/api/v1/me", created.Headers.Location?.OriginalString);

        // Web:RequireHttps: a plain-http request is sent to https below the same base (the proxy says http: X-Forwarded-Proto),
        // a static-looking path too, and no cache in front may keep that redirect (the review: a CDN for .css paths).
        using var plain = new HttpClient(Through(host, hop("http", Client))) { BaseAddress = new Uri("http://dekirai.example/") };
        foreach (var path in new[] { "/account?tab=1", "/css/site.css" })
        {
            var insecure = await plain.GetAsync(at + path);
            Assert.Equal(HttpStatusCode.Redirect, insecure.StatusCode);
            Assert.Equal($"https://dekirai.example{at}{path}", insecure.Headers.Location?.OriginalString);
            Assert.True(insecure.Headers.CacheControl?.NoStore, $"{path}: Cache-Control {insecure.Headers.CacheControl}");
        }
    }

    /// <summary>
    /// Traefik with stripPrefix but without Service:PathBase (DEPLOY.md 6.2 asks for PathBase in both variants): the
    /// X-Forwarded-Prefix counts only as Service:PathBase, so the pages link to the root; nothing is taken from the header.
    /// </summary>
    [DbFact]
    public async Task A_stripping_proxy_without_PathBase_names_no_prefix()
    {
        await using var host = await TestHost.StartAsync(new()
        {
            ["Service:BehindProxy"] = "true",
            ["Service:TrustedProxyNetworks:0"] = EdgeNetwork,
        });
        using var web = new WebBrowser(Through(host, new LocalProxy("/dw4online", true, Edge, connectingIp: Client)), new Uri("https://dekirai.example/"),
            "/dw4online");
        var home = await web.GetAsync("/");
        Assert.Equal(HttpStatusCode.OK, home.Status);
        Assert.Contains("href=\"/css/site.css\"", home.Html);
        Assert.DoesNotContain("/dw4online/", home.Html);
    }

    /// <summary>
    /// The per-IP form limits (here the login form's, one POST a minute) behind Cloudflare and Traefik at its default: two
    /// browsers behind the same edge are two addresses (Cloudflare's CF-Connecting-IP; without Service:TrustedProxyNetworks
    /// they were the edge's one), and nobody else can pick an address: not a browser's own X-Forwarded-For entries, not a
    /// CF-Connecting-IP sent straight to Traefik, not an address no browser has, and a direct client's headers count for
    /// nothing.
    /// </summary>
    [DbFact]
    public async Task Per_ip_limits_see_the_browser_behind_the_edge_and_nobody_can_pick_an_address()
    {
        var settings = new Dictionary<string, string?>
        {
            ["Service:PathBase"] = "/dw4online",
            ["Service:BehindProxy"] = "true",
            ["Service:TrustedProxyNetworks:0"] = EdgeNetwork,
            ["RateLimits:WebLoginPerMinute"] = "1",
        };
        await using var host = await TestHost.StartAsync(settings);
        async Task<HttpStatusCode> Login(HttpMessageHandler hop)
        {
            using var web = new WebBrowser(Through(host, hop), new Uri("https://dekirai.example/"), "/dw4online");
            return (await web.PostAsync("/login", new() { ["Username"] = "nobody", ["Password"] = "wrong password" })).Status;
        }
        LocalProxy ViaEdge(string browser, string edge = Edge, string? forwardedFor = null) =>
            new("/dw4online", true, forwardedFor ?? edge, connectingIp: browser);

        Assert.Equal(HttpStatusCode.OK, await Login(ViaEdge("203.0.113.30")));
        Assert.Equal(HttpStatusCode.TooManyRequests, await Login(ViaEdge("203.0.113.30")));
        // Another browser behind the same edge, or behind another one, has its own limit.
        Assert.Equal(HttpStatusCode.OK, await Login(ViaEdge("203.0.113.31")));
        Assert.Equal(HttpStatusCode.OK, await Login(ViaEdge("203.0.113.34", "198.51.100.9")));
        // A browser's own X-Forwarded-For entries (a local proxy that keeps them) change nothing.
        Assert.Equal(HttpStatusCode.TooManyRequests, await Login(ViaEdge("203.0.113.30", forwardedFor: "192.0.2.50, " + Edge)));
        Assert.Equal(HttpStatusCode.TooManyRequests, await Login(ViaEdge("203.0.113.30", forwardedFor: "192.0.2.51, 198.51.100.8, " + Edge)));
        // A request straight to Traefik (not through Cloudflare): Traefik names the browser it saw; its CF-Connecting-IP is its own.
        Assert.Equal(HttpStatusCode.OK, await Login(new LocalProxy("/dw4online", true, "203.0.113.32", connectingIp: "192.0.2.57")));
        Assert.Equal(HttpStatusCode.TooManyRequests, await Login(new LocalProxy("/dw4online", true, "203.0.113.32", connectingIp: "192.0.2.58")));
        // From an edge's network without the zone (DEPLOY.md 6.2): an address no browser has keeps the edge's own.
        Assert.Equal(HttpStatusCode.OK, await Login(ViaEdge("127.0.0.1", "198.51.100.10")));
        Assert.Equal(HttpStatusCode.TooManyRequests, await Login(ViaEdge("10.0.0.1", "198.51.100.10")));
        Assert.Equal(HttpStatusCode.TooManyRequests, await Login(ViaEdge("198.51.100.200", "198.51.100.10")));
        // A direct client (the game's port, no proxy): its own address, whatever it sends.
        Direct DirectWith(string forwardedFor) => new ForwardingDirect("203.0.113.33", forwardedFor);
        Assert.Equal(HttpStatusCode.OK, await Login(DirectWith("192.0.2.52")));
        Assert.Equal(HttpStatusCode.TooManyRequests, await Login(DirectWith("192.0.2.53")));
        Assert.Equal(HttpStatusCode.TooManyRequests, await Login(DirectWith(Edge)));
        // An edge address connecting to the service directly is no proxy either.
        Assert.Equal(HttpStatusCode.OK, await Login(new ForwardingDirect(Edge, "192.0.2.55")));
        Assert.Equal(HttpStatusCode.TooManyRequests, await Login(new ForwardingDirect(Edge, "192.0.2.56")));
    }

    /// <summary>Without Service:TrustedProxyNetworks the edge's address is everyone's (the reason for the setting).</summary>
    [DbFact]
    public async Task Without_trusted_networks_every_browser_behind_the_edge_shares_its_limit()
    {
        await using var host = await TestHost.StartAsync(new()
        {
            ["Service:PathBase"] = "/dw4online",
            ["Service:BehindProxy"] = "true",
            ["RateLimits:WebLoginPerMinute"] = "1",
        });
        async Task<HttpStatusCode> Login(string browser)
        {
            using var web = new WebBrowser(Through(host, new LocalProxy("/dw4online", true, Edge, connectingIp: browser)), new Uri("https://dekirai.example/"),
                "/dw4online");
            return (await web.PostAsync("/login", new() { ["Username"] = "nobody", ["Password"] = "wrong password" })).Status;
        }
        Assert.Equal(HttpStatusCode.OK, await Login("203.0.113.40"));
        Assert.Equal(HttpStatusCode.TooManyRequests, await Login("203.0.113.41"));
    }

    /// <summary>The audit log's address of an API login (the game's client could also come through the proxy).</summary>
    [DbFact]
    public async Task The_audit_log_records_the_browser_not_the_hops()
    {
        await using var host = await TestHost.StartAsync(new()
        {
            ["Service:PathBase"] = "/dw4online",
            ["Service:BehindProxy"] = "true",
            ["Service:TrustedProxyNetworks:0"] = EdgeNetwork,
            ["Service:TrustedProxyNetworks:1"] = "2001:db8:ed9e::/48",
        });
        await host.RegisterAsync("Audited");
        async Task Login(HttpMessageHandler hop)
        {
            using var api = new HttpClient(Through(host, hop)) { BaseAddress = new Uri("https://dekirai.example/") };
            var response = await api.PostAsJsonAsync("/dw4online/api/v1/auth/login", new { username = "Audited", password = "a decent password" });
            Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        }
        await Login(new LocalProxy("/dw4online", true, Edge, connectingIp: "203.0.113.50"));
        await Login(new LocalProxy("/dw4online", true, "2001:db8:ed9e::1", connectingIp: "2001:db8:c::7"));
        await Login(new LocalProxy("/dw4online", true, "203.0.113.51", connectingIp: "192.0.2.59"));
        await Login(new ForwardingDirect("203.0.113.52", "192.0.2.61"));
        await Login(new ForwardingDirect(Edge, "192.0.2.62"));
        await Login(new LocalProxy("/dw4online", true, Edge, connectingIp: "127.0.0.1")); // (the review's probe: loopback was recorded)
        await using var sql = await host.SqlAsync();
        var ips = (await sql.QueryAsync<byte[]>("SELECT ip FROM audit_log WHERE event = 'login_ok' ORDER BY id"))
            .Select(b => new IPAddress(b).ToString()).ToList();
        Assert.Equal(["203.0.113.50", "2001:db8:c::7", "203.0.113.51", "203.0.113.52", Edge, Edge], ips);
    }

    /// <summary>
    /// Web:Processors (DEPLOY.md 6.2, case 2: Cloudflare in front of the website) on the privacy page's "Who receives data";
    /// without it the page names the hosting provider only.
    /// </summary>
    [DbFact]
    public async Task The_privacy_page_names_the_processors_the_operator_lists()
    {
        static async Task<string> Text(TestHost host)
        {
            using var web = new WebBrowser(host);
            return Regex.Replace(WebUtility.HtmlDecode((await web.GetAsync("/privacy")).Html), @"\s+", " ");
        }
        await using (var plain = await TestHost.StartAsync())
        {
            var text = await Text(plain);
            Assert.Contains("Nobody. Data is not sold or shared. The hosting provider of the server processes it only on the operator's behalf.", text);
            Assert.DoesNotContain("Cloudflare", text);
        }
        await using var host = await TestHost.StartAsync(new()
        {
            ["Web:Processors:0"] = "Cloudflare, Inc. (USA): the website's content delivery network <sees everything in transit>",
            ["Web:Processors:1"] = " ",
        });
        var listed = await Text(host);
        Assert.Contains("Data is not sold. The hosting provider of the server and these service providers process it only on the operator's behalf:", listed);
        Assert.Contains("<li>Cloudflare, Inc. (USA): the website's content delivery network <sees everything in transit></li>", listed);
        Assert.Single(Regex.Matches(listed, "<li>"));
        Assert.DoesNotContain("Nobody. Data is not sold", listed);
        Assert.Contains("Your user name and, when your profile is public", listed);
    }

    /// <summary>A direct connection that sends forwarded headers of its own (they must count for nothing).</summary>
    private sealed class ForwardingDirect(string ip, string forwardedFor) : Direct(ip)
    {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken ct)
        {
            request.Headers.TryAddWithoutValidation("X-Forwarded-For", forwardedFor);
            request.Headers.TryAddWithoutValidation("X-Forwarded-Proto", "https");
            request.Headers.TryAddWithoutValidation("X-Forwarded-Prefix", "/elsewhere");
            request.Headers.TryAddWithoutValidation("CF-Connecting-IP", forwardedFor.Split(',')[0].Trim());
            return base.SendAsync(request, ct);
        }
    }
}
