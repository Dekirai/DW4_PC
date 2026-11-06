using System.Threading.Channels;
using DW4Online.Domain.Abstractions;
using DW4Online.Relay.Hub;
using Microsoft.Extensions.Logging;

namespace DW4Online.Relay.World;

/// <summary>
/// The world's writes off the hub lock (SEAMLESS.md 19.17.1: "written off the relay's lock through a queue"): the instance history
/// (its rows in order: an instance's start before its members, so every member row knows the history id), the audit rows and the
/// host's remembered visibility. One consumer task; a failed write is logged and dropped (the history is diagnostics, never a
/// reason to hold up play).
/// </summary>
public sealed class WorldHistoryQueue : IAsyncDisposable
{
    private readonly Channel<Func<CancellationToken, Task>> _work = Channel.CreateUnbounded<Func<CancellationToken, Task>>(new UnboundedChannelOptions { SingleReader = true });
    private readonly IInstanceHistoryRepository? _history;
    private readonly IAuditLog? _audit;
    private readonly ILogger? _log;
    private readonly CancellationTokenSource _stop = new();
    private readonly Task _runner;
    private long _pending;

    public WorldHistoryQueue(IInstanceHistoryRepository? history, IAuditLog? audit, ILogger? log)
    {
        _history = history;
        _audit = audit;
        _log = log;
        _runner = Task.Run(RunAsync);
    }

    /// <summary>Writes queued and not done yet.</summary>
    public long Pending => Interlocked.Read(ref _pending);
    public long Failed => Interlocked.Read(ref _failed);
    private long _failed;

    /// <summary>A history event of the hub (called under the hub lock: only queues).</summary>
    public void Enqueue(WorldHistoryEvent e)
    {
        if (_history is not { } h) return;
        Post(async ct =>
        {
            switch (e)
            {
                case HistoryStart s:
                    s.World.HistoryId = await h.StartAsync(s.Row, ct);
                    break;
                case HistoryEnd x when x.World.HistoryId is { } id:
                    await h.EndAsync(id, x.Reason, x.Peak, x.At, ct);
                    break;
                case HistoryJoined j when j.World.HistoryId is { } id:
                    await h.MemberJoinedAsync(id, j.Account, j.Box, j.Role, j.At, ct);
                    break;
                case HistoryLeft l when l.World.HistoryId is { } id:
                    await h.MemberLeftAsync(id, l.Account, l.JoinedAt, l.Reason, l.At, false, ct);
                    break;
            }
        });
    }

    /// <summary>An audit row (19.17.1's world events), queued.</summary>
    public void Audit(string evt, long? account, long? actor, object? detail)
    {
        if (_audit is not { } a) return;
        Post(ct => a.WriteAsync(evt, account, actor, null, detail, DateTime.UtcNow, ct));
    }

    /// <summary>Any other write off the lock (the remembered visibility).</summary>
    public void Post(Func<CancellationToken, Task> work)
    {
        Interlocked.Increment(ref _pending);
        if (!_work.Writer.TryWrite(work)) Interlocked.Decrement(ref _pending);
    }

    /// <summary>Waits until the queue is empty (tests, the service's stop), at most <paramref name="timeout"/>.</summary>
    public async Task<bool> DrainAsync(TimeSpan timeout)
    {
        var until = DateTime.UtcNow + timeout;
        while (Pending > 0)
        {
            if (DateTime.UtcNow >= until) return false;
            await Task.Delay(20);
        }
        return true;
    }

    private async Task RunAsync()
    {
        try
        {
            await foreach (var work in _work.Reader.ReadAllAsync(_stop.Token))
            {
                try
                {
                    await work(_stop.Token);
                }
                catch (OperationCanceledException) when (_stop.IsCancellationRequested)
                {
                    return;
                }
                catch (Exception e)
                {
                    Interlocked.Increment(ref _failed);
                    _log?.LogWarning(e, "A world history or audit write failed");
                }
                finally
                {
                    Interlocked.Decrement(ref _pending);
                }
            }
        }
        catch (OperationCanceledException)
        {
        }
    }

    public async ValueTask DisposeAsync()
    {
        _work.Writer.TryComplete();
        try
        {
            await _runner.WaitAsync(TimeSpan.FromSeconds(5));
        }
        catch (Exception)
        {
        }
        _stop.Cancel();
        _stop.Dispose();
    }
}
