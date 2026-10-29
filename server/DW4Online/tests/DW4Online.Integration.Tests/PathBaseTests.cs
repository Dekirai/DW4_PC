using System.Net;
using System.Net.Http.Json;
using System.Text.RegularExpressions;

namespace DW4Online.Integration.Tests;

/// <summary>
/// The service below a path (Service:PathBase), as it will run at https://dekirai.crygod.de/dw4online: every link,
/// redirect, form and cookie stays below it, and the root keeps answering for clients of the earlier address.
/// </summary>
public partial class PathBaseTests
{
    // A link, form or stylesheet that leaves the path base (anything root-relative not starting with /dw4online/).
    [GeneratedRegex("(?:href|action|src)=\"/(?!dw4online/)[^\"]*\"")]
    private static partial Regex RootLink();

    private static void AllBelowBase(WebBrowser.Page page)
    {
        Assert.True(page.Status == HttpStatusCode.OK, $"{page.Status}: {page.Html[..Math.Min(300, page.Html.Length)]}");
        var leak = RootLink().Match(page.Html);
        Assert.False(leak.Success, $"a link outside the path base: {leak.Value}");
        Assert.Contains("href=\"/dw4online/css/site.css\"", page.Html);
    }

    [DbFact]
    public async Task Website_and_api_work_below_a_path_base()
    {
        await using var host = await TestHost.StartAsync(new() { ["Service:PathBase"] = "dw4online/" }); // normalised to /dw4online

        // The API below the base, and still at the root.
        Assert.Equal(HttpStatusCode.OK, (await host.Client().GetAsync("/dw4online/api/v1/status")).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await host.Client().GetAsync("/api/v1/status")).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await host.Client().GetAsync("/dw4online/health")).StatusCode);
        var registered = await host.Client().PostAsJsonAsync("/dw4online/api/v1/auth/register", new { username = "ApiUser", password = "a decent password" });
        Assert.Equal(HttpStatusCode.Created, registered.StatusCode);
        Assert.Equal("/dw4online/api/v1/me", registered.Headers.Location?.OriginalString);

        using var web = new WebBrowser(host, "/dw4online");
        AllBelowBase(await web.GetAsync("/"));
        // The bare base without a trailing slash (what people type) is the home page too.
        var bare = await host.Client().GetAsync("/dw4online");
        Assert.Equal(HttpStatusCode.OK, bare.StatusCode);
        Assert.Contains("href=\"/dw4online/css/site.css\"", await bare.Content.ReadAsStringAsync());
        Assert.Equal(HttpStatusCode.OK, (await web.GetAsync("/css/site.css")).Status);

        // Signed out, the account page sends the browser to the login below the base and back.
        var challenge = await web.GetAsync("/account");
        Assert.Equal(HttpStatusCode.Redirect, challenge.Status);
        Assert.Equal("/dw4online/login?returnUrl=%2Fdw4online%2Faccount", challenge.Location);

        AllBelowBase(await web.GetAsync("/register"));
        AllBelowBase(await web.GetAsync("/login"));
        await web.RegisterAsync("PathUser");
        var cookie = Assert.Single(web.SetCookieLines, l => l.StartsWith("dw4_session=", StringComparison.Ordinal));
        Assert.Contains("path=/dw4online", cookie, StringComparison.OrdinalIgnoreCase);
        Assert.Contains(web.SetCookieLines, l => l.StartsWith("dw4_antiforgery=", StringComparison.Ordinal) &&
            l.Contains("path=/dw4online", StringComparison.OrdinalIgnoreCase));

        foreach (var page in new[] { "/account", "/achievements", "/rooms", "/privacy", "/u/PathUser", "/recover" })
            AllBelowBase(await web.GetAsync(page));
        var account = await web.GetAsync("/account");
        Assert.Contains("Account: PathUser", account.Html);
        Assert.Contains("action=\"/dw4online/account?handler=Password\"", account.Html);
        Assert.Contains("action=\"/dw4online/logout\"", account.Html);

        // Logging out and in again (with a return address) stays below the base.
        var logout = await web.PostAsync("/logout", new(), "/account");
        Assert.Equal("/dw4online/", logout.Location);
        var login = await web.LoginAsync("PathUser", returnUrl: "/dw4online/u/PathUser");
        Assert.Equal("/dw4online/u/PathUser", login.Location);
        var signedIn = await web.GetAsync("/login"); // signed in already: on to the account page
        Assert.Equal("/dw4online/account", signedIn.Location);

        // Error pages too.
        var missing = await web.GetAsync("/no-such-page");
        Assert.Equal(HttpStatusCode.NotFound, missing.Status);
        Assert.Contains("href=\"/dw4online/\"", missing.Html);
    }

    [DbFact]
    public async Task The_rate_limit_page_links_below_the_path_base()
    {
        await using var host = await TestHost.StartAsync(new() { ["Service:PathBase"] = "/dw4online", ["RateLimits:WebLoginPerMinute"] = "1" });
        using var web = new WebBrowser(host, "/dw4online");
        await web.PostAsync("/login", new() { ["Username"] = "x", ["Password"] = "y" });
        var limited = await web.PostAsync("/login", new() { ["Username"] = "x", ["Password"] = "y" });
        Assert.Equal(HttpStatusCode.TooManyRequests, limited.Status);
        Assert.Contains("href=\"/dw4online/css/site.css\"", limited.Html);
        Assert.Contains("href=\"/dw4online/\"", limited.Html);
    }

    /// <summary>Later behind a reverse proxy on the same machine: the client address and https come from it, from nobody else.</summary>
    [DbFact]
    public async Task A_proxy_on_this_machine_forwards_the_client_address()
    {
        await using var host = await TestHost.StartAsync(new() { ["Service:PathBase"] = "/dw4online", ["Service:BehindProxy"] = "true" });
        await host.RegisterAsync("Proxied");
        async Task Login(string from, string forwardedFor)
        {
            var request = new HttpRequestMessage(HttpMethod.Post, "/dw4online/api/v1/auth/login")
            {
                Content = JsonContent.Create(new { username = "Proxied", password = "a decent password" }),
            };
            request.Headers.Add(TestClientAddress.Header, from);
            request.Headers.Add("X-Forwarded-For", forwardedFor);
            request.Headers.Add("X-Forwarded-Proto", "https");
            Assert.Equal(HttpStatusCode.OK, (await host.Client().SendAsync(request)).StatusCode);
        }
        await Login("127.0.0.1", "203.0.113.9");   // the local proxy: its forwarded address counts
        await Login("198.51.100.4", "203.0.113.10"); // anyone else: the header is ignored
        await using var sql = await host.SqlAsync();
        var ips = (await Dapper.SqlMapper.QueryAsync<byte[]>(sql, "SELECT ip FROM audit_log WHERE event = 'login_ok' ORDER BY id"))
            .Select(b => new IPAddress(b).ToString()).ToList();
        Assert.Equal(["203.0.113.9", "198.51.100.4"], ips);
    }

    [Fact]
    public void Path_bases_are_normalised_and_checked()
    {
        Assert.Equal("", new Domain.ServiceOptions { PathBase = "" }.NormalizedPathBase());
        Assert.Equal("", new Domain.ServiceOptions { PathBase = "/" }.NormalizedPathBase());
        Assert.Equal("/dw4online", new Domain.ServiceOptions { PathBase = "dw4online/" }.NormalizedPathBase());
        Assert.Equal("/games/dw4", new Domain.ServiceOptions { PathBase = "/games/dw4" }.NormalizedPathBase());
        Assert.Throws<InvalidOperationException>(() => new Domain.ServiceOptions { PathBase = "/dw4 online" }.NormalizedPathBase());
        Assert.Throws<InvalidOperationException>(() => new Domain.ServiceOptions { PathBase = "/dw4?x" }.NormalizedPathBase());
    }
}
