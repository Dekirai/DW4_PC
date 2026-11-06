using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using Dapper;
using DW4Online.Server;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.TestHost;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using MySqlConnector;

namespace DW4Online.Integration.Tests;

/// <summary>A fact that only runs with a private test database (DW4ONLINE_TEST_MYSQL).</summary>
public sealed class DbFactAttribute : FactAttribute
{
    public DbFactAttribute()
    {
        if (string.IsNullOrEmpty(TestDb.AdminConnectionString))
            Skip = "DW4ONLINE_TEST_MYSQL is not set (start a private MariaDB with scripts/local-db.ps1).";
    }
}

/// <summary>A theory that only runs with a private test database (DW4ONLINE_TEST_MYSQL).</summary>
public sealed class DbTheoryAttribute : TheoryAttribute
{
    public DbTheoryAttribute()
    {
        if (string.IsNullOrEmpty(TestDb.AdminConnectionString))
            Skip = "DW4ONLINE_TEST_MYSQL is not set (start a private MariaDB with scripts/local-db.ps1).";
    }
}

public static class TestDb
{
    public static string? AdminConnectionString => Environment.GetEnvironmentVariable("DW4ONLINE_TEST_MYSQL");

    public static async Task<string> CreateAsync()
    {
        var name = "dw4online_it_" + Guid.NewGuid().ToString("N")[..10];
        await using var c = new MySqlConnection(AdminConnectionString);
        await c.ExecuteAsync($"CREATE DATABASE `{name}` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci");
        return name;
    }

    public static async Task DropAsync(string name)
    {
        if (!name.StartsWith("dw4online_it_", StringComparison.Ordinal)) throw new InvalidOperationException("only test databases are dropped");
        await using var c = new MySqlConnection(AdminConnectionString);
        await c.ExecuteAsync($"DROP DATABASE IF EXISTS `{name}`");
    }

    public static string ConnectionString(string database) =>
        new MySqlConnectionStringBuilder(AdminConnectionString!) { Database = database }.ConnectionString;
}

/// <summary>A controllable clock for expiry and lockout tests.</summary>
public sealed class ManualClock() : TimeProvider
{
    private DateTimeOffset _now = new(2026, 11, 1, 12, 0, 0, TimeSpan.Zero);
    private readonly Lock _gate = new();

    public override DateTimeOffset GetUtcNow()
    {
        lock (_gate) return _now;
    }

    public void Advance(TimeSpan by)
    {
        lock (_gate) _now += by;
    }
}

/// <summary>The whole service on a TestServer with its own fresh database.</summary>
public sealed class TestHost : IAsyncDisposable
{
    private TestHost(WebApplication app, string database, ManualClock clock)
    {
        App = app;
        Database = database;
        Clock = clock;
    }

    public WebApplication App { get; }
    public string Database { get; }
    public ManualClock Clock { get; }

    /// <summary>The stand-in world relay (snapshot, play contexts, admission, notifier calls); null with <c>fakeWorld: false</c>.</summary>
    public DW4Online.Tests.FakeWorldRelay? World => App.Services.GetService<DW4Online.Tests.FakeWorldRelay>();

    /// <param name="fakeWorld">
    /// Registers a <see cref="DW4Online.Tests.FakeWorldRelay"/> as the world relay and the notifier (after ServerApp's
    /// registrations, replacing them); false leaves the server's own (the relay lane's world relay, or the null ones).
    /// </param>
    public static async Task<TestHost> StartAsync(Dictionary<string, string?>? overrides = null, string? settingsFile = null, bool fakeWorld = true)
    {
        var database = await TestDb.CreateAsync();
        var clock = new ManualClock();
        var settings = Settings(database, overrides);
        var app = ServerApp.Build(["--environment", "Testing"], settingsFile: settingsFile, configure: builder =>
        {
            builder.WebHost.UseTestServer();
            builder.Configuration.AddInMemoryCollection(settings);
            builder.Services.AddSingleton<TimeProvider>(clock);
            builder.Services.AddSingleton<IStartupFilter, TestClientAddress>();
        }, overrideServices: services =>
        {
            if (!fakeWorld) return;
            // After the composition root's registrations: the stand-in replaces the relay's own (registered with AddSingleton
            // before the TryAdd lines) as well as the null objects.
            services.RemoveAll<DW4Online.Domain.Abstractions.IWorldRelay>();
            services.RemoveAll<DW4Online.Domain.Abstractions.IWorldNotifier>();
            services.AddSingleton<DW4Online.Tests.FakeWorldRelay>();
            services.AddSingleton<DW4Online.Domain.Abstractions.IWorldRelay>(sp => sp.GetRequiredService<DW4Online.Tests.FakeWorldRelay>());
            services.AddSingleton<DW4Online.Domain.Abstractions.IWorldNotifier>(sp => sp.GetRequiredService<DW4Online.Tests.FakeWorldRelay>());
        });
        await ServerApp.MigrateIfEnabledAsync(app);
        await app.StartAsync();
        return new TestHost(app, database, clock);
    }

