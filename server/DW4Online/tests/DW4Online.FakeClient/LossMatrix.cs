using System.Diagnostics;
using System.Globalization;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text.Json;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using Microsoft.Extensions.Logging.Abstractions;

namespace DW4Online.FakeClient;

/// <summary>One cell of the loss and jitter matrix (SEAMLESS.md 6.4).</summary>
public sealed record MatrixCell(double OneWayMs, double JitterMs, double Loss, bool OnlyFirstImpaired = false)
{
    public string Name => string.Create(CultureInfo.InvariantCulture,
        $"ow{OneWayMs:0}_j{JitterMs:0}_l{Loss * 100:0.0}{(OnlyFirstImpaired ? "_one" : "")}");
}

public sealed record MatrixMember(int Slot, bool Impaired, double FillRate, long Filled, long Sealed, long Late, long Hitches, double HitchesPerMinute,
    double StallMsPerMinute, int FinalDelay, int MaxDelay, double RttMs);

public sealed record MatrixResult(MatrixCell Cell, double MeasuredSeconds, IReadOnlyList<MatrixMember> Members, long LostSegments, long Segments,
    bool Sealed, string? Error)
{
    public Dictionary<string, long> Counters { get; init; } = [];
}

/// <summary>
/// The loss and jitter matrix for server-paced sealing (SEAMLESS.md 6.4): per cell an in-process DW4R2 hub on loopback, one
/// impairment proxy per client (the same one-way time, jitter and loss in both directions), three fake clients that enter
/// one stage together (a co-arrival, sealed from tick 0), then a warm-up (the delay rule settles) and a measured window:
/// the server's fill rate per member (pads it had to fill), late pads, the final input delay, and each client's hitches
/// (ticks that arrived after their playout time) and stall time.
/// </summary>
public static class LossMatrix
{
    /// <summary>The delay bound of the matrix runs (the server's default is 10).</summary>
    public static int MaxDelay { get; set; } = 20;

    public static IEnumerable<MatrixCell> DefaultCells()
    {
        // The machine's own noise: the proxy forwarding at once.
        yield return new MatrixCell(0, 0, 0);
        foreach (var ow in new[] { 10.0, 30.0, 60.0 })
            foreach (var j in new[] { 0.0, 10.0, 30.0 })
                foreach (var l in new[] { 0.0, 0.001, 0.005, 0.01, 0.02 })
                    yield return new MatrixCell(ow, j, l);
        // Isolation: only the first member's link is lossy; the others' hitches and fills stay those of a clean link.
        yield return new MatrixCell(30, 10, 0.02, OnlyFirstImpaired: true);
        yield return new MatrixCell(60, 30, 0.02, OnlyFirstImpaired: true);
    }

    public static async Task<IReadOnlyList<MatrixResult>> RunAsync(IEnumerable<MatrixCell> cells, PortPool ports, int clients, TimeSpan warmup, TimeSpan measure,
        int parallel, Action<string> log, CancellationToken ct)
    {
        var results = new List<MatrixResult>();
        var gate = new SemaphoreSlim(parallel);
        var tasks = cells.Select(async cell =>
        {
            await gate.WaitAsync(ct);
            try
            {
                var r = await RunCellAsync(cell, ports, clients, warmup, measure, ct);
                lock (results) results.Add(r);
                log(JsonSerializer.Serialize(r));
                return r;
            }
            finally
            {
                gate.Release();
            }
        }).ToList();
        await Task.WhenAll(tasks);
        return results.OrderBy(r => r.Cell.OnlyFirstImpaired).ThenBy(r => r.Cell.OneWayMs).ThenBy(r => r.Cell.JitterMs).ThenBy(r => r.Cell.Loss).ToList();
    }

