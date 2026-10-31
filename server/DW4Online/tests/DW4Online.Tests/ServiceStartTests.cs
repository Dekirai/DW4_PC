using DW4Online.Data.Migrations;
using DW4Online.Server;
using Microsoft.Extensions.Hosting.Internal;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DW4Online.Tests;

/// <summary>
/// The service start (ServerStartup with WindowsServiceLifetime) against a fake Service Control Manager: the process
/// connects first and reports START_PENDING with growing checkpoints for as long as the database wait and the
/// migrations take, RUNNING only once the host listens, and every failure ends in a FAIL log line and the SCM status
/// that gives the right follow-up (restart actions or none).
/// </summary>
public class ServiceStartTests
{
    private static readonly TimeSpan Tick = TimeSpan.FromMilliseconds(20);
    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(20);

    [Fact]
    public async Task A_slow_migration_runs_in_START_PENDING_with_growing_checkpoints_then_RUNNING_and_a_clean_stop()
    {
        using var scm = new FakeScm();
        var app = new ApplicationLifetime(NullLogger<ApplicationLifetime>.Instance);
        using var service = new WindowsServiceLifetime(scm, app, NullLogger.Instance, Tick);
        var log = new ListLogger();
        var steps = new List<string>();
        var start = Task.Run(() => ServerStartup.RunAsync(service, log,
            migrate: async () =>
            {
                // The SCM heard of the process before the migration began ...
                Assert.Equal(ServiceState.StartPending, scm.Statuses()[0].State);
                Assert.Equal(0, scm.Send(ServiceControlCode.Stop));                  // not accepted yet: ignored
                Assert.Equal(0, scm.Send(ServiceControlCode.Interrogate));
                Assert.Equal(120, scm.Send(0x10));                                    // PARAMCHANGE: not implemented
                // ... and keeps hearing of it while the migration takes many progress intervals.
                await scm.WaitUntil(s => s.Count(x => x.State == ServiceState.StartPending) >= 8);
                Assert.False(app.ApplicationStopping.IsCancellationRequested);
                steps.Add("migrated");
            },
            run: async () =>
            {
                steps.Add("host");
                await service.WaitForStartAsync(CancellationToken.None);
                app.NotifyStarted(); // the host listens
                var stopping = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                app.ApplicationStopping.Register(() => stopping.TrySetResult());
                await stopping.Task.WaitAsync(Timeout);
                await scm.WaitUntil(s => s.Count(x => x.State == ServiceState.StopPending) >= 3); // a stop that takes a while
                app.NotifyStopped();
            },
            dispose: () => ValueTask.CompletedTask));

        await scm.WaitUntil(s => s.Any(x => x.State == ServiceState.Running));
        Assert.Equal(0, scm.Send(ServiceControlCode.Stop));
        Assert.Equal(0, await start.WaitAsync(Timeout));
        Assert.Equal(["migrated", "host"], steps);

        var statuses = scm.Statuses();
        var startPending = statuses.TakeWhile(s => s.State == ServiceState.StartPending).ToList();
        Assert.True(startPending.Count >= 8);
        Assert.Equal(Enumerable.Range(1, startPending.Count), startPending.Select(s => s.CheckPoint)); // growing by one
        Assert.All(startPending, s =>
        {
            Assert.Equal(0, s.ControlsAccepted);                                     // no STOP while migrating
            Assert.Equal(WindowsServiceLifetime.PendingWaitHintMs, s.WaitHint);
        });
        Assert.True(TimeSpan.FromMilliseconds(WindowsServiceLifetime.PendingWaitHintMs) > 5 * WindowsServiceLifetime.DefaultProgressInterval);

        var running = statuses[startPending.Count];
        Assert.Equal(new ServiceStatus(ServiceState.Running, ServiceControlCode.AcceptStop | ServiceControlCode.AcceptShutdown, 0, 0, 0, 0), running);

        var stopPending = statuses.Skip(startPending.Count + 1).TakeWhile(s => s.State == ServiceState.StopPending).ToList();
        Assert.True(stopPending.Count >= 3);
        Assert.Equal(Enumerable.Range(1, stopPending.Count), stopPending.Select(s => s.CheckPoint));
        Assert.All(stopPending, s => Assert.Equal(0, s.ControlsAccepted));

        Assert.Equal(new ServiceStatus(ServiceState.Stopped, 0, 0, 0, 0, 0), statuses[^1]);
        Assert.Equal(startPending.Count + 1 + stopPending.Count + 1, statuses.Count);
        Assert.DoesNotContain(log.Entries, e => e.Level >= LogLevel.Error);
    }

