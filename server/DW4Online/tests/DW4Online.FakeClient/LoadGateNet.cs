using System.Buffers.Binary;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using Microsoft.Extensions.Logging.Abstractions;

namespace DW4Online.FakeClient;

/// <summary>
/// Round I1a's load gate over sockets (SEAMLESS.md 19.17.4, the review of the first runs): the real relay
/// (<see cref="RelayHub"/>: its connections' read and write loops, the sealing thread with its TryEnter, the housekeeping,
/// the browser's <see cref="RelayHub.Views"/>) in a child process of its own on loopback, and the gate's 200 members as
/// lean fake clients in this process, each on its own TCP connection, at the rates of <see cref="LoadGate"/>'s cells. So
/// the frames reach the hub's lock from socket completions (not from the harness's timers), the relay's CPU includes the
/// fan-out's sends, and the sealing thread's misses and lateness are the relay's own (<see cref="RelayHub.SealTryMisses"/>,
/// <see cref="RoomHub.SealLateness"/>). The hold times per path stay <see cref="LoadGate"/>'s (the same hub code).
/// </summary>
public static class LoadGateNet
{
    public const string Room = "WORLD1";
    private static readonly double Freq = Stopwatch.Frequency;
    private static double Ms(long ticks) => ticks * 1000.0 / Freq;

    // ---- The relay process ------------------------------------------------------------------------------------------

