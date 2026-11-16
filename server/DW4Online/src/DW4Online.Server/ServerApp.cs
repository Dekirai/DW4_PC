using System.Security.Cryptography;
using DW4Online.Api;
using DW4Online.Api.Auth;
using DW4Online.Data;
using DW4Online.Data.Migrations;
using DW4Online.Data.Repositories;
using DW4Online.Domain;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Rooms;
using DW4Online.Domain.Security;
using DW4Online.Domain.Services;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Web;
using DW4Online.Web.Auth;
using Microsoft.Extensions.DependencyInjection.Extensions;

namespace DW4Online.Server;

/// <summary>The composition root: one process with the API, the website and the relay (groundwork option A).</summary>
public static class ServerApp
{
    /// <param name="configure">Runs before the composition root's registrations (settings, a clock: TryAdd lines then add nothing).</param>
    /// <param name="overrideServices">Runs after them, right before the build (tests: a stand-in that replaces a registration
    /// whatever way it was made, such as the world relay's AddSingleton).</param>
    public static WebApplication Build(string[] args, bool windowsService = false, string? settingsFile = null, Action<WebApplicationBuilder>? configure = null,
        Action<IServiceCollection>? overrideServices = null)
    {
        var builder = WebApplication.CreateBuilder(new WebApplicationOptions
        {
            Args = args,
            ContentRootPath = AppContext.BaseDirectory, // a service starts in System32
        });
        // Local secrets (connection string, relay key) live next to the executable and never in Git.
        builder.Configuration.AddJsonFile("appsettings.Local.json", optional: true, reloadOnChange: false);
        if (settingsFile is not null) builder.Configuration.AddJsonFile(settingsFile, optional: false, reloadOnChange: false);
        configure?.Invoke(builder);
        var config = builder.Configuration;

        if (config.GetSection("Logging:File").GetValue<string>("Directory") is { Length: > 0 } logDir)
        {
            var path = Path.IsPathRooted(logDir) ? logDir : Path.Combine(AppContext.BaseDirectory, logDir);
            var level = config.GetSection("Logging:File").GetValue("MinimumLevel", LogLevel.Information);
            var retain = config.GetSection("Logging:File").GetValue("RetainDays", 30);
            // Made by the container, so disposing the application disposes it: the queue is written out and the file
            // closed before the process ends (an instance from Logging.AddProvider would never be disposed, and the
            // last lines, a start's FAIL line among them, could be lost).
            builder.Services.AddSingleton<ILoggerProvider>(_ => new FileLoggerProvider(path, retain, level));
        }
        if (windowsService && OperatingSystem.IsWindows())
            builder.Services.AddSingleton<IHostLifetime>(NewServiceLifetime);

        var accounts = Bind<AccountOptions>(config, "Accounts");
        var character = Bind<OnlineCharacterOptions>(config, "OnlineCharacter");
        var cloud = Bind<CloudSaveOptions>(config, "CloudSave");
        var play = Bind<PlayOptions>(config, "Play");
        // The room era's name, read for one release (SEAMLESS.md 19.17.5).
        if (config["Play:RequireWorld"] is null && play.RequireRoom is { } requireRoom) play.RequireWorld = requireRoom;
        // (web's keys of the section World; the relay binds its own class to the same section, 19.17.5)
        var worldWeb = Bind<WorldWebOptions>(config, "World");
        if (worldWeb.RankedDataHashes.Count == 0 && config.GetSection("Rooms:RankedDataHashes").Get<List<string>>() is { Count: > 0 } ranked)
            worldWeb.RankedDataHashes = ranked;
        var friends = Bind<FriendsOptions>(config, "Friends");
        var reports = Bind<ReportOptions>(config, "Reports");
        var service = Bind<ServiceOptions>(config, "Service");
        var proxy = ProxyHeaders.From(service); // (checks Service:PathBase and Service:TrustedProxyNetworks)
        var limits = Bind<RateLimitOptions>(config, "RateLimits");
        var relay = Bind<RelayOptions>(config, "Relay");
        var rooms = Bind<RoomOptions>(config, "Rooms");
        var sealing = Bind<SealingOptions>(config, "Sealing");
        var instances = Bind<InstanceOptions>(config, "Instances");
        // The relay's world keys (SEAMLESS.md 19.17.5); web's WorldWebOptions binds the same section for its keys.
        var world = Bind<DW4Online.Relay.World.WorldHubOptions>(config, "World");
        instances.PlayersPerInstance = rooms.PlayersPerInstance;
        var tls = Bind<TlsOptions>(config, "Tls");
        var database = Bind<DatabaseOptions>(config, "Database");
        if (string.IsNullOrEmpty(database.ConnectionString)) database.ConnectionString = config.GetConnectionString("Main") ?? "";

        var services = builder.Services;
        services.TryAddSingleton(TimeProvider.System); // tests register a controllable clock first
        services.AddSingleton(accounts).AddSingleton(character).AddSingleton(cloud).AddSingleton(play)
            .AddSingleton(service).AddSingleton(limits).AddSingleton(relay).AddSingleton(rooms).AddSingleton(database)
            .AddSingleton(sealing).AddSingleton(instances).AddSingleton(friends).AddSingleton(world)
            .AddSingleton(reports);
        services.AddSingleton(_ => new Database(database.ConnectionString));
        services.AddSingleton<IAccountRepository, AccountRepository>();
        services.AddSingleton<ISessionRepository, SessionRepository>();
        services.AddSingleton<IAuditLog, AuditLog>();
        services.AddSingleton<IOnlineCharacterRepository, OnlineCharacterRepository>();
        services.AddSingleton<IOnlineStoryRepository, OnlineStoryRepository>();
        services.AddSingleton<ICloudSaveRepository, CloudSaveRepository>();
        services.AddSingleton<IAchievementRepository, AchievementRepository>();
        services.AddSingleton<IRoomRepository, RoomRepository>();
        services.AddSingleton<ISocialRepository, SocialRepository>();
        services.AddSingleton<IReportRepository, ReportRepository>();
        // Round I1: the world relay is the domain's IWorldRelay and listens to its IWorldNotifier (one WorldGateway), registered
        // BEFORE the TryAdd lines (they then add nothing; without the relay lane's registration they keep the null objects).
        services.AddSingleton<WorldGateway>();
        services.AddSingleton<IWorldRelay>(sp => sp.GetRequiredService<WorldGateway>());
        services.AddSingleton<IWorldNotifier>(sp => sp.GetRequiredService<WorldGateway>());
        services.TryAddSingleton<IWorldNotifier>(NullWorldNotifier.Instance);
        // Round I1a: the world relay as the domain sees it (SEAMLESS.md 19.17). The relay lane registers its implementation
        // (admission, the browser snapshot, the play context, End and Remove) BEFORE this line; until then nobody is online.
        services.TryAddSingleton<IWorldRelay>(NullWorldRelay.Instance);
        services.AddSingleton(worldWeb);
        services.AddSingleton<WorldRepository>();
        services.AddSingleton<IWorldRepository>(sp => sp.GetRequiredService<WorldRepository>());
        services.AddSingleton<IInstanceHistoryRepository>(sp => sp.GetRequiredService<WorldRepository>());
        services.AddSingleton<MaintenanceRepository>();
        services.AddSingleton(_ => new PasswordHasher(accounts.PasswordIterations));
        services.AddSingleton<DW4Online.Domain.Accounts.LoginThrottle>();
        services.AddSingleton<VersionGate>();
        services.AddSingleton<AccountService>();
        services.AddSingleton<AchievementService>();
        services.AddSingleton<DW4Online.Domain.Story.OnlineStoryService>();
        services.AddSingleton<OnlineCharacterService>();
        services.AddSingleton<CloudSaveService>();
        services.AddSingleton<SocialService>();
        services.AddSingleton<ReportService>();
        services.AddHostedService<MaintenanceService>();

        // Relay:TicketKey signs relay tickets and (through HKDF) keys the security log's tags of unknown user names.
        services.AddSingleton(sp => new ServerSecret(RelayKey(relay, builder.Environment, sp.GetRequiredService<ILogger<RelayHub>>())));
        services.AddSingleton(sp => new RelayTickets(sp.GetRequiredService<ServerSecret>().Key,
            TimeSpan.FromSeconds(relay.TicketLifetimeSeconds), sp.GetRequiredService<TimeProvider>()));
        services.AddSingleton(sp => new AuditNameHasher(sp.GetRequiredService<ServerSecret>().Key));
        services.AddHostedService<StoryFlushService>(); // (before the relay: it stops after it)
        if (relay.Enabled)
        {
            services.AddSingleton<RelayHub>();
            services.AddHostedService(sp => sp.GetRequiredService<RelayHub>());
            // The world gateway sets the relay's admission, story saver and history hooks: made at the start, not at its first use.
            services.AddHostedService(sp => new WorldGatewayStart(sp.GetRequiredService<WorldGateway>()));
        }
        services.AddSingleton<RelayGateway>();
        services.AddSingleton<IRelayGateway>(sp => sp.GetRequiredService<RelayGateway>());
        services.AddSingleton<RoomService>();
        services.AddSingleton(sp =>
        {
            var service = ActivatorUtilities.CreateInstance<WorldService>(sp);
            var log = sp.GetRequiredService<ILogger<WorldService>>();
            service.WriteFailed = (what, e) => log.LogWarning(e, "World: {What} failed", what);
            return service;
        });
        services.AddHostedService<WorldHousekeeping>();
        // The test stack's ordered stop (tools/online_test: the restart scenarios): only with Testing:StopFile set.
        if (config["Testing:StopFile"] is { Length: > 0 } stopFile)
            services.AddHostedService(sp => ActivatorUtilities.CreateInstance<StopFileWatcher>(sp, stopFile));

        builder.WebHost.ConfigureKestrel(kestrel =>
        {
            kestrel.AddServerHeader = false;
            kestrel.Limits.MaxRequestBodySize = 64 * 1024; // the cloud save upload raises its own limit
            if (tls.StoreHostName.Length > 0 && OperatingSystem.IsWindows())
                UseStoreCertificate(kestrel, tls);
        });

        services.AddDw4Api(limits);
        services.AddDw4Web(config, builder.Environment);
        // One default scheme: API paths authenticate the bearer token, website paths the session cookie.
        services.AddAuthentication(o =>
            {
                o.DefaultScheme = DefaultScheme;
                o.DefaultChallengeScheme = DefaultScheme;
                o.DefaultForbidScheme = DefaultScheme;
            })
            .AddPolicyScheme(DefaultScheme, "API token or website session", o => o.ForwardDefaultSelector = ctx =>
                WebSetup.IsWebPath(ctx.Request.Path) ? WebSessionHandler.SchemeName : TokenAuthenticationHandler.SchemeName);

        overrideServices?.Invoke(builder.Services);
        var app = builder.Build();
        // The forwarded headers of a proxy on this machine (Service:BehindProxy, Service:TrustedProxyNetworks) and
        // Service:PathBase, before everything else: the client address the rate limits see, the scheme, the path base of
        // every link, redirect and cookie (ProxyHeaders, DEPLOY.md section 6.2).
        if (proxy.BehindProxy)
            app.Services.GetRequiredService<ILoggerFactory>().CreateLogger("Proxy").LogInformation(
                "Behind a proxy: X-Forwarded-For, -Proto and -Prefix (= Service:PathBase) from plain-HTTP connections of this machine, " +
                "CF-Connecting-IP from {Count} trusted edge network(s)",
                proxy.Edges.Count);
        app.Use((context, next) =>
        {
            proxy.Apply(context);
            return next(context);
        });
        // The room era's settings (SEAMLESS.md 19.17.5): ignored since the world, with one warning; removed in the cleanup.
        if (config.GetSection("Rooms").GetChildren().Any(c => c.Key is not ("PlayersPerInstance" or "RankedDataHashes")))
            app.Services.GetRequiredService<ILoggerFactory>().CreateLogger("World").LogWarning(
                "The section Rooms of the settings is no longer used (rooms gave way to the online world); remove it (DEPLOY.md section 9.2)");
        app.UseDw4Web(app.Services.GetRequiredService<WebOptions>(), app.Environment.IsDevelopment());
        app.UseDw4Api();
        app.MapDw4Api();
        app.MapDw4Web();
        if (tls.AcmeChallengeDirectory.Length > 0) MapAcmeChallenge(app, tls.AcmeChallengeDirectory);
        app.MapGet("/health", async (Database db, CancellationToken ct) =>
        {
            try
            {
                var schema = await new Migrator(db).CurrentVersionAsync(ct);
                return Results.Ok(new { status = "ok", schema });
            }
            catch (Exception e) when (e is MySqlConnector.MySqlException or InvalidOperationException)
            {
                return Results.Problem(statusCode: StatusCodes.Status503ServiceUnavailable, title: "The database is not reachable.",
                    type: "urn:dw4online:problem:database_unavailable");
            }
        }).AllowAnonymous();
        return app;
    }

