using System.Diagnostics;
using System.Text.Json;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using Microsoft.Extensions.Logging.Abstractions;

namespace DW4Online.FakeClient;

/// <summary>
/// Round I1a's load gate (SEAMLESS.md 19.17.4), phase 1 measured on today's hub: one <see cref="RoomHub"/> (one lock,
/// <c>hub.Sync</c>) holds every member of the world in-process (no sockets: what is measured is the lock, not the network),
/// worker threads send each member's frames at the gate's rates through <see cref="RoomHub.Receive"/> under the lock, and
/// the sealing thread (<c>Monitor.TryEnter</c>, as RelayHub.SealLoop) and the 250 ms housekeeping run beside them. Measured:
/// the lock's hold time per path (p50 / p99 / max), the wait for it, the sealing lateness (a SEALED tick's due time against
/// the Seal call that sends it), the sealing thread's failed TryEnters, the process CPU and memory.
/// The world layer (instances, cells, the presence selection) is not built yet: the cells run today's code at the world's
/// scale, and the selection's cost is a prototype computed under the lock (cell "select").
/// </summary>
public static class LoadGate
{
    private static readonly double Freq = Stopwatch.Frequency;
    private static double Ms(long ticks) => ticks * 1000.0 / Freq;
    /// <summary>Off during the warm-up (JIT, the first state transfers): nothing is recorded.</summary>
    private static volatile bool Measuring;
    private const int WarmupSeconds = 3;

    private sealed class Peer : IHubPeer
    {
        public long Frames, Bytes;
        public volatile bool StateDone;
        public volatile int JoinReqs;

        public void Send(byte[] frame, bool bulk = false)
        {
            Interlocked.Increment(ref Frames);
            Interlocked.Add(ref Bytes, frame.Length);
            if (frame[0] == Msg.StateData)
            {
                var m = StateDataMsg.Parse(frame.AsSpan(Wire.HeaderBytes));
                if (m.Offset + (uint)m.Chunk.Length >= m.Total) StateDone = true;
            }
            else if (frame[0] == Msg.JoinReq) Interlocked.Increment(ref JoinReqs);
        }

        public void Close(CloseReason reason, string text)
        {
        }
    }

    /// <summary>Hold times per path, one per thread (merged at the end).</summary>
    private sealed class Stats
    {
        public readonly Dictionary<string, List<double>> Hold = [];
        public readonly List<double> Wait = [];

        public void Add(string path, long hold, long wait)
        {
            if (!Measuring) return;
            if (!Hold.TryGetValue(path, out var list)) Hold[path] = list = [];
            list.Add(Ms(hold));
            Wait.Add(Ms(wait));
        }
    }

    private sealed class Member
    {
        public int Index;
        public HubMember Hub = null!;
        public Peer Peer = null!;
        public string Role = "lobby"; // lobby, sealed, solo, joiner
        public uint NextTick;          // sealed: the next tick its INPUT names; solo: the next LOG tick
        public long NextAt;            // the next action (Stopwatch)
        public long Period;
        public int Stage;              // solo: the stage counter of its barriers
        public float Phase;
        public string PresenceStage = "stagML11";
    }

    /// <summary>A cell of the gate; SealWaitMs above 0: the sealing thread waits that long for the lock instead of trying once
    /// and coming back a millisecond later (RelayHub.SealLoop's TryEnter).</summary>
    public sealed record Cell(string Name, int Groups, int Solo, int Lobby, int PresenceStages, double BarriersPerSecond, double BrowsePerSecond,
        bool Select, double StormSeconds, double SealWaitMs = 0);

    public static Cell[] Cells() =>
    [
        new("base", 30, 20, 60, 1, 1, 10, false, 0),
        new("fullhub", 0, 0, 200, 1, 0, 10, false, 0),
        new("select", 0, 0, 200, 7, 0, 10, true, 0),
        new("storm", 30, 20, 60, 1, 1, 10, false, 10),
        new("sealonly", 30, 0, 0, 1, 0, 0, false, 0),
        new("base-wait", 30, 20, 60, 1, 1, 10, false, 0, 2),
        new("storm-wait", 30, 20, 60, 1, 1, 10, false, 10, 2),
    ];

