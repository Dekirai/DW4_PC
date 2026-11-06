using System.Diagnostics;
using System.Text.Json;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;
using Microsoft.Extensions.Logging.Abstractions;

namespace DW4Online.FakeClient;

/// <summary>
/// Round I1a's load gate (SEAMLESS.md 19.17.4), phase 1 measured on today's hub: one <see cref="RoomHub"/> (one lock,
/// <c>hub.Sync</c>) holds every member of the world in-process (no sockets: what is measured is the lock, not the network;
/// <see cref="LoadGateNet"/> runs the same load over loopback sockets against a real relay), worker threads send each
/// member's frames at the gate's rates through <see cref="RoomHub.Receive"/> under the lock, and the sealing thread
/// (<see cref="RelayHub.SealRoom"/>'s <c>Monitor.TryEnter</c>) and the 250 ms housekeeping run beside them. Measured: the
/// lock's hold time per path (p50 / p99 / max), the wait for it, the sealing lateness (a SEALED tick's due time against the
/// Seal call that sends it), the sealing thread's failed TryEnters and when the threads woke (coincident wakes), the
/// process CPU and memory. A run whose frames the hub refused or dropped (an implausible presence, a full presence bucket)
/// measured less than it claims: it is marked invalid and the command fails.
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
    /// <summary>The fingerprint every sealed member reports (all engines agree).</summary>
    public const ulong Fingerprint = 0x1234;
    /// <summary>
    /// The rooms' client protocol: the newest, <see cref="Wire.ReadyCheckProtocol"/> (48), so every NETFP of a sealed member
    /// also runs round J's check-fingerprint pass (RoomHub.OnNetFp) and a READY from a checkpoint names its check tick. The
    /// first r71 measurements (SEAMLESS.md 19.17.4) ran at 47 (<see cref="Wire.CheckpointProtocol"/>); --protocol 47 repeats
    /// them (the integration review of r71).
    /// </summary>
    public static int Protocol { get; set; } = Wire.WorldProtocol;
    public const ulong Context = 5;

    private sealed class Peer : IHubPeer
    {
        public long Frames, Bytes;
        public volatile bool StateDone;
        public volatile int JoinReqs;
        public volatile BarrierOkMsg? LastOk;

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
            else if (frame[0] == Msg.BarrierOk) LastOk = BarrierOkMsg.Parse(frame.AsSpan(Wire.HeaderBytes));
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
        public string Role = "lobby"; // lobby, sealed, solo, joining
        public uint NextTick;          // sealed: the next tick its INPUT names; solo: the next LOG tick
        public uint NextFp;            // sealed: the next fingerprint tick it reports once that tick is sealed
        public long NextAt;            // the next action (Stopwatch)
        public long Period;
        public int Stage;              // solo: the stage counter of its barriers
        public float Phase;
        public string PresenceStage = "stagML11";
    }

    /// <summary>
    /// A cell of the gate. SealWaitMs above 0: the sealing thread waits that long for the lock instead of trying once and
    /// coming back a millisecond later (RelayHub.SealRoom's TryEnter). GroupsOfThree: that many of the sealed groups have
    /// three members (a free slot for the joins). JoinsPerMinute: joins of lobby members into those groups, each with its
    /// state build, READY and JOIN, from the measured window's first second.
    /// </summary>
    public sealed record Cell(string Name, int Groups, int Solo, int Lobby, int PresenceStages, double BarriersPerSecond, double BrowsePerSecond,
        bool Select, double StormSeconds, double SealWaitMs = 0, int GroupsOfThree = 0, double JoinsPerMinute = 0, double MinSeconds = 0)
    {
        public int Members => Groups * 4 - GroupsOfThree + Solo + Lobby;
    }

    public static Cell[] Cells() =>
    [
        new("base", 30, 20, 60, 1, 1, 10, false, 0),
        new("fullhub", 0, 0, 200, 1, 0, 10, false, 0),
        // Round I1a: the real selection (19.13, computed off the lock) in the five hub stages (stagML11-14, stagML02; 40 each).
        new("select", 0, 0, 200, 5, 0, 10, false, 0),
        new("storm", 30, 20, 60, 1, 1, 10, false, 10),
        new("sealonly", 30, 0, 0, 1, 0, 0, false, 0),
        new("base-wait", 30, 20, 60, 1, 1, 10, false, 0, 2),
        new("storm-wait", 30, 20, 60, 1, 1, 10, false, 10, 2),
        // 19.17.4's joins cell: the base load with 15 of the 30 groups at three, ten joins a minute into them (60 s: ten joins).
        new("joins", 30, 20, 75, 1, 1, 10, false, 0, 0, 15, 10, 60),
    ];

    /// <summary>
    /// A lobby member's PRESENCE2 at the gate's 10 Hz: a figure circling (its position from its phase), facing its yaw in
    /// -pi..pi as the client's atan2 gives it (the hub drops a yaw beyond two turns as implausible; round I1a's first runs
    /// sent the growing phase as the yaw and lost about half their presence frames from the twelfth second on).
    /// </summary>
    public static Presence2Msg LobbyPresence(int index, float phase, string stage, uint inst, uint sentMs)
    {
        var x = 120f * MathF.Cos(phase + index);
        var z = 120f * MathF.Sin(phase * 0.7f + index);
        var yaw = MathF.IEEERemainder(phase, 2 * MathF.PI);
        var presence = new PresenceMsg(stage, inst, x, 40, z, yaw, (byte)(index % 16), 20, 100, 100, 50, 50);
        return new Presence2Msg(Presence2Msg.CurrentVersion, presence, "runD", Presence2Msg.Loop, 0, sentMs, "w_common05");
    }

    /// <summary>
    /// Why a run measured less than it claims (empty: valid): the hub dropped presence frames as implausible (any), or more
    /// than 1 % of them for the presence bucket, or refused frames (a protocol error).
    /// </summary>
    public static List<string> InvalidReasons(IReadOnlyDictionary<string, long> counters, long presenceSent, long refused)
    {
        var why = new List<string>();
        foreach (var (k, v) in counters)
            if (k.StartsWith("presence_implausible", StringComparison.Ordinal) && v > 0) why.Add($"{k} {v}");
        var dropped = counters.GetValueOrDefault("presence_dropped");
        if (presenceSent > 0 && dropped > presenceSent / 100) why.Add($"presence_dropped {dropped} of {presenceSent}");
        if (refused > 0) why.Add($"refused frames {refused}");
        return why;
    }

    /// <summary>The joins of a joins cell (one row each).</summary>
    private sealed record JoinRow(int Member, uint Target, string Outcome, double BarrierToStateMs, double BarrierToJoinMs);

    public static Dictionary<string, object> Run(Cell cell, double seconds, int threads, Action<string> log)
    {
        seconds = Math.Max(seconds, cell.MinSeconds);
        var instances = new InstanceOptions();
        var relay = new RelayOptions { MaxConnectionsPerIp = 1024 };
        var builds = new SemaphoreSlim(2); // Relay:MaxConcurrentStateBuilds
        RoomHub hub = null!;
        var stateStats = new Stats();
        hub = new RoomHub(new RoomInfo("WORLD1", Protocol, 1, 2, 4), relay, new SealingOptions(), instances, NullLogger.Instance, null,
            job => _ = Task.Run(async () =>
            {
                await builds.WaitAsync();
                var tb = Stopwatch.GetTimestamp();
                try
                {
                    if (!job.Superseded) job.Build();
                }
                finally
                {
                    builds.Release();
                }
                var t0 = Stopwatch.GetTimestamp();
                lock (stateStats) stateStats.Add("build_off_lock", t0 - tb, 0);
                lock (hub.Sync)
                {
                    var t1 = Stopwatch.GetTimestamp();
                    hub.FinishState(job);
                    lock (stateStats) stateStats.Add("finish_state", Stopwatch.GetTimestamp() - t1, t1 - t0);
                }
            }));
        using var wakeTimer = new HighResolutionTimer();
        hub.OnSealingStarted(wakeTimer.Wake);
        // The presence selection off the lock, as the relay runs it (M4 (c)).
        hub.SelectionRunner = f => Task.Run(f);

        // Members: the sealed groups (of four, or three for the joins), the solo-led ones, the lobby.
        var members = new List<Member>();
        var total = cell.Members;
        var now = Stopwatch.GetTimestamp();
        lock (hub.Sync)
            for (var k = 0; k < total; ++k)
            {
                var peer = new Peer();
                var m = hub.Connect(k, 1000 + k, "m" + k, k == 0, peer, now);
                members.Add(new Member { Index = k, Hub = m, Peer = peer });
            }
        long refused = 0;
        void Send(Member m, byte[] frame)
        {
            lock (hub.Sync)
            {
                try
                {
                    hub.Receive(m.Hub, frame[0], frame.AsSpan(Wire.HeaderBytes), Stopwatch.GetTimestamp());
                }
                catch (ProtocolException)
                {
                    Interlocked.Increment(ref refused);
                }
            }
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
        var threeGroups = new List<Member>(); // a member of each group of three (the joins' targets)
        for (var g = 0; g < cell.Groups; ++g)
        {
            var size = g < cell.GroupsOfThree ? 3 : 4;
            var group = members.GetRange(at, size);
            at += size;
            var stage = $"stagT{g:D3}";
            foreach (var m in group) Send(m, new HeadingMsg(m.Hub.Instance!.Id, stage, 0, Context).Frame());
            foreach (var m in group)
            {
                var i = m.Hub.Instance!;
                Send(m, new BarrierMsg(i.Id, i.Visit, 0, stage, 0, i.PresentMask, i.PresentMask, Context, [1]).Frame());
            }
            var leader = group.First(m => m.Hub.Instance is { } i && i.Slots[0] == m.Hub);
            Upload(leader, group);
            // (The co-arrivals' state transfers are built on the thread pool.)
            for (var w = 0; w < 200 && !leader.Hub.Instance!.Sealing; ++w) Thread.Sleep(5);
            var inst = leader.Hub.Instance!;
            if (inst.Mode == InstanceMode.Sealed && inst.Sealing && group.All(m => m.Hub.Instance == inst)) ++sealedOk;
            // The members' STATE after the load end (joinable; the hub takes the longest-standing member's).
            foreach (var m in group)
                Send(m, new StateMsg(inst.Id, inst.Visit, 0, (byte)(StateMsg.Joinable | StateMsg.MembershipSafe | StateMsg.LoadEnded), Context,
                    StateReason.None).Frame());
            foreach (var m in group)
            {
                m.Role = "sealed";
                m.NextTick = inst.NextSeal;
                m.NextFp = (inst.NextSeal + 59) / 60 * 60;
            }
            if (size == 3) threeGroups.Add(leader);
        }
        var soloOk = 0;
        for (var s = 0; s < cell.Solo; ++s)
        {
            var m = members[at++];
            m.Role = "solo";
            var i = m.Hub.Instance!;
            Send(m, new BarrierMsg(i.Id, i.Visit, 0, $"stagS{s:D3}", 0, 1, 1, Context, [1]).Frame());
            Upload(m, [m]);
            if (m.Hub.Instance is { Mode: InstanceMode.SoloLed }) ++soloOk;
            m.NextTick = 0;
        }
        // (The world's hub stages: presence goes by the selection only in them, 19.13.)
        string[] hubStages = ["stagML11", "stagML12", "stagML13", "stagML14", "stagML02"];
        var stagesOfPresence = Enumerable.Range(0, Math.Clamp(cell.PresenceStages, 1, hubStages.Length)).Select(k => hubStages[k]).ToArray();
        var lobbyMembers = new List<Member>();
        for (var l = 0; at < members.Count; ++l)
        {
            var m = members[at++];
            m.Role = "lobby";
            m.PresenceStage = stagesOfPresence[l % stagesOfPresence.Length];
            lobbyMembers.Add(m);
        }
        log($"{cell.Name}: {members.Count} members, {sealedOk}/{cell.Groups} sealed groups ({cell.GroupsOfThree} of three), {soloOk}/{cell.Solo} solo-led, " +
            $"{lobbyMembers.Count} in the lobby ({stagesOfPresence.Length} presence stage(s)); {hub.Instances.Count} instances");

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
        long presenceSent = 0;

        // When the threads woke (the wake diagnostics): every worker wake and every sealing pass (its wake, and whether it
        // missed the lock) of the measured window, at most 4 M worker wakes.
        var workerWakes = new long[4_000_000];
        var workerWakeCount = 0;
        var sealWakes = new List<(long At, bool Missed)>();
        void WorkerWoke(long t)
        {
            if (!Measuring) return;
            var k = Interlocked.Increment(ref workerWakeCount) - 1;
            if (k < workerWakes.Length) workerWakes[k] = t;
        }

        // The sealing thread (RelayHub.SealRoom) with the lateness of every due tick it seals.
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
                    if (Measuring)
                    {
                        ++tryMisses;
                        sealWakes.Add((t, true));
                    }
                    next = t + Stopwatch.Frequency / 1000;
                }
                else
                {
                    try
                    {
                        // (The pass's time is read with the lock held, as RelayHub.SealRoom: a bounded wait never seals with a
                        // stale time.)
                        var t1 = Stopwatch.GetTimestamp();
                        foreach (var i in hub.Instances)
                        {
                            if (i.Mode != InstanceMode.Sealed || !i.Sealing || i.Ended) continue;
                            var due = i.S0 + (long)(i.NextSeal * Wire.TickSeconds * Freq);
                            if (due <= t1 && Measuring) lateness.Add(Ms(t1 - due));
                        }
                        next = hub.Seal(t1);
                        sealStats.Add("seal", Stopwatch.GetTimestamp() - t1, t1 - t);
                        if (Measuring)
                        {
                            ++sealPasses;
                            sealWakes.Add((t, false));
                        }
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
                        var f = new BarrierMsg(i.Id, i.Visit, m.NextTick == 0 ? 0 : m.NextTick - 1, stage, 0, 1, 1, Context, [1]).Frame();
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

        // The joins (19.17.4's joins cell): a lobby member's BARRIER into a group of three's stage (the decision and the
        // state's capture under the lock), the state built off the lock and FinishState (the state thread above), READY
        // (the JOIN stamped at a sealed tick); from then the member sends its INPUT as a sealed member.
        var joinStats = new Stats();
        var joins = new List<JoinRow>();
        var joiner = new Thread(() =>
        {
            if (cell.JoinsPerMinute <= 0 || threeGroups.Count == 0) return;
            while (!Measuring && !Volatile.Read(ref stop)) Thread.Sleep(5);
            var next = Stopwatch.GetTimestamp() + Stopwatch.Frequency; // the first a second into the window
            var period = (long)(60.0 / cell.JoinsPerMinute * Freq);
            var candidates = new Queue<Member>(lobbyMembers.Skip(lobbyMembers.Count / 2));
            var targets = new Queue<Member>(threeGroups);
            while (!Volatile.Read(ref stop) && targets.Count > 0 && candidates.Count > 0)
            {
                if (Stopwatch.GetTimestamp() < next)
                {
                    Thread.Sleep(2);
                    continue;
                }
                next += period;
                var j = candidates.Dequeue();
                var target = targets.Dequeue().Hub.Instance!;
                long t0;
                lock (j)
                {
                    j.Role = "joining";
                    var i = j.Hub.Instance!;
                    t0 = Stopwatch.GetTimestamp();
                    lock (hub.Sync)
                    {
                        var t1 = Stopwatch.GetTimestamp();
                        var f = new BarrierMsg(i.Id, i.Visit, 0, target.Stage, 0, i.PresentMask, i.PresentMask, Context, [2]).Frame();
                        hub.Receive(j.Hub, f[0], f.AsSpan(Wire.HeaderBytes), t1);
                        joinStats.Add("join_barrier", Stopwatch.GetTimestamp() - t1, t1 - t0);
                    }
                }
                var deadline = Stopwatch.GetTimestamp() + 10 * Stopwatch.Frequency;
                while (!j.Peer.StateDone && Stopwatch.GetTimestamp() < deadline && !Volatile.Read(ref stop)) Thread.Sleep(1);
                var tState = Stopwatch.GetTimestamp();
                var ok = j.Peer.LastOk;
                if (!j.Peer.StateDone || ok is null || ok.Decision != BarrierDecision.Join)
                {
                    lock (joins) joins.Add(new JoinRow(j.Index, target.Id, $"no join ({ok?.Decision}, {ok?.Notice})", -1, -1));
                    continue;
                }
                lock (j)
                {
                    // READY at the newest fingerprint tick the members reported (the replay's fingerprint agrees).
                    uint readyTick;
                    lock (hub.Sync) readyTick = target.NextSeal == 0 ? 0 : (target.NextSeal - 1) / 60 * 60;
                    var w0 = Stopwatch.GetTimestamp();
                    lock (hub.Sync)
                    {
                        var t1 = Stopwatch.GetTimestamp();
                        var f = new ReadyMsg(ok.NewInst, ok.NewVisit, readyTick, Fingerprint, 3000, [7]).Frame();
                        hub.Receive(j.Hub, f[0], f.AsSpan(Wire.HeaderBytes), t1);
                        joinStats.Add("join_ready", Stopwatch.GetTimestamp() - t1, t1 - w0);
                        if (j.Hub.Instance == target)
                        {
                            j.Role = "sealed";
                            j.Period = tickTs;
                            j.NextTick = target.NextSeal;
                            j.NextFp = (target.NextSeal + 59) / 60 * 60;
                        }
                    }
                    var joined = j.Role == "sealed";
                    lock (joins)
                        joins.Add(new JoinRow(j.Index, target.Id, joined ? "joined" : "no join after READY", Math.Round(Ms(tState - t0), 2),
                            joined ? Math.Round(Ms(Stopwatch.GetTimestamp() - t0), 2) : -1));
                }
            }
        }) { IsBackground = true, Name = "joins" };

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
                WorkerWoke(t);
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
                    if (!Monitor.TryEnter(m)) continue; // (a barrier, a join or a reconnect of this member runs)
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
            if (m.Role == "sealed" && i.Mode == InstanceMode.Sealed)
            {
                // The member's own pads up to two ticks ahead of the sealing (the client's lead), one INPUT per pass; the
                // fingerprint of every 60th tick once that tick is sealed (the client reports it after executing it).
                var target = i.S0 == 0 ? i.NextSeal : (uint)Math.Max(0, (t - i.S0) / tickTs + 2);
                if (m.NextTick < i.NextSeal) m.NextTick = i.NextSeal;
                if (target >= m.NextTick)
                {
                    var count = (int)Math.Min(8, target - m.NextTick + 1);
                    var pads = new Pad[count];
                    Array.Fill(pads, pad);
                    Locked(m, new InputMsg(i.Id, i.Visit, m.NextTick, pads).Frame(), "input", stats);
                    m.NextTick += (uint)count;
                }
                if (i.NextSeal > m.NextFp)
                {
                    Locked(m, new NetFpMsg(i.Id, i.Visit, m.NextFp, Fingerprint).Frame(), "netfp", stats);
                    m.NextFp = (i.NextSeal + 59) / 60 * 60;
                }
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
                    Locked(m, new StateMsg(i.Id, i.Visit, m.NextTick - 1, (byte)(StateMsg.Joinable | StateMsg.MembershipSafe | StateMsg.LoadEnded), Context,
                        StateReason.None).Frame(), "state", stats);
                return;
            }
            if (m.Role == "lobby")
            {
                m.Phase += 0.05f;
                Locked(m, LobbyPresence(m.Index, m.Phase, m.PresenceStage, i.Id, (uint)Ms(t)).Frame(), "presence", stats);
                if (Measuring) Interlocked.Increment(ref presenceSent);
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
                    Interlocked.Increment(ref refused);
                    stats.Add("refused", Stopwatch.GetTimestamp() - t1, t1 - t0);
                    return;
                }
                stats.Add(path, Stopwatch.GetTimestamp() - t1, t1 - t0);
            }
        }

        sealer.Start();
        house.Start();
        barrierer.Start();
        joiner.Start();
        storm.Start();
        foreach (var w in workers) w.Start();
        Thread.Sleep(TimeSpan.FromSeconds(WarmupSeconds));
        proc.Refresh();
        var pause0 = GC.GetTotalPauseDuration();
        cpu0 = proc.TotalProcessorTime;
        gc0 = GC.CollectionCount(0) + GC.CollectionCount(1) + GC.CollectionCount(2);
        Dictionary<string, long> counters0;
        lock (hub.Sync) counters0 = new Dictionary<string, long>(hub.Counters);
        var refused0 = Interlocked.Read(ref refused);
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
        var measuredTo = Stopwatch.GetTimestamp();
        Volatile.Write(ref stop, true);
        foreach (var w in workers) w.Join();
        sealer.Join();
        house.Join();
        barrierer.Join();
        joiner.Join();
        storm.Join();
        var wall = Ms(measuredTo - measuredFrom) / 1000.0;
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
                if (k == "build_off_lock") continue; // (not a hold of the lock: reported apart)
                if (!all.TryGetValue(k, out var list)) all[k] = list = [];
                list.AddRange(v);
            }
            waits.AddRange(s.Wait);
        }
        foreach (var s in perThread) Merge(s);
        Merge(sealStats);
        Merge(houseStats);
        Merge(barrierStats);
        Merge(joinStats);
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
        Dictionary<string, long> counters;
        lock (hub.Sync) counters = hub.Counters.ToDictionary(kv => kv.Key, kv => kv.Value - counters0.GetValueOrDefault(kv.Key));
        counters = counters.Where(kv => kv.Value != 0).ToDictionary();
        var receivers = members.Count(m => m.Role == "lobby");
        var invalid = InvalidReasons(counters, Interlocked.Read(ref presenceSent), Interlocked.Read(ref refused) - refused0);
        var result = new Dictionary<string, object>
        {
            ["cell"] = cell.Name,
            ["valid"] = invalid.Count == 0,
            ["invalid_reasons"] = invalid,
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
            ["wakes"] = Wakes(workerWakes, Math.Min(workerWakeCount, workerWakes.Length), sealWakes, wall),
            ["process_cpu_cores"] = Math.Round(cpu / wall, 3),
            ["working_set_mb"] = Math.Round(proc.WorkingSet64 / 1048576.0, 1),
            ["managed_heap_mb"] = Math.Round(GC.GetTotalMemory(false) / 1048576.0, 1),
            ["gcs"] = gcs,
            ["gc_pause_ms"] = Math.Round(pauseMs, 2),
            ["housekeep_slow"] = houseSlow,
            ["server_gc"] = System.Runtime.GCSettings.IsServerGC,
            ["presence_sent_per_second"] = Math.Round(Interlocked.Read(ref presenceSent) / wall),
            ["frames_out_per_second"] = Math.Round((frames - framesBefore) / wall),
            ["bytes_out_per_second"] = Math.Round((bytes - bytesBefore) / wall),
            ["bytes_out_per_lobby_receiver_per_second"] = receivers == 0 ? 0 : Math.Round((bytes - bytesBefore) / wall / receivers),
            ["counters"] = counters,
        };
        lock (stateStats)
            if (stateStats.Hold.TryGetValue("build_off_lock", out var built)) result["state_build_off_lock"] = Summary(built);
        if (cell.JoinsPerMinute > 0)
        {
            lock (joins) result["joins"] = joins.ToList();
            result["joins_done"] = joins.Count(x => x.Outcome == "joined");
        }
        foreach (var (k, v) in all.OrderBy(kv => kv.Key)) result["hold_" + k] = Summary(v);
        lock (hub.Sync) hub.CloseAll(CloseReason.ServerStopping, "load gate done");
        return result;
    }

    /// <summary>
    /// The wake diagnostics: how often a worker woke within 2 / 10 µs of a sealing pass (against what independent wakes at
    /// the workers' rate would give), and the same for the passes that missed the lock. Wakes on the same timer interrupt
    /// come out far above the independent share; the misses then come from the harness's timers, not from the load.
    /// </summary>
    private static Dictionary<string, object> Wakes(long[] workers, int count, List<(long At, bool Missed)> seals, double wall)
    {
        var w = workers.AsSpan(0, count).ToArray();
        Array.Sort(w);
        double Within(IEnumerable<long> at, double us)
        {
            var window = (long)(us * Freq / 1e6);
            long n = 0, hit = 0;
            foreach (var t in at)
            {
                ++n;
                var k = Array.BinarySearch(w, t - window);
                if (k < 0) k = ~k;
                if (k < w.Length && w[k] <= t + window) ++hit;
            }
            return n == 0 ? 0 : Math.Round((double)hit / n, 4);
        }
        var rate = count / wall;
        return new Dictionary<string, object>
        {
            ["worker_wakes_per_second"] = Math.Round(rate),
            ["seal_passes"] = seals.Count,
            ["passes_with_worker_wake_within_2us"] = Within(seals.Select(s => s.At), 2),
            ["passes_with_worker_wake_within_10us"] = Within(seals.Select(s => s.At), 10),
            ["independent_share_2us"] = Math.Round(1 - Math.Exp(-rate * 4e-6), 4),
            ["independent_share_10us"] = Math.Round(1 - Math.Exp(-rate * 20e-6), 4),
            ["misses"] = seals.Count(s => s.Missed),
            ["misses_with_worker_wake_within_2us"] = Within(seals.Where(s => s.Missed).Select(s => s.At), 2),
            ["misses_with_worker_wake_within_10us"] = Within(seals.Where(s => s.Missed).Select(s => s.At), 10),
            ["hits_with_worker_wake_within_2us"] = Within(seals.Where(s => !s.Missed).Select(s => s.At), 2),
        };
    }

    /// <summary>
    /// The joins cell, its lock part on an idle hub (19.17.4): one join into a solo-led visit of <paramref name="minutes"/>
    /// of log, on a clock the measurement moves (a LOG never runs ahead of the game's pace), each step's hold of the lock
    /// timed: the joiner's BARRIER (the decision and the state's capture), the state's build (off the lock, as RelayHub runs
    /// it), FinishState, READY, JOIN_AT; once the classic way (Instances:CheckpointJoins off: the whole log) and once from a
    /// checkpoint of 1.1 MB (round J's raw tick state size) uploaded in 32 KB chunks, each chunk's hold timed too. A join
    /// the hub refuses reports the BARRIER_OK's notice (TooLongToCatchUp: the catch-up passes Instances:MaxCatchUpSeconds).
    /// The joins under the base load are cell "joins" of <see cref="Run"/>.
    /// </summary>
    public static Dictionary<string, object> Join(double minutes, Action<string> log)
    {
        var result = new Dictionary<string, object> { ["minutes"] = minutes };
        foreach (var checkpoints in new[] { false, true })
        {
            StateJob? job = null;
            var options = new InstanceOptions { CheckpointJoins = checkpoints, CheckpointWaitMs = 60000 };
            var hub = new RoomHub(new RoomInfo("WORLD1", Protocol, 1, 2, 4), new RelayOptions(), new SealingOptions(),
                options, NullLogger.Instance, null, j => job = j);
            var now = Stopwatch.GetTimestamp();
            var peers = new[] { new Peer(), new Peer() };
            var a = hub.Connect(0, 1000, "a", true, peers[0], now);
            var b = hub.Connect(1, 1001, "b", false, peers[1], now);
            var holds = new Dictionary<string, object>();
            double Timed(HubMember m, byte[] f, string? name)
            {
                var t0 = Stopwatch.GetTimestamp();
                hub.Receive(m, f[0], f.AsSpan(Wire.HeaderBytes), now);
                var ms = Ms(Stopwatch.GetTimestamp() - t0);
                if (name is not null) holds[name] = Math.Round(ms, 4);
                return ms;
            }
            var i = a.Instance!;
            Timed(a, new BarrierMsg(i.Id, i.Visit, 0, "stag0101", 0, 1, 1, Context, [1]).Frame(), null);
            i = a.Instance!;
            var snapshot = new byte[20000];
            Timed(a, new SnapshotMsg(i.Id, i.Visit, (uint)snapshot.Length, 0, snapshot).Frame(), null);
            Timed(a, new SnapHashMsg(i.Id, i.Visit, 0xAA).Frame(), null);
            var ticks = (uint)(minutes * 60 / Wire.TickSeconds);
            var pads = Enumerable.Range(0, LogMsg.MaxPads).Select(k => new Pad((ushort)(k % 7 == 0 ? 0x100 : 0), (sbyte)(k % 60), (sbyte)(-(k % 50)), 0, 0)).ToArray();
            var logHolds = new List<double>();
            for (uint t = 0; t < ticks; t += (uint)pads.Length)
            {
                var n = (int)Math.Min(pads.Length, ticks - t);
                now += (long)(n * Wire.TickSeconds * Freq);
                logHolds.Add(Timed(a, new LogMsg(i.Id, i.Visit, t, n == pads.Length ? pads : pads[..n]).Frame(), null));
            }
            holds["log_1200_pads"] = Summary(logHolds);
            Timed(a, new StateMsg(i.Id, i.Visit, ticks - 1, (byte)(StateMsg.Joinable | StateMsg.MembershipSafe | StateMsg.LoadEnded), Context, StateReason.None).Frame(), null);
            if (checkpoints) Timed(a, new StateHashMsg(i.Id, i.Visit, ticks / 2, 0xC0FFEE, 20, 1).Frame(), null);
            var bi = b.Instance!;
            Timed(b, new BarrierMsg(bi.Id, bi.Visit, 0, "stag0101", 0, 1, 1, Context, [2]).Frame(), "barrier");
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
                var ok = peers[1].LastOk;
                var catchUp = HubRules.CatchUpSeconds(ticks, options.DefaultReplayRate);
                holds["refused"] = $"BARRIER_OK {ok?.Decision} notice {ok?.Notice}; the catch-up of {ticks} ticks at the default replay rate " +
                                   $"{options.DefaultReplayRate} is {catchUp:0.0} s, Instances:MaxCatchUpSeconds {options.MaxCatchUpSeconds}";
                result[checkpoints ? "checkpoint" : "classic"] = holds;
                log($"join after {minutes} min of log ({ticks} ticks), {(checkpoints ? "from a checkpoint" : "classic")}: {Json(holds)}");
                continue;
            }
            var tb = Stopwatch.GetTimestamp();
            job.Build();
            holds["build_off_lock"] = Math.Round(Ms(Stopwatch.GetTimestamp() - tb), 4);
            var tf = Stopwatch.GetTimestamp();
            hub.FinishState(job);
            holds["finish_state"] = Math.Round(Ms(Stopwatch.GetTimestamp() - tf), 4);
            holds["state_bytes"] = peers[1].Bytes;
            var target = b.Joining?.Target ?? b.Instance!;
            var readyTick = ticks;
            ulong readyFp = 0;
            if (checkpoints && Protocol >= Wire.ReadyCheckProtocol)
            {
                // Protocol 48: the READY names the checkpoint's check tick (Wire.ReadyCheckTicks replayed steps), checked against
                // the donor's NETFP of that tick (OnNetFp's check-fingerprint pass, timed too).
                readyTick = ticks - 30 + Wire.ReadyCheckTicks - 1;
                readyFp = Fingerprint;
                Timed(a, new NetFpMsg(i.Id, i.Visit, readyTick, Fingerprint).Frame(), "check_netfp");
            }
            Timed(b, new ReadyMsg(target.Id, target.Visit, readyTick, readyFp, 3000, [7], checkpoints ? ReadyMsg.FromCheckpoint : (byte)0).Frame(), "ready");
            Timed(a, new JoinAtMsg(i.Id, i.Visit, ticks + 10).Frame(), "join_at");
            holds["join_reqs"] = peers[0].JoinReqs;
            holds["mode_after"] = (int)i.Mode;
            holds["protocol"] = Protocol;
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

    public static Dictionary<string, object> Summary(List<double> v)
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
            ["p9999"] = Math.Round(P(0.9999), 4),
            ["max"] = Math.Round(s[^1], 4),
            ["mean"] = Math.Round(s.Average(), 4),
        };
    }

    public static string Json(object o) => JsonSerializer.Serialize(o, new JsonSerializerOptions { WriteIndented = true });
}