    public static async Task<MatrixResult> RunCellAsync(MatrixCell cell, PortPool ports, int clients, TimeSpan warmup, TimeSpan measure, CancellationToken ct)
    {
        var hubPort = ports.Take();
        var proxyPorts = Enumerable.Range(0, clients).Select(_ => ports.Take()).ToList();
        var key = new byte[32];
        Random.Shared.NextBytes(key);
        var tickets = new RelayTickets(key, TimeSpan.FromSeconds(60), TimeProvider.System);
        // The delay may follow the network beyond the default bound (Sealing:MaxDelayTicks 10) and nobody is taken out for
        // lag, so every cell measures the transport; the final d says what the delay rule needed.
        var relay = new RelayOptions { Enabled = true, ListenAddress = "127.0.0.1", Port = hubPort, IdleTimeoutSeconds = 120, LagLeaveSeconds = 600 };
        var hub = new RelayHub(relay, new SealingOptions { MaxDelayTicks = MaxDelay }, new InstanceOptions(), tickets, NullLoggerFactory.Instance);
        const string room = "MATRIX";
        hub.Admission = a => ValueTask.FromResult<HubAdmitted?>(new HubAdmitted((int)a.AccountId, "m" + a.AccountId, a.AccountId == 0,
            new RoomInfo(room, 43, 1, 0x1234, 10)));
        using var stop = CancellationTokenSource.CreateLinkedTokenSource(ct);
        var proxies = new List<ImpairmentProxy>();
        try
        {
            await hub.StartAsync(stop.Token);
            await hub.Started.WaitAsync(TimeSpan.FromSeconds(10), ct);
            for (var k = 0; k < clients; ++k)
            {
                var imp = !cell.OnlyFirstImpaired || k == 0 ? new Impairment(cell.OneWayMs, cell.JitterMs, cell.Loss) : new Impairment(cell.OneWayMs, cell.JitterMs, 0);
                proxies.Add(new ImpairmentProxy(new IPEndPoint(IPAddress.Loopback, proxyPorts[k]), new IPEndPoint(IPAddress.Loopback, hubPort), imp, imp, 1000 + k));
            }
            var total = warmup + measure + TimeSpan.FromSeconds(4);
            var fakes = Enumerable.Range(0, clients).Select(k => new FakeClient(new FakeClientOptions
            {
                Port = proxyPorts[k],
                Ticket = _ => Task.FromResult(tickets.Issue(room, k, RelayRoles.Member)),
                SettingsHash = 0x1234,
                Name = "m" + k,
                Seed = k + 1,
                UniquePads = true,
                Duration = total,
                Script = [new ScriptAction(0.5, "exit", "stag0101", 0)],
            })).ToList();
            var runs = fakes.Select(f => f.RunAsync(stop.Token)).ToList();

            // Wait for the sealed instance, then take the counters at the end of the warm-up and of the window.
            var started = Stopwatch.GetTimestamp();
            HubInstance? inst = null;
            while (inst is null && Stopwatch.GetElapsedTime(started) < TimeSpan.FromSeconds(10 + cell.OneWayMs / 5))
            {
                await Task.Delay(100, ct);
                if (hub.Room(room) is { } r)
                    lock (r.Sync) inst = r.Instances.FirstOrDefault(i => i.Mode == InstanceMode.Sealed && i.Sealing && i.Count == clients);
            }
            if (inst is null)
            {
                await Task.WhenAll(runs);
                return new MatrixResult(cell, 0, [], proxies.Sum(p => p.LostSegments), proxies.Sum(p => p.Segments), false, "no sealed instance of all members");
            }
            await Task.Delay(warmup, ct);
            var before = Snapshot(hub.Room(room)!, inst);
            var hitchesBefore = fakes.Select(f => (f.Stats.Hitches, f.Stats.StallMs)).ToList();
            var t0 = Stopwatch.GetTimestamp();
            await Task.Delay(measure, ct);
            var after = Snapshot(hub.Room(room)!, inst);
            bool changed;
            lock (hub.Room(room)!.Sync) changed = inst.Ended || inst.Mode != InstanceMode.Sealed || inst.Count != clients;
            if (changed || after.Any(kv => before.TryGetValue(kv.Key, out var b0) && kv.Value.Sealed < b0.Sealed))
            {
                await Task.WhenAll(runs);
                return new MatrixResult(cell, 0, [], proxies.Sum(p => p.LostSegments), proxies.Sum(p => p.Segments), false, "the instance changed during the window");
            }
            var hitchesAfter = fakes.Select(f => (f.Stats.Hitches, f.Stats.StallMs)).ToList();
            var seconds = Stopwatch.GetElapsedTime(t0).TotalSeconds;
            var members = new List<MatrixMember>();
            for (var k = 0; k < clients; ++k)
            {
                var slot = fakes[k].Slot;
                var b = before.GetValueOrDefault(slot) ?? new SlotCounters(0, 0, 0, 0, 0);
                var a = after.GetValueOrDefault(slot) ?? new SlotCounters(0, 0, 0, 0, 0);
                var sealedN = a.Sealed - b.Sealed;
                var filled = a.Filled - b.Filled;
                var hitches = hitchesAfter[k].Hitches - hitchesBefore[k].Hitches;
                var stall = hitchesAfter[k].StallMs - hitchesBefore[k].StallMs;
                members.Add(new MatrixMember(slot, !cell.OnlyFirstImpaired || k == 0, sealedN == 0 ? 0 : (double)filled / sealedN, filled, sealedN, a.Late - b.Late,
                    hitches, hitches / seconds * 60, stall / seconds * 60, a.Delay, fakes[k].Stats.Delays.DefaultIfEmpty(a.Delay).Max(), a.Rtt));
            }
            await Task.WhenAll(runs);
            Dictionary<string, long> counters;
            lock (hub.Room(room)!.Sync) counters = new Dictionary<string, long>(hub.Room(room)!.Counters);
            foreach (var f in fakes)
                foreach (var (k, v) in f.Stats.Frames) counters["client_" + k] = counters.GetValueOrDefault("client_" + k) + v;
            counters["client_pads_sent"] = fakes.Sum(f => f.Stats.OwnPadsSent);
            counters["client_sealed_executed"] = fakes.Sum(f => f.Stats.SealedExecuted);
            return new MatrixResult(cell, seconds, members, proxies.Sum(p => p.LostSegments), proxies.Sum(p => p.Segments), true, null) { Counters = counters };
        }
        catch (Exception e) when (e is not OperationCanceledException || !ct.IsCancellationRequested)
        {
            return new MatrixResult(cell, 0, [], 0, 0, false, e.GetType().Name + ": " + e.Message);
        }
        finally
        {
            foreach (var p in proxies) await p.DisposeAsync();
            await hub.StopAsync(CancellationToken.None);
            hub.Dispose();
            ports.Return(hubPort);
            foreach (var p in proxyPorts) ports.Return(p);
        }
    }