    /// <summary>
    /// The child: the relay on 127.0.0.1:<paramref name="port"/> with the tickets of <paramref name="keyHex"/>, every account
    /// admitted to one world room (member index = account). Prints "LISTENING port", then answers commands on stdin:
    /// "browse N" (the browser's reads N times a second), "mark" (the window starts), "report" (one line "REPORT json"),
    /// "status" (one line "STATUS json"), "quit".
    /// </summary>
    public static async Task<int> RelayHostAsync(int port, string keyHex)
    {
        var tickets = new RelayTickets(Convert.FromHexString(keyHex), TimeSpan.FromMinutes(30), TimeProvider.System);
        var options = new RelayOptions
        {
            Enabled = true, ListenAddress = "127.0.0.1", Port = port, MaxConnectionsPerIp = 1024, IdleTimeoutSeconds = 120, LagLeaveSeconds = 600,
        };
        var relay = new RelayHub(options, new SealingOptions(), new InstanceOptions(), tickets, NullLoggerFactory.Instance);
        relay.Admission = a => ValueTask.FromResult<HubAdmitted?>(new HubAdmitted((int)a.AccountId, "m" + a.AccountId, a.AccountId == 0,
            new RoomInfo(Room, LoadGate.Protocol, 1, 2, 250)));
        using var stop = new CancellationTokenSource();
        await relay.StartAsync(stop.Token);
        Console.WriteLine($"LISTENING {await relay.Started}");
        var proc = Process.GetCurrentProcess();
        var browse = 0.0;
        var browsing = new Thread(() =>
        {
            var next = Stopwatch.GetTimestamp();
            while (!stop.IsCancellationRequested)
            {
                if (browse > 0 && Stopwatch.GetTimestamp() >= next)
                {
                    next += (long)(Freq / browse);
                    _ = relay.Views(Room);
                }
                Thread.Sleep(1);
            }
        }) { IsBackground = true, Name = "browse" };
        browsing.Start();
        TimeSpan cpu0 = default;
        long passes0 = 0, misses0 = 0, at0 = 0;
        int gc0 = 0;
        long[] late0 = new long[SealLateness.BoundsMs.Length + 1];
        Dictionary<string, long> counters0 = [];
        while (Console.ReadLine() is { } line)
        {
            var parts = line.Split(' ');
            var hub = relay.Room(Room);
            switch (parts[0])
            {
                case "browse":
                    browse = double.Parse(parts[1], System.Globalization.CultureInfo.InvariantCulture);
                    break;
                case "mark":
                    proc.Refresh();
                    cpu0 = proc.TotalProcessorTime;
                    gc0 = GC.CollectionCount(0) + GC.CollectionCount(1) + GC.CollectionCount(2);
                    passes0 = relay.SealPasses;
                    misses0 = relay.SealTryMisses;
                    if (hub is not null)
                        lock (hub.Sync)
                        {
                            late0 = hub.SealLateness.Counts.ToArray();
                            counters0 = new Dictionary<string, long>(hub.Counters);
                        }
                    at0 = Stopwatch.GetTimestamp();
                    Console.WriteLine("MARKED");
                    break;
                case "report":
                {
                    var wall = Ms(Stopwatch.GetTimestamp() - at0) / 1000.0;
                    proc.Refresh();
                    var r = new Dictionary<string, object>
                    {
                        ["relay_seconds"] = Math.Round(wall, 1),
                        ["relay_cpu_cores"] = Math.Round((proc.TotalProcessorTime - cpu0).TotalSeconds / wall, 3),
                        ["relay_working_set_mb"] = Math.Round(proc.WorkingSet64 / 1048576.0, 1),
                        ["relay_gcs"] = GC.CollectionCount(0) + GC.CollectionCount(1) + GC.CollectionCount(2) - gc0,
                        ["relay_server_gc"] = System.Runtime.GCSettings.IsServerGC,
                    };
                    var passes = relay.SealPasses - passes0;
                    var misses = relay.SealTryMisses - misses0;
                    r["seal_passes_per_second"] = Math.Round(passes / wall);
                    r["seal_trymiss_per_second"] = Math.Round(misses / wall, 1);
                    r["seal_trymiss_share"] = passes == 0 ? 0 : Math.Round((double)misses / passes, 4);
                    if (hub is not null)
                        lock (hub.Sync)
                        {
                            var counts = hub.SealLateness.Counts.Select((c, k) => c - late0[k]).ToArray();
                            var n = counts.Sum();
                            double Over(int bound) => n == 0 ? 0 : Math.Round((double)counts.Skip(bound + 1).Sum() / n, 5);
                            r["seal_lateness"] = new Dictionary<string, object>
                            {
                                ["passes"] = n,
                                ["buckets_ms"] = SealLateness.BoundsMs,
                                ["counts"] = counts,
                                ["share_over_1ms"] = Over(2),
                                ["share_over_2ms"] = Over(3),
                                ["share_over_5ms"] = Over(4),
                                ["max_ms_since_start"] = Math.Round(hub.SealLateness.MaxMs, 2),
                            };
                            r["counters"] = hub.Counters.Select(kv => (kv.Key, V: kv.Value - counters0.GetValueOrDefault(kv.Key))).Where(x => x.V != 0)
                                .ToDictionary(x => x.Key, x => x.V);
                            r["instances"] = hub.Instances.GroupBy(i => i.Mode.ToString()).ToDictionary(g => g.Key, g => g.Count());
                            r["connected"] = hub.Members.Count(m => m.Connected);
                        }
                    Console.WriteLine("REPORT " + JsonSerializer.Serialize(r));
                    break;
                }
                case "status":
                {
                    var s = new Dictionary<string, object>();
                    if (hub is not null)
                        lock (hub.Sync)
                        {
                            s["instances"] = hub.Instances.GroupBy(i => i.Mode + (i.Sealing ? "+sealing" : "")).ToDictionary(g => g.Key, g => g.Count());
                            s["connected"] = hub.Members.Count(m => m.Connected);
                        }
                    Console.WriteLine("STATUS " + JsonSerializer.Serialize(s));
                    break;
                }
                case "quit":
                    await stop.CancelAsync();
                    await relay.StopAsync(CancellationToken.None);
                    relay.Dispose();
                    return 0;
            }
        }
        return 0;
    }

    // ---- The clients ------------------------------------------------------------------------------------------------