    public static Dictionary<string, object> Run(Cell cell, double seconds, int threads, Action<string> log)
    {
        var instances = new InstanceOptions();
        var relay = new RelayOptions { MaxConnectionsPerIp = 1024 };
        var builds = new SemaphoreSlim(2); // Relay:MaxConcurrentStateBuilds
        RoomHub hub = null!;
        var stateStats = new Stats();
        hub = new RoomHub(new RoomInfo("WORLD1", Wire.CheckpointProtocol, 1, 2, 4), relay, new SealingOptions(), instances, NullLogger.Instance, null,
            job => _ = Task.Run(async () =>
            {
                await builds.WaitAsync();
                try
                {
                    if (!job.Superseded) job.Build();
                }
                finally
                {
                    builds.Release();
                }
                var t0 = Stopwatch.GetTimestamp();
                lock (hub.Sync)
                {
                    var t1 = Stopwatch.GetTimestamp();
                    hub.FinishState(job);
                    lock (stateStats) stateStats.Add("finish_state", Stopwatch.GetTimestamp() - t1, t1 - t0);
                }
            }));
        using var wakeTimer = new HighResolutionTimer();
        hub.OnSealingStarted(wakeTimer.Wake);

        // Members: the sealed groups of four, the solo-led ones, the lobby.
        var members = new List<Member>();
        var total = cell.Groups * 4 + cell.Solo + cell.Lobby;
        var now = Stopwatch.GetTimestamp();
        lock (hub.Sync)
            for (var k = 0; k < total; ++k)
            {
                var peer = new Peer();
                var m = hub.Connect(k, 1000 + k, "m" + k, k == 0, peer, now);
                members.Add(new Member { Index = k, Hub = m, Peer = peer });
            }
        void Send(Member m, byte[] frame)
        {
            lock (hub.Sync) hub.Receive(m.Hub, frame[0], frame.AsSpan(Wire.HeaderBytes), Stopwatch.GetTimestamp());
        }
        var snapshot = Enumerable.Range(0, 100).Select(x => (byte)x).ToArray();
        void Upload(Member leader, IEnumerable<Member> group)
        {
            var i = leader.Hub.Instance!;
            Send(leader, new SnapshotMsg(i.Id, i.Visit, (uint)snapshot.Length, 0, snapshot).Frame());
            foreach (var m in group) Send(m, new SnapHashMsg(i.Id, i.Visit, 0xAA).Frame());
        }
        var at = 0;
        var sealedOk = 0;
        for (var g = 0; g < cell.Groups; ++g)
        {
            var group = members.GetRange(at, 4);
            at += 4;
            var stage = $"stagT{g:D3}";
            foreach (var m in group) Send(m, new HeadingMsg(m.Hub.Instance!.Id, stage, 0, 5).Frame());
            foreach (var m in group)
            {
                var i = m.Hub.Instance!;
                Send(m, new BarrierMsg(i.Id, i.Visit, 0, stage, 0, i.PresentMask, i.PresentMask, 5, [1]).Frame());
            }
            var leader = group.First(m => m.Hub.Instance is { } i && i.Slots[0] == m.Hub);
            Upload(leader, group);
            // (The co-arrivals' state transfers are built on the thread pool.)
            for (var w = 0; w < 200 && !leader.Hub.Instance!.Sealing; ++w) Thread.Sleep(5);
            var inst = leader.Hub.Instance!;
            if (inst.Mode == InstanceMode.Sealed && inst.Sealing && group.All(m => m.Hub.Instance == inst)) ++sealedOk;
            foreach (var m in group)
            {
                m.Role = "sealed";
                m.NextTick = inst.NextSeal;
            }
        }
        var soloOk = 0;
        for (var s = 0; s < cell.Solo; ++s)
        {
            var m = members[at++];
            m.Role = "solo";
            var i = m.Hub.Instance!;
            Send(m, new BarrierMsg(i.Id, i.Visit, 0, $"stagS{s:D3}", 0, 1, 1, 5, [1]).Frame());
            Upload(m, [m]);
            if (m.Hub.Instance is { Mode: InstanceMode.SoloLed }) ++soloOk;
            m.NextTick = 0;
        }
        var stagesOfPresence = Enumerable.Range(0, Math.Max(1, cell.PresenceStages)).Select(k => k == 0 ? "stagML11" : $"stagML1{(char)('A' + k)}").ToArray();
        for (var l = 0; at < members.Count; ++l)
        {
            var m = members[at++];
            m.Role = "lobby";
            m.PresenceStage = stagesOfPresence[l % stagesOfPresence.Length];
        }
        log($"{cell.Name}: {members.Count} members, {sealedOk}/{cell.Groups} sealed groups of four, {soloOk}/{cell.Solo} solo-led, " +
            $"{members.Count(m => m.Role == "lobby")} in the lobby ({stagesOfPresence.Length} presence stage(s)); {hub.Instances.Count} instances");

        // The run.
        var stop = false;
        var start = Stopwatch.GetTimestamp();
        var tickTs = (long)(Wire.TickSeconds * Freq);
        var rng = new Random(7);
        foreach (var m in members)
        {
            m.Period = m.Role switch
            {
                "sealed" => tickTs,
                "solo" => (long)(0.1 * Freq),
                _ => (long)(0.1 * Freq),
            };
            m.NextAt = start + (long)(rng.NextDouble() * m.Period);
            m.Phase = (float)(rng.NextDouble() * 6.28);
        }
        var perThread = Enumerable.Range(0, threads).Select(_ => new Stats()).ToArray();
        var proc = Process.GetCurrentProcess();
        Measuring = false;
        var cpu0 = proc.TotalProcessorTime;
        var gc0 = GC.CollectionCount(0) + GC.CollectionCount(1) + GC.CollectionCount(2);

        // The sealing thread (RelayHub.SealLoop) with the lateness of every due tick it seals.
        var sealStats = new Stats();
        var lateness = new List<double>();
        long tryMisses = 0, sealPasses = 0;
        var sealer = new Thread(() =>
        {
            using var timer = new HighResolutionTimer();
            while (!Volatile.Read(ref stop))
            {
                var t = Stopwatch.GetTimestamp();
                long next;
                if (cell.SealWaitMs > 0 ? !Monitor.TryEnter(hub.Sync, TimeSpan.FromMilliseconds(cell.SealWaitMs)) : !Monitor.TryEnter(hub.Sync))
                {
                    if (Measuring) ++tryMisses;
                    next = t + Stopwatch.Frequency / 1000;
                }
                else
                {
                    try
                    {
                        var t1 = Stopwatch.GetTimestamp();
                        foreach (var i in hub.Instances)
                        {
                            if (i.Mode != InstanceMode.Sealed || !i.Sealing || i.Ended) continue;
                            var due = i.S0 + (long)(i.NextSeal * Wire.TickSeconds * Freq);
                            if (due <= t1 && Measuring) lateness.Add(Ms(t1 - due));
                        }
                        var t2 = Stopwatch.GetTimestamp();
                        next = hub.Seal(t2);
                        sealStats.Add("seal", Stopwatch.GetTimestamp() - t2, t1 - t);
                        if (Measuring) ++sealPasses;
                    }
                    finally
                    {
                        Monitor.Exit(hub.Sync);
                    }
                }
                timer.WaitUntil(Math.Min(next, Stopwatch.GetTimestamp() + Stopwatch.Frequency / 20));
            }
        }) { IsBackground = true, Priority = ThreadPriority.AboveNormal, Name = "sealer" };

        // Housekeeping (RelayHub.HousekeepAsync: every 250 ms under the lock), the browser's reads and the selection prototype.
        var houseStats = new Stats();
        var houseSlow = new List<object>(); // (the housekeeping passes over 2 ms: when, how long, whether a collection ran in them)
        var house = new Thread(() =>
        {
            var nextHouse = Stopwatch.GetTimestamp();
            var nextBrowse = nextHouse;
            var nextSelect = nextHouse;
            while (!Volatile.Read(ref stop))
            {
                var t = Stopwatch.GetTimestamp();
                if (t >= nextHouse)
                {
                    nextHouse += Stopwatch.Frequency / 4;
                    lock (hub.Sync)
                    {
                        var t1 = Stopwatch.GetTimestamp();
                        var gcBefore = GC.CollectionCount(0);
                        hub.Housekeep(t1);
                        foreach (var m in hub.Members)
                            if (m.Peer is { } p && (t1 - m.LastHeard) > (long)relay.IdleTimeoutSeconds * Stopwatch.Frequency)
                                p.Close(CloseReason.Timeout, "Nothing heard from this connection.");
                        _ = hub.Members.Where(m => m.Connected).Select(m => m.AccountId).ToList();
                        var held = Stopwatch.GetTimestamp() - t1;
                        houseStats.Add("housekeep", held, t1 - t);
                        if (Measuring && Ms(held) > 2)
                            houseSlow.Add(new { at_s = Math.Round(Ms(t1 - start) / 1000.0, 2), ms = Math.Round(Ms(held), 2), gc = GC.CollectionCount(0) != gcBefore });
                    }
                }
                if (cell.BrowsePerSecond > 0 && t >= nextBrowse)
                {
                    nextBrowse += (long)(Stopwatch.Frequency / cell.BrowsePerSecond);
                    lock (hub.Sync)
                    {
                        var t1 = Stopwatch.GetTimestamp();
                        _ = hub.Views();
                        houseStats.Add("browse", Stopwatch.GetTimestamp() - t1, t1 - t);
                    }
                }
                if (cell.Select && t >= nextSelect)
                {
                    nextSelect += Stopwatch.Frequency;
                    lock (hub.Sync)
                    {
                        var t1 = Stopwatch.GetTimestamp();
                        SelectNearest(hub, 30);
                        houseStats.Add("select", Stopwatch.GetTimestamp() - t1, t1 - t);
                    }
                }
                Thread.Sleep(1);
            }
        }) { IsBackground = true, Name = "house" };

        // One barrier a second somewhere: a solo-led member goes on to its next stage (NEW, snapshot, hash).
        var barrierStats = new Stats();
        var soloMembers = members.Where(m => m.Role == "solo").ToList();
        var barrierer = new Thread(() =>
        {
            var next = Stopwatch.GetTimestamp() + Stopwatch.Frequency / 2;
            var k = 0;
            while (!Volatile.Read(ref stop) && cell.BarriersPerSecond > 0 && soloMembers.Count > 0)
            {
                if (Stopwatch.GetTimestamp() < next)
                {
                    Thread.Sleep(2);
                    continue;
                }
                next += (long)(Stopwatch.Frequency / cell.BarriersPerSecond);
                var m = soloMembers[k++ % soloMembers.Count];
                lock (m)
                {
                    var i = m.Hub.Instance;
                    if (i is null) continue;
                    var stage = $"stagS{m.Index:D3}x{++m.Stage % 2}";
                    var t0 = Stopwatch.GetTimestamp();
                    lock (hub.Sync)
                    {
                        var t1 = Stopwatch.GetTimestamp();
                        var f = new BarrierMsg(i.Id, i.Visit, m.NextTick == 0 ? 0 : m.NextTick - 1, stage, 0, 1, 1, 5, [1]).Frame();
                        hub.Receive(m.Hub, f[0], f.AsSpan(Wire.HeaderBytes), t1);
                        barrierStats.Add("barrier", Stopwatch.GetTimestamp() - t1, t1 - t0);
                    }
                    var ni = m.Hub.Instance!;
                    Send(m, new SnapshotMsg(ni.Id, ni.Visit, (uint)snapshot.Length, 0, snapshot).Frame());
                    Send(m, new SnapHashMsg(ni.Id, ni.Visit, 0xAA).Frame());
                    m.NextTick = 0;
                }
            }
        }) { IsBackground = true, Name = "barriers" };

        // The reconnect storm: every member once within StormSeconds (a router restart), from the run's third second.
        var stormStats = new Stats();
        var storm = new Thread(() =>
        {
            if (cell.StormSeconds <= 0) return;
            Thread.Sleep(WarmupSeconds * 1000);
            var gap = cell.StormSeconds / members.Count;
            var t0 = Stopwatch.GetTimestamp();
            for (var k = 0; k < members.Count && !Volatile.Read(ref stop); ++k)
            {
                var due = t0 + (long)(k * gap * Freq);
                while (Stopwatch.GetTimestamp() < due) Thread.Sleep(1);
                var m = members[k];
                lock (m)
                {
                    var peer = new Peer();
                    var w0 = Stopwatch.GetTimestamp();
                    lock (hub.Sync)
                    {
                        var t1 = Stopwatch.GetTimestamp();
                        hub.Disconnect(m.Hub, m.Peer, t1);
                        stormStats.Add("disconnect", Stopwatch.GetTimestamp() - t1, t1 - w0);
                    }
                    var w1 = Stopwatch.GetTimestamp();
                    lock (hub.Sync)
                    {
                        var t1 = Stopwatch.GetTimestamp();
                        m.Hub = hub.Connect(m.Index, 1000 + m.Index, "m" + m.Index, m.Index == 0, peer, t1);
                        stormStats.Add("connect", Stopwatch.GetTimestamp() - t1, t1 - w1);
                    }
                    m.Peer = peer;
                    // A sealed member resumes its instance (RESUME from its last tick), the others start over where they are.
                    if (m.Role == "sealed" && m.Hub.Instance is { Mode: InstanceMode.Sealed } si && m.Hub.ResumeAllowed)
                    {
                        var w2 = Stopwatch.GetTimestamp();
                        lock (hub.Sync)
                        {
                            var t1 = Stopwatch.GetTimestamp();
                            var f = new ResumeMsg(si.Id, si.Visit, (byte)m.Hub.Slot, si.NextSeal == 0 ? 0 : si.NextSeal - 1).Frame();
                            hub.Receive(m.Hub, f[0], f.AsSpan(Wire.HeaderBytes), t1);
                            stormStats.Add("resume", Stopwatch.GetTimestamp() - t1, t1 - w2);
                        }
                    }
                }
            }
        }) { IsBackground = true, Name = "storm" };

        // The members' own frames, spread over the worker threads.
        var workers = Enumerable.Range(0, threads).Select(w => new Thread(() =>
        {
            var mine = members.Where(m => m.Index % threads == w).ToList();
            var stats = perThread[w];
            using var timer = new HighResolutionTimer();
            var pad = new Pad(0, 0, 0, 0, 0);
            while (!Volatile.Read(ref stop))
            {
                var t = Stopwatch.GetTimestamp();
                var soonest = long.MaxValue;
                foreach (var m in mine)
                {
                    if (m.NextAt > t)
                    {
                        soonest = Math.Min(soonest, m.NextAt);
                        continue;
                    }
                    m.NextAt += m.Period;
                    if (m.NextAt < t) m.NextAt = t + m.Period; // (behind: no burst to catch up)
                    soonest = Math.Min(soonest, m.NextAt);
                    if (!Monitor.TryEnter(m)) continue; // (a barrier or a reconnect of this member runs)
                    try
                    {
                        Act(m, t, stats, pad);
                    }
                    finally
                    {
                        Monitor.Exit(m);
                    }
                }
                if (soonest != long.MaxValue) timer.WaitUntil(soonest);
            }
        }) { IsBackground = true, Name = "worker" + w }).ToList();

        void Act(Member m, long t, Stats stats, Pad pad)
        {
            var i = m.Hub.Instance;
            if (i is null) return;
            byte[]? frame = null;
            string path;
            if (m.Role == "sealed" && i.Mode == InstanceMode.Sealed)
            {
                // The member's own pads up to two ticks ahead of the sealing (the client's lead), one INPUT per pass; a
                // NETFP every 60 ticks.
                var target = i.S0 == 0 ? i.NextSeal : (uint)Math.Max(0, (t - i.S0) / tickTs + 2);
                if (m.NextTick < i.NextSeal) m.NextTick = i.NextSeal;
                if (target < m.NextTick) return;
                var count = (int)Math.Min(8, target - m.NextTick + 1);
                var pads = new Pad[count];
                Array.Fill(pads, pad);
                frame = new InputMsg(i.Id, i.Visit, m.NextTick, pads).Frame();
                var fpDue = m.NextTick / 60 != (m.NextTick + (uint)count) / 60;
                m.NextTick += (uint)count;
                path = "input";
                Locked(m, frame, path, stats);
                if (fpDue) Locked(m, new NetFpMsg(i.Id, i.Visit, m.NextTick / 60 * 60, 0x1234).Frame(), "netfp", stats);
                return;
            }
            if (m.Role == "solo" && i.Mode == InstanceMode.SoloLed)
            {
                // LOG every 100 ms (six ticks), a STATE once a second.
                var pads = new Pad[6];
                Array.Fill(pads, pad);
                Locked(m, new LogMsg(i.Id, i.Visit, m.NextTick, pads).Frame(), "log", stats);
                m.NextTick += 6;
                if (m.NextTick % 60 == 0)
                    Locked(m, new StateMsg(i.Id, i.Visit, m.NextTick - 1, (byte)(StateMsg.Joinable | StateMsg.MembershipSafe | StateMsg.LoadEnded), 5,
                        StateReason.None).Frame(), "state", stats);
                return;
            }
            if (m.Role == "lobby")
            {
                m.Phase += 0.05f;
                var x = 120f * MathF.Cos(m.Phase + m.Index);
                var z = 120f * MathF.Sin(m.Phase * 0.7f + m.Index);
                var presence = new PresenceMsg(m.PresenceStage, i.Id, x, 40, z, m.Phase, (byte)(m.Index % 16), 20, 100, 100, 50, 50);
                frame = new Presence2Msg(Presence2Msg.CurrentVersion, presence, "runD", Presence2Msg.Loop, 0, (uint)Ms(t), "w_common05").Frame();
                Locked(m, frame, "presence", stats);
            }
        }

        void Locked(Member m, byte[] frame, string path, Stats stats)
        {
            var t0 = Stopwatch.GetTimestamp();
            lock (hub.Sync)
            {
                var t1 = Stopwatch.GetTimestamp();
                try
                {
                    hub.Receive(m.Hub, frame[0], frame.AsSpan(Wire.HeaderBytes), t1);
                }
                catch (ProtocolException)
                {
                    stats.Add("refused", Stopwatch.GetTimestamp() - t1, t1 - t0);
                    return;
                }
                stats.Add(path, Stopwatch.GetTimestamp() - t1, t1 - t0);
            }
        }

        sealer.Start();
        house.Start();
        barrierer.Start();
        storm.Start();
        foreach (var w in workers) w.Start();
        Thread.Sleep(TimeSpan.FromSeconds(WarmupSeconds));
        proc.Refresh();
        var pause0 = GC.GetTotalPauseDuration();
        cpu0 = proc.TotalProcessorTime;
        gc0 = GC.CollectionCount(0) + GC.CollectionCount(1) + GC.CollectionCount(2);
        var measuredFrom = Stopwatch.GetTimestamp();
        long framesBefore = 0, bytesBefore = 0;
        foreach (var m in members)
        {
            framesBefore += Interlocked.Read(ref m.Peer.Frames);
            bytesBefore += Interlocked.Read(ref m.Peer.Bytes);
        }
        Measuring = true;
        Thread.Sleep(TimeSpan.FromSeconds(seconds));
        Measuring = false;
        Volatile.Write(ref stop, true);
        foreach (var w in workers) w.Join();
        sealer.Join();
        house.Join();
        barrierer.Join();
        storm.Join();
        var wall = Ms(Stopwatch.GetTimestamp() - measuredFrom) / 1000.0;
        proc.Refresh();
        var cpu = (proc.TotalProcessorTime - cpu0).TotalSeconds;
        var gcs = GC.CollectionCount(0) + GC.CollectionCount(1) + GC.CollectionCount(2) - gc0;
        var pauseMs = (GC.GetTotalPauseDuration() - pause0).TotalMilliseconds;

        // Merge.
        var all = new Dictionary<string, List<double>>();
        var waits = new List<double>();
        void Merge(Stats s)
        {
            foreach (var (k, v) in s.Hold)
            {
                if (!all.TryGetValue(k, out var list)) all[k] = list = [];
                list.AddRange(v);
            }
            waits.AddRange(s.Wait);
        }
        foreach (var s in perThread) Merge(s);
        Merge(sealStats);
        Merge(houseStats);
        Merge(barrierStats);
        Merge(stormStats);
        lock (stateStats) Merge(stateStats);
        var every = all.Values.SelectMany(v => v).ToList();
        var lockedMs = every.Sum();
        long frames = 0, bytes = 0;
        foreach (var m in members)
        {
            frames += Interlocked.Read(ref m.Peer.Frames); // (since the warm-up's end: framesBefore below)
            bytes += Interlocked.Read(ref m.Peer.Bytes);
        }
        var result = new Dictionary<string, object>
        {
            ["cell"] = cell.Name,
            ["members"] = members.Count,
            ["seconds"] = Math.Round(wall, 1),
            ["threads"] = threads,
            ["hold_all"] = Summary(every),
            ["wait_all"] = Summary(waits),
            ["lock_busy_share"] = Math.Round(lockedMs / 1000.0 / wall, 4),
            ["acquisitions_per_second"] = Math.Round(every.Count / wall),
            ["seal_lateness"] = Summary(lateness),
            ["seal_trymiss"] = tryMisses,
            ["seal_trymiss_per_second"] = Math.Round(tryMisses / wall, 1),
            ["seal_passes_per_second"] = Math.Round(sealPasses / wall),
            ["process_cpu_cores"] = Math.Round(cpu / wall, 3),
            ["working_set_mb"] = Math.Round(proc.WorkingSet64 / 1048576.0, 1),
            ["managed_heap_mb"] = Math.Round(GC.GetTotalMemory(false) / 1048576.0, 1),
            ["gcs"] = gcs,
            ["gc_pause_ms"] = Math.Round(pauseMs, 2),
            ["housekeep_slow"] = houseSlow,
            ["server_gc"] = System.Runtime.GCSettings.IsServerGC,
            ["frames_out_per_second"] = Math.Round((frames - framesBefore) / wall),
            ["bytes_out_per_second"] = Math.Round((bytes - bytesBefore) / wall),
            ["counters"] = hub.Counters.ToDictionary(kv => kv.Key, kv => kv.Value),
        };
        foreach (var (k, v) in all.OrderBy(kv => kv.Key)) result["hold_" + k] = Summary(v);
        lock (hub.Sync) hub.CloseAll(CloseReason.ServerStopping, "load gate done");
        return result;
    }