    [Fact]
    public async Task A_failed_migration_writes_a_FAIL_line_then_reports_STOPPED_with_a_service_specific_error()
    {
        using var scm = new FakeScm();
        var app = new ApplicationLifetime(NullLogger<ApplicationLifetime>.Instance);
        using var service = new WindowsServiceLifetime(scm, app, NullLogger.Instance, Tick);
        var log = new ListLogger();
        var statusesAtDispose = -1;
        var error = new MigrationException("Migration 0003 (rooms) failed after 0.20 s: Duplicate column name 'randomizer'. Way out: ...",
            new InvalidOperationException("inner"));
        var code = await ServerStartup.RunAsync(service, log,
            migrate: () => Task.FromException(error),
            run: () => throw new InvalidOperationException("the host must not start"),
            dispose: () =>
            {
                statusesAtDispose = scm.Statuses().Count; // the log is flushed before the SCM hears of the end
                return ValueTask.CompletedTask;
            });

        Assert.Equal(ServerStartup.ExitStartRefused, code);
        var statuses = scm.Statuses();
        Assert.All(statuses.Take(statuses.Count - 1), s => Assert.Equal(ServiceState.StartPending, s.State));
        Assert.Equal(new ServiceStatus(ServiceState.Stopped, 0, WindowsServiceLifetime.ErrorServiceSpecificError,
            ServerStartup.ExitStartRefused, 0, 0), statuses[^1]);
        Assert.Equal(statuses.Count - 1, statusesAtDispose);
        var fail = Assert.Single(log.Entries, e => e.Level == LogLevel.Error);
        Assert.Same(error, fail.Exception);
        Assert.Contains("Start failed: Migration 0003 (rooms) failed", fail.Message);
        Assert.Contains("no restart", fail.Message);
    }

    [Fact]
    public async Task A_database_that_never_answers_ends_the_process_without_STOPPED_so_the_restart_actions_run()
    {
        using var scm = new FakeScm();
        var app = new ApplicationLifetime(NullLogger<ApplicationLifetime>.Instance);
        using var service = new WindowsServiceLifetime(scm, app, NullLogger.Instance, Tick);
        var log = new ListLogger();
        var disposed = false;
        var code = await ServerStartup.RunAsync(service, log,
            migrate: async () =>
            {
                await scm.WaitUntil(s => s.Count >= 4); // waiting for the database, still START_PENDING
                throw new DatabaseUnavailableException("The database did not answer within 60 s (connect failed).", new TimeoutException());
            },
            run: () => throw new InvalidOperationException("the host must not start"),
            dispose: () =>
            {
                disposed = true;
                return ValueTask.CompletedTask;
            });

        Assert.Equal(ServerStartup.ExitDatabaseUnavailable, code);
        Assert.True(disposed);
        Assert.All(scm.Statuses(), s => Assert.Equal(ServiceState.StartPending, s.State)); // no STOPPED: the SCM sees a failure
        var fail = Assert.Single(log.Entries, e => e.Level == LogLevel.Error);
        Assert.IsType<DatabaseUnavailableException>(fail.Exception);
        Assert.Contains("restart actions", fail.Message);
    }

    [Fact]
    public async Task A_host_that_fails_to_start_ends_the_process_without_STOPPED()
    {
        using var scm = new FakeScm();
        var app = new ApplicationLifetime(NullLogger<ApplicationLifetime>.Instance);
        using var service = new WindowsServiceLifetime(scm, app, NullLogger.Instance, Tick);
        var log = new ListLogger();
        var entriesAtDispose = -1;
        var code = await ServerStartup.RunAsync(service, log,
            migrate: () => Task.CompletedTask,
            run: async () =>
            {
                await service.WaitForStartAsync(CancellationToken.None);
                app.StopApplication(); // what a host does when a listener cannot bind
                app.NotifyStopped();
                throw new IOException("Failed to bind to address https://0.0.0.0:8443: address already in use.");
            },
            dispose: () =>
            {
                entriesAtDispose = log.Entries.Count; // the host (and its log providers) goes only after the FAIL line
                return ValueTask.CompletedTask;
            });

        Assert.Equal(ServerStartup.ExitHostFailed, code);
        Assert.All(scm.Statuses(), s => Assert.Equal(ServiceState.StartPending, s.State));
        var fail = Assert.Single(log.Entries, e => e.Level == LogLevel.Error);
        Assert.IsType<IOException>(fail.Exception);
        Assert.Contains("8443", fail.Message);
        Assert.Equal(log.Entries.Count, entriesAtDispose);
    }