    /// <summary>The test settings: a private database, cheap hashing, rate limits out of the way, the relay off.</summary>
    public static Dictionary<string, string?> Settings(string database, Dictionary<string, string?>? overrides)
    {
        var settings = new Dictionary<string, string?>
        {
            ["Database:ConnectionString"] = TestDb.ConnectionString(database),
            ["Accounts:PasswordIterations"] = "1000",
            ["RateLimits:GlobalPerMinute"] = "100000",
            ["RateLimits:LoginPerMinute"] = "100000",
            ["RateLimits:RegisterPerHour"] = "100000",
            ["RateLimits:CharacterWritesPerMinute"] = "100000",
            ["RateLimits:CloudWritesPerHour"] = "100000",
            ["RateLimits:AchievementsPerMinute"] = "100000",
            ["RateLimits:AccountChangesPerMinute"] = "100000",
            ["RateLimits:StaffPerMinute"] = "100000",
            ["RateLimits:RoomCreatesPerMinute"] = "100000",
            ["RateLimits:RoomJoinsPerMinute"] = "100000",
            ["RateLimits:WorldEntersPerMinute"] = "100000",
            ["RateLimits:WebLoginPerMinute"] = "100000",
            ["RateLimits:WebRegisterPerHour"] = "100000",
            ["RateLimits:WebAccountChangesPerMinute"] = "100000",
            ["RateLimits:StageChangesPerMinute"] = "100000",
            ["RateLimits:SocialPerMinute"] = "100000",
            ["RateLimits:FriendListSeconds"] = "0",
            ["RateLimits:WebSocialPerMinute"] = "100000",
            ["Rooms:PasswordIterations"] = "1000",
            ["Web:DataProtectionKeysDirectory"] = "memory",
            ["Relay:Enabled"] = "false",
            ["Play:RequireWorld"] = "true",
            ["Logging:LogLevel:Default"] = "Warning",
        };
        if (overrides is not null)
            foreach (var (k, v) in overrides) settings[k] = v;
        return settings;
    }

    /// <summary>An API client; <paramref name="ip"/> is the client address the service sees (default 192.0.2.1).</summary>
    public HttpClient Client(string? token = null, string? ip = null)
    {
        var client = App.GetTestClient();
        if (token is not null) client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", token);
        if (ip is not null) client.DefaultRequestHeaders.Add(TestClientAddress.Header, ip);
        return client;
    }

    public async Task<MySqlConnection> SqlAsync()
    {
        var c = new MySqlConnection(TestDb.ConnectionString(Database));
        await c.OpenAsync();
        return c;
    }

    /// <summary>Registers an account and returns its token and recovery code.</summary>
    public async Task<(string Token, string RecoveryCode, long Id)> RegisterAsync(string username, string password = "a decent password")
    {
        var response = await Client().PostAsJsonAsync("/api/v1/auth/register", new { username, password, deviceName = "tests" });
        Assert.Equal(HttpStatusCode.Created, response.StatusCode);
        var body = await response.Json();
        return (body.GetProperty("token").GetString()!, body.GetProperty("recoveryCode").GetString()!, body.GetProperty("account").GetProperty("id").GetInt64());
    }

