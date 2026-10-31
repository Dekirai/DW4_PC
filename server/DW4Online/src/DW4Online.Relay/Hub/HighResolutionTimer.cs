using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace DW4Online.Relay.Hub;

/// <summary>
/// Waits until a Stopwatch timestamp with sub-millisecond precision, or until <see cref="Wake"/>. On Windows it uses a
/// high-resolution waitable timer (CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, Windows 10 1803 and later), which needs no
/// change of the system timer resolution; elsewhere (or without it) a sleep to about 1 ms before the target, then a
/// short spin. The sealing thread of the relay waits with it (SEAMLESS.md 6.4: 59.94 Hz on a dedicated thread).
/// </summary>
public sealed class HighResolutionTimer : IDisposable
{
    private readonly AutoResetEvent _wake = new(false);
    private readonly TimerHandle? _timer;

    public HighResolutionTimer()
    {
        if (OperatingSystem.IsWindows())
        {
            var h = CreateWaitableTimerExW(IntPtr.Zero, null, CreateWaitableTimerHighResolution, TimerAllAccess);
            if (h != IntPtr.Zero) _timer = new TimerHandle(h);
        }
    }

    /// <summary>True when the high-resolution waitable timer is in use.</summary>
    public bool HighResolution => _timer is not null;

    public void Wake()
    {
        try
        {
            _wake.Set();
        }
        catch (ObjectDisposedException)
        {
            // Nothing waits any more (RelayHub disposes it after its sealing thread ended).
        }
    }

    /// <summary>Waits until <paramref name="dueTimestamp"/> (Stopwatch ticks) or a wake; returns false on a wake.</summary>
    public bool WaitUntil(long dueTimestamp)
    {
        var remaining = dueTimestamp - Stopwatch.GetTimestamp();
        if (remaining <= 0) return true;
        var ms = remaining * 1000.0 / Stopwatch.Frequency;
        if (_timer is not null)
        {
            var due = -(long)(ms * 10_000.0); // relative, 100 ns units
            if (SetWaitableTimer(_timer.SafeWaitHandle, ref due, 0, IntPtr.Zero, IntPtr.Zero, false))
                return WaitHandle.WaitAny([_timer, _wake]) == 0;
        }
        if (ms > 2.0 && _wake.WaitOne(TimeSpan.FromMilliseconds(ms - 1.5))) return false;
        var spin = new SpinWait();
        while (Stopwatch.GetTimestamp() < dueTimestamp) spin.SpinOnce(-1);
        return true;
    }

    public void Dispose()
    {
        _timer?.Dispose();
        _wake.Dispose();
    }

    private const uint CreateWaitableTimerHighResolution = 0x00000002;
    private const uint TimerAllAccess = 0x1F0003;

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr CreateWaitableTimerExW(IntPtr attributes, string? name, uint flags, uint access);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetWaitableTimer(SafeWaitHandle timer, ref long dueTime, int period, IntPtr completion, IntPtr arg,
        [MarshalAs(UnmanagedType.Bool)] bool resume);

    private sealed class TimerHandle : WaitHandle
    {
        public TimerHandle(IntPtr handle) => SafeWaitHandle = new SafeWaitHandle(handle, ownsHandle: true);
    }
}