    /// <summary>
    /// The joins cell, its lock part (19.17.4): one join into a solo-led visit of <paramref name="minutes"/> of log, on a clock
    /// the measurement moves (a LOG never runs ahead of the game's pace), each step's hold of the lock timed: the joiner's
    /// BARRIER (the decision and the state's capture), the state's build (off the lock, as RelayHub runs it), FinishState,
    /// READY, JOIN_AT; once the classic way (Instances:CheckpointJoins off: the whole log) and once from a checkpoint of
    /// 1.1 MB (round J's raw tick state size) uploaded in 32 KB chunks, each chunk's hold timed too.
    /// </summary>
    public static Dictionary<string, object> Join(double minutes, Action<string> log)
    {
        var result = new Dictionary<string, object> { ["minutes"] = minutes };
        foreach (var checkpoints in new[] { false, true })
        {
            StateJob? job = null;
            var hub = new RoomHub(new RoomInfo("WORLD1", Wire.CheckpointProtocol, 1, 2, 4), new RelayOptions(), new SealingOptions(),
                new InstanceOptions { CheckpointJoins = checkpoints, CheckpointWaitMs = 60000 }, NullLogger.Instance, null, j => job = j);
            var now = Stopwatch.GetTimestamp();
            var peers = new[] { new Peer(), new Peer() };
            var a = hub.Connect(0, 1000, "a", true, peers[0], now);
            var b = hub.Connect(1, 1001, "b", false, peers[1], now);
            var holds = new Dictionary<string, double>();
            double Timed(HubMember m, byte[] f, string? name)
            {
                var t0 = Stopwatch.GetTimestamp();
                hub.Receive(m, f[0], f.AsSpan(Wire.HeaderBytes), now);
                var ms = Ms(Stopwatch.GetTimestamp() - t0);
                if (name is not null) holds[name] = Math.Round(ms, 4);
                return ms;
            }
            var i = a.Instance!;
            Timed(a, new BarrierMsg(i.Id, i.Visit, 0, "stag0101", 0, 1, 1, 5, [1]).Frame(), null);
            i = a.Instance!;
            var snapshot = new byte[20000];
            Timed(a, new SnapshotMsg(i.Id, i.Visit, (uint)snapshot.Length, 0, snapshot).Frame(), null);
            Timed(a, new SnapHashMsg(i.Id, i.Visit, 0xAA).Frame(), null);
            var ticks = (uint)(minutes * 60 / Wire.TickSeconds);
            var pads = Enumerable.Range(0, LogMsg.MaxPads).Select(k => new Pad((ushort)(k % 7 == 0 ? 0x100 : 0), (sbyte)(k % 60), (sbyte)(-(k % 50)), 0, 0)).ToArray();
            double logMax = 0;
            for (uint t = 0; t < ticks; t += (uint)pads.Length)
            {
                var n = (int)Math.Min(pads.Length, ticks - t);
                now += (long)(n * Wire.TickSeconds * Freq);
                logMax = Math.Max(logMax, Timed(a, new LogMsg(i.Id, i.Visit, t, n == pads.Length ? pads : pads[..n]).Frame(), null));
            }
            holds["log_1200_pads_max"] = Math.Round(logMax, 4);
            Timed(a, new StateMsg(i.Id, i.Visit, ticks - 1, (byte)(StateMsg.Joinable | StateMsg.MembershipSafe | StateMsg.LoadEnded), 5, StateReason.None).Frame(), null);
            if (checkpoints) Timed(a, new StateHashMsg(i.Id, i.Visit, ticks / 2, 0xC0FFEE, 20, 1).Frame(), null);
            var bi = b.Instance!;
            Timed(b, new BarrierMsg(bi.Id, bi.Visit, 0, "stag0101", 0, 1, 1, 5, [2]).Frame(), "barrier");
            if (checkpoints)
            {
                // The donor's answer to CHECKPOINT_ASK: 1.1 MB in 32 KB chunks.
                var cp = new byte[1_100_000];
                new Random(1).NextBytes(cp);
                double chunkMax = 0;
                for (var off = 0; off < cp.Length; off += 32768)
                {
                    var f = new CheckpointMsg(i.Id, i.Visit, ticks - 30, 0xBEEF, 20, (uint)cp.Length, (uint)off, cp.AsSpan(off, Math.Min(32768, cp.Length - off)).ToArray()).Frame();
                    chunkMax = Math.Max(chunkMax, Timed(a, f, null));
                }
                holds["checkpoint_chunk_max"] = Math.Round(chunkMax, 4);
            }
            if (job is null)
            {
                result[checkpoints ? "checkpoint" : "classic"] = "no state job (" + string.Join(",", hub.Counters.Keys) + ")";
                continue;
            }
            var tb = Stopwatch.GetTimestamp();
            job.Build();
            holds["build_off_lock"] = Math.Round(Ms(Stopwatch.GetTimestamp() - tb), 4);
            var tf = Stopwatch.GetTimestamp();
            hub.FinishState(job);
            holds["finish_state"] = Math.Round(Ms(Stopwatch.GetTimestamp() - tf), 4);
            holds["state_bytes"] = peers[1].Bytes;
            var ok = b.Joining?.Target ?? b.Instance!;
            Timed(b, new ReadyMsg(ok.Id, ok.Visit, ticks, 0, 3000, [7], checkpoints ? ReadyMsg.FromCheckpoint : (byte)0).Frame(), "ready");
            Timed(a, new JoinAtMsg(i.Id, i.Visit, ticks + 10).Frame(), "join_at");
            holds["join_reqs"] = peers[0].JoinReqs;
            holds["mode_after"] = (int)i.Mode;
            result[checkpoints ? "checkpoint" : "classic"] = holds;
            log($"join after {minutes} min of log ({ticks} ticks), {(checkpoints ? "from a checkpoint" : "classic")}: {Json(holds)}");
        }
        return result;
    }

