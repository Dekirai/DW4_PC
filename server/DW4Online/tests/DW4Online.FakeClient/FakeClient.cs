using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.FakeClient;

/// <summary>One scripted action of a fake client, at a time in seconds since it connected.</summary>
/// <param name="Do">"exit" (press the EXIT bit with Stage and Point), "choice" (Arg wait / alone at a boss hold), "bye" (leave the relay), "stop" (end the run).</param>
public sealed record ScriptAction(double At, string Do, string? Stage = null, byte Point = 0, string? Arg = null);

public sealed class FakeClientOptions
{
    public string Host { get; set; } = "127.0.0.1";
    public int Port { get; set; }
    public required Func<CancellationToken, Task<string>> Ticket { get; init; }
    /// <summary>The world relay speaks only protocol 49 (SEAMLESS.md 19.18).</summary>
    public int Protocol { get; set; } = Wire.WorldProtocol;
    public ulong DataHash { get; set; } = 1;
    public ulong SettingsHash { get; set; }
    public string Name { get; set; } = "fake";
    public byte Quest { get; set; }
    /// <summary>A real game's session context hash to report instead of the stand-in's (W29: a real host's instance).</summary>
    public ulong? Context { get; set; }
    public int Seed { get; set; } = 1;
    public List<ScriptAction> Script { get; set; } = [];
    /// <summary>Pads that encode their tick (fills are then exact to detect; the loss matrix).</summary>
    public bool UniquePads { get; set; }
    /// <summary>Steps per second while catching up (0 = as fast as it goes); reported in RATE and READY.</summary>
    public int ReplayRate { get; set; } = 3000;
    /// <summary>The stand-in engine's entry scene and departure (every client of a run must use the same values).</summary>
    public int EntryTicks { get; set; } = 120;
    public int DepartTicks { get; set; } = 120;
    public TimeSpan Duration { get; set; } = TimeSpan.FromSeconds(30);
    /// <summary>Receives the client's log as JSON lines (membership events, fingerprints, decisions, stats).</summary>
    public Action<string>? Log { get; set; }
}

/// <summary>Statistics of a fake client's run.</summary>
public sealed class FakeStats
{
    public long SealedExecuted { get; set; }
    public long Hitches { get; set; }
    public double StallMs { get; set; }
    public long OwnPadsSent { get; set; }
    public long OwnPadsSealed { get; set; }
    public long OwnPadsFilled { get; set; }
    public List<int> Delays { get; } = [];
    public double RttMedian { get; set; }
    public Dictionary<string, long> Frames { get; } = [];
    /// <summary>(instance, visit, tick) -> net fingerprint after that tick (every 60 ticks).</summary>
    public Dictionary<(uint Inst, uint Visit, uint Tick), ulong> Fingerprints { get; } = [];
    public List<string> Membership { get; } = [];
    public string? CloseReason { get; set; }
    public bool Ended { get; set; }
}

/// <summary>
/// A fake game client that speaks DW4R2 exactly as SEAMLESS.md 6.3 and its appendix specify: solo-led (own 59.94 Hz clock,
/// LOG every 100 ms), sealed (executes only sealed ticks with a one-tick playout buffer whose pace follows the server's by at
/// most 0.1 %, pads d ticks ahead), barriers with every decision, the fallback snapshot of a join, catch-up as an observer
/// with READY, JOIN_REQ / JOIN_AT, forks with SPLIT and REASSIGN, LEAD_FROM, DELAY, SNAPSHOT / SNAPHASH, NETFP, STATE,
/// PRESENCE and PONG. Single-threaded: a reader task only queues frames.
/// </summary>
public sealed class FakeClient
{
    private static readonly double Freq = Stopwatch.Frequency;
    private static readonly long TickTs = (long)(Wire.TickSeconds * Stopwatch.Frequency);

    private readonly FakeClientOptions _o;
    private readonly FakeEngine _engine = new();
    private readonly ConcurrentQueue<(byte Type, byte[] Payload, long At)> _inbox = new();
    private readonly HighResolutionTimer _timer = new();
    private readonly SortedDictionary<uint, (TickRecord Rec, long At)> _buffer = [];
    private readonly Dictionary<uint, Pad> _sent = [];
    private readonly Dictionary<uint, ulong> _fpHistory = [];
    private readonly List<ScriptAction> _script;
    private NetworkStream? _stream;
    private TcpClient? _tcp;
    private readonly SemaphoreSlim _write = new(1, 1);
    private long _start;

    private enum Phase { Connecting, Running, Parked, AwaitState, CatchingUp, Done }

    private Phase _phase = Phase.Connecting;
    private InstanceMode _mode;
    private uint _inst, _prevInst, _visit;
    private int _slot;
    private int _delay = 2;
    private int _member;
    private bool _replaying;

