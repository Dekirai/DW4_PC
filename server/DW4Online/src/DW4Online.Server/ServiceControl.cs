using System.Runtime.InteropServices;
using System.Runtime.Versioning;

namespace DW4Online.Server;

/// <summary>SERVICE_STATUS as this service reports it (the service type is always SERVICE_WIN32_OWN_PROCESS).</summary>
internal readonly record struct ServiceStatus(int State, int ControlsAccepted, int Win32ExitCode, int ServiceSpecificExitCode,
    int CheckPoint, int WaitHint);

internal static class ServiceState
{
    public const int Stopped = 1, StartPending = 2, StopPending = 3, Running = 4;
}

internal static class ServiceControlCode
{
    public const int Stop = 1, Interrogate = 4, Shutdown = 5;
    public const int AcceptStop = 0x1, AcceptShutdown = 0x4;
}

/// <summary>
/// The three advapi32 calls a service process makes, behind a seam: <see cref="NativeServiceControlApi"/> in the
/// service, a fake Service Control Manager in the tests. Every method returns 0 or the Win32 error.
/// </summary>
internal interface IServiceControlApi
{
    /// <summary>StartServiceCtrlDispatcher: calls <paramref name="serviceMain"/> on a thread of the SCM and blocks until the service has reported STOPPED.</summary>
    int RunDispatcher(Action serviceMain);

    /// <summary>RegisterServiceCtrlHandlerEx: the handler gets the control code and returns NO_ERROR or ERROR_CALL_NOT_IMPLEMENTED.</summary>
    int RegisterHandler(Func<int, int> handler);

    /// <summary>SetServiceStatus.</summary>
    int SetStatus(ServiceStatus status);
}

/// <summary>The real Service Control Manager (advapi32).</summary>
[SupportedOSPlatform("windows")]
internal sealed class NativeServiceControlApi : IServiceControlApi
{
    private const int ServiceWin32OwnProcess = 0x10;

    // Kept in fields: the native side calls these delegates for as long as the process runs.
    private ServiceMainCallback? _main;
    private HandlerExCallback? _handler;
    private IntPtr _statusHandle;

    public int RunDispatcher(Action serviceMain)
    {
        _main = (_, _) => serviceMain();
        var name = Marshal.StringToHGlobalUni("");
        try
        {
            var table = new ServiceTableEntry[]
            {
                new() { ServiceName = name, ServiceProc = Marshal.GetFunctionPointerForDelegate(_main) },
                new() { ServiceName = IntPtr.Zero, ServiceProc = IntPtr.Zero },
            };
            return StartServiceCtrlDispatcherW(table) ? 0 : Marshal.GetLastWin32Error();
        }
        finally
        {
            Marshal.FreeHGlobal(name);
        }
    }

    public int RegisterHandler(Func<int, int> handler)
    {
        _handler = (control, _, _, _) => handler(control);
        _statusHandle = RegisterServiceCtrlHandlerExW("", _handler, IntPtr.Zero);
        return _statusHandle == IntPtr.Zero ? Marshal.GetLastWin32Error() : 0;
    }

    public int SetStatus(ServiceStatus status)
    {
        if (_statusHandle == IntPtr.Zero) return 6; // ERROR_INVALID_HANDLE: not registered yet
        var native = new NativeStatus
        {
            ServiceType = ServiceWin32OwnProcess,
            CurrentState = status.State,
            ControlsAccepted = status.ControlsAccepted,
            Win32ExitCode = status.Win32ExitCode,
            ServiceSpecificExitCode = status.ServiceSpecificExitCode,
            CheckPoint = status.CheckPoint,
            WaitHint = status.WaitHint,
        };
        return SetServiceStatus(_statusHandle, ref native) ? 0 : Marshal.GetLastWin32Error();
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
    private struct NativeStatus
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
    private static extern bool SetServiceStatus(IntPtr handle, ref NativeStatus status);
}
