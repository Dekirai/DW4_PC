using System.Diagnostics;
using System.Net;
using Dapper;
using DW4Online.Server;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.TestHost;
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
        var error = await Assert.ThrowsAsync<DatabaseUnavailableException>(() => ServerApp.MigrateIfEnabledAsync(app, TimeSpan.FromSeconds(3)));
        Assert.Equal(MySqlErrorCode.UnableToConnectToHost, Assert.IsType<MySqlException>(error.InnerException).ErrorCode);
        Assert.Contains("did not answer within 3 s", error.Message);
        Assert.InRange(watch.Elapsed, TimeSpan.FromSeconds(3), TimeSpan.FromSeconds(30)); // tried again, then gave up
    }

    /// <summary>
    /// The start as the service runs it (ServerStartup, here in the console): the migrations with their times in the
    /// file log, then the host; the stop returns exit code 0.
    /// </summary>
    [DbFact]
    public async Task The_start_migrates_with_its_times_in_the_file_log_then_serves()
    {
        var database = await TestDb.CreateAsync();
        var logs = Path.Combine(Path.GetTempPath(), "dw4online-it-" + Guid.NewGuid().ToString("N")[..8]);
        try
        {
            var app = BuildWithFileLog(database, logs);
            var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            app.Lifetime.ApplicationStarted.Register(() => started.TrySetResult());
            var run = ServerStartup.RunAsync(app, asService: false);
            await started.Task.WaitAsync(TimeSpan.FromSeconds(60));
            var health = await (await app.GetTestClient().GetAsync("/health")).Json();
            var all = DW4Online.Data.Migrations.Migrator.LoadEmbedded();
            var last = all[^1].Version;
            Assert.Equal(last, health.GetProperty("schema").GetInt32());
            app.Lifetime.StopApplication();
            Assert.Equal(0, await run.WaitAsync(TimeSpan.FromSeconds(60)));

            var log = await ReadLogAsync(logs);
            Assert.Contains($"INFO Migrator: Migrating database '{database}' from schema 0 to {last} ({all.Count} migration(s)) ...", log);
            Assert.Matches(@"INFO Migrator: Applied migration 0001 initial in \d+\.\d\d s", log);
            Assert.Matches($@"INFO Migrator: Migrating database '{database}' done in \d+\.\d\d s: schema {last}", log);
            Assert.Contains("INFO Migrator: Applied migrations " + string.Join(", ", all.Select(m => m.Version)), log);
            Assert.DoesNotContain(" FAIL ", log);
        }
        finally
        {
            await TestDb.DropAsync(database);
            if (Directory.Exists(logs)) Directory.Delete(logs, true);
        }
    }

    /// <summary>A start the migrator refuses ends with ExitStartRefused and the reason as a FAIL line in the file log.</summary>
    [DbFact]
    public async Task A_refused_start_leaves_its_reason_and_the_way_out_in_the_file_log()
    {
        var database = await TestDb.CreateAsync();
        var logs = Path.Combine(Path.GetTempPath(), "dw4online-it-" + Guid.NewGuid().ToString("N")[..8]);
        try
        {
            await using (var c = new MySqlConnection(TestDb.ConnectionString(database)))
            {
                await c.ExecuteAsync("CREATE TABLE someone_elses (id INT PRIMARY KEY); INSERT INTO someone_elses VALUES (1)");
            }
            var app = BuildWithFileLog(database, logs);
            Assert.Equal(ServerStartup.ExitStartRefused, await ServerStartup.RunAsync(app, asService: false));

            var log = await ReadLogAsync(logs);
            Assert.Contains($" FAIL DW4Online.Server.Startup: Start failed: Database '{database}' has tables but no schema_version; refusing to touch it.", log);
            Assert.Contains("Its tables hold rows (someone_elses)", log);
            Assert.Contains("DW4Online.Data.Migrations.MigrationException", log); // the exception follows the line
            await using var check = new MySqlConnection(TestDb.ConnectionString(database));
            Assert.Equal(1, await check.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE()"));
        }
        finally
        {
            await TestDb.DropAsync(database);
            if (Directory.Exists(logs)) Directory.Delete(logs, true);
        }
    }

    private static WebApplication BuildWithFileLog(string database, string logs) =>
        ServerApp.Build(["--environment", "Testing"], configure: builder =>
        {
            builder.WebHost.UseTestServer();
            builder.Configuration.AddInMemoryCollection(TestHost.Settings(database, new Dictionary<string, string?>
            {
                ["Logging:LogLevel:Default"] = "Information",
                ["Logging:File:Directory"] = logs,
            }));
        });

    private static async Task<string> ReadLogAsync(string directory)
    {
        var file = Assert.Single(Directory.GetFiles(directory, "dw4online-*.log"));
        await using var stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
        return await new StreamReader(stream).ReadToEndAsync();
    }
}
