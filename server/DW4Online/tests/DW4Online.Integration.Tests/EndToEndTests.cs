using System.Diagnostics;
using System.Net;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using Dapper;
using DW4Online.Domain.Save;
using DW4Online.Server;
using DW4Online.Tests;
using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using MySqlConnector;

namespace DW4Online.Integration.Tests;

/// <summary>Where the end-to-end tools are: the WinHTTP test client (tests/winhttp_client/build.cmd) and a Python interpreter.</summary>
public static class EndToEnd
{
    public static string ServerRoot { get; } = FindRoot();

    /// <summary>DW4ONLINE_E2E_CLIENT, or the build output of tests/winhttp_client/build.cmd.</summary>
    public static string? ClientPath =>
        Environment.GetEnvironmentVariable("DW4ONLINE_E2E_CLIENT") is { Length: > 0 } path ? path
        : Path.Combine(ServerRoot, "tests", "winhttp_client", "out", "dw4online_client.exe") is var built && File.Exists(built) ? built : null;

    /// <summary>DW4ONLINE_PYTHON: the python.exe that runs tests/e2e/relay_spike_test.py.</summary>
    public static string? PythonPath => Environment.GetEnvironmentVariable("DW4ONLINE_PYTHON") is { Length: > 0 } p && File.Exists(p) ? p : null;

    public static async Task<(int Exit, string Output)> RunAsync(string exe, IEnumerable<string> args, TimeSpan timeout)
    {
        var start = new ProcessStartInfo(exe)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8,
        };
        foreach (var a in args) start.ArgumentList.Add(a);
        using var process = Process.Start(start)!;
        var stdout = process.StandardOutput.ReadToEndAsync();
        var stderr = process.StandardError.ReadToEndAsync();
        using var cts = new CancellationTokenSource(timeout);
        try
        {
            await process.WaitForExitAsync(cts.Token);
        }
        catch (OperationCanceledException)
        {
            process.Kill(entireProcessTree: true); // only the process this test started
            throw new TimeoutException($"{Path.GetFileName(exe)} did not finish in {timeout}.\n{await stdout}");
        }
        return (process.ExitCode, await stdout + await stderr);
    }

    private static string FindRoot()
    {
        for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir is not null; dir = dir.Parent)
            if (File.Exists(Path.Combine(dir.FullName, "DW4Online.slnx"))) return dir.FullName;
        return AppContext.BaseDirectory;
    }
}

public sealed class ClientFactAttribute : FactAttribute
{
    public ClientFactAttribute()
    {
        if (string.IsNullOrEmpty(TestDb.AdminConnectionString)) Skip = "DW4ONLINE_TEST_MYSQL is not set.";
        else if (EndToEnd.ClientPath is null) Skip = "The WinHTTP test client is not built (tests/winhttp_client/build.cmd) and DW4ONLINE_E2E_CLIENT is not set.";
    }
}

/// <summary>
/// Opt-in (DW4ONLINE_TEST_CERT_STORE=1): the test runs the deploy kit's new-selfsigned-cert.ps1 against the current
/// user's certificate store (a CN=127.0.0.2 test certificate, removed again with its key afterwards).
/// </summary>
public sealed class CertStoreFactAttribute : FactAttribute
{
    public CertStoreFactAttribute()
    {
        if (!OperatingSystem.IsWindows()) Skip = "Windows only.";
        else if (string.IsNullOrEmpty(TestDb.AdminConnectionString)) Skip = "DW4ONLINE_TEST_MYSQL is not set.";
        else if (EndToEnd.ClientPath is null) Skip = "The WinHTTP test client is not built (tests/winhttp_client/build.cmd).";
        else if (Environment.GetEnvironmentVariable("DW4ONLINE_TEST_CERT_STORE") != "1")
            Skip = "DW4ONLINE_TEST_CERT_STORE=1 lets this test add (and remove) a test certificate in the current user's store.";
    }
}

