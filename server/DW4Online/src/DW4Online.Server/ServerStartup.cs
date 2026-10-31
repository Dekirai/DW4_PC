namespace DW4Online.Server;

/// <summary>The database did not answer within Database:StartupWaitSeconds (MariaDB not started, or still starting).</summary>
internal sealed class DatabaseUnavailableException(string message, Exception inner) : Exception(message, inner);

/// <summary>
/// The start of the server process (console or Windows service), in this order:
/// <list type="number">
/// <item>As a service: enter the Service Control Manager's dispatcher at once and report START_PENDING, with a new
/// checkpoint every few seconds from then on (<see cref="WindowsServiceLifetime"/>). The SCM gives a process only 30 s
/// to connect; nothing slow happens before this point.</item>
/// <item>Check the settings the host would otherwise trip over only while it starts (<see cref="ServerApp.CheckSettings"/>),
/// wait for the database and apply pending migrations (<see cref="ServerApp.MigrateIfEnabledAsync"/>), however long
/// they take.</item>
/// <item>Start the host (Kestrel, the relay, background services); RUNNING once it listens; then run until a stop.</item>
/// </list>
/// Every failure is written to the log as a FAIL line with its exception while the host and its log providers still
/// exist; then the host is disposed (the file log is flushed) before the SCM hears of the end:
/// <list type="bullet">
/// <item>the database did not answer: the process ends without reporting STOPPED, so the SCM runs the service's
/// restart actions (MariaDB may still be starting after a reboot);</item>
/// <item>a migration failed or was refused, or a checked setting is wrong: STOPPED with the service-specific exit code
/// <see cref="ExitStartRefused"/> and no restart (an operator has to act; the FAIL line names the way out);</item>
/// <item>the host failed to start (for example a port in use), or stopped by itself after RUNNING (a background service
/// failed): the process ends without STOPPED, restart actions;</item>
/// <item>the host failed while the SCM stopped it: STOPPED with <see cref="ExitHostFailed"/>, no restart (the operator
/// asked for the stop).</item>
/// </list>
/// </summary>
internal static class ServerStartup
{
    /// <summary>
    /// Process exit codes. <see cref="ExitStartRefused"/> is also the service-specific exit code of the STOPPED report:
    /// Windows shows that number with its own text for it (event 7024), so it is ERROR_INVALID_DATA's 13, "The data is
    /// invalid.", which at least does not point anywhere else; the log's FAIL line says what is wrong.
    /// </summary>
    public const int ExitHostFailed = 1, ExitStartRefused = 13, ExitDatabaseUnavailable = 3;

    private const string Restarts = " The service ends now; its restart actions start it again.";

    public static Task<int> RunAsync(WebApplication app, bool asService)
    {
        var log = app.Services.GetRequiredService<ILoggerFactory>().CreateLogger("DW4Online.Server.Startup");
        WindowsServiceLifetime? service = null;
        if (asService)
        {
            service = app.Services.GetService<IHostLifetime>() as WindowsServiceLifetime;
            if (service is null)
            {
                Console.Error.WriteLine("--service runs only on Windows.");
                return Task.FromResult(ExitHostFailed);
            }
        }
        return RunAsync(service, log,
            migrate: () =>
            {
                ServerApp.CheckSettings(app);
                return ServerApp.MigrateIfEnabledAsync(app);
            },
            // Not app.RunAsync(): that disposes the host, and with it every log provider, before a failure could be
            // logged (the event log provider then threw out of here, the file log dropped the FAIL line).
            run: async () =>
            {
                await app.StartAsync();
                await app.WaitForShutdownAsync();
            },
            dispose: app.DisposeAsync);
    }

    /// <summary>The start with its steps as delegates (the tests drive it with a fake SCM); <paramref name="run"/> does not dispose.</summary>
    internal static async Task<int> RunAsync(WindowsServiceLifetime? service, ILogger log, Func<Task> migrate, Func<Task> run,
        Func<ValueTask> dispose)
    {
        if (service is not null)
        {
            try
            {
                await service.ConnectAsync();
            }
            catch (InvalidOperationException e)
            {
                Console.Error.WriteLine(e.Message);
                await dispose();
                return ExitHostFailed;
            }
            log.LogInformation("Service start: connected to the Service Control Manager; START_PENDING until the host listens");
        }

        try
        {
            await migrate();
        }
        catch (DatabaseUnavailableException e)
        {
            log.LogError(e, "Start failed: {Message}{Next}", e.Message, service is null ? "" : Restarts);
            await dispose();
            return ExitDatabaseUnavailable; // as a service: no STOPPED, so the SCM runs the restart actions
        }
        catch (Exception e)
        {
            log.LogError(e, "Start failed: {Message}{Next}", e.Message,
                service is null ? "" : " The service stops (no restart): fix the cause, then Start-Service.");
            await dispose();
            service?.ReportStartFailed(ExitStartRefused);
            return ExitStartRefused;
        }

        try
        {
            await run();
        }
        catch (Exception e)
        {
            if (service is not null && service.StopRequested)
            {
                log.LogError(e, "The host failed while it stopped: {Message} The service stops (no restart).", e.Message);
                await dispose();
                service.ReportStopFailed(ExitHostFailed); // the operator asked for the stop: no restart actions
                return ExitHostFailed;
            }
            log.LogError(e, "The host failed: {Message}{Next}", e.Message, service is null ? "" : Restarts);
            await dispose();
            return ExitHostFailed; // as a service: no STOPPED (restart actions), as an unhandled exception did before
        }

        if (service is not null && !service.StopRequested)
        {
            // A background service failed and stopped the host (its FAIL / CRIT lines come before this one), or
            // something else ended it from inside. Ending without STOPPED makes the SCM run the restart actions.
            log.LogError("The host stopped although the Service Control Manager did not ask it to (see the FAIL and CRIT lines " +
                         "before this one).{Next}", Restarts);
            await dispose();
            return ExitHostFailed;
        }
        await dispose();
        return 0;
    }
}
