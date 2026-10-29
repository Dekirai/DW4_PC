using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.AspNetCore.TestHost;

namespace DW4Online.Integration.Tests;

/// <summary>Settings that switch the relay on for a test host: a real TCP listener on a free loopback port.</summary>
public static class RelaySettings
{
    public static Dictionary<string, string?> On(Dictionary<string, string?>? more = null)
    {
        var settings = new Dictionary<string, string?>
        {
            ["Relay:Enabled"] = "true",
            ["Relay:ListenAddress"] = "127.0.0.1",
            ["Relay:Port"] = "0",
            ["Relay:TicketKey"] = Convert.ToBase64String(RandomNumberGenerator.GetBytes(32)),
            ["Service:RelayHost"] = "127.0.0.1",
            ["Service:RelayPort"] = "0",
        };
        if (more is not null)
            foreach (var (k, v) in more) settings[k] = v;
        return settings;
    }
}

/// <summary>A browser for the website on the test server: keeps cookies, fetches anti-forgery tokens, never follows redirects.</summary>
public sealed partial class WebBrowser : IDisposable
{
    private static readonly Uri Base = new("http://localhost/");
    private readonly CookieContainer _jar = new();
    private readonly HttpClient _http;

    public WebBrowser(TestHost host)
    {
        _http = new HttpClient(new CookieHandler(_jar) { InnerHandler = host.App.GetTestServer().CreateHandler() }) { BaseAddress = Base };
    }

    public sealed record Page(HttpStatusCode Status, string Html, string? Location, HttpResponseMessage Response);

    [GeneratedRegex("name=\"__RequestVerificationToken\" type=\"hidden\" value=\"([^\"]+)\"")]
    private static partial Regex TokenField();

    [GeneratedRegex("<p class=\"secret\"[^>]*>([^<]+)</p>")]
    private static partial Regex SecretField();

    public async Task<Page> GetAsync(string path)
    {
        var response = await _http.GetAsync(path);
        return new Page(response.StatusCode, await response.Content.ReadAsStringAsync(), response.Headers.Location?.OriginalString, response);
    }

    /// <summary>Posts a form; the anti-forgery token comes from <paramref name="formPage"/> (default: the same path).</summary>
    public async Task<Page> PostAsync(string path, Dictionary<string, string> fields, string? formPage = null, bool withToken = true)
    {
        var all = new Dictionary<string, string>(fields);
        if (withToken)
        {
            var form = await GetAsync(formPage ?? path);
            var token = TokenField().Match(form.Html);
            Assert.True(token.Success, $"no anti-forgery token on {formPage ?? path} ({form.Status})");
            all["__RequestVerificationToken"] = WebUtility.HtmlDecode(token.Groups[1].Value);
        }
        var response = await _http.PostAsync(path, new FormUrlEncodedContent(all));
        return new Page(response.StatusCode, await response.Content.ReadAsStringAsync(), response.Headers.Location?.OriginalString, response);
    }

    /// <summary>Registers through the website form; returns the recovery code shown once.</summary>
    public async Task<string> RegisterAsync(string username, string password = "a decent password", string email = "")
    {
        var page = await PostAsync("/register", new() { ["Username"] = username, ["Password"] = password, ["ConfirmPassword"] = password, ["Email"] = email });
        Assert.Equal(HttpStatusCode.OK, page.Status);
        Assert.Contains("Your recovery code", page.Html);
        return Secret(page.Html)!;
    }

    public async Task<Page> LoginAsync(string username, string password = "a decent password", string? returnUrl = null)
    {
        var path = returnUrl is null ? "/login" : "/login?returnUrl=" + Uri.EscapeDataString(returnUrl);
        return await PostAsync(path, new() { ["Username"] = username, ["Password"] = password, ["ReturnUrl"] = returnUrl ?? "" });
    }

    public static string? Secret(string html) => SecretField().Match(html) is { Success: true } m ? WebUtility.HtmlDecode(m.Groups[1].Value).Trim() : null;

    public string? Cookie(string name) => _jar.GetCookies(Base)[name]?.Value;

    public void Dispose() => _http.Dispose();

    private sealed class CookieHandler(CookieContainer jar) : DelegatingHandler
    {
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken ct)
        {
            var header = jar.GetCookieHeader(request.RequestUri!);
            if (header.Length > 0) request.Headers.TryAddWithoutValidation("Cookie", header);
            var response = await base.SendAsync(request, ct);
            if (response.Headers.TryGetValues("Set-Cookie", out var values))
                foreach (var value in values) jar.SetCookies(request.RequestUri!, value);
            return response;
        }
    }
}

/// <summary>A minimal relay client in C#: the handshake lines and raw bytes over loopback TCP.</summary>
public sealed class RelayConnection : IAsyncDisposable
{
    private readonly TcpClient _client;

    private RelayConnection(TcpClient client)
    {
        _client = client;
        Stream = client.GetStream();
    }

    public NetworkStream Stream { get; }

    public static async Task<RelayConnection> OpenAsync(int port, string line)
    {
        var client = new TcpClient { NoDelay = true };
        await client.ConnectAsync(IPAddress.Loopback, port);
        var connection = new RelayConnection(client);
        await connection.Stream.WriteAsync(Encoding.ASCII.GetBytes(line + "\n"));
        return connection;
    }

    public async Task<string> ReadLineAsync()
    {
        var sb = new StringBuilder();
        var one = new byte[1];
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        while (await Stream.ReadAsync(one, timeout.Token) == 1 && one[0] != '\n') sb.Append((char)one[0]);
        return sb.ToString();
    }

    public async Task<byte[]> ReadExactlyAsync(int count)
    {
        var buffer = new byte[count];
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        await Stream.ReadExactlyAsync(buffer, timeout.Token);
        return buffer;
    }

    /// <summary>True when the relay closed the connection within the timeout.</summary>
    public async Task<bool> ClosedAsync()
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        try
        {
            return await Stream.ReadAsync(new byte[16], timeout.Token) == 0;
        }
        catch (IOException)
        {
            return true;
        }
    }

    public ValueTask DisposeAsync()
    {
        _client.Dispose();
        return ValueTask.CompletedTask;
    }
}
