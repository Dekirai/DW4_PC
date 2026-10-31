using System.ComponentModel;

namespace DW4Online.Server;

/// <summary>
/// Runs the host under the Windows Service Control Manager (started with "--service", see deploy/DEPLOY.md).
/// A small dependency-free equivalent of Microsoft.Extensions.Hosting.WindowsServices, with one difference that matters
/// here: <see cref="ConnectAsync"/> enters the SCM dispatcher (on its own thread) BEFORE the database wait and the
/// migrations (<see cref="ServerStartup"/>), so the SCM's 30 s limit for a process to connect no longer covers them.
/// From then on the service reports START_PENDING with a growing checkpoint every few seconds (and a wait hint well
/// above that interval) until the host has started and listens: RUNNING. STOP and SHUTDOWN are accepted only while
/// RUNNING (an interrupted migration is the one thing the start must never risk); they stop the application,
/// STOP_PENDING with growing checkpoints follows, and STOPPED once the host is down. A start that fails for good is
/// reported as STOPPED with a service-specific exit code (<see cref="ReportStartFailed"/>); a start that should be
/// tried again ends the process without STOPPED, which the SCM treats as a failure and answers with the service's
/// restart actions (install-service.ps1: 5 s, 30 s, 60 s).
/// </summary>
internal sealed class WindowsServiceLifetime : IHostLifetime, IDisposable
{
    /// <summary>How often a pending start or stop reports progress (a new checkpoint).</summary>
    public static readonly TimeSpan DefaultProgressInterval = TimeSpan.FromSeconds(5);

    /// <summary>The wait hint of every pending status: the next checkpoint comes long before it runs out.</summary>
    public const int PendingWaitHintMs = 30_000;

    /// <summary>ERROR_SERVICE_SPECIFIC_ERROR: Win32ExitCode of a STOPPED that carries a service-specific exit code.</summary>
    public const int ErrorServiceSpecificError = 1066;

    private const int NoError = 0, ErrorCallNotImplemented = 120;

    private readonly IServiceControlApi _scm;
    private readonly IHostApplicationLifetime _lifetime;
    private readonly ILogger _log;
    private readonly TimeSpan _interval;
    private readonly Lock _gate = new();
    private readonly TaskCompletionSource _connected = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private Thread? _dispatcher;
    private Timer? _progress;
    private int _state;        // 0 until ServiceMain ran
    private int _checkPoint;
    private int _win32ExitCode;
    private int _specificExitCode;
    private bool _started;     // the host reached ApplicationStarted

    public WindowsServiceLifetime(IServiceControlApi scm, IHostApplicationLifetime lifetime, ILogger<WindowsServiceLifetime> log)
        : this(scm, lifetime, log, DefaultProgressInterval)
    {
    }

    internal WindowsServiceLifetime(IServiceControlApi scm, IHostApplicationLifetime lifetime, ILogger log, TimeSpan progressInterval)
    {
        _scm = scm;
        _lifetime = lifetime;
        _log = log;
        _interval = progressInterval;
    }

    /// <summary>
    /// Enters the SCM dispatcher (once) and completes when the service reported START_PENDING; fails with an
    /// <see cref="InvalidOperationException"/> when the process was not started by the SCM.
    /// </summary>
    public Task ConnectAsync(CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            if (_dispatcher is null)
            {
                _dispatcher = new Thread(RunDispatcher) { IsBackground = true, Name = "SCM dispatcher" };
                _dispatcher.Start();
            }
        }
        return _connected.Task.WaitAsync(cancellationToken);
    }

    public Task WaitForStartAsync(CancellationToken cancellationToken)
    {
        _lifetime.ApplicationStarted.Register(OnStarted);
        _lifetime.ApplicationStopping.Register(OnStopping);
        _lifetime.ApplicationStopped.Register(OnStopped);
        return ConnectAsync(cancellationToken);
    }

    public Task StopAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    /// <summary>The start failed for good (no restart helps): STOPPED with a service-specific exit code (event 7024).</summary>
    public void ReportStartFailed(int serviceSpecificExitCode) =>
        Set(ServiceState.Stopped, ErrorServiceSpecificError, serviceSpecificExitCode);

    public void Dispose()
    {
        lock (_gate)
        {
            _progress?.Dispose();
            _progress = null;
        }
    }

    private void RunDispatcher()
    {
        var error = _scm.RunDispatcher(ServiceMain);
        if (error != 0)
        {
            _connected.TrySetException(new InvalidOperationException(
                "--service was given, but the process was not started by the Service Control Manager.", new Win32Exception(error)));
        }
    }

    private void ServiceMain()
    {
        var error = _scm.RegisterHandler(Handler);
        if (error != 0)
        {
            _connected.TrySetException(new InvalidOperationException("RegisterServiceCtrlHandlerEx failed.", new Win32Exception(error)));
            return;
        }
        Set(ServiceState.StartPending);
        _connected.TrySetResult();
    }

    private int Handler(int control)
    {
        switch (control)
        {
            case ServiceControlCode.Stop:
            case ServiceControlCode.Shutdown:
                lock (_gate)
                {
                    if (_state != ServiceState.Running) return NoError; // only RUNNING accepts it; a second request is already under way
                }
                _log.LogInformation("Service control {Control}: stopping", control == ServiceControlCode.Stop ? "STOP" : "SHUTDOWN");
                Set(ServiceState.StopPending);
                _lifetime.StopApplication();
                return NoError;
            case ServiceControlCode.Interrogate:
                return NoError;
            default:
                return ErrorCallNotImplemented;
        }
    }

    private void OnStarted()
    {
        lock (_gate) _started = true;
        Set(ServiceState.Running);
    }

    // A host that fails while it starts never reached RUNNING: ServerStartup decides how that start ends.
    private void OnStopping()
    {
        if (Started()) Set(ServiceState.StopPending);
    }

    private void OnStopped()
    {
        if (Started()) Set(ServiceState.Stopped);
    }

    private bool Started()
    {
        lock (_gate) return _started;
    }

    private void Set(int state, int win32ExitCode = 0, int specificExitCode = 0)
    {
        lock (_gate)
        {
            if (_state == ServiceState.Stopped || _state == state) return; // nothing after STOPPED; pending states tick themselves
            _state = state;
            _checkPoint = 0;
            _win32ExitCode = win32ExitCode;
            _specificExitCode = specificExitCode;
            Report();
            if (state is ServiceState.StartPending or ServiceState.StopPending)
            {
                _progress ??= new Timer(_ => Tick(), null, _interval, _interval);
            }
            else
            {
                _progress?.Dispose();
                _progress = null;
            }
        }
    }

    private void Tick()
    {
        lock (_gate)
        {
            if (_state is ServiceState.StartPending or ServiceState.StopPending) Report();
        }
    }

    // Under _gate: the checkpoints of one pending phase reach the SCM in order.
    private void Report()
    {
        var pending = _state is ServiceState.StartPending or ServiceState.StopPending;
        var status = new ServiceStatus(
            _state,
            _state == ServiceState.Running ? ServiceControlCode.AcceptStop | ServiceControlCode.AcceptShutdown : 0,
            _win32ExitCode,
            _specificExitCode,
            pending ? ++_checkPoint : 0,
            pending ? PendingWaitHintMs : 0);
        var error = _scm.SetStatus(status);
        if (error != 0) _log.LogWarning("SetServiceStatus({State}) failed: {Error}", _state, error);
    }
}