    private const string DefaultScheme = "dw4";

    [System.Runtime.Versioning.SupportedOSPlatform("windows")]
    private static IHostLifetime NewServiceLifetime(IServiceProvider services) =>
        new WindowsServiceLifetime(new NativeServiceControlApi(), services.GetRequiredService<IHostApplicationLifetime>(),
            services.GetRequiredService<ILogger<WindowsServiceLifetime>>());

    /// <summary>The server secret (Relay:TicketKey, or a key for this process only in Development / with the relay off).</summary>
    private sealed record ServerSecret(byte[] Key);

    /// <summary>
    /// Runs the startup migrator when Database:AutoMigrate is on (the default). A database that does not answer yet
    /// (MariaDB still starting after a reboot) is tried again every 2 s for up to <paramref name="waitForDatabase"/>
    /// (Database:StartupWaitSeconds, 60 s); after that a <see cref="DatabaseUnavailableException"/> ends the start and
    /// the service's restart-on-failure actions take over. As a service this runs while the service reports
    /// START_PENDING with growing checkpoints (<see cref="ServerStartup"/>), so neither the wait nor a long migration
    /// meets the Service Control Manager's 30 s limit. Migration failures and refusals propagate (a FAIL line in the log).
    /// </summary>
    public static async Task MigrateIfEnabledAsync(WebApplication app, TimeSpan? waitForDatabase = null)
    {
        var options = app.Services.GetRequiredService<DatabaseOptions>();
        var log = app.Services.GetRequiredService<ILoggerFactory>().CreateLogger("Migrator");
        if (!options.AutoMigrate)
        {
            log.LogInformation("Database:AutoMigrate is off: the schema is not checked at the start (run 'migrate' after every update)");
            return;
        }
        var migrator = new Migrator(app.Services.GetRequiredService<Database>(), m => log.LogInformation("{Message}", m),
            w => log.LogWarning("{Message}", w));
        var wait = waitForDatabase ?? TimeSpan.FromSeconds(Math.Max(0, options.StartupWaitSeconds));
        var started = System.Diagnostics.Stopwatch.GetTimestamp();
        IReadOnlyList<int> applied;
        while (true)
        {
            try
            {
                applied = await migrator.MigrateAsync();
                break;
            }
            catch (MySqlConnector.MySqlException e) when (e.ErrorCode == MySqlConnector.MySqlErrorCode.UnableToConnectToHost)
            {
                var elapsed = System.Diagnostics.Stopwatch.GetElapsedTime(started);
                if (elapsed >= wait)
                {
                    throw new DatabaseUnavailableException(
                        $"The database did not answer within {wait.TotalSeconds:0} s ({e.Message}). Is the MariaDB service running, " +
                        "and do the server and port in Database:ConnectionString match it?", e);
                }
                log.LogWarning("The database does not answer yet ({Message}); trying again in 2 s ({Elapsed:0} of {Wait:0} s)",
                    e.Message, elapsed.TotalSeconds, wait.TotalSeconds);
                await Task.Delay(TimeSpan.FromSeconds(2));
            }
        }
        log.LogInformation(applied.Count == 0 ? "Database schema is current" : "Applied migrations {Versions}", string.Join(", ", applied));
    }

