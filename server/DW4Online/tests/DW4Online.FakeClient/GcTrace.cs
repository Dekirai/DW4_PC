using System.Diagnostics;
using System.Diagnostics.Tracing;

namespace DW4Online.FakeClient;

/// <summary>
/// The load gate's GC trace (round fix-019, SEAMLESS.md 19.17.4: the storm cell again with a GC trace, after the first storm
/// run over sockets of round I1a's relay lane had one stall of the relay process of 1.34 s whose cause was not found). In
/// the process it runs in (the relay's child process of <c>loadgate --sockets</c>, or the in-process hub): every garbage
/// collection from the runtime's own events (its generation, reason and kind, and the pause from the execution engine's
/// suspension to its restart, the time no managed thread runs), and the process's stalls: a watcher thread at the highest
/// priority sleeps a millisecond and notes every wake-up later than <see cref="StallMs"/>. A stall that no GC pause
/// overlaps is the machine's (the scheduler, paging, another process), not the runtime's.
/// </summary>
public sealed class GcTrace : EventListener
{
    /// <summary>A watcher wake-up this much later than its 1 ms sleep counts as a stall.</summary>
    public const double StallMs = 20;

    public sealed record Collection(double AtS, int Index, int Generation, string Reason, string Kind, double PauseMs);
    public sealed record Stall(double AtS, double Ms, double GcPauseOverlapMs);

    private readonly object _sync = new();
    private readonly List<Collection> _collections = [];
    private readonly List<(DateTime From, DateTime To)> _pauses = [];
    private readonly List<(long At, double Ms)> _stalls = [];
    private readonly Dictionary<int, (int Generation, string Reason, string Kind)> _started = [];
    private DateTime? _suspendAt;
    private int _suspendIndex;
    private int _suspendReason;
    private DateTime _markUtc = DateTime.UtcNow;
    private long _markTicks = Stopwatch.GetTimestamp();
    private volatile bool _stop;
    private readonly Thread _watcher;

    public GcTrace()
    {
        _watcher = new Thread(Watch) { IsBackground = true, Name = "gc trace stall watcher", Priority = ThreadPriority.Highest };
        _watcher.Start();
    }

    protected override void OnEventSourceCreated(EventSource source)
    {
        // (The runtime's provider, keyword 0x1 = GC, at Informational: GCStart, GCEnd, the EE suspension and restart.)
        if (source.Name == "Microsoft-Windows-DotNETRuntime") EnableEvents(source, EventLevel.Informational, (EventKeywords)0x1);
    }

    protected override void OnEventWritten(EventWrittenEventArgs e)
    {
        lock (_sync)
        {
            switch (e.EventName)
            {
                case "GCStart_V2":
                case "GCStart_V1":
                case "GCStart":
                    _started[Convert.ToInt32(Field(e, "Count"))] = (Convert.ToInt32(Field(e, "Depth")), ReasonName(Convert.ToInt32(Field(e, "Reason"))),
                        KindName(Convert.ToInt32(Field(e, "Type") ?? 0)));
                    break;
                case "GCSuspendEEBegin_V1":
                case "GCSuspendEEBegin":
                    _suspendAt = e.TimeStamp.ToUniversalTime();
                    _suspendIndex = Convert.ToInt32(Field(e, "Count") ?? 0);
                    _suspendReason = Convert.ToInt32(Field(e, "Reason") ?? 0);
                    break;
                case "GCRestartEEEnd_V1":
                case "GCRestartEEEnd":
                    if (_suspendAt is { } from)
                    {
                        var to = e.TimeStamp.ToUniversalTime();
                        _pauses.Add((from, to));
                        // (A suspension without a collection of that number: the runtime stopped the threads for something else,
                        // the JIT's or a debugger's; its reason is the suspension's.)
                        var info = _started.TryGetValue(_suspendIndex, out var s) ? s : (-1, "suspend_" + SuspendName(_suspendReason), "suspension");
                        _started.Remove(_suspendIndex);
                        _collections.Add(new Collection(Math.Round((from - _markUtc).TotalSeconds, 3), _suspendIndex, info.Item1, info.Item2, info.Item3,
                            Math.Round((to - from).TotalMilliseconds, 3)));
                        _suspendAt = null;
                    }
                    break;
            }
        }
    }

