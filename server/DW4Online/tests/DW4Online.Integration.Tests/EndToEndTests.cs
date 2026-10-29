using System.Diagnostics;
using System.Text;
using DW4Online.Domain.Save;
using DW4Online.Server;
using DW4Online.Tests;
using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.Configuration;

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
    private RealHost(WebApplication app, string database, Uri baseUri)
    {
        App = app;
        Database = database;
        BaseUri = baseUri;
    }

    public WebApplication App { get; }
    public string Database { get; }
    public Uri BaseUri { get; }
    public string Base => BaseUri.ToString().TrimEnd('/');

    public static async Task<RealHost> StartAsync(Dictionary<string, string?>? overrides = null)
    {
        var database = await TestDb.CreateAsync();
        var settings = TestHost.Settings(database, overrides);
        settings["Kestrel:Endpoints:Http:Url"] = "http://127.0.0.1:0";
        var app = ServerApp.Build(["--environment", "Testing"], configure: builder => builder.Configuration.AddInMemoryCollection(settings));
        await ServerApp.MigrateIfEnabledAsync(app);
        await app.StartAsync();
        return new RealHost(app, database, new Uri(app.Urls.First() + "/"));
    }

    public async ValueTask DisposeAsync()
    {
        await App.StopAsync();
        await App.DisposeAsync();
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
            var character = Samples.Fresh("E2E");
            var record = Path.Combine(dir, "record.bin");
            await File.WriteAllBytesAsync(record, RecordCodec.Encode(character));
            var saveBytes = new GciEntry().Encode()
                .Concat(SaveFile.Write(new byte[SaveFile.HeaderSize], [character, new SaveRecord(), new SaveRecord(), new SaveRecord()])).ToArray();
            var save = Path.Combine(dir, "savedata.dat");
            await File.WriteAllBytesAsync(save, saveBytes);

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