    private static T Bind<T>(IConfiguration config, string section) where T : new() => config.GetSection(section).Get<T>() ?? new T();

    /// <summary>Serves ACME http-01 challenge files (win-acme "filesystem" validation) from a folder.</summary>
    private static void MapAcmeChallenge(WebApplication app, string directory)
    {
        var root = Path.GetFullPath(Path.IsPathRooted(directory) ? directory : Path.Combine(AppContext.BaseDirectory, directory));
        app.MapGet("/.well-known/acme-challenge/{token}", (string token) =>
        {
            if (token.Length is 0 or > 128 || token.Any(c => !(char.IsAsciiLetterOrDigit(c) || c is '-' or '_'))) return Results.NotFound();
            var path = Path.Combine(root, token);
            return File.Exists(path) ? Results.File(path, "text/plain") : Results.NotFound();
        }).AllowAnonymous();
    }

    [System.Runtime.Versioning.SupportedOSPlatform("windows")]
    private static void UseStoreCertificate(Microsoft.AspNetCore.Server.Kestrel.Core.KestrelServerOptions kestrel, TlsOptions tls)
    {
        var source = kestrel.ApplicationServices.GetService<TlsCertificateSource>(); // tests only; the Windows store otherwise
        var selector = new HostCertificateSelector(tls, kestrel.ApplicationServices.GetRequiredService<ILoggerFactory>().CreateLogger("Tls"),
            source is null ? null : source.Read);
        kestrel.ConfigureHttpsDefaults(https => https.ServerCertificateSelector = (_, name) => selector.Select(name));
    }