    private static object? Field(EventWrittenEventArgs e, string name)
    {
        if (e.PayloadNames is null || e.Payload is null) return null;
        var i = e.PayloadNames.IndexOf(name);
        return i >= 0 ? e.Payload[i] : null;
    }

    private static string ReasonName(int r) => r switch
    {
        0 => "alloc_small", 1 => "induced", 2 => "low_memory", 3 => "empty", 4 => "alloc_large", 5 => "oos_small", 6 => "oos_large",
        7 => "induced_not_forced", 8 => "stress", 9 => "low_memory_induced", 10 => "induced_compacting", 11 => "low_memory_host",
        12 => "pm_full_gc", 13 => "low_memory_host_blocking", _ => "reason_" + r,
    };

    private static string SuspendName(int r) => r switch
    {
        0 => "other", 1 => "gc", 2 => "appdomain_shutdown", 3 => "code_pitching", 4 => "shutdown", 5 => "debugger", 6 => "gc_prep",
        7 => "debugger_sweep", 8 => "profiler", 9 => "rejit", _ => r.ToString(System.Globalization.CultureInfo.InvariantCulture),
    };

    private static string KindName(int t) => t switch { 0 => "blocking", 1 => "background", 2 => "foreground", _ => "kind_" + t };

    private void Watch()
    {
        var last = Stopwatch.GetTimestamp();
        while (!_stop)
        {
            Thread.Sleep(1);
            var now = Stopwatch.GetTimestamp();
            var late = (now - last) * 1000.0 / Stopwatch.Frequency - 1;
            if (late > StallMs)
                lock (_sync)
                    _stalls.Add((last, late));
            last = now;
        }
    }

    /// <summary>The window starts now: the times of the report count from here, the earlier events go.</summary>
    public void Mark()
    {
        lock (_sync)
        {
            _collections.Clear();
            _pauses.Clear();
            _stalls.Clear();
            _markUtc = DateTime.UtcNow;
            _markTicks = Stopwatch.GetTimestamp();
        }
    }

    /// <summary>The window's collections (count, pause total, max and the ten longest) and stalls (each with how much of it a GC
    /// pause covered).</summary>
    public Dictionary<string, object> Report()
    {
        // (The runtime's events reach a listener a moment late: the last ones are waited for.)
        Thread.Sleep(1500);
        lock (_sync)
        {
            var stalls = _stalls.Select(s =>
            {
                var from = _markUtc + TimeSpan.FromSeconds((s.At - _markTicks) / (double)Stopwatch.Frequency);
                var to = from + TimeSpan.FromMilliseconds(s.Ms + 1);
                var overlap = _pauses.Sum(p => Math.Max(0, (Min(to, p.To) - Max(from, p.From)).TotalMilliseconds));
                return new Stall(Math.Round((from - _markUtc).TotalSeconds, 3), Math.Round(s.Ms, 2), Math.Round(overlap, 2));
            }).ToList();
            return new Dictionary<string, object>
            {
                ["collections"] = _collections.Count,
                ["by_generation"] = _collections.GroupBy(c => c.Generation).OrderBy(g => g.Key).ToDictionary(g => "gen" + g.Key, g => g.Count()),
                ["pause_total_ms"] = Math.Round(_collections.Sum(c => c.PauseMs), 2),
                ["pause_max_ms"] = _collections.Count == 0 ? 0 : _collections.Max(c => c.PauseMs),
                ["longest_pauses"] = _collections.OrderByDescending(c => c.PauseMs).Take(10).ToList(),
                ["stall_threshold_ms"] = StallMs,
                ["stalls"] = stalls.OrderByDescending(s => s.Ms).Take(20).ToList(),
                ["stall_count"] = stalls.Count,
                ["stall_max_ms"] = stalls.Count == 0 ? 0 : stalls.Max(s => s.Ms),
                ["server_gc"] = System.Runtime.GCSettings.IsServerGC,
            };
        }
    }

    private static DateTime Min(DateTime a, DateTime b) => a < b ? a : b;
    private static DateTime Max(DateTime a, DateTime b) => a > b ? a : b;

    public override void Dispose()
    {
        _stop = true;
        base.Dispose();
    }
}