    /// <summary>
    /// The world's presence selection (19.13), a prototype: for every lobby member the 30 nearest others of its stage by
    /// their last presence (party and friends do not exist yet), computed in one pass under the lock as a once-a-second
    /// recomputation would.
    /// </summary>
    private static int SelectNearest(RoomHub hub, int count)
    {
        var lobby = hub.Members.Where(m => m.Presence is not null).ToList();
        var chosen = 0;
        var distances = new List<(float D, int K)>(lobby.Count);
        for (var r = 0; r < lobby.Count; ++r)
        {
            var me = lobby[r].Presence!;
            distances.Clear();
            for (var k = 0; k < lobby.Count; ++k)
            {
                if (k == r) continue;
                var o = lobby[k].Presence!;
                if (!string.Equals(o.Stage, me.Stage, StringComparison.OrdinalIgnoreCase)) continue;
                var dx = o.X - me.X;
                var dz = o.Z - me.Z;
                distances.Add((dx * dx + dz * dz, k));
            }
            distances.Sort((a, b) => a.D.CompareTo(b.D));
            chosen += Math.Min(count, distances.Count);
        }
        return chosen;
    }

    private static Dictionary<string, object> Summary(List<double> v)
    {
        if (v.Count == 0) return new Dictionary<string, object> { ["n"] = 0 };
        var s = v.ToArray();
        Array.Sort(s);
        double P(double q) => s[Math.Min(s.Length - 1, (int)(q * s.Length))];
        return new Dictionary<string, object>
        {
            ["n"] = s.Length,
            ["p50"] = Math.Round(P(0.50), 4),
            ["p99"] = Math.Round(P(0.99), 4),
            ["p999"] = Math.Round(P(0.999), 4),
            ["max"] = Math.Round(s[^1], 4),
            ["mean"] = Math.Round(s.Average(), 4),
        };
    }

    public static string Json(object o) => JsonSerializer.Serialize(o, new JsonSerializerOptions { WriteIndented = true });
}