    /// <summary>
    /// Checks the settings that the host would otherwise trip over only while it starts (as a service: a crash and a
    /// restart loop instead of a clear refusal): Relay:TicketKey. Throws an <see cref="InvalidOperationException"/>
    /// that names the setting.
    /// </summary>
    public static void CheckSettings(WebApplication app) => app.Services.GetRequiredService<ServerSecret>();

    private static byte[] RelayKey(RelayOptions relay, IHostEnvironment env, ILogger log)
    {
        if (!string.IsNullOrEmpty(relay.TicketKey))
        {
            byte[] key;
            try
            {
                key = Convert.FromBase64String(relay.TicketKey);
            }
            catch (FormatException e)
            {
                throw new InvalidOperationException(
                    "Relay:TicketKey is not valid base64 (put the line 'DW4Online.Server gen-relay-key' prints into the settings file).", e);
            }
            if (key.Length < 32) throw new InvalidOperationException("Relay:TicketKey must hold at least 32 bytes (base64).");
            return key;
        }
        if (relay.Enabled && !env.IsDevelopment())
            throw new InvalidOperationException("Relay:TicketKey is not configured (run 'DW4Online.Server gen-relay-key').");
        // Also with the relay off: the key tags the security log's unknown user names, and a temporary one makes the
        // tags of earlier runs unmatched (audit-tag finds nothing, an account deletion leaves them).
        log.LogWarning("Relay:TicketKey is not configured; using a temporary key until the process exits (relay tickets and the " +
                       "security log's name tags change at every restart; run 'DW4Online.Server gen-relay-key').");
        return RandomNumberGenerator.GetBytes(32);
    }
}