    private sealed class NetMember
    {
        public int Index;
        public TcpClient? Client;
        public NetworkStream? Stream;
        public readonly Lock WriteGate = new();
        public Task? Reader;
        public volatile WelcomeMsg? Welcome;
        public volatile BarrierOkMsg? LastOk;
        public volatile bool StateDone;
        public volatile string? Closed;
        public uint Inst, Visit;                 // (written by the reader, read by the senders: Volatile)
        public int Slot;
        public uint SealedHead;                   // the next tick after the newest SEALED tick received
        public long FramesIn, BytesIn, PresenceIn;
        public string Role = "lobby";
        public uint NextTick, NextFp;
        public long NextAt, Period;
        public int Stage;
        public float Phase;
        public string PresenceStage = "stagML11";
        public uint OwnInst => Volatile.Read(ref Inst);
        public uint OwnVisit => Volatile.Read(ref Visit);

        public void Write(byte[] frame)
        {
            lock (WriteGate) Stream!.Write(frame);
        }
    }

    private static async Task<string> ReadLineAsync(NetworkStream s, CancellationToken ct)
    {
        var sb = new StringBuilder();
        var one = new byte[1];
        while (sb.Length < 200)
        {
            if (await s.ReadAsync(one, ct) == 0) throw new IOException("closed during the handshake");
            if (one[0] == '\n') return sb.ToString();
            sb.Append((char)one[0]);
        }
        throw new IOException("handshake line too long");
    }

    private static async Task ConnectAsync(NetMember m, int port, RelayTickets tickets, CancellationToken ct)
    {
        var client = new TcpClient { NoDelay = true };
        await client.ConnectAsync(IPAddress.Loopback, port, ct);
        var stream = client.GetStream();
        var ticket = tickets.Issue(Room, m.Index, RelayRoles.Member);
        await stream.WriteAsync(Encoding.ASCII.GetBytes($"{Wire.Magic} {ticket} {LoadGate.Protocol} 1 2\n"), ct);
        var line = await ReadLineAsync(stream, ct);
        if (!line.StartsWith("OK ", StringComparison.Ordinal)) throw new IOException($"member {m.Index}: {line}");
        m.Welcome = null;
        m.Closed = null;
        m.Client = client;
        m.Stream = stream;
        m.Reader = Task.Run(() => ReadLoopAsync(m, stream, ct), CancellationToken.None);
        var deadline = Stopwatch.GetTimestamp() + 10 * Stopwatch.Frequency;
        while (m.Welcome is null && Stopwatch.GetTimestamp() < deadline) await Task.Delay(2, ct);
        if (m.Welcome is null) throw new IOException($"member {m.Index}: no WELCOME");
    }

