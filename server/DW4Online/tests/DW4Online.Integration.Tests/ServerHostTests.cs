using System.Diagnostics;
using System.Net;
using DW4Online.Server;
using Microsoft.Extensions.Configuration;
using MySqlConnector;

namespace DW4Online.Integration.Tests;

public class ServerHostTests
{
    [DbFact]
    public async Task Settings_file_and_acme_challenge_folder()
    {
        var dir = Path.Combine(Path.GetTempPath(), "dw4online-it-" + Guid.NewGuid().ToString("N")[..8]);
        Directory.CreateDirectory(Path.Combine(dir, "acme"));
        try
        {
            await File.WriteAllTextAsync(Path.Combine(dir, "acme", "tok-EN_123"), "tok-EN_123.key");
            var settings = Path.Combine(dir, "appsettings.Production.json");
            await File.WriteAllTextAsync(settings,
                "{ \"Service\": { \"Motd\": \"from the settings file\" }, \"Tls\": { \"AcmeChallengeDirectory\": " +
                System.Text.Json.JsonSerializer.Serialize(Path.Combine(dir, "acme")) + " } }");
            await using var host = await TestHost.StartAsync(settingsFile: settings);
            var status = await (await host.Client().GetAsync("/api/v1/status")).Json();
            Assert.Equal("from the settings file", status.GetProperty("motd").GetString());
            var challenge = await host.Client().GetAsync("/.well-known/acme-challenge/tok-EN_123");
            Assert.Equal(HttpStatusCode.OK, challenge.StatusCode);
            Assert.Equal("tok-EN_123.key", await challenge.Content.ReadAsStringAsync());
            Assert.Equal(HttpStatusCode.NotFound, (await host.Client().GetAsync("/.well-known/acme-challenge/missing")).StatusCode);
            Assert.Equal(HttpStatusCode.NotFound, (await host.Client().GetAsync("/.well-known/acme-challenge/..%5Cappsettings.Production.json")).StatusCode);
        }
        finally
        {
            Directory.Delete(dir, true);
        }
    }

    /// <summary>A database that does not answer yet (MariaDB still starting) is tried again for a while, then the start fails.</summary>
    [Fact]
    public async Task The_startup_waits_a_while_for_the_database()
    {
        await using var app = ServerApp.Build(["--environment", "Testing"], configure: builder => builder.Configuration.AddInMemoryCollection(
            new Dictionary<string, string?>
            {
                ["Database:ConnectionString"] = "Server=127.0.0.1;Port=1;Database=x;User ID=x;Password=x;Connection Timeout=2",
                ["Relay:Enabled"] = "false",
                ["Web:DataProtectionKeysDirectory"] = "memory",
                ["Logging:LogLevel:Default"] = "None",
            }));
        var watch = Stopwatch.StartNew();
        var error = await Assert.ThrowsAsync<MySqlException>(() => ServerApp.MigrateIfEnabledAsync(app, TimeSpan.FromSeconds(3)));
        Assert.Equal(MySqlErrorCode.UnableToConnectToHost, error.ErrorCode);
        Assert.InRange(watch.Elapsed, TimeSpan.FromSeconds(3), TimeSpan.FromSeconds(30)); // tried again, then gave up
    }
}
