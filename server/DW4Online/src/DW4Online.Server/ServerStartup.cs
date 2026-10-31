namespace DW4Online.Server;

/// <summary>The database did not answer within Database:StartupWaitSeconds (MariaDB not started, or still starting).</summary>
internal sealed class DatabaseUnavailableException(string message, Exception inner) : Exception(message, inner);

/// <summary>
/// The start of the server process (console or Windows service), in this order:
/// <list type="number">
/// <item>As a service: enter the Service Control Manager's dispatcher at once and report START_PENDING, with a new
/// checkpoint every few seconds from then on (<see cref="WindowsServiceLifetime"/>). The SCM gives a process only 30 s
/// to connect; nothing slow happens before this point.</item>
/// <item>Wait for the database and apply pending migrations (<see cref="ServerApp.MigrateIfEnabledAsync"/>), however
/// long they take.</item>
/// <item>Start the host (Kestrel, the relay, background services); RUNNING once it listens.</item>
/// </list>
/// Every failure is written to the log as a FAIL line with its exception before the process ends, and the log is
/// flushed (the host is disposed) before the SCM hears of it:
/// <list type="bullet">
/// <item>the database did not answer: the process ends without reporting STOPPED, so the SCM runs the service's
/// restart actions (MariaDB may still be starting after a reboot);</item>
/// <item>a migration failed or was refused, or the settings are wrong: STOPPED with the service-specific exit code
/// <see cref="ExitStartRefused"/> and no restart (an operator has to act; the FAIL line names the way out);</item>
/// <item>the host failed to start (for example a port in use): the process ends without STOPPED, restart actions.</item>
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
        return RunAsync(service, log, () => ServerApp.MigrateIfEnabledAsync(app), () => app.RunAsync(), app.DisposeAsync);
    }

    /// <summary>The start with its steps as delegates (the tests drive it with a fake SCM).</summary>
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
            log.LogError(e, "Start failed: {Message}{Next}", e.Message,
                service is null ? "" : " The service ends now; its restart actions start it again.");
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
            log.LogError(e, "The host failed: {Message}{Next}", e.Message,
                service is null ? "" : " The service ends now; its restart actions start it again.");
            await dispose();
            return ExitHostFailed; // as a service: no STOPPED (restart actions), as an unhandled exception did before
        }
        return 0;
    }
}