    // own clock (hub, solo-led)
    private long _clockStart;
    private uint _clockTick0;
    // solo-led LOG
    private readonly List<Pad> _logPads = [];
    private uint _logFirst;
    private long _lastLogFlush;
    // JOIN_REQ handling (solo leader)
    private uint? _joinAt;
    private Pad _lastPad;
    // sealed playout
    private bool _anchored;
    private long _anchor; // scheduled time of tick _anchorTick
    private uint _anchorTick;
    private readonly Queue<double> _offsets = new();
    private uint _nextPadTick;
    private bool _stalled;
    private uint? _leadFrom;
    // join / state transfer
    private byte[]? _stateBuf;
    private int _stateGot;
    private byte[]? _fallback;
    private bool _readySent;
    private bool _wasJoinable;
    private bool _wasSafe;
    private long _lastPresence;
    private string? _pendingExitStage;
    private bool _pendingGroup;
    private byte _pendingExitPoint;
    private string _choice = "wait";

    public FakeClient(FakeClientOptions options)
    {
        _o = options;
        _engine.Quest = options.Quest;
        _engine.ContextOverride = options.Context;
        _engine.EntryTicks = options.EntryTicks;
        _engine.DepartTicks = options.DepartTicks;
        _script = options.Script.OrderBy(a => a.At).ToList();
    }

    public FakeStats Stats { get; } = new();
    public uint Instance => _inst;
    public uint Visit => _visit;
    public int Slot => _slot;
    public InstanceMode Mode => _mode;
    public string Stage => _engine.Stage;
    public uint EngineTick => _engine.Tick;
    public byte Present => _engine.Present;
    public int MemberIndex => _member;
    public string Name => _o.Name;
    public bool Parked => _phase is Phase.Parked or Phase.AwaitState or Phase.CatchingUp;

    private void Log(string ev, params (string Key, object? Value)[] fields)
    {
        var d = new Dictionary<string, object?> { ["t"] = Math.Round(Ms(Stopwatch.GetTimestamp() - _start) / 1000.0, 3), ["client"] = _o.Name, ["ev"] = ev,
            ["inst"] = _inst, ["visit"] = _visit, ["stage"] = _engine.Stage, ["tick"] = _engine.Tick };
        foreach (var (k, v) in fields) d[k] = v;
        var line = JsonSerializer.Serialize(d);
        if (ev is "join_applied" or "leave_applied" or "fork" or "barrier" or "decision" or "lead_from" or "reassign") Stats.Membership.Add(line);
        _o.Log?.Invoke(line);
    }

    private static double Ms(long ticks) => ticks * 1000.0 / Freq;

    /// <summary>Connects, runs the script for the configured duration and returns the statistics.</summary>
    public async Task<FakeStats> RunAsync(CancellationToken ct)
    {
        _start = Stopwatch.GetTimestamp();
        var ticket = await _o.Ticket(ct);
        _tcp = new TcpClient { NoDelay = true };
        await _tcp.ConnectAsync(_o.Host, _o.Port, ct);
        _stream = _tcp.GetStream();
        var hello = FormattableString.Invariant($"{Wire.Magic} {ticket} {_o.Protocol} {_o.DataHash} {_o.SettingsHash:x16}\n");
        await _stream.WriteAsync(Encoding.ASCII.GetBytes(hello), ct);
        var line = await ReadLineAsync(_stream, ct);
        if (!line.StartsWith("OK ", StringComparison.Ordinal))
        {
            Stats.CloseReason = line;
            Stats.Ended = true;
            _tcp.Dispose();
            return Stats;
        }
        _member = int.Parse(line.Split(' ')[1], CultureInfo.InvariantCulture);
        Log("connected", ("member", _member));
        var reader = Task.Run(() => ReadLoopAsync(ct), CancellationToken.None);
        try
        {
            await Task.Run(() => Loop(ct), CancellationToken.None);
        }
        finally
        {
            try
            {
                if (_phase != Phase.Done) await SendAsync(new ByeMsg(0).Frame());
            }
            catch (Exception e) when (e is IOException or SocketException or ObjectDisposedException)
            {
            }
            await Task.Delay(50, CancellationToken.None);
            _tcp.Dispose();
            try
            {
                await reader;
            }
            catch (Exception e) when (e is IOException or SocketException or ObjectDisposedException or OperationCanceledException)
            {
            }
            Stats.RttMedian = _rtt.Count == 0 ? 0 : _rtt.OrderBy(x => x).ElementAt(_rtt.Count / 2);
            Stats.Ended = true;
            Log("end", ("hitches", Stats.Hitches), ("stall_ms", Math.Round(Stats.StallMs, 1)), ("filled", Stats.OwnPadsFilled), ("sealed", Stats.OwnPadsSealed));
        }
        return Stats;
    }