    /// <summary>Logs in through the API (a game session) and returns its token.</summary>
    public async Task<(string Token, long Id)> LoginAsync(string username, string password = "a decent password")
    {
        var response = await Client().PostAsJsonAsync("/api/v1/auth/login", new { username, password, deviceName = "tests" });
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        var body = await response.Json();
        return (body.GetProperty("token").GetString()!, body.GetProperty("account").GetProperty("id").GetInt64());
    }

    /// <summary>A durable room row with members, as the room service of the room era wrote it (read-only since round I1).</summary>
    public async Task<string> CreateRoomAsync(long host, IEnumerable<long> guests, bool ranked = true, bool randomizer = false)
    {
        var code = Guid.NewGuid().ToString("N")[..6].ToUpperInvariant();
        await using var c = await SqlAsync();
        var now = Clock.GetUtcNow().UtcDateTime;
        var id = await c.ExecuteScalarAsync<long>(
            "INSERT INTO rooms (public_code, name, host_account_id, max_players, protocol, data_hash, client_version, ranked, randomizer, status, created_at) " +
            "VALUES (@code, 'Test room', @host, 10, 40, 1, 'tests', @ranked, @randomizer, 'playing', @now); SELECT LAST_INSERT_ID();",
            new { code, host, ranked, randomizer, now });
        var slot = 0;
        foreach (var member in guests.Prepend(host))
            await c.ExecuteAsync("INSERT INTO room_members (room_id, account_id, slot, joined_at) VALUES (@id, @member, @slot, @now)",
                new { id, member, slot = slot++, now });
        return code;
    }

    /// <summary>
    /// Puts the account in the world for the achievements (19.8.9): at home (wid 0), or in instance <paramref name="wid"/>
    /// as host or guest with <paramref name="players"/> players of the last minute.
    /// </summary>
    public void InWorld(long account, uint wid = 0, bool host = true, int players = 1, bool randomizer = false, ulong dataHash = 1,
        long? historyId = null) =>
        World!.SetPlayContext(account, new DW4Online.Domain.Abstractions.WorldPlayContext(wid, historyId ?? (wid == 0 ? null : wid), host, players,
            randomizer, dataHash, 0));

    /// <summary>A protocol the version gate takes (the world's floor).</summary>
    public const int Protocol = DW4Online.Domain.World.WorldProtocol.Version;

    public async ValueTask DisposeAsync()
    {
        await App.StopAsync();
        await App.DisposeAsync();
        await TestDb.DropAsync(Database);
    }
}

public static class HttpExtensions
{
    public static async Task<JsonElement> Json(this HttpResponseMessage response) =>
        await response.Content.ReadFromJsonAsync<JsonElement>();

    public static async Task<string> ProblemCode(this HttpResponseMessage response)
    {
        Assert.Equal("application/problem+json", response.Content.Headers.ContentType?.MediaType);
        return (await response.Json()).GetProperty("code").GetString()!;
    }

    public static Task<HttpResponseMessage> PutJson(this HttpClient client, string url, object body, string? ifMatch = null, bool create = false)
    {
        var request = new HttpRequestMessage(HttpMethod.Put, url) { Content = JsonContent.Create(body) };
        if (ifMatch is not null) request.Headers.TryAddWithoutValidation("If-Match", ifMatch);
        if (create) request.Headers.TryAddWithoutValidation("If-None-Match", "*");
        return client.SendAsync(request);
    }
}

/// <summary>
/// TestServer has no real connection: a test request names its client address in the X-Test-Ip header (default
/// 192.0.2.1, a documentation address), so per-IP rules can be tested. Test hosts only.
/// </summary>
public sealed class TestClientAddress : IStartupFilter
{
    public const string Header = "X-Test-Ip";
    public static readonly IPAddress Default = IPAddress.Parse("192.0.2.1");

    public Action<IApplicationBuilder> Configure(Action<IApplicationBuilder> next) => app =>
    {
        app.Use(async (ctx, call) =>
        {
            ctx.Connection.RemoteIpAddress = ctx.Request.Headers.TryGetValue(Header, out var value) && IPAddress.TryParse(value, out var ip)
                ? ip
                : ctx.Connection.RemoteIpAddress ?? Default;
            await call();
        });
        next(app);
    };
}