public sealed class PythonFactAttribute : FactAttribute
{
    public PythonFactAttribute()
    {
        if (string.IsNullOrEmpty(TestDb.AdminConnectionString)) Skip = "DW4ONLINE_TEST_MYSQL is not set.";
        else if (EndToEnd.PythonPath is null) Skip = "DW4ONLINE_PYTHON (a python.exe) is not set.";
    }
}

/// <summary>The whole service on real Kestrel (127.0.0.1, a free port) with its own fresh database.</summary>
public sealed class RealHost : IAsyncDisposable
{
    private RealHost(WebApplication app, string database, Uri baseUri, List<X509Certificate2> certificates)
    {
        App = app;
        Database = database;
        BaseUri = baseUri;
        _certificates = certificates;
    }

    private readonly List<X509Certificate2> _certificates;

    public WebApplication App { get; }
    public string Database { get; }
    public Uri BaseUri { get; }
    public string Base => BaseUri.ToString().TrimEnd('/');

    /// <summary>
    /// Starts the service. With <paramref name="httpsPfx"/> it listens on https://127.0.0.1 (a free port) and serves that
    /// certificate through the same selector as the Windows store (Tls:StoreHostName 127.0.0.1, an IP certificate like
    /// the server's), read from memory instead of a store; <see cref="BaseUri"/> is then the https address.
    /// </summary>
    /// <remarks>
    /// <paramref name="httpsAddress"/> without <paramref name="httpsPfx"/>: https on that loopback address with the
    /// certificate from the Windows store (Tls:StoreLocation from the overrides), exactly as the service runs.
    /// </remarks>
    public static async Task<RealHost> StartAsync(Dictionary<string, string?>? overrides = null, byte[]? httpsPfx = null, string? httpsAddress = null)
    {
        var database = await TestDb.CreateAsync();
        var settings = TestHost.Settings(database, overrides);
        settings["Kestrel:Endpoints:Http:Url"] = "http://127.0.0.1:0";
        var https = httpsPfx is not null || httpsAddress is not null;
        if (https)
        {
            settings["Kestrel:Endpoints:Https:Url"] = $"https://{httpsAddress ?? "127.0.0.1"}:0";
            settings["Tls:StoreHostName"] = httpsAddress ?? "127.0.0.1";
        }
        var handedOut = new List<X509Certificate2>();
        var app = ServerApp.Build(["--environment", "Testing"], configure: builder =>
        {
            builder.Configuration.AddInMemoryCollection(settings);
            // Like X509Store.Certificates, new objects on every read. Schannel needs a key it can open by name, so the
            // PKCS#12 import is not ephemeral: it puts a temporary key file into the user profile, deleted when the
            // object is disposed (the selector disposes what it does not keep, DisposeAsync the rest).
            if (httpsPfx is not null)
                builder.Services.AddSingleton(new TlsCertificateSource(() =>
                {
                    var cert = X509CertificateLoader.LoadPkcs12(httpsPfx, null);
                    lock (handedOut) handedOut.Add(cert);
                    return new X509Certificate2Collection(cert);
                }));
        });
        await ServerApp.MigrateIfEnabledAsync(app);
        await app.StartAsync();
        var scheme = https ? "https://" : "http://";
        return new RealHost(app, database, new Uri(app.Urls.First(u => u.StartsWith(scheme, StringComparison.Ordinal)) + "/"), handedOut);
    }

    public async Task<MySqlConnection> SqlAsync()
    {
        var c = new MySqlConnection(TestDb.ConnectionString(Database));
        await c.OpenAsync();
        return c;
    }

    public async ValueTask DisposeAsync()
    {
        await App.StopAsync();
        await App.DisposeAsync();
        lock (_certificates)
            foreach (var cert in _certificates) cert.Dispose(); // deletes the temporary key files
        await TestDb.DropAsync(Database);
    }
}

