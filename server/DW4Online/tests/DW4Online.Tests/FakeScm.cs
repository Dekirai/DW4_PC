using DW4Online.Server;
using Microsoft.Extensions.Logging;

namespace DW4Online.Tests;

/// <summary>
/// A Service Control Manager in memory: records every status, delivers controls to the handler. Shared with the
/// integration tests (linked there), which run a real host under it.
/// </summary>
internal sealed class FakeScm : IServiceControlApi, IDisposable
{
    public static readonly TimeSpan Timeout = TimeSpan.FromSeconds(20);

    private readonly Lock _gate = new();
    private readonly List<ServiceStatus> _statuses = [];
    private readonly ManualResetEventSlim _stopped = new();
    private Func<int, int>? _handler;

    public int DispatcherError { get; init; }

    public int RunDispatcher(Action serviceMain)
    {
        if (DispatcherError != 0) return DispatcherError;
        serviceMain();
        _stopped.Wait(); // StartServiceCtrlDispatcher returns once the service reported STOPPED
        return 0;
    }

    public int RegisterHandler(Func<int, int> handler)
    {
        _handler = handler;
        return 0;
    }

    public int SetStatus(ServiceStatus status)
    {
        lock (_gate) _statuses.Add(status); // the tests check that nothing follows STOPPED
        if (status.State == ServiceState.Stopped) _stopped.Set();
        return 0;
    }

    public int Send(int control) => _handler!(control);

    public List<ServiceStatus> Statuses()
    {
        lock (_gate) return [.. _statuses];
    }

    public async Task WaitUntil(Func<List<ServiceStatus>, bool> condition)
    {
        var until = DateTime.UtcNow + Timeout;
        while (!condition(Statuses()))
        {
            if (DateTime.UtcNow > until) throw new TimeoutException("The fake SCM did not see the expected statuses.");
            await Task.Delay(5);
        }
    }

    public void Dispose() => _stopped.Set(); // releases a dispatcher thread whose service never stopped
}

/// <summary>A logger that keeps every entry.</summary>
internal sealed class ListLogger : ILogger
{
    private readonly Lock _gate = new();
    private readonly List<(LogLevel Level, string Message, Exception? Exception)> _entries = [];

    public List<(LogLevel Level, string Message, Exception? Exception)> Entries
    {
        get
        {
            lock (_gate) return [.. _entries];
        }
    }

    public IDisposable? BeginScope<TState>(TState state) where TState : notnull => null;

    public bool IsEnabled(LogLevel logLevel) => true;

    public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception, Func<TState, Exception?, string> formatter)
    {
        lock (_gate) _entries.Add((logLevel, formatter(state, exception), exception));
    }
}