    private readonly List<double> _rtt = [];

    private async Task ReadLoopAsync(CancellationToken ct)
    {
        var header = new byte[Wire.HeaderBytes];
        try
        {
            while (!ct.IsCancellationRequested)
            {
                await _stream!.ReadExactlyAsync(header, ct);
                var len = (int)System.Buffers.Binary.BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(1));
                var payload = new byte[len];
                if (len > 0) await _stream.ReadExactlyAsync(payload, ct);
                _inbox.Enqueue((header[0], payload, Stopwatch.GetTimestamp()));
                _timer.Wake();
            }
        }
        catch (Exception e) when (e is IOException or SocketException or ObjectDisposedException or OperationCanceledException or EndOfStreamException)
        {
            _inbox.Enqueue((0, [], Stopwatch.GetTimestamp())); // connection gone
            _timer.Wake();
        }
    }

    private void Send(byte[] frame)
    {
        _write.Wait();
        try
        {
            _stream!.Write(frame);
        }
        catch (Exception e) when (e is IOException or SocketException or ObjectDisposedException)
        {
        }
        finally
        {
            _write.Release();
        }
    }

    private async Task SendAsync(byte[] frame)
    {
        await _write.WaitAsync();
        try
        {
            await _stream!.WriteAsync(frame);
        }
        finally
        {
            _write.Release();
        }
    }

    private void Loop(CancellationToken ct)
    {
        var end = _start + (long)(_o.Duration.TotalSeconds * Freq);
        while (_phase != Phase.Done && !ct.IsCancellationRequested)
        {
            var now = Stopwatch.GetTimestamp();
            if (now >= end) break;
            while (_inbox.TryDequeue(out var f))
            {
                if (f.Type == 0)
                {
                    _phase = Phase.Done;
                    Log("connection_lost");
                    break;
                }
                Stats.Frames[Msg.Name(f.Type)] = Stats.Frames.GetValueOrDefault(Msg.Name(f.Type)) + 1;
                Handle(f.Type, f.Payload, f.At);
            }
            if (_phase == Phase.Done) break;
            RunScript(now);
            var wake = Advance(Stopwatch.GetTimestamp());
            _timer.WaitUntil(Math.Min(wake, Stopwatch.GetTimestamp() + (long)(0.005 * Freq)));
        }
    }

    private void RunScript(long now)
    {
        var at = Ms(now - _start) / 1000.0;
        while (_script.Count > 0 && _script[0].At <= at)
        {
            var a = _script[0];
            _script.RemoveAt(0);
            Log("script", ("do", a.Do), ("to", a.Stage));
            switch (a.Do)
            {
                case "exit":
                case "group_exit":
                    _pendingExitStage = a.Stage;
                    _pendingExitPoint = a.Point;
                    _pendingGroup = a.Do == "group_exit";
                    break;
                case "choice":
                    _choice = a.Arg ?? "wait";
                    break;
                case "bye":
                case "stop":
                    _phase = Phase.Done;
                    break;
            }
        }
    }

    // Pads -----------------------------------------------------------------------------------------------------------------

    private Pad SamplePad(uint tick)
    {
        Pad p;
        if (_o.UniquePads)
        {
            // Buttons and sticks encode the tick (never the EXIT bit), so a fill is always visible.
            p = new Pad((ushort)((tick << 3) & 0xFFF8), (sbyte)(tick % 101 - 50), (sbyte)((tick / 101) % 101 - 50), (sbyte)(_o.Seed % 50), 0);
        }
        else
        {
            var x = FakeEngine.FnvMix((ulong)_o.Seed * 0x9E37UL, tick / 7);
            p = new Pad((ushort)(x & 0xFFF0), (sbyte)((int)((x >> 16) % 145) - 72), (sbyte)((int)((x >> 24) % 145) - 72),
                (sbyte)((int)((x >> 32) % 119) - 59), (sbyte)((int)((x >> 40) % 119) - 59));
        }
        if (_pendingExitStage is { } stage)
        {
            var index = Array.FindIndex(FakeEngine.Stages, s => string.Equals(s, stage, StringComparison.OrdinalIgnoreCase));
            p = new Pad((ushort)(p.Buttons | (_pendingGroup ? FakeEngine.GroupExitBit : FakeEngine.ExitBit)), (sbyte)Math.Max(0, index), (sbyte)_pendingExitPoint, p.Rx, p.Ry);
            _pendingExitStage = null;
            Log("exit_pressed", ("for_tick", tick), ("to", stage));
        }
        return p;
    }

    private static TickRecord Own(int slot, Pad pad)
    {
        var r = new TickRecord { Mask = (byte)(1 << slot) };
        r.Pads[slot] = pad;
        return r;
    }

    // Advancing ------------------------------------------------------------------------------------------------------------

    /// <summary>Runs whatever is due; returns when to look again.</summary>
    private long Advance(long now)
    {
        if (_phase == Phase.Running && _mode is InstanceMode.Hub or InstanceMode.SoloLed)
        {
            var due = _clockStart + (long)((_engine.Tick - _clockTick0) * Wire.TickSeconds * Freq);
            var guard = 0;
            while (now >= due && _phase == Phase.Running && _mode is InstanceMode.Hub or InstanceMode.SoloLed && guard++ < 8)
            {
                var t = _engine.Tick;
                Pad pad;
                if (_joinAt is not null)
                {
                    pad = _lastPad; // delayed sampling has begun: the solo ticks before J repeat the last pad
                    SendOwnPadsUpTo(t + (uint)_delay);
                }
                else pad = SamplePad(t);
                _lastPad = pad;
                _engine.Step(Own(_slot, pad));
                if (_mode == InstanceMode.SoloLed)
                {
                    if (_logPads.Count == 0) _logFirst = t;
                    _logPads.Add(pad);
                }
                AfterStep(t, live: true);
                if (_joinAt is { } j && _engine.Tick >= j && _phase == Phase.Running)
                {
                    FlushLog();
                    _mode = InstanceMode.Sealed;
                    _anchored = false;
                    _joinAt = null;
                    Log("sealed_from", ("tick", j));
                }
                due = _clockStart + (long)((_engine.Tick - _clockTick0) * Wire.TickSeconds * Freq);
            }
            if (_mode == InstanceMode.SoloLed && Ms(now - _lastLogFlush) >= 100) FlushLog();
            SendPresence(now);
            return due;
        }
        if (_phase == Phase.Running && _mode == InstanceMode.Sealed)
        {
            var next = now + (long)(0.005 * Freq);
            var guard = 0;
            while (_buffer.TryGetValue(_engine.Tick, out var item) && guard++ < 64)
            {
                var t = _engine.Tick;
                if (!_anchored)
                {
                    _anchored = true;
                    _anchorTick = t;
                    _anchor = item.At + TickTs; // one tick of playout buffer
                    _offsets.Clear();
                }
                var scheduled = _anchor + (long)((t - _anchorTick) * Wire.TickSeconds * Freq);
                if (now < scheduled)
                {
                    next = scheduled;
                    break;
                }
                _buffer.Remove(t);
                Track(t, item.At, scheduled);
                ExecuteSealed(t, item.Rec);
                if (_phase != Phase.Running || _mode != InstanceMode.Sealed) break;
            }
            if (!_buffer.ContainsKey(_engine.Tick) && _anchored)
            {
                var scheduled = _anchor + (long)((_engine.Tick - _anchorTick) * Wire.TickSeconds * Freq);
                if (now > scheduled + (long)(0.0005 * Freq) && !_stalled)
                {
                    _stalled = true;
                    ++Stats.Hitches;
                }
            }
            SendPresence(now);
            return next;
        }
        if (_phase == Phase.CatchingUp)
        {
            // An observer replays what came in as fast as its replay rate allows, then reports READY.
            if (_replayClock == 0) _replayClock = now;
            _replayBudget = Math.Min(_replayBudget + Ms(now - _replayClock) / 1000.0 * (_o.ReplayRate <= 0 ? 1e9 : _o.ReplayRate), 1e6);
            _replayClock = now;
            while (_replayBudget >= 1 && _buffer.Remove(_engine.Tick, out var item))
            {
                var t = _engine.Tick;
                _replayBudget -= 1;
                ExecuteSealed(t, item.Rec, observer: true);
                if (_phase != Phase.CatchingUp) break;
            }
            if (_phase == Phase.CatchingUp && !_readySent && _buffer.Count <= 10 && _engine.Tick > 0)
            {
                var t60 = (_engine.Tick - 1) / 60 * 60;
                _fpHistory.TryGetValue(t60, out var fp);
                Send(new ReadyMsg(_inst, _visit, t60, fp, (uint)Math.Max(0, _o.ReplayRate), FakeEngine.Carry(_o.Name)).Frame());
                _readySent = true;
                Log("ready", ("at", t60));
            }
            return now + (long)(0.005 * Freq);
        }
        return now + (long)(0.005 * Freq);
    }

    /// <summary>Playout: the anchor follows the observed arrival offsets (99th percentile + one tick), by at most 0.1 % of the pace.</summary>
    private void Track(uint t, long arrival, long scheduled)
    {
        if (arrival > scheduled + (long)(0.0005 * Freq))
        {
            if (!_stalled) ++Stats.Hitches;
            Stats.StallMs += Ms(arrival - Math.Max(scheduled, _lastExec));
            _stalled = true;
        }
        else _stalled = false;
        _lastExec = Math.Max(arrival, scheduled);
        var nominal = _anchor + (long)((t - _anchorTick) * Wire.TickSeconds * Freq) - TickTs;
        _offsets.Enqueue(Ms(arrival - nominal));
        while (_offsets.Count > 600) _offsets.Dequeue();
        if (_offsets.Count >= 60 && t % 30 == 0)
        {
            var sorted = _offsets.OrderBy(x => x).ToArray();
            var p99 = sorted[(int)(sorted.Length * 0.99)];
            // target: the 99th percentile arrival plus the one-tick buffer; move by at most 0.1 % of 30 ticks.
            var shift = Math.Clamp(p99, -0.5, 0.5);
            _anchor += (long)(shift / 1000.0 * Freq);
        }
    }

    private long _lastExec;
    private long _replayClock;
    private double _replayBudget;

    private void ExecuteSealed(uint t, TickRecord rec, bool observer = false)
    {
        if (!observer)
        {
            ++Stats.SealedExecuted;
            if ((rec.Mask & (1 << _slot)) != 0 && _sent.Remove(t, out var mine))
            {
                ++Stats.OwnPadsSealed;
                if (rec.Pads[_slot] != mine) ++Stats.OwnPadsFilled;
            }
            else if ((rec.Mask & (1 << _slot)) != 0 && (_engine.Present & (1 << _slot)) != 0)
            {
                ++Stats.OwnPadsSealed;
                ++Stats.OwnPadsFilled;
            }
        }
        foreach (var e in rec.Events)
        {
            if (e.Type == Ev.Delay && e.Payload[0] == _slot && !_replaying)
            {
                _delay = e.Payload[1];
                Stats.Delays.Add(_delay);
                Log("delay", ("d", _delay));
            }
            // LEAD_FROM is for the member that plays on (live), never for an observer replaying the history.
            if (e.Type == Ev.LeadFrom && !observer) _leadFrom = System.Buffers.Binary.BinaryPrimitives.ReadUInt32LittleEndian(e.Payload);
        }
        var wasMember = (_engine.Present & (1 << _slot)) != 0;
        _engine.Step(rec);
        AfterStep(t, live: !observer || wasMember);
        if (_phase == Phase.CatchingUp && (_engine.Present & (1 << _slot)) != 0 && !wasMember)
        {
            // Our own JOIN applied: from here this machine plays (pads from the next tick on).
            _phase = Phase.Running;
            _mode = InstanceMode.Sealed;
            _anchored = false;
            _nextPadTick = _engine.Tick;
            SendOwnPadsUpTo(_engine.Tick + (uint)_delay);
            Log("joined_live", ("slot", _slot));
            return;
        }
        if (!observer && _phase == Phase.Running && _mode == InstanceMode.Sealed) SendOwnPadsUpTo(t + (uint)_delay);
        if (_leadFrom is { } lead && _engine.Tick >= lead && _phase == Phase.Running)
        {
            _leadFrom = null;
            _mode = InstanceMode.SoloLed;
            _clockStart = Stopwatch.GetTimestamp();
            _clockTick0 = _engine.Tick;
            _lastLogFlush = _clockStart;
            Log("lead_from", ("from", lead));
        }
    }

    private void SendOwnPadsUpTo(uint target)
    {
        if (_nextPadTick > target) return; // the delay dropped: skip one sample
        var first = _nextPadTick;
        var pads = new List<Pad>();
        while (_nextPadTick <= target && pads.Count < InputMsg.MaxPads)
        {
            var p = SamplePad(_nextPadTick);
            _sent[_nextPadTick] = p;
            pads.Add(p);
            ++_nextPadTick;
        }
        if (pads.Count == 0) return;
        Stats.OwnPadsSent += pads.Count;
        Send(new InputMsg(_inst, _visit, first, pads.ToArray()).Frame());
    }

    private void FlushLog()
    {
        _lastLogFlush = Stopwatch.GetTimestamp();
        if (_logPads.Count == 0) return;
        for (var i = 0; i < _logPads.Count; i += LogMsg.MaxPads)
        {
            var n = Math.Min(LogMsg.MaxPads, _logPads.Count - i);
            Send(new LogMsg(_inst, _visit, _logFirst + (uint)i, _logPads.GetRange(i, n).ToArray()).Frame());
        }
        _logPads.Clear();
    }

    /// <summary>What a live step reports: joins / leaves, a fork (SPLIT, HEADING), the barrier (BARRIER), NETFP, STATE.</summary>
    private void AfterStep(uint t, bool live)
    {
        var (joined, left) = _engine.LastMembership;
        if (joined != 0) Log("join_applied", ("slots", joined), ("at", t));
        if (left != 0) Log("leave_applied", ("slots", left), ("at", t));
        if (_engine.LastFork is { } f)
        {
            Log("fork", ("at", f.Tick), ("leavers", f.Leavers), ("local_leaves", (f.Leavers & (1 << _slot)) != 0));
            if (live && _phase == Phase.Running)
            {
                Send(new SplitMsg(_inst, _visit, f.Tick, f.Leavers, SplitKind.Exit).Frame());
                if ((f.Leavers & (1 << _slot)) != 0 && _engine.Departure is { } dep) Send(new HeadingMsg(_inst, dep.Stage, dep.Point, _engine.ContextHash).Frame());
            }
        }
        else if (live && _phase == Phase.Running && _engine.Departure is { } dep && _engine.DepartLeft == _engine.DepartTicks)
            Send(new HeadingMsg(_inst, dep.Stage, dep.Point, _engine.ContextHash).Frame());
        if (_mode != InstanceMode.Hub && t % 60 == 0)
        {
            _fpHistory[t] = _engine.NetFingerprint;
            if (_fpHistory.Count > 200) _fpHistory.Remove(_fpHistory.Keys.Min());
            if (live && _phase == Phase.Running)
            {
                Stats.Fingerprints[(_inst, _visit, t)] = _engine.NetFingerprint;
                Send(new NetFpMsg(_inst, _visit, t, _engine.NetFingerprint).Frame());
            }
            else if (_phase == Phase.CatchingUp) Stats.Fingerprints[(_inst, _visit, t)] = _engine.NetFingerprint;
        }
        if (live && _phase == Phase.Running && _mode != InstanceMode.Hub && (_engine.Joinable != _wasJoinable || _engine.MembershipSafe != _wasSafe))
        {
            _wasJoinable = _engine.Joinable;
            _wasSafe = _engine.MembershipSafe;
            SendState(t);
        }
        if (_engine.Barrier is { } b && _phase == Phase.Running && live)
        {
            if (_mode == InstanceMode.SoloLed) FlushLog();
            _phase = Phase.Parked;
            _buffer.Clear();
            Send(new BarrierMsg(_inst, _visit, b.Tick, b.Stage, b.Point, _engine.Present, _engine.Present, _engine.ContextHash, FakeEngine.Carry(_o.Name)).Frame());
            Log("barrier", ("at", b.Tick), ("to", b.Stage), ("present", _engine.Present));
        }
    }

    private void SendState(uint t)
    {
        var flags = (byte)((_engine.Joinable ? StateMsg.Joinable : 0) | (_engine.MembershipSafe ? StateMsg.MembershipSafe : 0));
        Send(new StateMsg(_inst, _visit, t, flags, _engine.ContextHash, _engine.Joinable ? StateReason.None : StateReason.EntryScene).Frame());
    }

    private void SendPresence(long now)
    {
        if (Ms(now - _lastPresence) < 200) return;
        _lastPresence = now;
        Send(new PresenceMsg(_engine.Stage, _inst, _engine.Tick % 500, 0, _slot * 50, 0, 3, 12, 100, 120, 30, 40).Frame());
    }

    // Frames ---------------------------------------------------------------------------------------------------------------

    private void Handle(byte type, byte[] p, long at)
    {
        switch (type)
        {
            case Msg.Welcome:
            {
                var m = WelcomeMsg.Parse(p);
                _inst = m.Inst;
                _visit = m.Visit;
                _slot = m.Slot;
                _mode = m.Mode;
                _engine.LoadSolo(m.Stage);
                StartOwnClock();
                _phase = Phase.Running;
                Log("welcome", ("mode", m.Mode.ToString()), ("resumable", (m.Flags & WelcomeMsg.Resumable) != 0));
                Send(new RateMsg((uint)Math.Max(0, _o.ReplayRate)).Frame());
                break;
            }
            case Msg.Ping:
            {
                var m = PingMsg.Parse(p);
                Send(new PongMsg(m.Nonce, (ulong)(Ms(Stopwatch.GetTimestamp() - _start))).Frame());
                break;
            }
            case Msg.Sealed:
            case Msg.Live:
            {
                var m = TicksMsg.Parse(type, p);
                if (m.Inst != _inst && m.Inst != _prevInst) break;
                if (_phase is Phase.Parked or Phase.AwaitState) break; // ticks after the barrier (or before the state) are discarded
                for (var k = 0; k < m.Ticks.Count; ++k)
                {
                    var t = m.FirstTick + (uint)k;
                    if (t >= _engine.Tick) _buffer[t] = (m.Ticks[k], at);
                }
                break;
            }
            case Msg.BarrierWait:
            {
                var m = BarrierWaitMsg.Parse(p);
                Log("wait", ("reason", m.Reason.ToString()), ("names", m.Names), ("seconds", m.SecondsLeft));
                if ((m.Flags & BarrierWaitMsg.ChoiceNeeded) != 0)
                    Send(new BarrierChoiceMsg(_inst, _choice == "alone" ? BarrierChoiceMsg.Alone : BarrierChoiceMsg.Wait).Frame());
                break;
            }
            case Msg.Options:
            {
                // Round I1b: a held barrier's prompt (19.7.5) answered as DW4_ONLINE_AUTOCHOICE=first does: the first choosable row,
                // else New instance. (The fake client makes no plans; a refresh after the answer needs none.)
                var m = OptionsMsg.Parse(p);
                if (m.Plan != 0 || (m.Flags & OptionsMsg.Refresh) != 0) break;
                var row = m.Rows.FirstOrDefault(r => OptionStates.Choosable(r.State));
                Log("prompt", ("rows", m.Rows.Count), ("chosen", row?.Wid ?? 0u));
                Send(new ChoiceMsg(0, m.Inst, m.Visit, row is null ? ChoiceKind.New : ChoiceKind.Join, row?.Wid ?? 0, m.Visibility, 0,
                    ChoiceMsg.DefaultBackPoint).Frame());
                break;
            }
            case Msg.BarrierOk: OnBarrierOk(BarrierOkMsg.Parse(p)); break;
            case Msg.StateData: OnStateData(StateDataMsg.Parse(p)); break;
            case Msg.JoinReq:
            {
                var m = JoinReqMsg.Parse(p);
                if (m.Inst != _inst || _mode != InstanceMode.SoloLed || _phase != Phase.Running) break;
                _delay = Math.Max(1, (int)m.Delay);
                var j = _engine.Tick + (uint)_delay;
                _joinAt = j;
                _nextPadTick = j;
                Send(new JoinAtMsg(_inst, _visit, j).Frame());
                Log("join_at", ("j", j), ("slot", m.Slot));
                break;
            }
            case Msg.Reassign:
            {
                var m = ReassignMsg.Parse(p);
                if (m.OldInst != _inst) break;
                _prevInst = _inst;
                _inst = m.NewInst;
                Log("reassign", ("from", m.OldInst), ("to", m.NewInst), ("from_tick", m.FromTick));
                break;
            }
            case Msg.Resync:
                _phase = Phase.AwaitState;
                Log("resync");
                break;
            case Msg.Close:
            {
                var m = CloseMsg.Parse(p);
                Stats.CloseReason = m.Reason + ": " + m.Text;
                Log("closed", ("reason", m.Reason.ToString()), ("text", m.Text));
                _phase = Phase.Done;
                break;
            }
            case Msg.Room:
            case Msg.Roster:
            case Msg.PresenceOut:
            case Msg.StoryAck:
                break;
        }
    }

    private void StartOwnClock()
    {
        _clockStart = Stopwatch.GetTimestamp();
        _clockTick0 = _engine.Tick;
        _lastLogFlush = _clockStart;
        _logPads.Clear();
    }

    private void OnBarrierOk(BarrierOkMsg m)
    {
        if (_phase != Phase.Parked && _phase != Phase.CatchingUp) return;
        Log("decision", ("decision", m.Decision.ToString()), ("to", m.Stage), ("new_inst", m.NewInst), ("new_visit", m.NewVisit), ("slot", m.OwnSlot),
            ("mode", m.Mode.ToString()), ("co_arrivals", m.CoArrivals.Count), ("notice", m.Notice.ToString()));
        _prevInst = _inst;
        _inst = m.NewInst;
        _visit = m.NewVisit;
        _delay = Math.Max(1, (int)m.Delay);
        _buffer.Clear();
        _sent.Clear();
        _readySent = false;
        _wasJoinable = false;
        _wasSafe = false;
        switch (m.Decision)
        {
            case BarrierDecision.Dissolve:
                _slot = 0;
                _mode = InstanceMode.Hub;
                _engine.LocalSlot = 0;
                _engine.LoadSolo(m.Stage);
                StartOwnClock();
                _phase = Phase.Running;
                break;
            case BarrierDecision.New:
            {
                if (_phase == Phase.CatchingUp && _fallback is not null)
                {
                    // Fallback: the own engine as it parked before the join, loaded alone (the server's map: own slot -> 0).
                    _engine.Restore(_fallback);
                    _engine.Load(m.Stage, m.Point, m.SlotMap, []);
                }
                else _engine.Load(m.Stage, m.Point, m.SlotMap, m.CoArrivals.Select(c => (int)c.Slot));
                _slot = m.OwnSlot;
                _engine.LocalSlot = _slot;
                _mode = m.Mode;
                var lowest = m.SlotMap.Where(s => s >= 0).Select(s => (int)s).DefaultIfEmpty(_slot).Min();
                if (_phase == Phase.CatchingUp) lowest = _slot;
                if (lowest == _slot) UploadSnapshot();
                Send(new SnapHashMsg(_inst, _visit, _engine.CanonicalHash()).Frame());
                _phase = Phase.Running;
                if (_mode == InstanceMode.SoloLed) StartOwnClock();
                else
                {
                    _anchored = false;
                    _nextPadTick = 0;
                    SendOwnPadsUpTo((uint)_delay - 1);
                }
                break;
            }
            case BarrierDecision.CoArrive:
                _slot = m.OwnSlot;
                _engine.LocalSlot = _slot;
                _mode = InstanceMode.Sealed;
                _phase = Phase.AwaitState;
                break;
            case BarrierDecision.Join:
            {
                // Keep the own load snapshot as the fallback (SEAMLESS.md 6.6), then wait for the target's state.
                _fallback = _engine.Snapshot(canonical: false);
                _slot = m.OwnSlot;
                _engine.LocalSlot = _slot;
                _mode = m.Mode;
                _phase = Phase.AwaitState;
                break;
            }
        }
    }

    private void UploadSnapshot()
    {
        var snap = _engine.Snapshot(canonical: false);
        for (var off = 0; off < snap.Length; off += Wire.MaxChunkBytes)
        {
            var n = Math.Min(Wire.MaxChunkBytes, snap.Length - off);
            Send(new SnapshotMsg(_inst, _visit, (uint)snap.Length, (uint)off, snap.AsSpan(off, n).ToArray()).Frame());
        }
    }

    private void OnStateData(StateDataMsg m)
    {
        if (m.Inst != _inst) return;
        if (m.Offset == 0 || _stateBuf is null || _stateBuf.Length != m.Total)
        {
            _stateBuf = new byte[m.Total];
            _stateGot = 0;
        }
        m.Chunk.CopyTo(_stateBuf, (int)m.Offset);
        _stateGot += m.Chunk.Length;
        if (_stateGot < m.Total) return;
        var blob = StateBlob.Parse(_stateBuf);
        _stateBuf = null;
        _engine.Restore(blob.Snapshot);
        _engine.LocalSlot = _slot;
        foreach (var f in blob.Forks) _engine.ForkSides[f.Tick] = f.Side;
        Log("state", ("purpose", blob.Purpose.ToString()), ("ticks", blob.Ticks.Count), ("forks", blob.Forks.Count));
        if (blob.Purpose == StatePurpose.CoArrival)
        {
            Send(new SnapHashMsg(_inst, _visit, _engine.CanonicalHash()).Frame());
            _phase = Phase.Running;
            _anchored = false;
            _nextPadTick = 0;
            SendOwnPadsUpTo((uint)_delay - 1);
            return;
        }
        // Join or resync: replay as an observer (no SPLIT, no barrier messages) at the replay rate, then READY (join) or
        // play on (resync); the blob's ticks join the LIVE ones in the buffer.
        for (var k = 0; k < blob.Ticks.Count; ++k) _buffer[_engine.Tick + (uint)k] = (blob.Ticks[k], Stopwatch.GetTimestamp());
        _replayClock = 0;
        _replayBudget = 0;
        if (_o.ReplayRate <= 0 || blob.Purpose == StatePurpose.Resync)
        {
            _replaying = true;
            while (_buffer.Remove(_engine.Tick, out var item)) ExecuteSealed(_engine.Tick, item.Rec, observer: true);
            _replaying = false;
        }
        if (blob.Purpose == StatePurpose.Resync || (_engine.Present & (1 << _slot)) != 0)
        {
            _phase = Phase.Running;
            _mode = InstanceMode.Sealed;
            _anchored = false;
            _nextPadTick = Math.Max(_nextPadTick, _engine.Tick);
            return;
        }
        _phase = Phase.CatchingUp;
    }

    private static async Task<string> ReadLineAsync(NetworkStream stream, CancellationToken ct)
    {
        var sb = new StringBuilder();
        var one = new byte[1];
        while (sb.Length < 200)
        {
            if (await stream.ReadAsync(one, ct) == 0) break;
            if (one[0] == '\n') break;
            sb.Append((char)one[0]);
        }
        return sb.ToString().TrimEnd('\r');
    }
}
