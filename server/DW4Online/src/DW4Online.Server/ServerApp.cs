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
using DW4Online.Web;
using DW4Online.Web.Auth;
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
            builder.Logging.AddProvider(new FileLoggerProvider(path, config.GetSection("Logging:File").GetValue("RetainDays", 30), level));
        }
        if (windowsService && OperatingSystem.IsWindows())
            builder.Services.AddSingleton<IHostLifetime, WindowsServiceLifetime>();

        var accounts = Bind<AccountOptions>(config, "Accounts");
        var character = Bind<OnlineCharacterOptions>(config, "OnlineCharacter");
        var cloud = Bind<CloudSaveOptions>(config, "CloudSave");
        var play = Bind<PlayOptions>(config, "Play");
        var service = Bind<ServiceOptions>(config, "Service");
        var limits = Bind<RateLimitOptions>(config, "RateLimits");
        var relay = Bind<RelayOptions>(config, "Relay");
        var rooms = Bind<RoomOptions>(config, "Rooms");
        var tls = Bind<TlsOptions>(config, "Tls");
        var database = Bind<DatabaseOptions>(config, "Database");
        if (string.IsNullOrEmpty(database.ConnectionString)) database.ConnectionString = config.GetConnectionString("Main") ?? "";

        var services = builder.Services;
        services.TryAddSingleton(TimeProvider.System); // tests register a controllable clock first
        services.AddSingleton(accounts).AddSingleton(character).AddSingleton(cloud).AddSingleton(play)
            .AddSingleton(service).AddSingleton(limits).AddSingleton(relay).AddSingleton(rooms).AddSingleton(database);
        services.AddSingleton(_ => new Database(database.ConnectionString));
        services.AddSingleton<IAccountRepository, AccountRepository>();
        services.AddSingleton<ISessionRepository, SessionRepository>();
        services.AddSingleton<IAuditLog, AuditLog>();
        services.AddSingleton<IOnlineCharacterRepository, OnlineCharacterRepository>();
        services.AddSingleton<ICloudSaveRepository, CloudSaveRepository>();
        services.AddSingleton<IAchievementRepository, AchievementRepository>();
        services.AddSingleton<IRoomDirectory, RoomDirectory>();
        services.AddSingleton<IRoomRepository, RoomRepository>();
        services.AddSingleton<MaintenanceRepository>();
        services.AddSingleton(_ => new PasswordHasher(accounts.PasswordIterations));
        services.AddSingleton<VersionGate>();
        services.AddSingleton<AccountService>();
        services.AddSingleton<AchievementService>();
        services.AddSingleton<OnlineCharacterService>();
        services.AddSingleton<CloudSaveService>();
        services.AddHostedService<MaintenanceService>();

        // Relay:TicketKey signs relay tickets and (through HKDF) keys the security log's tags of unknown user names.
        services.AddSingleton(sp => new ServerSecret(RelayKey(relay, builder.Environment, sp.GetRequiredService<ILogger<RelayServer>>())));
        services.AddSingleton(sp => new RelayTickets(sp.GetRequiredService<ServerSecret>().Key,
            TimeSpan.FromSeconds(relay.TicketLifetimeSeconds), sp.GetRequiredService<TimeProvider>()));
        services.AddSingleton(sp => new AuditNameHasher(sp.GetRequiredService<ServerSecret>().Key));
        if (relay.Enabled)
        {
            services.AddSingleton<RelayServer>();
            services.AddHostedService(sp => sp.GetRequiredService<RelayServer>());
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

    /// <summary>The server secret (Relay:TicketKey, or a key for this process only in Development / with the relay off).</summary>
    private sealed record ServerSecret(byte[] Key);

    /// <summary>Runs the startup migrator when Database:AutoMigrate is on (the default).</summary>
    public static async Task MigrateIfEnabledAsync(WebApplication app)
    {
        if (!app.Services.GetRequiredService<DatabaseOptions>().AutoMigrate) return;
        var log = app.Services.GetRequiredService<ILoggerFactory>().CreateLogger("Migrator");
        var applied = await new Migrator(app.Services.GetRequiredService<Database>(), m => log.LogInformation("{Message}", m)).MigrateAsync();
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
        var selector = new StoreCertificateSelector(tls, kestrel.ApplicationServices.GetRequiredService<ILoggerFactory>().CreateLogger("Tls"));
        kestrel.ConfigureHttpsDefaults(https => https.ServerCertificateSelector = (_, _) => selector.Select());
    }

    private static byte[] RelayKey(RelayOptions relay, IHostEnvironment env, ILogger log)
    {
        if (!string.IsNullOrEmpty(relay.TicketKey))
        {
            var key = Convert.FromBase64String(relay.TicketKey);
            if (key.Length < 32) throw new InvalidOperationException("Relay:TicketKey must hold at least 32 bytes (base64).");
            return key;
        }
        if (relay.Enabled && !env.IsDevelopment())
            throw new InvalidOperationException("Relay:TicketKey is not configured (run 'DW4Online.Server gen-relay-key').");
        if (relay.Enabled) log.LogWarning("Relay:TicketKey is not configured; using a temporary key until the process exits.");
        return RandomNumberGenerator.GetBytes(32);
    }
}

/// <summary>Hourly housekeeping: audit retention (180 days) and expired sessions.</summary>
internal sealed class MaintenanceService(MaintenanceRepository repository, TimeProvider time, ILogger<MaintenanceService> log) : BackgroundService
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
                    var (audit, sessions) = await repository.PruneAsync(Clock.Now(time), 180, stop);
                    if (audit + sessions > 0) log.LogInformation("Pruned {Audit} audit rows and {Sessions} sessions", audit, sessions);
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