    private static async Task ReadLoopAsync(NetMember m, NetworkStream stream, CancellationToken ct)
    {
        var header = new byte[Wire.HeaderBytes];
        var payload = new byte[64 * 1024];
        try
        {
            while (!ct.IsCancellationRequested)
            {
                await stream.ReadExactlyAsync(header, ct);
                var type = header[0];
                var len = (int)BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(1));
                if (len > payload.Length) payload = new byte[len];
                await stream.ReadExactlyAsync(payload.AsMemory(0, len), ct);
                Interlocked.Increment(ref m.FramesIn);
                Interlocked.Add(ref m.BytesIn, len + Wire.HeaderBytes);
                var p = payload.AsSpan(0, len);
                switch (type)
                {
                    case Msg.Welcome:
                    {
                        var w = WelcomeMsg.Parse(p);
                        Volatile.Write(ref m.Inst, w.Inst);
                        Volatile.Write(ref m.Visit, w.Visit);
                        Volatile.Write(ref m.Slot, w.Slot);
                        m.Welcome = w;
                        break;
                    }
                    case Msg.BarrierOk:
                    {
                        var ok = BarrierOkMsg.Parse(p);
                        Volatile.Write(ref m.Inst, ok.NewInst);
                        Volatile.Write(ref m.Visit, ok.NewVisit);
                        Volatile.Write(ref m.Slot, ok.OwnSlot);
                        Volatile.Write(ref m.SealedHead, 0);
                        m.LastOk = ok;
                        break;
                    }
                    case Msg.Reassign:
                    {
                        var r = ReassignMsg.Parse(p);
                        Volatile.Write(ref m.Inst, r.NewInst);
                        break;
                    }
                    case Msg.Sealed:
                    {
                        // inst u32 | visit u32 | first tick u32 | count u16 | records
                        var first = BinaryPrimitives.ReadUInt32LittleEndian(p[8..]);
                        var count = BinaryPrimitives.ReadUInt16LittleEndian(p[12..]);
                        if (first + count > Volatile.Read(ref m.SealedHead)) Volatile.Write(ref m.SealedHead, first + count);
                        break;
                    }
                    case Msg.Ping:
                    {
                        var ping = PingMsg.Parse(p);
                        m.Write(new PongMsg(ping.Nonce, (ulong)(Stopwatch.GetTimestamp() * 1000.0 / Freq)).Frame());
                        break;
                    }
                    case Msg.StateData:
                    {
                        var s = StateDataMsg.Parse(p);
                        if (s.Offset + (uint)s.Chunk.Length >= s.Total) m.StateDone = true;
                        break;
                    }
                    case Msg.PresenceOut:
                    case Msg.Presence2Out:
                        Interlocked.Increment(ref m.PresenceIn);
                        break;
                    case Msg.Close:
                        m.Closed = CloseMsg.Parse(p).Text;
                        return;
                }
            }
        }
        catch (Exception e) when (e is IOException or SocketException or OperationCanceledException or ObjectDisposedException or EndOfStreamException)
        {
            m.Closed ??= "socket: " + e.GetType().Name;
        }
    }

    private static async Task<bool> WaitAsync(Func<bool> done, double seconds)
    {
        var deadline = Stopwatch.GetTimestamp() + (long)(seconds * Freq);
        while (!done())
        {
            if (Stopwatch.GetTimestamp() > deadline) return false;
            await Task.Delay(2);
        }
        return true;
    }

    /// <summary>One cell of <see cref="LoadGate.Cells"/> over sockets (not the joins: they stay in-process).</summary>
    public static async Task<Dictionary<string, object>> RunAsync(LoadGate.Cell cell, int port, double seconds, int threads, Action<string> log)
    {
        var key = RandomNumberGenerator.GetBytes(32);
        var tickets = new RelayTickets(key, TimeSpan.FromMinutes(30), TimeProvider.System);
        // (This executable again; under "dotnet FakeClient.dll" the dll goes first.)
        var exe = Environment.ProcessPath!;
        var args = $"loadgate-relay --port {port} --key {Convert.ToHexString(key)} --protocol {LoadGate.Protocol}";
        if (Path.GetFileNameWithoutExtension(exe).Equals("dotnet", StringComparison.OrdinalIgnoreCase))
            args = $"\"{typeof(LoadGateNet).Assembly.Location}\" " + args;
        var child = Process.Start(new ProcessStartInfo(exe, args)
        {
            RedirectStandardInput = true, RedirectStandardOutput = true, UseShellExecute = false, CreateNoWindow = true,
        })!;
        var lines = new System.Collections.Concurrent.BlockingCollection<string>();
        _ = Task.Run(() =>
        {
            while (child.StandardOutput.ReadLine() is { } l) lines.Add(l);
            lines.CompleteAdding();
        });
        string Expect(string prefix)
        {
            foreach (var l in lines.GetConsumingEnumerable())
                if (l.StartsWith(prefix, StringComparison.Ordinal)) return l[prefix.Length..].Trim();
            throw new IOException("the relay process ended before " + prefix);
        }
        void Command(string c)
        {
            child.StandardInput.WriteLine(c);
            child.StandardInput.Flush();
        }
        using var cts = new CancellationTokenSource();
        var members = new List<NetMember>();
        var result = new Dictionary<string, object> { ["cell"] = cell.Name, ["mode"] = "sockets", ["relay_pid"] = child.Id };
        try
        {
            var listening = int.Parse(Expect("LISTENING"), System.Globalization.CultureInfo.InvariantCulture);
            var total = cell.Members;
            for (var k = 0; k < total; ++k) members.Add(new NetMember { Index = k });
            foreach (var batch in members.Chunk(20)) await Task.WhenAll(batch.Select(m => ConnectAsync(m, listening, tickets, cts.Token)));
            var snapshot = Enumerable.Range(0, 100).Select(x => (byte)x).ToArray();

            // The sealed groups: HEADING, BARRIER (co-arrival), the leader's snapshot and everyone's hash, STATE.
            var at = 0;
            var sealedOk = 0;
            for (var g = 0; g < cell.Groups; ++g)
            {
                var size = g < cell.GroupsOfThree ? 3 : 4;
                var group = members.GetRange(at, size);
                at += size;
                var stage = $"stagT{g:D3}";
                foreach (var m in group) m.Write(new HeadingMsg(m.OwnInst, stage, 0, LoadGate.Context).Frame());
                await Task.Delay(20);
                foreach (var m in group)
                {
                    var mask = (byte)(1 << Volatile.Read(ref m.Slot));
                    m.LastOk = null;
                    m.Write(new BarrierMsg(m.OwnInst, m.OwnVisit, 0, stage, 0, mask, mask, LoadGate.Context, [1]).Frame());
                }
                if (!await WaitAsync(() => group.All(m => m.LastOk is not null), 10)) throw new IOException($"group {g}: no BARRIER_OK");
                var leader = group.First(m => m.LastOk!.Decision == BarrierDecision.New);
                leader.Write(new SnapshotMsg(leader.OwnInst, leader.OwnVisit, (uint)snapshot.Length, 0, snapshot).Frame());
                foreach (var m in group) m.Write(new SnapHashMsg(m.OwnInst, m.OwnVisit, 0xAA).Frame());
                if (await WaitAsync(() => Volatile.Read(ref leader.SealedHead) > 0, 10)) ++sealedOk;
                foreach (var m in group)
                {
                    m.Write(new StateMsg(m.OwnInst, m.OwnVisit, 0, (byte)(StateMsg.Joinable | StateMsg.MembershipSafe | StateMsg.LoadEnded), LoadGate.Context,
                        StateReason.None).Frame());
                    m.Role = "sealed";
                    m.NextTick = Volatile.Read(ref m.SealedHead);
                    m.NextFp = (m.NextTick + 59) / 60 * 60;
                }
            }
            var soloOk = 0;
            for (var s = 0; s < cell.Solo; ++s)
            {
                var m = members[at++];
                m.Role = "solo";
                m.LastOk = null;
                m.Write(new BarrierMsg(m.OwnInst, m.OwnVisit, 0, $"stagS{s:D3}", 0, 1, 1, LoadGate.Context, [1]).Frame());
                if (!await WaitAsync(() => m.LastOk is not null, 10)) throw new IOException($"solo {s}: no BARRIER_OK");
                m.Write(new SnapshotMsg(m.OwnInst, m.OwnVisit, (uint)snapshot.Length, 0, snapshot).Frame());
                m.Write(new SnapHashMsg(m.OwnInst, m.OwnVisit, 0xAA).Frame());
                if (m.LastOk!.Mode == InstanceMode.SoloLed) ++soloOk;
                m.NextTick = 0;
            }
            var stagesOfPresence = Enumerable.Range(0, Math.Max(1, cell.PresenceStages)).Select(k => k == 0 ? "stagML11" : $"stagML1{(char)('A' + k)}").ToArray();
            var lobbyCount = 0;
            for (var l = 0; at < members.Count; ++l)
            {
                var m = members[at++];
                m.Role = "lobby";
                m.PresenceStage = stagesOfPresence[l % stagesOfPresence.Length];
                ++lobbyCount;
            }
            Command("status");
            var status = Expect("STATUS");
            log($"{cell.Name} (sockets, relay pid {child.Id}, port {listening}): {members.Count} members, {sealedOk}/{cell.Groups} sealed groups, " +
                $"{soloOk}/{cell.Solo} solo-led, {lobbyCount} in the lobby; relay {status}");
            result["setup"] = new Dictionary<string, object> { ["sealed_groups"] = sealedOk, ["solo_led"] = soloOk, ["lobby"] = lobbyCount, ["relay"] = status };
            if (cell.BrowsePerSecond > 0) Command(FormattableString.Invariant($"browse {cell.BrowsePerSecond}"));

            // The run (the senders as LoadGate's workers, writing to the sockets).
            var stop = false;
            var measuring = false;
            var start = Stopwatch.GetTimestamp();
            var rng = new Random(7);
            var tickTs = (long)(Wire.TickSeconds * Freq);
            foreach (var m in members)
            {
                m.Period = m.Role == "sealed" ? tickTs : (long)(0.1 * Freq);
                m.NextAt = start + (long)(rng.NextDouble() * m.Period);
                m.Phase = (float)(rng.NextDouble() * 6.28);
            }
            long presenceSent = 0, sendErrors = 0;
            var pad = new Pad(0, 0, 0, 0, 0);
            void Act(NetMember m, long t)
            {
                if (m.Closed is not null || m.Stream is null) return;
                try
                {
                    if (m.Role == "sealed")
                    {
                        // Its pads up to three ticks past the newest sealed one it has (the client's lead), one INPUT per pass; the
                        // fingerprint of every 60th tick once that tick is sealed.
                        var head = Volatile.Read(ref m.SealedHead);
                        if (head == 0) return;
                        if (m.NextTick < head) m.NextTick = head;
                        var target = head + 3;
                        if (target >= m.NextTick)
                        {
                            var count = (int)Math.Min(8, target - m.NextTick + 1);
                            var pads = new Pad[count];
                            Array.Fill(pads, pad);
                            m.Write(new InputMsg(m.OwnInst, m.OwnVisit, m.NextTick, pads).Frame());
                            m.NextTick += (uint)count;
                        }
                        if (head > m.NextFp)
                        {
                            m.Write(new NetFpMsg(m.OwnInst, m.OwnVisit, m.NextFp, LoadGate.Fingerprint).Frame());
                            m.NextFp = (head + 59) / 60 * 60;
                        }
                        return;
                    }
                    if (m.Role == "solo")
                    {
                        var pads = new Pad[6];
                        Array.Fill(pads, pad);
                        m.Write(new LogMsg(m.OwnInst, m.OwnVisit, m.NextTick, pads).Frame());
                        m.NextTick += 6;
                        if (m.NextTick % 60 == 0)
                            m.Write(new StateMsg(m.OwnInst, m.OwnVisit, m.NextTick - 1, (byte)(StateMsg.Joinable | StateMsg.MembershipSafe | StateMsg.LoadEnded),
                                LoadGate.Context, StateReason.None).Frame());
                        return;
                    }
                    if (m.Role == "lobby")
                    {
                        m.Phase += 0.05f;
                        m.Write(LoadGate.LobbyPresence(m.Index, m.Phase, m.PresenceStage, m.OwnInst, (uint)Ms(t)).Frame());
                        if (Volatile.Read(ref measuring)) Interlocked.Increment(ref presenceSent);
                    }
                }
                catch (Exception e) when (e is IOException or SocketException or ObjectDisposedException or InvalidOperationException)
                {
                    Interlocked.Increment(ref sendErrors);
                }
            }
            var workers = Enumerable.Range(0, threads).Select(w => new Thread(() =>
            {
                var mine = members.Where(m => m.Index % threads == w).ToList();
                using var timer = new HighResolutionTimer();
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
                        if (m.NextAt < t) m.NextAt = t + m.Period;
                        soonest = Math.Min(soonest, m.NextAt);
                        if (!Monitor.TryEnter(m)) continue;
                        try
                        {
                            Act(m, t);
                        }
                        finally
                        {
                            Monitor.Exit(m);
                        }
                    }
                    if (soonest != long.MaxValue) timer.WaitUntil(soonest);
                }
            }) { IsBackground = true, Name = "worker" + w }).ToList();

            // One barrier a second: a solo-led member goes on to its next stage (NEW, snapshot, hash).
            var soloMembers = members.Where(m => m.Role == "solo").ToList();
            long barriersDone = 0;
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
                    next += (long)(Freq / cell.BarriersPerSecond);
                    var m = soloMembers[k++ % soloMembers.Count];
                    lock (m)
                    {
                        if (m.Closed is not null) continue;
                        m.LastOk = null;
                        m.Write(new BarrierMsg(m.OwnInst, m.OwnVisit, m.NextTick == 0 ? 0 : m.NextTick - 1, $"stagS{m.Index:D3}x{++m.Stage % 2}", 0, 1, 1,
                            LoadGate.Context, [1]).Frame());
                        var deadline = Stopwatch.GetTimestamp() + Stopwatch.Frequency;
                        while (m.LastOk is null && Stopwatch.GetTimestamp() < deadline) Thread.Sleep(1);
                        if (m.LastOk is null) continue;
                        m.Write(new SnapshotMsg(m.OwnInst, m.OwnVisit, (uint)snapshot.Length, 0, snapshot).Frame());
                        m.Write(new SnapHashMsg(m.OwnInst, m.OwnVisit, 0xAA).Frame());
                        m.NextTick = 0;
                        Interlocked.Increment(ref barriersDone);
                    }
                }
            }) { IsBackground = true, Name = "barriers" };

            // The reconnect storm: every member once within StormSeconds (a router restart: the socket is reset), from the
            // run's third second; a sealed member still in its instance resumes it (RESUME from its newest sealed tick).
            long reconnects = 0, resumes = 0;
            var stormErrors = new List<string>();
            var storm = new Thread(() =>
            {
                if (cell.StormSeconds <= 0) return;
                Thread.Sleep(3000);
                var gap = cell.StormSeconds / members.Count;
                var t0 = Stopwatch.GetTimestamp();
                for (var k = 0; k < members.Count && !Volatile.Read(ref stop); ++k)
                {
                    var due = t0 + (long)(k * gap * Freq);
                    while (Stopwatch.GetTimestamp() < due) Thread.Sleep(1);
                    var m = members[k];
                    lock (m)
                    {
                        try
                        {
                            var old = (Inst: m.OwnInst, Visit: m.OwnVisit, Slot: Volatile.Read(ref m.Slot), Head: Volatile.Read(ref m.SealedHead));
                            m.Client!.Client.LingerState = new LingerOption(true, 0);
                            m.Client.Close();
                            m.Reader?.Wait(2000);
                            ConnectAsync(m, listening, tickets, cts.Token).GetAwaiter().GetResult();
                            Interlocked.Increment(ref reconnects);
                            var w = m.Welcome!;
                            if (m.Role == "sealed" && w.Mode == InstanceMode.Sealed && w.Inst == old.Inst)
                            {
                                Volatile.Write(ref m.SealedHead, old.Head);
                                m.Write(new ResumeMsg(old.Inst, old.Visit, (byte)old.Slot, old.Head == 0 ? 0 : old.Head - 1).Frame());
                                Interlocked.Increment(ref resumes);
                            }
                        }
                        catch (Exception e)
                        {
                            lock (stormErrors) stormErrors.Add($"{m.Index}: {e.GetType().Name} {e.Message}");
                        }
                    }
                }
            }) { IsBackground = true, Name = "storm" };

            foreach (var w in workers) w.Start();
            barrierer.Start();
            storm.Start();
            await Task.Delay(TimeSpan.FromSeconds(3)); // warm-up
            var me = Process.GetCurrentProcess();
            me.Refresh();
            var clientCpu0 = me.TotalProcessorTime;
            long framesBefore = members.Sum(m => Interlocked.Read(ref m.FramesIn));
            long bytesBefore = members.Sum(m => Interlocked.Read(ref m.BytesIn));
            long presenceBefore = members.Sum(m => Interlocked.Read(ref m.PresenceIn));
            Command("mark");
            Expect("MARKED");
            var t0 = Stopwatch.GetTimestamp();
            Volatile.Write(ref measuring, true);
            await Task.Delay(TimeSpan.FromSeconds(seconds));
            Volatile.Write(ref measuring, false);
            Command("report");
            var report = Expect("REPORT");
            var wall = Ms(Stopwatch.GetTimestamp() - t0) / 1000.0;
            long frames = members.Sum(m => Interlocked.Read(ref m.FramesIn)) - framesBefore;
            long bytes = members.Sum(m => Interlocked.Read(ref m.BytesIn)) - bytesBefore;
            long presenceIn = members.Sum(m => Interlocked.Read(ref m.PresenceIn)) - presenceBefore;
            me.Refresh();
            var clientCpu = (me.TotalProcessorTime - clientCpu0).TotalSeconds / wall;
            Volatile.Write(ref stop, true);
            foreach (var w in workers) w.Join();
            barrierer.Join();
            storm.Join();

            var relayReport = JsonSerializer.Deserialize<Dictionary<string, JsonElement>>(report)!;
            foreach (var (k, v) in relayReport) result[k] = v;
            var counters = relayReport.TryGetValue("counters", out var c) ? c.Deserialize<Dictionary<string, long>>()! : [];
            var closed = members.Where(m => m.Closed is not null).Select(m => $"{m.Index}: {m.Closed}").ToList();
            var invalid = LoadGate.InvalidReasons(counters, Interlocked.Read(ref presenceSent), 0);
            if (closed.Count > 0) invalid.Add($"{closed.Count} connections closed ({string.Join("; ", closed.Take(3))})");
            if (Interlocked.Read(ref sendErrors) > 0) invalid.Add($"{sendErrors} send errors");
            lock (stormErrors)
                if (stormErrors.Count > 0) invalid.Add($"{stormErrors.Count} reconnects failed ({string.Join("; ", stormErrors.Take(3))})");
            result["valid"] = invalid.Count == 0;
            result["invalid_reasons"] = invalid;
            result["members"] = members.Count;
            result["seconds"] = Math.Round(wall, 1);
            result["client_cpu_cores"] = Math.Round(clientCpu, 3);
            result["presence_sent_per_second"] = Math.Round(Interlocked.Read(ref presenceSent) / wall);
            result["frames_in_per_second"] = Math.Round(frames / wall);
            result["bytes_in_per_second"] = Math.Round(bytes / wall);
            result["presence_in_per_second"] = Math.Round(presenceIn / wall);
            result["bytes_in_per_member_per_second"] = Math.Round(bytes / wall / members.Count);
            result["barriers_per_second"] = Math.Round(Interlocked.Read(ref barriersDone) / wall, 2);
            result["reconnects"] = Interlocked.Read(ref reconnects);
            result["resumes"] = Interlocked.Read(ref resumes);
            return result;
        }
        catch (Exception e)
        {
            result["valid"] = false;
            result["invalid_reasons"] = new List<string> { e.GetType().Name + ": " + e.Message };
            return result;
        }
        finally
        {
            await cts.CancelAsync();
            foreach (var m in members)
            {
                try
                {
                    m.Client?.Close();
                }
                catch (Exception)
                {
                }
            }
            try
            {
                Command("quit");
                if (!child.WaitForExit(10000)) child.Kill(); // (our own child, by its handle)
            }
            catch (Exception)
            {
            }
            child.Dispose();
        }
    }
}
