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
using Microsoft.AspNetCore.HttpOverrides;
using Microsoft.Extensions.DependencyInjection.Extensions;

namespace DW4Online.Server;

/// <summary>The composition root: one process with the API, the website and the relay (groundwork option A).</summary>
public static class ServerApp
{
    public static WebApplication Build(string[] args, bool windowsService = false, string? settingsFile = null, Action<WebApplicationBuilder>? configure = null)
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
        var friends = Bind<FriendsOptions>(config, "Friends");
        var service = Bind<ServiceOptions>(config, "Service");
        var limits = Bind<RateLimitOptions>(config, "RateLimits");
        var relay = Bind<RelayOptions>(config, "Relay");
        var rooms = Bind<RoomOptions>(config, "Rooms");
        var sealing = Bind<SealingOptions>(config, "Sealing");
        var instances = Bind<InstanceOptions>(config, "Instances");
        instances.PlayersPerInstance = rooms.PlayersPerInstance;
        var tls = Bind<TlsOptions>(config, "Tls");
        var database = Bind<DatabaseOptions>(config, "Database");
        if (string.IsNullOrEmpty(database.ConnectionString)) database.ConnectionString = config.GetConnectionString("Main") ?? "";

        var services = builder.Services;
        services.TryAddSingleton(TimeProvider.System); // tests register a controllable clock first
        services.AddSingleton(accounts).AddSingleton(character).AddSingleton(cloud).AddSingleton(play)
            .AddSingleton(service).AddSingleton(limits).AddSingleton(relay).AddSingleton(rooms).AddSingleton(database)
            .AddSingleton(sealing).AddSingleton(instances).AddSingleton(friends);
        services.AddSingleton(_ => new Database(database.ConnectionString));
        services.AddSingleton<IAccountRepository, AccountRepository>();
        services.AddSingleton<ISessionRepository, SessionRepository>();
        services.AddSingleton<IAuditLog, AuditLog>();
        services.AddSingleton<IOnlineCharacterRepository, OnlineCharacterRepository>();
        services.AddSingleton<IOnlineStoryRepository, OnlineStoryRepository>();
        services.AddSingleton<ICloudSaveRepository, CloudSaveRepository>();
        services.AddSingleton<IAchievementRepository, AchievementRepository>();
        services.AddSingleton<IRoomDirectory, RoomDirectory>();
        services.AddSingleton<IRoomRepository, RoomRepository>();
        services.AddSingleton<ISocialRepository, SocialRepository>();
        // Round I2-social: nobody listens yet. Round I1's relay registers its in-process implementation BEFORE this line
        // (then this TryAdd adds nothing) or replaces it after it (services.Replace / AddSingleton): a TryAdd after this
        // line would be silently ignored.
        services.TryAddSingleton<IWorldNotifier>(NullWorldNotifier.Instance);
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
        }
        services.AddSingleton<RelayGateway>();
        services.AddSingleton<IRelayGateway>(sp => sp.GetRequiredService<RelayGateway>());
        services.AddSingleton<RoomService>();
        services.AddHostedService<RoomSweeper>();

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

        var app = builder.Build();
        if (service.BehindProxy)
        {
            // Only a proxy on this machine is trusted (the defaults: 127.0.0.0/8 and ::1).
            app.UseForwardedHeaders(new ForwardedHeadersOptions
            {
                ForwardedHeaders = ForwardedHeaders.XForwardedFor | ForwardedHeaders.XForwardedProto,
            });
        }
        if (service.NormalizedPathBase() is { Length: > 0 } pathBase) app.UsePathBase(pathBase);
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
/// Hourly housekeeping: audit retention (180 days), expired sessions, the history of reset online characters and expired
/// friend requests (Friends:RequestDays).
/// </summary>
internal sealed class MaintenanceService(MaintenanceRepository repository, SocialService social, OnlineCharacterOptions characters,
    TimeProvider time, ILogger<MaintenanceService> log) : BackgroundService
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
                    if (audit + sessions + revisions + requests > 0)
                        log.LogInformation("Pruned {Audit} audit rows, {Sessions} sessions, {Revisions} revisions of reset characters and {Requests} expired friend requests",
                            audit, sessions, revisions, requests);
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