/// <summary>
/// Hourly housekeeping: audit retention (180 days), expired sessions, the history of reset online characters, expired
/// friend requests (Friends:RequestDays) and done reports (Reports:DoneDays).
/// </summary>
internal sealed class MaintenanceService(MaintenanceRepository repository, SocialService social, ReportService reports,
    OnlineCharacterOptions characters, TimeProvider time, ILogger<MaintenanceService> log) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stop)
    {
        try
        {
            await Task.Delay(TimeSpan.FromMinutes(1), time, stop);
            while (!stop.IsCancellationRequested)
            {
                try
                {
                    var (audit, sessions, revisions) = await repository.PruneAsync(Clock.Now(time), 180, characters.ResetHistoryDays, stop);
                    var requests = await social.PurgeExpiredAsync(stop);
                    var done = await reports.PruneDoneAsync(stop);
                    if (audit + sessions + revisions + requests + done > 0)
                        log.LogInformation("Pruned {Audit} audit rows, {Sessions} sessions, {Revisions} revisions of reset characters, {Requests} expired friend requests and {Reports} done reports",
                            audit, sessions, revisions, requests, done);
                }
                catch (MySqlConnector.MySqlException e)
                {
                    log.LogWarning(e, "Housekeeping failed");
                }
                await Task.Delay(TimeSpan.FromHours(1), time, stop);
            }
        }
        catch (OperationCanceledException)
        {
        }
    }
}

/// <summary>Makes the world gateway when the host starts (its constructor hooks the relay up).</summary>
internal sealed class WorldGatewayStart(WorldGateway gateway) : IHostedService
{
    public WorldGateway Gateway { get; } = gateway;
    public Task StartAsync(CancellationToken cancellationToken) => Task.CompletedTask;
    public Task StopAsync(CancellationToken cancellationToken) => Gateway.Queue.DrainAsync(TimeSpan.FromSeconds(5));
}