    /// <summary>
    /// A host that stops after RUNNING although the SCM did not ask it to (a background service failed and stopped the
    /// host): the process ends without STOP_PENDING or STOPPED, so the SCM sees a failure and runs the restart actions.
    /// </summary>
    [Fact]
    public async Task A_host_that_stops_by_itself_after_RUNNING_ends_the_process_without_STOPPED()
    {
        using var scm = new FakeScm();
        var app = new ApplicationLifetime(NullLogger<ApplicationLifetime>.Instance);
        using var service = new WindowsServiceLifetime(scm, app, NullLogger.Instance, Tick);
        var log = new ListLogger();
        var disposed = false;
        var code = await ServerStartup.RunAsync(service, log,
            migrate: () => Task.CompletedTask,
            run: async () =>
            {
                await service.WaitForStartAsync(CancellationToken.None);
                app.NotifyStarted();
                await scm.WaitUntil(s => s.Any(x => x.State == ServiceState.Running));
                app.StopApplication(); // BackgroundServiceExceptionBehavior.StopHost
                await Task.Delay(Tick * 5); // a stop that takes a few progress intervals
                app.NotifyStopped();
            },
            dispose: () =>
            {
                disposed = true;
                return ValueTask.CompletedTask;
            });

        Assert.Equal(ServerStartup.ExitHostFailed, code);
        Assert.True(disposed);
        var statuses = scm.Statuses();
        Assert.Equal(ServiceState.Running, statuses[^1].State); // nothing after RUNNING: the SCM sees the process end
        Assert.DoesNotContain(statuses, s => s.State is ServiceState.StopPending or ServiceState.Stopped);
        var fail = Assert.Single(log.Entries, e => e.Level == LogLevel.Error);
        Assert.Contains("stopped although the Service Control Manager did not ask it to", fail.Message);
        Assert.Contains("restart actions", fail.Message);
    }

    /// <summary>A stop the SCM asked for ends in STOPPED even when the host's stop fails: no restart against the operator's will.</summary>
    [Fact]
    public async Task A_host_that_fails_while_the_SCM_stops_it_still_reports_STOPPED()
    {
        using var scm = new FakeScm();
        var app = new ApplicationLifetime(NullLogger<ApplicationLifetime>.Instance);
        using var service = new WindowsServiceLifetime(scm, app, NullLogger.Instance, Tick);
        var log = new ListLogger();
        var start = Task.Run(() => ServerStartup.RunAsync(service, log,
            migrate: () => Task.CompletedTask,
            run: async () =>
            {
                await service.WaitForStartAsync(CancellationToken.None);
                app.NotifyStarted();
                var stopping = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                app.ApplicationStopping.Register(() => stopping.TrySetResult());
                await stopping.Task.WaitAsync(Timeout);
                throw new AggregateException("One or more hosted services failed to stop.", new InvalidOperationException("stuck"));
            },
            dispose: () => ValueTask.CompletedTask));

        await scm.WaitUntil(s => s.Any(x => x.State == ServiceState.Running));
        Assert.Equal(0, scm.Send(ServiceControlCode.Stop));
        Assert.Equal(ServerStartup.ExitHostFailed, await start.WaitAsync(Timeout));
        var statuses = scm.Statuses();
        Assert.Equal(new ServiceStatus(ServiceState.Stopped, 0, WindowsServiceLifetime.ErrorServiceSpecificError,
            ServerStartup.ExitHostFailed, 0, 0), statuses[^1]);
        var fail = Assert.Single(log.Entries, e => e.Level == LogLevel.Error);
        Assert.Contains("The host failed while it stopped", fail.Message);
        Assert.DoesNotContain("restart actions", fail.Message);
    }

    [Fact]
    public async Task Outside_the_Service_Control_Manager_the_service_mode_stops_before_the_database()
    {
        using var scm = new FakeScm { DispatcherError = 1063 }; // ERROR_FAILED_SERVICE_CONTROLLER_CONNECT
        var app = new ApplicationLifetime(NullLogger<ApplicationLifetime>.Instance);
        using var service = new WindowsServiceLifetime(scm, app, NullLogger.Instance, Tick);
        var migrated = false;
        var code = await ServerStartup.RunAsync(service, new ListLogger(),
            migrate: () =>
            {
                migrated = true;
                return Task.CompletedTask;
            },
            run: () => Task.CompletedTask,
            dispose: () => ValueTask.CompletedTask);

        Assert.Equal(ServerStartup.ExitHostFailed, code);
        Assert.False(migrated);
        Assert.Empty(scm.Statuses());
    }

    [Fact]
    public async Task In_the_console_the_same_failures_give_exit_codes_and_FAIL_lines()
    {
        var log = new ListLogger();
        Assert.Equal(ServerStartup.ExitStartRefused, await ServerStartup.RunAsync(null, log,
            () => Task.FromException(new MigrationException("refused")), () => Task.CompletedTask, () => ValueTask.CompletedTask));
        Assert.Equal(ServerStartup.ExitDatabaseUnavailable, await ServerStartup.RunAsync(null, log,
            () => Task.FromException(new DatabaseUnavailableException("no answer", new TimeoutException())), () => Task.CompletedTask,
            () => ValueTask.CompletedTask));
        Assert.Equal(0, await ServerStartup.RunAsync(null, log, () => Task.CompletedTask, () => Task.CompletedTask, () => ValueTask.CompletedTask));
        Assert.Equal(2, log.Entries.Count(e => e.Level == LogLevel.Error));
    }
}