/// <summary>
/// End to end on loopback against real Kestrel and the real relay: website registration, then the C++ WinHTTP client
/// (login, cloud save up and down, online character, room, stage and relay handshake, achievement), then the profile
/// page; and the groundwork spike's Python test against the room service and relay.
/// </summary>
public class EndToEndTests(Xunit.Abstractions.ITestOutputHelper log)
{
    [ClientFact]
    public async Task Website_registration_cpp_client_and_profile()
    {
        await using var host = await RealHost.StartAsync(RelaySettings.On());
        using var web = WebBrowser.For(host.BaseUri);
        await web.RegisterAsync("E2eTamer", "e2e test password");

        var dir = Path.Combine(Path.GetTempPath(), "dw4online-e2e-" + Guid.NewGuid().ToString("N")[..8]);
        Directory.CreateDirectory(dir);
        try
        {
            var (save, record, saveBytes) = await WriteClientInputsAsync(dir, "E2E");

            var (exit, output) = await EndToEnd.RunAsync(EndToEnd.ClientPath!,
                ["--base", host.Base, "--user", "E2eTamer", "--password", "e2e test password", "--save", save, "--record", record],
                TimeSpan.FromSeconds(120));
            log.WriteLine(output);
            Assert.True(exit == 0, output);
            Assert.Contains("ALL PASSED", output);
            Assert.Contains("ok   relay host handshake", output);

            // What the client did shows on the website.
            using var visitor = WebBrowser.For(host.BaseUri);
            var profile = await visitor.GetAsync("/u/E2eTamer");
            Assert.Contains("Agumon", profile.Html);
            Assert.Contains("E2E", profile.Html);
            Assert.Contains("1 h 00 min", profile.Html);
            Assert.Contains("Connected", profile.Html);
            Assert.Contains("1 of 72 unlocked", profile.Html);
            var account = await web.GetAsync("/account");
            Assert.Contains("from dw4online_client", account.Html);
            Assert.Equal(saveBytes, await web.GetBytesAsync("/account?handler=CloudSave"));
        }
        finally
        {
            Directory.Delete(dir, true);
        }
    }

