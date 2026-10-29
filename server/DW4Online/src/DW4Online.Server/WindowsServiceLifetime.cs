using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;

namespace DW4Online.Server;

/// <summary>
/// Runs the host under the Windows Service Control Manager (started with "--service", see deploy/DEPLOY.md).
/// A small dependency-free equivalent of Microsoft.Extensions.Hosting.WindowsServices: the SCM dispatcher runs on its
/// own thread; START_PENDING until the host has started, RUNNING after, STOP / SHUTDOWN stop the application, and
/// STOPPED is reported once the host is down. Swap in the official package (UseWindowsService) if preferred.
/// </summary>
[SupportedOSPlatform("windows")]
internal sealed class WindowsServiceLifetime : IHostLifetime, IDisposable
{
    private const int ServiceWin32OwnProcess = 0x10;
    private const int StateStopped = 1, StateStartPending = 2, StateStopPending = 3, StateRunning = 4;
    private const int AcceptStop = 0x1, AcceptShutdown = 0x4;
    private const int ControlStop = 1, ControlInterrogate = 4, ControlShutdown = 5;
    private const int NoError = 0, ErrorCallNotImplemented = 120;

    private readonly IHostApplicationLifetime _lifetime;
    private readonly ILogger<WindowsServiceLifetime> _log;
    private readonly TaskCompletionSource _serviceMainEntered = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly ServiceMainCallback _main;
    private readonly HandlerExCallback _handler;
    private IntPtr _statusHandle;
    private int _checkPoint;
    private Thread? _dispatcher;

    public WindowsServiceLifetime(IHostApplicationLifetime lifetime, ILogger<WindowsServiceLifetime> log)
    {
        _lifetime = lifetime;
        _log = log;
        _main = ServiceMain;     // kept in fields so the delegates outlive the native calls
        _handler = Handler;
    }

    public Task WaitForStartAsync(CancellationToken cancellationToken)
    {
        _lifetime.ApplicationStarted.Register(() => Report(StateRunning, 0));
        _lifetime.ApplicationStopping.Register(() => Report(StateStopPending, 30_000));
        _lifetime.ApplicationStopped.Register(() => Report(StateStopped, 0));
        _dispatcher = new Thread(RunDispatcher) { IsBackground = true, Name = "SCM dispatcher" };
        _dispatcher.Start();
        return _serviceMainEntered.Task.WaitAsync(cancellationToken);
    }

    public Task StopAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    public void Dispose()
    {
    }

    private void RunDispatcher()
    {
        var table = new ServiceTableEntry[]
        {
            new() { ServiceName = Marshal.StringToHGlobalUni(""), ServiceProc = Marshal.GetFunctionPointerForDelegate(_main) },
            new() { ServiceName = IntPtr.Zero, ServiceProc = IntPtr.Zero },
        };
        try
        {
            if (!StartServiceCtrlDispatcherW(table))
            {
                var error = new Win32Exception(Marshal.GetLastWin32Error());
                _serviceMainEntered.TrySetException(new InvalidOperationException(
                    "--service was given, but the process was not started by the Service Control Manager.", error));
            }
        }
        finally
        {
            Marshal.FreeHGlobal(table[0].ServiceName);
        }
    }

    private void ServiceMain(int argc, IntPtr argv)
    {
        _statusHandle = RegisterServiceCtrlHandlerExW("", _handler, IntPtr.Zero);
        if (_statusHandle == IntPtr.Zero)
        {
            _serviceMainEntered.TrySetException(new Win32Exception(Marshal.GetLastWin32Error()));
            return;
        }
        Report(StateStartPending, 60_000);
        _serviceMainEntered.TrySetResult();
    }

    private int Handler(int control, int eventType, IntPtr eventData, IntPtr context)
    {
        switch (control)
        {
            case ControlStop:
            case ControlShutdown:
                _log.LogInformation("Service control {Control}: stopping", control == ControlStop ? "STOP" : "SHUTDOWN");
                Report(StateStopPending, 30_000);
                _lifetime.StopApplication();
                return NoError;
            case ControlInterrogate:
                return NoError;
            default:
                return ErrorCallNotImplemented;
        }
    }

    private void Report(int state, int waitHint)
    {
        if (_statusHandle == IntPtr.Zero) return;
        var pending = state is StateStartPending or StateStopPending;
        var status = new ServiceStatus
        {
            ServiceType = ServiceWin32OwnProcess,
            CurrentState = state,
            ControlsAccepted = state == StateStartPending ? 0 : AcceptStop | AcceptShutdown,
            Win32ExitCode = 0,
            ServiceSpecificExitCode = 0,
            CheckPoint = pending ? Interlocked.Increment(ref _checkPoint) : 0,
            WaitHint = waitHint,
        };
        if (!SetServiceStatus(_statusHandle, ref status))
            _log.LogWarning("SetServiceStatus({State}) failed: {Error}", state, Marshal.GetLastWin32Error());
    }

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    private delegate void ServiceMainCallback(int argc, IntPtr argv);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    private delegate int HandlerExCallback(int control, int eventType, IntPtr eventData, IntPtr context);

    [StructLayout(LayoutKind.Sequential)]
    private struct ServiceTableEntry
    {
        public IntPtr ServiceName;
        public IntPtr ServiceProc;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct ServiceStatus
    {
        public int ServiceType;
        public int CurrentState;
        public int ControlsAccepted;
        public int Win32ExitCode;
        public int ServiceSpecificExitCode;
        public int CheckPoint;
        public int WaitHint;
    }

    [DllImport("advapi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool StartServiceCtrlDispatcherW([In] ServiceTableEntry[] table);

    [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr RegisterServiceCtrlHandlerExW(string serviceName, HandlerExCallback handler, IntPtr context);

    [DllImport("advapi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetServiceStatus(IntPtr handle, ref ServiceStatus status);
}