    private sealed record SlotCounters(long Sealed, long Filled, long Late, int Delay, double Rtt);

    private static Dictionary<int, SlotCounters> Snapshot(RoomHub room, HubInstance inst)
    {
        var d = new Dictionary<int, SlotCounters>();
        lock (room.Sync)
        {
            for (var s = 0; s < Wire.MaxSlots; ++s)
            {
                if (inst.Streams[s] is not { } st) continue;
                var rtt = inst.Slots[s]?.Rtt.Median ?? 0;
                d[s] = new SlotCounters(st.Sealed, st.Filled, st.Late, st.Delay.Current, rtt);
            }
        }
        return d;
    }

    /// <summary>Writes the matrix as a Markdown table (for SEAMLESS.md).</summary>
    public static string Markdown(IReadOnlyList<MatrixResult> results)
    {
        var sb = new System.Text.StringBuilder();
        sb.AppendLine("| one-way ms | jitter ms | loss % | fill % (each member) | late pads/min | final d | hitches/min | stall ms/min | lost segments |");
        sb.AppendLine("| --- | --- | --- | --- | --- | --- | --- | --- | --- |");
        foreach (var r in results)
        {
            if (!r.Sealed)
            {
                sb.AppendLine(string.Create(CultureInfo.InvariantCulture, $"| {r.Cell.OneWayMs:0} | {r.Cell.JitterMs:0} | {r.Cell.Loss * 100:0.0} | {r.Error} | | | | | |"));
                continue;
            }
            string Join(Func<MatrixMember, string> f) => string.Join(" / ", r.Members.Select(f));
            var minutes = r.MeasuredSeconds / 60;
            sb.AppendLine(string.Create(CultureInfo.InvariantCulture,
                $"| {r.Cell.OneWayMs:0} | {r.Cell.JitterMs:0} | {r.Cell.Loss * 100:0.0}{(r.Cell.OnlyFirstImpaired ? " (first member only)" : "")} | " +
                $"{Join(m => (m.FillRate * 100).ToString("0.00", CultureInfo.InvariantCulture))} | {Join(m => (m.Late / minutes).ToString("0", CultureInfo.InvariantCulture))} | " +
                $"{Join(m => m.FinalDelay.ToString(CultureInfo.InvariantCulture))} | {Join(m => m.HitchesPerMinute.ToString("0.0", CultureInfo.InvariantCulture))} | " +
                $"{Join(m => m.StallMsPerMinute.ToString("0", CultureInfo.InvariantCulture))} | {r.LostSegments} |"));
        }
        return sb.ToString();
    }
}

/// <summary>
/// Ports of a fixed range (the lane's 53100-53199) that are free now: taken from the system's active listeners and
/// connections once, then handed out and returned; a bind that still fails moves on to the next port.
/// </summary>
public sealed class PortPool
{
    private readonly Queue<int> _free = new();
    private readonly Lock _gate = new();

    public PortPool(int first, int last)
    {
        var props = IPGlobalProperties.GetIPGlobalProperties();
        var used = props.GetActiveTcpListeners().Select(e => e.Port).Concat(props.GetActiveTcpConnections().Select(c => c.LocalEndPoint.Port)).ToHashSet();
        for (var p = first; p <= last; ++p)
            if (!used.Contains(p)) _free.Enqueue(p);
    }

    public int Count
    {
        get
        {
            lock (_gate) return _free.Count;
        }
    }

    public int Take()
    {
        lock (_gate)
        {
            while (_free.Count > 0)
            {
                var p = _free.Dequeue();
                try
                {
                    var probe = new TcpListener(IPAddress.Loopback, p);
                    probe.Start();
                    probe.Stop();
                    return p;
                }
                catch (SocketException)
                {
                }
            }
        }
        throw new InvalidOperationException("no free port left in the range");
    }

    public void Return(int port)
    {
        lock (_gate) _free.Enqueue(port);
    }
}