    /// <summary>
    /// The IP phase: the client talks to https://&lt;IP address&gt; with a self-signed certificate it pins (SHA-256 SPKI),
    /// here below the later path base /dw4online. The C++ client without the pin refuses the certificate, with a wrong
    /// pin sends nothing at all (no login reaches the server), and with the right pin runs every step; the website works
    /// over the same certificate at the IP address, below the path base and at the root.
    /// </summary>
    [ClientFact]
    public async Task Pinned_self_signed_https_at_an_ip_address_below_a_path_base()
    {
        var pfx = TestCertificates.SelfSignedPfx("127.0.0.1", DateTimeOffset.UtcNow.AddDays(-1), DateTimeOffset.UtcNow.AddYears(10));
        string pin;
        using (var cert = X509CertificateLoader.LoadPkcs12(pfx, null, X509KeyStorageFlags.EphemeralKeySet)) pin = SpkiPin.Of(cert);
        var wrongPin = Convert.ToBase64String(new byte[32]);
        await using var host = await RealHost.StartAsync(RelaySettings.On(new() { ["Service:PathBase"] = "/dw4online" }), httpsPfx: pfx);
        Assert.StartsWith("https://127.0.0.1:", host.Base);
        var service = host.Base + "/dw4online";

        // The website over the pinned certificate: below the path base the session cookie is __Secure- with that path.
        using var web = WebBrowser.ForPinned(host.BaseUri, pin, "/dw4online");
        await web.RegisterAsync("PinnedTamer", "pinned test password");
        var cookie = Assert.Single(web.SetCookieLines, l => l.StartsWith("__Secure-dw4_session=", StringComparison.Ordinal));
        Assert.Contains("path=/dw4online", cookie, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("secure", cookie, StringComparison.OrdinalIgnoreCase);
        // The root of the same address keeps working (clients of the earlier root address), with the __Host- cookie.
        using var rootWeb = WebBrowser.ForPinned(host.BaseUri, pin);
        var login = await rootWeb.LoginAsync("PinnedTamer", "pinned test password");
        Assert.Equal(HttpStatusCode.Redirect, login.Status);
        Assert.Equal("/account", login.Location);
        Assert.Contains(rootWeb.SetCookieLines, l => l.StartsWith("__Host-dw4_session=", StringComparison.Ordinal) &&
            l.Contains("path=/;", StringComparison.OrdinalIgnoreCase));
        // Another key is refused before anything is sent.
        using var stranger = WebBrowser.ForPinned(host.BaseUri, wrongPin, "/dw4online");
        await Assert.ThrowsAsync<HttpRequestException>(() => stranger.GetAsync("/"));

        var dir = Path.Combine(Path.GetTempPath(), "dw4online-e2e-" + Guid.NewGuid().ToString("N")[..8]);
        Directory.CreateDirectory(dir);
        try
        {
            var (save, record, _) = await WriteClientInputsAsync(dir, "PIN");
            string[] Args(params string[] more) =>
                ["--base", service, "--user", "PinnedTamer", "--password", "pinned test password", "--save", save, "--record", record, .. more];

            async Task<long> AuditRows()
            {
                await using var sql = await host.SqlAsync();
                return await sql.ExecuteScalarAsync<long>("SELECT COUNT(*) FROM audit_log");
            }
            var before = await AuditRows();

            var (exit, output) = await EndToEnd.RunAsync(EndToEnd.ClientPath!, Args(), TimeSpan.FromSeconds(120));
            log.WriteLine("--- no pin ---" + Environment.NewLine + output);
            Assert.NotEqual(0, exit); // a self-signed certificate without a pin: Schannel refuses it
            Assert.Contains("WinHTTP error 12175", output); // ERROR_WINHTTP_SECURE_FAILURE

            (exit, output) = await EndToEnd.RunAsync(EndToEnd.ClientPath!, Args("--pin", wrongPin), TimeSpan.FromSeconds(120));
            log.WriteLine("--- wrong pin ---" + Environment.NewLine + output);
            Assert.NotEqual(0, exit);
            Assert.Contains("pin mismatch (server key " + pin + ")", output);
            Assert.Contains("PIN FAILURES", output);
            Assert.Equal(before, await AuditRows()); // not even a failed login reached the server

            // The client pins the key in use and a spare (deploy/new-selfsigned-cert.ps1 prints both); either one passes.
            (exit, output) = await EndToEnd.RunAsync(EndToEnd.ClientPath!, Args("--pin", wrongPin + "," + pin), TimeSpan.FromSeconds(120));
            log.WriteLine("--- pin ---" + Environment.NewLine + output);
            Assert.True(exit == 0, output);
            Assert.Contains("ALL PASSED", output);
            Assert.Contains("ok   relay host handshake", output);
            Assert.DoesNotContain("PIN FAILURES", output);
        }
        finally
        {
            Directory.Delete(dir, true);
        }
    }

    /// <summary>
    /// The deploy kit as the server runs it: new-selfsigned-cert.ps1 creates the IP certificate with a key that cannot
    /// be exported (here in the current user's store, for 127.0.0.2), the service reads it from the store and serves it
    /// over Schannel, and the C++ client with the pin the script printed passes every step below /dw4online.
    /// </summary>
    [CertStoreFact]
    [System.Runtime.Versioning.SupportedOSPlatform("windows")]
    public async Task Deploy_kit_certificate_from_the_store_and_the_pinned_client()
    {
        const string address = "127.0.0.2";
        RemoveTestCertificates(address);
        try
        {
            var script = Path.Combine(EndToEnd.ServerRoot, "deploy", "new-selfsigned-cert.ps1");
            var exe = Path.Combine(AppContext.BaseDirectory, "DW4Online.Server.exe");
            var (exit, output) = await EndToEnd.RunAsync("powershell.exe",
                ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script, "-StoreLocation", "CurrentUser", "-IpAddress", address,
                    "-ServiceAccount", Environment.UserDomainName + "\\" + Environment.UserName, "-Exe", exe], TimeSpan.FromSeconds(120));
            log.WriteLine(output);
            Assert.True(exit == 0, output);
            Assert.Contains("(the same pin as DW4Online.Server.exe tls-pin)", output);
            var pin = output.Split('\n').Select(l => l.Trim()).First(l => l.StartsWith("SPKI pin (SHA-256, base64): ", StringComparison.Ordinal))[28..];

            await using var host = await RealHost.StartAsync(RelaySettings.On(new()
            {
                ["Service:PathBase"] = "/dw4online",
                ["Tls:StoreLocation"] = "CurrentUser",
                ["Relay:ListenAddress"] = address,
                ["Service:RelayHost"] = "", // the request's host: the IP address the client used
            }), httpsAddress: address);
            Assert.StartsWith($"https://{address}:", host.Base);
            using var web = WebBrowser.ForPinned(host.BaseUri, pin, "/dw4online");
            await web.RegisterAsync("StoreTamer", "store test password");

            var dir = Path.Combine(Path.GetTempPath(), "dw4online-e2e-" + Guid.NewGuid().ToString("N")[..8]);
            Directory.CreateDirectory(dir);
            try
            {
                var (save, record, _) = await WriteClientInputsAsync(dir, "STORE");
                (exit, output) = await EndToEnd.RunAsync(EndToEnd.ClientPath!,
                    ["--base", host.Base + "/dw4online", "--user", "StoreTamer", "--password", "store test password", "--save", save,
                        "--record", record, "--pin", pin], TimeSpan.FromSeconds(120));
                log.WriteLine(output);
                Assert.True(exit == 0, output);
                Assert.Contains("ALL PASSED", output);
                Assert.Contains("ok   relay host handshake", output);
            }
            finally
            {
                Directory.Delete(dir, true);
            }
        }
        finally
        {
            RemoveTestCertificates(address);
        }
    }

    /// <summary>Removes the deploy-kit test certificates for <paramref name="address"/> from the current user's store, keys included.</summary>
    [System.Runtime.Versioning.SupportedOSPlatform("windows")]
    private static void RemoveTestCertificates(string address)
    {
        using var store = new X509Store(StoreName.My, StoreLocation.CurrentUser);
        store.Open(OpenFlags.ReadWrite | OpenFlags.OpenExistingOnly);
        foreach (var cert in store.Certificates)
        {
            using (cert)
            {
                if (cert.Subject != "CN=" + address || !cert.FriendlyName.StartsWith("DW4Online " + address, StringComparison.Ordinal)) continue;
                using (var key = cert.GetRSAPrivateKey()) (key as System.Security.Cryptography.RSACng)?.Key.Delete();
                store.Remove(cert);
            }
        }
    }

    /// <summary>The C++ client's inputs: a fresh character's record and a GCI save holding it.</summary>
    private static async Task<(string Save, string Record, byte[] SaveBytes)> WriteClientInputsAsync(string dir, string name)
    {
        var character = Samples.Fresh(name);
        var record = Path.Combine(dir, "record.bin");
        await File.WriteAllBytesAsync(record, RecordCodec.Encode(character));
        var saveBytes = new GciEntry().Encode()
            .Concat(SaveFile.Write(new byte[SaveFile.HeaderSize], [character, new SaveRecord(), new SaveRecord(), new SaveRecord()])).ToArray();
        var save = Path.Combine(dir, "savedata.dat");
        await File.WriteAllBytesAsync(save, saveBytes);
        return (save, record, saveBytes);
    }

    [PythonFact]
    public async Task Spike_python_relay_test()
    {
        await using var host = await RealHost.StartAsync(RelaySettings.On(new()
        {
            ["RateLimits:LoginPerMinute"] = "5",
            ["RateLimits:RegisterPerHour"] = "100",
        }));
        var script = Path.Combine(EndToEnd.ServerRoot, "tests", "e2e", "relay_spike_test.py");
        var (exit, output) = await EndToEnd.RunAsync(EndToEnd.PythonPath!, ["-I", script, "--base", host.Base], TimeSpan.FromSeconds(120));
        log.WriteLine(output);
        Assert.True(exit == 0, output);
        Assert.Contains("ALL PASSED", output);
        Assert.Contains("relay round trip ms", output);
    }
}
