using System.Diagnostics;
using System.Numerics;
using DW4Online.Domain.Rooms;
using DW4Online.Domain.Story;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;
using Microsoft.Extensions.Logging;

namespace DW4Online.Relay.Hub;

/// <summary>Receives the membership events of a room (diagnostics, the online test stack).</summary>
public interface IMembershipLog
{
    void Write(string room, IReadOnlyDictionary<string, object?> entry);
}

/// <summary>
/// The relay side of the world (SEAMLESS.md 3, 6, 19): its members' connections, the stage groups (solo-led and sealed
/// lockstep groups; "instances" in sections 3 and 6), server-paced sealing, events, barrier decisions, forks, the snapshot and
/// log store and state delivery, and the user's instances (World/WorldHub.cs). One hub for the whole server, generalized from
/// round S1's room hub and not renamed (19.17.4). The server never simulates. Every public method runs under
/// <c>lock (hub.Sync)</c>; sending only queues frames.
/// </summary>
public sealed partial class RoomHub
{
    private static readonly double Freq = Stopwatch.Frequency;
    private readonly RelayOptions _relay;
    private readonly SealingOptions _sealing;
    private readonly InstanceOptions _inst;
    private readonly ILogger _log;
    private readonly IMembershipLog? _mlog;
    private readonly Action<StateJob>? _stateRunner;
    private long _lastFaultLog;
    private readonly Dictionary<int, HubMember> _members = [];
    private readonly Dictionary<uint, HubInstance> _instances = [];
    private readonly List<ArrivalGroup> _holds = [];
    private uint _nextInstance = 1;
    private uint _pingNonce;
    private long _lastPing;

    /// <param name="stateRunner">Builds state transfers outside the hub lock (the relay: on the thread pool, then
    /// <see cref="FinishState"/> under the lock); null builds them at once (the tests).</param>
    public RoomHub(RoomInfo info, RelayOptions relay, SealingOptions sealing, InstanceOptions instances, ILogger log, IMembershipLog? mlog = null,
        Action<StateJob>? stateRunner = null, WorldHubOptions? world = null)
    {
        Info = info;
        _relay = relay;
        _sealing = sealing;
        _inst = instances;
        _log = log;
        _mlog = mlog;
        _stateRunner = stateRunner;
        _world = world ?? new WorldHubOptions();
    }

    /// <summary>Saves a keeper's new revision as its owner's online story (the account, a copy, whether only counters changed); set by
    /// the relay (it only queues: the hub lock is held).</summary>
    public Action<long, RoomStoryData, bool>? StorySaver { get; set; }

    /// <summary>The views as the last housekeeping pass saw them (the room API's answer while the room is busy).</summary>
    public IReadOnlyList<InstanceView> CachedViews { get; private set; } = [];

    /// <summary>A fault in the room logic: counted, logged at most every ten seconds; the room goes on.</summary>
    public void Fault(Exception e, string what)
    {
        Count("faults");
        var now = Stopwatch.GetTimestamp();
        if (_lastFaultLog != 0 && Ms(now - _lastFaultLog) < 10_000) return;
        _lastFaultLog = now;
        _log.LogError(e, "Room {Room}: {What} failed ({Count} faults so far)", Code, what, Counters.GetValueOrDefault("faults"));
    }

    public object Sync => this;
    /// <summary>The relay dropped this hub from its rooms (empty, or the room closed): a new connection takes a fresh one.</summary>
    public bool Removed { get; set; }
    public RoomInfo Info { get; set; }
    public string Code => Info.Code;
    public bool Empty => _members.Values.All(m => !m.Connected) && _instances.Count == 0;
    public IReadOnlyCollection<HubMember> Members => _members.Values;
    public HubInstance? Instance(uint id) => _instances.GetValueOrDefault(id);
    public IReadOnlyCollection<HubInstance> Instances => _instances.Values;
    /// <summary>Counters for the tests and the diagnostics.</summary>
    public Dictionary<string, long> Counters { get; } = [];
    /// <summary>How late this hub's sealing passes come (diagnostics and the load gate, SEAMLESS.md 19.17.4).</summary>
    public SealLateness SealLateness { get; } = new();

    private int Capacity => Math.Clamp(_inst.PlayersPerInstance, 1, Wire.MaxSlots);
    private uint MaxReplayRate => (uint)Math.Max(1, _inst.MaxReplayRate);
    private uint MaxVisitTicks => (uint)Math.Clamp(_inst.MaxVisitHours * 3600.0 / Wire.TickSeconds, 1, uint.MaxValue - 1);
    private static double Ms(long ticks) => ticks * 1000.0 / Freq;
    private static long FromMs(double ms) => (long)(ms * Freq / 1000.0);
    private static long TickTs => (long)(Wire.TickSeconds * Freq);
    private long Due(HubInstance i, uint tick) => i.S0 + (long)(tick * Wire.TickSeconds * Freq);

    private void Count(string key, long by = 1) => Counters[key] = Counters.GetValueOrDefault(key) + by;

    private void Note(string ev, HubInstance? i, params (string Key, object? Value)[] fields)
    {
        if (_mlog is null) return;
        var d = new Dictionary<string, object?> { ["ev"] = ev };
        if (i is not null)
        {
            d["inst"] = i.Id;
            d["visit"] = i.Visit;
            d["stage"] = i.Stage;
        }
        foreach (var (k, v) in fields) d[k] = v;
        _mlog.Write(Code, d);
    }

    // Connections -------------------------------------------------------------------------------------------------------

    /// <summary>
    /// The server is about to lose the member's place (a reconnect without resuming, a lost connection in a lobby or at a
    /// barrier): its client plays on in its stage alone, or goes on as NEW into its barrier's target. Its STORY counts
    /// against that stage until the server places it again (8.7).
    /// </summary>
    private static void MarkLone(HubMember m)
    {
        if (m.Arrival is { } parked)
        {
            m.LoneStage = parked.Stage;
            m.LoneSince = parked.ParkedAt;
            m.LoneKey = 1UL << 63 | (ulong)parked.ParkedAt;
        }
        else if (m.Instance is { Ended: false } was && !string.IsNullOrEmpty(was.Stage) && m.Slot >= 0)
        {
            m.LoneStage = was.Stage;
            m.LoneSince = Math.Max(m.InstanceSince, was.VisitStartedAt);
            // The visit goes on alone: it keeps its key, so what it gave (one phase step, one key item) stays given (a
            // reconnect without resuming made a fresh visit of it before).
            m.LoneKey = (ulong)was.Id << 32 | was.Visit;
        }
    }

    /// <summary>
    /// A member's connection closed (its slot stays filled for Relay:ReconnectSeconds in a logged stage group; a host's for his
    /// grace, 19.6.7). BYE (graceful) is quitting: the member leaves the world (a hosted instance ends, host_left).
    /// </summary>
    public void Disconnect(HubMember m, IHubPeer peer, long now, bool graceful = false)
    {
        if (m.Peer != peer) return;
        m.Peer = null;
        m.DisconnectedAt = now;
        Note("disconnect", m.Instance, ("member", m.Id), ("account", m.AccountId), ("graceful", graceful), ("wid", m.World?.Wid ?? 0));
        if (graceful)
        {
            LeaveWorld(m, "left", now);
            return;
        }
        if (m.World is { } w)
        {
            MarkInfoDirty(w);
            if (w.Host == m) StartGrace(w, now, "closed");
        }
        if (m.Joining is not null || m.Arrival is not null || m.Instance is null || m.Instance.Mode == InstanceMode.Hub || m.Instance.Barrier is not null)
        {
            // (A lobby or a parked barrier: the client plays on there alone, its STORY after the reconnect counts there. A host at
            // a barrier: the barrier is decided without him, he keeps his seat and his grace; back in time, his parked barrier is
            // decided again inside his instance, 19.6.7.)
            MarkLone(m);
            DetachLocked(m, now, leaveNow: true);
        }
        else if (m.Instance is { Mode: InstanceMode.Sealed, Sealing: false, Barrier: null, Ended: false } waiting)
            TryStartSealing(waiting, now); // (its hash is not awaited from a gone member; the reconnect window runs from tick 0)
    }

    /// <summary>The member left (a staff lock, a deletion): its connection closes, it leaves the world at once.</summary>
    public void RemoveMember(long accountId, CloseReason reason, string text, long now)
    {
        if (!_byAccount.TryGetValue(accountId, out var m)) return;
        m.Peer?.Close(reason, text);
        m.Peer = null;
        LeaveWorld(m, "left", now);
    }

    /// <summary>The server stops: every instance ends (server_stop), every connection gets CLOSE; nothing of the world survives the
    /// process (a client in a field takes the home departure itself, 19.6.10).</summary>
    public void CloseAll(CloseReason reason, string text)
    {
        var now = Stopwatch.GetTimestamp();
        foreach (var w in _worlds.Values.ToList())
            if (!w.Ending)
            {
                FlushVisibility(w);
                w.Ending = true;
                w.EndReason = EndReason.ServerStop;
                History?.Invoke(new HistoryEnd(w, EndReasons.Text(EndReason.ServerStop), w.PeakPlayers, UtcNow));
                foreach (var (m, seat) in w.Seats) History?.Invoke(new HistoryLeft(w, m.AccountId, seat.SinceUtc, "ended", UtcNow));
            }
        foreach (var m in _members.Values.ToList())
        {
            m.Peer?.Close(reason, text);
            m.Peer = null;
        }
        _members.Clear();
        _byAccount.Clear();
        _instances.Clear();
        _holds.Clear();
        _worlds.Clear();
        _byArea.Clear();
        Note("stopped", null, ("at", now));
    }

    /// <summary>
    /// Takes a member out of whatever it is in: a parked group, a join, its instance (sealed: LEAVE at a future tick;
    /// solo-led and hub: the instance ends).
    /// </summary>
    private void DetachLocked(HubMember m, long now, bool leaveNow)
    {
        if (m.Arrival is { } a)
        {
            a.Members.RemoveAll(x => x.Member == m);
            foreach (var c in a.CoArrivals) c.Members.RemoveAll(x => x.Member == m);
            m.Arrival = null;
            if (a.Members.Count == 0)
            {
                _holds.Remove(a);
                foreach (var c in a.CoArrivals)
                    if (c.Members.Count > 0) Decide(c, now);
            }
        }
        if (m.Joining is { } j)
        {
            j.Target.Observers.Remove(m);
            if (j.Target.Reserved[j.Slot] == m) j.Target.Reserved[j.Slot] = null;
            if (j.Target.SoloJoin?.Joiner == m) j.Target.SoloJoin = null;
            m.Joining = null;
        }
        if (m.Instance is { } i && !i.Ended && m.Slot >= 0 && i.Slots[m.Slot] == m)
        {
            if (i.Barrier is { } b) b.Remove(m.Slot);
            if (i.Mode == InstanceMode.Sealed && i.Barrier is null && leaveNow)
            {
                // Sealing or not yet: every engine of the visit has this member (a co-arrival was spawned in the load
                // step), so it leaves by a LEAVE event, and a member left alone leads on its own (LEAD_FROM).
                var unsealed = !i.Sealing;
                StampLeave(i, m.Slot, now);
                if (unsealed && !i.Ended) TryStartSealing(i, now);
            }
            else
            {
                i.Slots[m.Slot] = null;
                if (i.Streams[m.Slot] is { } s) s.UntilTick = i.NextSeal;
                if (i.Count == 0) EndInstance(i, now);
                else if (i.Barrier is not null)
                {
                    // The others may all have reported already: they park now, not after the report wait.
                    m.Instance = null;
                    m.Slot = -1;
                    CheckBarrier(i, now);
                }
                else TryStartSealing(i, now);
            }
        }
        m.Instance = null;
        m.Slot = -1;
    }

    // Receive ------------------------------------------------------------------------------------------------------------

    /// <summary>Handles one frame of a member; a <see cref="ProtocolException"/> closes the connection.</summary>
    public void Receive(HubMember m, byte type, ReadOnlySpan<byte> p, long now)
    {
        m.LastHeard = now;
        switch (type)
        {
            case Msg.Input: OnInput(m, InputMsg.Parse(p), now); break;
            case Msg.Log: OnLog(m, LogMsg.Parse(p), now); break;
            case Msg.Heading: OnHeading(m, HeadingMsg.Parse(p), now); break;
            case Msg.Barrier: OnBarrier(m, BarrierMsg.Parse(p), now); break;
            case Msg.BarrierChoice: OnChoice(m, BarrierChoiceMsg.Parse(p), now); break;
            case Msg.Split: OnSplit(m, SplitMsg.Parse(p), now); break;
            case Msg.Snapshot: OnSnapshot(m, SnapshotMsg.Parse(p), now); break;
            case Msg.SnapHash: OnSnapHash(m, SnapHashMsg.Parse(p), now); break;
            case Msg.Ready: OnReady(m, ReadyMsg.Parse(p), now); break;
            case Msg.JoinAt: OnJoinAt(m, JoinAtMsg.Parse(p), now); break;
            case Msg.NetFp: OnNetFp(m, NetFpMsg.Parse(p), now); break;
            case Msg.State: OnState(m, StateMsg.Parse(p), now); break;
            case Msg.Intent: OnIntent(m, IntentMsg.Parse(p), now); break;
            case Msg.Story: OnStory(m, StoryMsg.Parse(p), now); break;
            case Msg.Presence: OnPresence(m, PresenceMsg.Parse(p), now); break;
            case Msg.Presence2:
                // (Any version stays within MaxBytes: a larger one is dropped unread.)
                if (p.Length > Presence2Msg.MaxBytes) Count("presence_oversize");
                else OnPresence2(m, Presence2Msg.Parse(p), now);
                break;
            case Msg.Resume: OnResume(m, ResumeMsg.Parse(p), now); break;
            case Msg.Rate: m.Rate = Math.Min(RateMsg.Parse(p).StepsPerSecond, MaxReplayRate); break;
            case Msg.Pong: OnPong(m, PongMsg.Parse(p), now); break;
            case Msg.StateHash: OnStateHash(m, StateHashMsg.Parse(p), now); break;
            case Msg.Checkpoint: OnCheckpoint(m, CheckpointMsg.Parse(p), now); break;
            case Msg.CheckpointBad: OnCheckpointBad(m, CheckpointBadMsg.Parse(p), now); break;
            case Msg.InstanceOp: OnInstanceOp(m, InstanceOpMsg.Parse(p), now); break;
            case Msg.Report: OnReport(m, ReportMsg.Parse(p), now); break;
            case Msg.Bye: ByeMsg.Parse(p); break; // the connection layer closes it gracefully
            default: throw new ProtocolException("unknown frame type " + Msg.Name(type));
        }
    }

    /// <summary>The member's instance when the frame names it (or the id it had before a fork); else null (stale, counted).</summary>
    private HubInstance? Mine(HubMember m, uint inst, uint visit)
    {
        if (m.Instance is { Ended: false } i && (i.Id == inst || m.PreviousInstanceId == inst) && i.Visit == visit && m.Slot >= 0) return i;
        Count("stale_frames");
        return null;
    }

    private void OnInput(HubMember m, InputMsg msg, long now)
    {
        Heard(m, now);
        if (Mine(m, msg.Inst, msg.Visit) is not { Mode: InstanceMode.Sealed } i || i.Streams[m.Slot] is not { } s) return;
        for (var k = 0; k < msg.Pads.Length; ++k)
        {
            var tick = msg.FirstTick + (uint)k;
            var margin = i.Sealing || i.S0 != 0 ? Ms(Due(i, tick) - now) : 1000.0;
            switch (s.Offer(tick, msg.Pads[k], i.NextSeal, margin, Ms(now)))
            {
                case SlotStream.Submit.Late: Count("late_pads"); break;
                case SlotStream.Submit.TooEarly: Count("early_pads"); break;
            }
        }
    }

    private void OnLog(HubMember m, LogMsg msg, long now)
    {
        // A solo-led leader's LOG, also for the ticks before J after JOIN_AT made the instance sealed from J.
        if (Mine(m, msg.Inst, msg.Visit) is not { } i || i.Barrier is not null) return;
        if (i.Mode != InstanceMode.SoloLed && !(i.Mode == InstanceMode.Sealed && i.Log.Count < i.SealedFrom)) return;
        // Only the member that leads the solo stretch logs it (a joiner's LOG for the ticks before J would put its pads
        // and slot into the leader's history).
        if ((i.Mode == InstanceMode.SoloLed ? i.Leader : i.LogOwner) != m)
        {
            Count("log_foreign");
            return;
        }
        if (i.LogFull) return;
        var next = i.Log.Count;
        var first = msg.FirstTick;
        if (first > next) throw new ProtocolException("LOG skips ticks");
        var end = first + (ulong)msg.Pads.Length;
        if (end > next)
        {
            // A solo member steps at the game's pace: its log cannot run ahead of the wall clock since the visit began.
            var allowed = Ms(now - i.VisitStartedAt) / 1000.0 / Wire.TickSeconds * 1.02 + _inst.LogLeadTicks;
            if (end > allowed) throw new ProtocolException("LOG ahead of the game's pace");
            if (end > MaxVisitTicks)
            {
                i.LogFull = true;
                i.Joinable = false;
                Count("log_full");
                Note("log_full", i, ("ticks", next));
                return;
            }
        }
        var mask = (byte)(1 << m.Slot);
        var appended = false;
        for (var k = 0; k < msg.Pads.Length; ++k)
        {
            var tick = first + (uint)k;
            if (tick < next) continue; // a resent overlap
            if (i.SoloJoin is null && i.Ahead.Count > 0 && tick >= i.Ahead.Keys.First()) break;
            var rec = new TickRecord { Mask = mask };
            rec.Pads[m.Slot] = msg.Pads[k];
            i.Log.Append(rec.ToBytes());
            appended = true;
        }
        if (appended)
        {
            i.LastLogAt = now;
            FlushAhead(i);
            PumpAll(i);
            if (i.SoloPending && i.StreamEnd >= i.NextSeal)
            {
                i.SoloPending = false;
                i.Mode = InstanceMode.SoloLed;
                Note("lead_from", i, ("tick", i.NextSeal));
            }
        }
    }

    private static void FlushAhead(HubInstance i)
    {
        while (i.Ahead.Remove(i.Log.Count, out var rec)) i.Log.Append(rec);
    }

    private void OnHeading(HubMember m, HeadingMsg msg, long now)
    {
        m.Heading = new Heading(msg.Stage, msg.Point, msg.ContextHash, now);
        Note("heading", m.Instance, ("member", m.Index), ("to", msg.Stage), ("point", msg.Point));
    }

    /// <summary>PRESENCE2 a member may send per second on average (ten while its figure moves) and at once.</summary>
    public const double PresenceRate = 12;
    public const double PresenceBurst = 3;

    private void OnPresence(HubMember m, PresenceMsg msg, long now)
    {
        if (m.PresenceAt != 0 && Ms(now - m.PresenceAt) < 150)
        {
            Count("presence_dropped");
            return;
        }
        // (The bounds of PRESENCE2: a PRESENCE drives a ghost figure too.)
        if (!Plausible(msg)) return;
        FanPresence(m, msg.Clamped(), null, now);
    }

    private bool Plausible(PresenceMsg msg)
    {
        if (msg.Implausible() is not { } why) return true;
        Count("presence_implausible");
        Count("presence_implausible_" + why);
        return false;
    }

    private void OnPresence2(HubMember m, Presence2Msg msg, long now)
    {
        // (Sent PRESENCE2 from now on, also when this one is dropped.)
        m.PresenceV2 = true;
        // A later version: only its version 1 part was read (its appended bytes skipped) and that part alone goes on,
        // checked below and written at version 1 (Presence2Msg.Write), with the flags version 1 knows; so the hub never
        // forwards what it cannot check, and the member's tag and figure still show.
        if (msg.Version != Presence2Msg.CurrentVersion)
        {
            Count("presence_version");
            msg = msg with { Version = Presence2Msg.CurrentVersion, Flags = (byte)(msg.Flags & Presence2Msg.KnownFlags) };
        }
        if (msg.Implausible() is { } why)
        {
            Count("presence_implausible");
            Count("presence_implausible_" + why);
            return;
        }
        msg = msg with { Presence = msg.Presence.Clamped() };
        // The token bucket: about ten a second while a figure moves, five while it stands; more is dropped (counted).
        m.PresenceTokens = m.PresenceTokensAt == 0
            ? PresenceBurst
            : Math.Min(PresenceBurst, m.PresenceTokens + Ms(now - m.PresenceTokensAt) / 1000.0 * PresenceRate);
        m.PresenceTokensAt = now;
        if (m.PresenceTokens < 1)
        {
            Count("presence_dropped");
            return;
        }
        m.PresenceTokens -= 1;
        FanPresence(m, msg.Presence, msg, now);
    }

    /// <summary>A member's presence (19.13): server-wide in the hub stages by the selection, in a field to the own instance's members
    /// (World/WorldHub.cs, FanWorldPresence).</summary>
    private void FanPresence(HubMember m, PresenceMsg msg, Presence2Msg? msg2, long now) => FanWorldPresence(m, msg, msg2, now);

    private void OnPong(HubMember m, PongMsg msg, long now)
    {
        if (msg.Nonce != m.PingNonce || m.PingSentAt == 0) return;
        m.Rtt.Add(Ms(now - m.PingSentAt));
        m.PingSentAt = 0;
        Heard(m, now);
    }

    /// <summary>INPUT or PONG from the member: a host silent in his grace (an open connection, 19.6.7) is back.</summary>
    private void Heard(HubMember m, long now)
    {
        m.LastInputOrPong = now;
        if (m.World is { HostAway: true } w && w.Host == m && m.Connected) HostBack(w, now);
    }

    // Sealing ------------------------------------------------------------------------------------------------------------

    /// <summary>
    /// Seals every due tick of the room's sealed instances (the dedicated timer thread calls it); returns the next due
    /// time, or long.MaxValue when nothing is sealed.
    /// </summary>
    public long Seal(long now)
    {
        var next = long.MaxValue;
        foreach (var i in _instances.Values.ToList())
        {
            if (i.Mode != InstanceMode.Sealed || !i.Sealing || i.Ended) continue;
            try
            {
                next = Math.Min(next, SealInstance(i, now));
            }
            catch (Exception e)
            {
                // One instance's fault never stops the room's other instances (logged at most every ten seconds).
                Fault(e, "sealing instance " + i.Id);
            }
        }
        return next;
    }

    private long SealInstance(HubInstance i, long now)
    {
        var next = long.MaxValue;
        {
            if (i.Sealing && Due(i, i.NextSeal) <= now && (i.SealStop is not { } held || i.NextSeal <= held)) SealLateness.Add(now - Due(i, i.NextSeal));
            var batch = new List<byte[]>();
            while (i.Sealing && Due(i, i.NextSeal) <= now)
            {
                if (i.SealStop is { } stop && i.NextSeal > stop) break;
                batch.Add(SealOne(i, now));
                if (i.LeadFrom is { } lead && i.NextSeal >= lead)
                {
                    // The remaining member leads alone from here (LEAD_FROM was in the last sealed tick, which goes out first).
                    PumpAll(i);
                    batch.Clear();
                    i.Sealing = false;
                    i.LeadFrom = null;
                    i.LastLogAt = now;
                    Array.Clear(i.Streams);
                    // A HOME stamped beyond the lead tick would never be sealed now (an END's HOME stamped World:EndNoticeTicks
                    // ahead, then the lost or leaving host's LEAVE at an earlier tick): it goes to the remaining member through
                    // JOIN_REQ home instead (W7b).
                    RefileHomesAfterLead(i, i.NextSeal, now);
                    if (i.StreamEnd >= i.NextSeal)
                    {
                        i.Mode = InstanceMode.SoloLed;
                        Note("lead_from", i, ("tick", i.NextSeal));
                    }
                    else
                    {
                        // The sealed ticks still wait for the leader's LOG before J (a JOIN_AT whose joiner went): the
                        // instance turns solo-led once they went out (OnLog).
                        i.SoloPending = true;
                        Note("lead_from_pending", i, ("tick", i.NextSeal), ("log", i.Log.Count));
                    }
                }
            }
            if (batch.Count > 0)
            {
                if (batch.Count > 1) Count("sealed_batches");
                PumpAll(i);
            }
            if (i.Sealing && i.Mode == InstanceMode.Sealed && (i.SealStop is null || i.NextSeal <= i.SealStop)) next = Math.Min(next, Due(i, i.NextSeal));
        }
        return next;
    }

    private byte[] SealOne(HubInstance i, long now)
    {
        var t = i.NextSeal;
        byte mask = 0;
        var rec = new TickRecord();
        for (var s = 0; s < Wire.MaxSlots; ++s)
        {
            if (i.Streams[s] is not { } st || !st.Carries(t)) continue;
            mask |= (byte)(1 << s);
            rec.Pads[s] = st.Take(t, now, out var filled);
            if (filled) Count("filled_pads");
        }
        rec = Rebuild(rec, mask);
        if (i.PendingEvents.Count > 0)
        {
            // At most MaxEventsPerTick per record (the count is a u8); the rest follow in the next ticks, in order.
            // LEAD_FROM is never deferred (the instance turns solo-led after its tick).
            foreach (var key in i.PendingEvents.Keys.Where(k => k <= t).ToList())
            {
                var list = i.PendingEvents[key];
                foreach (var lead in list.Where(e => e.Type == Ev.LeadFrom).ToList())
                {
                    rec.Events.Add(lead);
                    list.Remove(lead);
                }
                var take = Math.Min(list.Count, Math.Max(0, MaxEventsPerTick - rec.Events.Count));
                rec.Events.AddRange(list.Take(take));
                list.RemoveRange(0, take);
                if (list.Count == 0) i.PendingEvents.Remove(key);
            }
            if (i.PendingEvents.Keys.Any(k => k <= t)) Count("events_deferred");
        }
        var bytes = rec.ToBytes();
        if (i.Log.Count == t) i.Log.Append(bytes);
        else i.Ahead[t] = bytes;
        FlushAhead(i);
        i.NextSeal = t + 1;
        if (t % 60 == 59) EvaluateDelays(i, t);
        if (!i.LogFull && i.NextSeal >= MaxVisitTicks)
        {
            // A sealed visit past Instances:MaxVisitHours takes no joins either (nobody could catch up on it).
            i.LogFull = true;
            i.Joinable = false;
            Count("log_full");
            Note("log_full", i, ("ticks", i.NextSeal));
        }
        return bytes;
    }

    /// <summary>Events in one sealed record (below the format's 255: LEAD_FROM always fits).</summary>
    public const int MaxEventsPerTick = 200;

    private static TickRecord Rebuild(TickRecord r, byte mask)
    {
        var n = new TickRecord { Mask = mask };
        Array.Copy(r.Pads, n.Pads, Wire.MaxSlots);
        return n;
    }

    /// <summary>A member's sealed stream from <paramref name="from"/> with delay <paramref name="d"/>: the configured bound
    /// and late share, and the bar of the member's previous stream (DelayController.Inherit).</summary>
    private SlotStream NewStream(HubMember m, int slot, uint from, int d)
    {
        var stream = new SlotStream(slot, from, d) { Delay = { Max = _sealing.MaxDelayTicks, LateTarget = _sealing.LateTarget } };
        stream.Delay.Inherit(m.LastDelay);
        m.LastDelay = stream.Delay;
        return stream;
    }

    private void EvaluateDelays(HubInstance i, uint t)
    {
        foreach (var st in i.Streams)
        {
            if (st is null || !st.Carries(t)) continue;
            if (st.Delay.Evaluate() is { } d)
            {
                i.AddEvent(t + 2, TickEvent.Delay(st.Slot, d));
                Count("delay_changes");
                Note("delay", i, ("slot", st.Slot), ("d", d), ("tick", t + 2), ("why", st.Delay.LastReason), ("barred", st.Delay.Barred));
            }
        }
    }

    /// <summary>The tick at which the server stamps an event: the largest member horizon plus a margin (SEAMLESS.md 6.5).</summary>
    private uint StampTick(HubInstance i)
    {
        var t = i.NextSeal;
        foreach (var st in i.Streams)
            if (st is { HasHorizon: true } && st.Horizon + 1 > t) t = st.Horizon + 1;
        var maxDelay = i.Streams.Where(s => s is not null).Select(s => s!.Delay.Current).DefaultIfEmpty(0).Max();
        return Math.Max(t, i.NextSeal + (uint)maxDelay) + (uint)Math.Max(1, _sealing.EventMarginTicks);
    }

    private void StampLeave(HubInstance i, int slot, long now)
    {
        var m = i.Slots[slot];
        var j = StampTick(i);
        i.AddEvent(j, TickEvent.Leave(slot));
        if (i.Streams[slot] is { } st) st.UntilTick = Math.Min(st.UntilTick, j);
        i.Slots[slot] = null;
        if (m is not null && m.Instance == i)
        {
            m.Instance = null;
            m.Slot = -1;
            // Its client plays on alone in this stage until its next barrier: its STORY counts against it (8.7).
            m.LoneStage = i.Stage;
            m.LoneSince = Math.Max(m.InstanceSince, i.VisitStartedAt);
            m.LoneKey = (ulong)i.Id << 32 | i.Visit;
            // A member taken out while it still plays (lag, a resync too long or too many, a missing hash) gets the ticks
            // up to its LEAVE: its client sees it and continues alone until its next barrier (before, it waited for
            // ticks nobody sent it). A member that parks, joins or reconnects meanwhile drops out of this (PumpAll).
            if (m.Connected) i.Departed[m] = j;
        }
        Note("leave", i, ("slot", slot), ("member", m?.Index), ("tick", j));
        var remaining = i.Count;
        if (remaining == 0) EndInstance(i, now);
        else if (remaining == 1) ScheduleLeadFrom(i, j + 1);
    }

    private void ScheduleLeadFrom(HubInstance i, uint lead)
    {
        if (i.LeadFrom is { } already && already <= lead) return;
        i.AddEvent(lead - 1, TickEvent.LeadFrom(lead));
        i.LeadFrom = lead;
    }

    private void OnIntent(HubMember m, IntentMsg msg, long now)
    {
        if (Mine(m, msg.Inst, msg.Visit) is not { } i) return;
        if (msg.Kind == IntentKind.GoAlong)
        {
            // Round I1b's go along; the I1a relay makes no offers.
            Count("intent_ignored");
            return;
        }
        if (msg.Kind == IntentKind.Leave)
        {
            // *Leave instance* (19.6.4): HOME for the own slot (sealed: stamped; solo-led: JOIN_REQ kind home). A host's leave ends
            // his instance (19.6.5: on purpose, host_left).
            if (m.World is not { } w || i.Mode == InstanceMode.Hub)
            {
                Count("intent_ignored");
                return;
            }
            Note("leave_intent", i, ("member", m.Id), ("wid", w.Wid));
            if (w.Host == m) EndWorld(w, EndReason.HostLeft, now);
            else HomeMember(m, HomeReason.Leave, now); // (at a barrier: asked again in its next stage group, RetryHome)
            return;
        }
        if (i.Mode != InstanceMode.Sealed || !i.Sealing)
        {
            Count("intent_ignored");
            return;
        }
        // One open INTENT per member (a player makes one choice at a time; a burst would overflow the record).
        if (i.PendingEvents.Values.Any(l => l.Any(e => e.Type == Ev.Intent && e.Slot == m.Slot)))
        {
            Count("intent_dropped");
            return;
        }
        var j = StampTick(i);
        i.AddEvent(j, TickEvent.Intent(m.Slot, msg.Kind, msg.Argument));
        // M3: a give-up copy's game over (a barrier with alive mask 0) is on purpose, not "defeated".
        if (msg.Kind == IntentKind.GiveUp) m.GaveUp = true;
        Note("intent", i, ("slot", m.Slot), ("kind", msg.Kind.ToString()), ("tick", j));
    }

    // Snapshot, fingerprints, state --------------------------------------------------------------------------------------

    private void OnSnapshot(HubMember m, SnapshotMsg msg, long now)
    {
        if (Mine(m, msg.Inst, msg.Visit) is not { } i || !i.Logged) return;
        if (i.Snapshot.Uploader is { } up && up != m) throw new ProtocolException("only the instance's leader uploads the snapshot");
        if (i.Snapshot.AddChunk(msg.Total, msg.Offset, msg.Chunk))
        {
            Note("snapshot", i, ("bytes", msg.Total));
            // (A fork's parent and children share an upload in flight: each one's joiners and co-arrivals get it.)
            foreach (var x in _instances.Values.Where(x => x == i || (!x.Ended && x.Snapshot.SharesWith(i.Snapshot))).ToList())
            {
                foreach (var o in x.Observers.ToList()) DeliverState(o, now);
                foreach (var mem in x.CoArrivalPending.ToList()) SendCoArrivalState(x, mem);
            }
        }
    }

    private void OnSnapHash(HubMember m, SnapHashMsg msg, long now)
    {
        if (Mine(m, msg.Inst, msg.Visit) is not { } i) return;
        i.Snapshot.Hashes[m.Slot] = msg.Hash;
        if (i.Snapshot.Hashes.Values.Distinct().Count() > 1 && !i.Snapshot.HashMismatch)
        {
            i.Snapshot.HashMismatch = true;
            ++i.Desyncs;
            Count("snapshot_hash_mismatch");
            _log.LogWarning("Room {Room} instance {Inst} visit {Visit}: canonical snapshot hashes differ", Code, i.Id, i.Visit);
            Note("snaphash_mismatch", i);
        }
        TryStartSealing(i, now);
    }

    private void TryStartSealing(HubInstance i, long now)
    {
        if (i.Mode != InstanceMode.Sealed || i.Sealing || i.Barrier is not null || i.SoloPending) return;
        if (i.Seats.Any(x => !i.Snapshot.Hashes.ContainsKey(x.Slot) && x.Member.Connected)) return;
        // (The members' round trips without their host-loop stalls: a load's late PONG once placed S0 half a second out.)
        var rtt = i.Members.Select(x =>
        {
            var (median, spread) = DelayController.RoundTrip(x.Rtt.All);
            return median + spread;
        }).DefaultIfEmpty(0).Max();
        i.S0 = now + FromMs(Math.Min(rtt, 500)) + 2 * TickTs - (long)(i.NextSeal * Wire.TickSeconds * Freq);
        i.Sealing = true;
        Note("sealing", i, ("from", i.NextSeal));
        StampPendingHome(i, now);
        _sealer?.Invoke();
    }

    private Action? _sealer;
    /// <summary>The relay wakes its sealing thread through this when an instance starts sealing.</summary>
    public void OnSealingStarted(Action wake) => _sealer = wake;

    private void OnNetFp(HubMember m, NetFpMsg msg, long now)
    {
        if (Mine(m, msg.Inst, msg.Visit) is not { } i) return;
        // A member reports ticks it executed: sealed ones (a solo stretch has nobody to compare with).
        if (i.Mode == InstanceMode.Sealed && msg.Tick >= i.NextSeal)
        {
            Count("netfp_ahead");
            return;
        }
        if (Info.Protocol >= Wire.ReadyCheckProtocol)
        {
            // Protocol 48: the donor's fingerprint of its checkpoint's check tick (Wire.ReadyCheckTicks after the checkpoint
            // tick; on a multiple of 60 its ordinary NETFP), which a READY from that checkpoint is checked against.
            // (Also a waiting joiner's checkpoint that newer uploads pushed out of the store, Instances:KeepCheckpoints.)
            var checkpoints = i.Checkpoints.Concat(i.Observers.Select(o => o.Joining).OfType<JoinState>().Where(oj => oj.Target == i)
                    .Select(oj => oj.Checkpoint).OfType<StoredCheckpoint>())
                .Where(c => c.Donor == m && c.CheckTick == msg.Tick && c.CheckFp is null).Distinct().ToList();
            foreach (var c in checkpoints) c.CheckFp = msg.Hash;
            if (msg.Tick % 60 != 0)
            {
                // (Off the 60-tick raster only that one: nothing else votes on such a tick.)
                if (checkpoints.Count == 0)
                {
                    Count("netfp_offgrid");
                    return;
                }
                Count("check_fingerprints");
                foreach (var o in i.Observers.ToList())
                    if (o.Joining is { PendingReady: { } waiting, Checkpoint: { } used } wj && wj.Target == i && waiting.Tick == msg.Tick &&
                        checkpoints.Contains(used))
                    {
                        OnReady(o, waiting, now);
                        if (i.Ended) return;
                    }
                return;
            }
        }
        if (!i.Fingerprints.TryGetValue(msg.Tick, out var d)) i.Fingerprints[msg.Tick] = d = [];
        // One report per member and tick: a second one (a forged NETFP for a tick already compared equal) started a vote
        // against its partner whenever it liked.
        if (!d.TryAdd(m.Slot, msg.Hash))
        {
            Count("netfp_repeat");
            return;
        }
        while (i.Fingerprints.Count > 64) i.Fingerprints.Remove(i.Fingerprints.Keys.First());
        // A joiner's READY from a checkpoint that waited for this tick's fingerprint is checked now (round J's review).
        foreach (var o in i.Observers.ToList())
            if (o.Joining is { PendingReady: { } pending } oj && oj.Target == i && pending.Tick == msg.Tick)
            {
                OnReady(o, pending, now);
                if (i.Ended) return;
            }
        // A resync's replay of the log tells what every engine had at this tick (6.9).
        if (i.ReplayFp.TryGetValue(msg.Tick, out var truth) && truth != msg.Hash)
        {
            ReplayDisagrees(i, m, msg.Tick, now);
            if (i.Ended) return;
        }
        TryVote(i, msg.Tick, now);
    }

    /// <summary>
    /// Votes on a tick once every expected member reported it, once per tick. Not while a fork reported at or before the
    /// tick waits for its confirmation (the two sides' engines differ from it): the fork's end votes again when it is not
    /// confirmed, so a forged SPLIT can no longer hide every comparison.
    /// </summary>
    private void TryVote(HubInstance i, uint tick, long now)
    {
        if (i.Voted.Contains(tick) || !i.Fingerprints.TryGetValue(tick, out var d)) return;
        if (i.Splits.Values.Any(v => !v.Done && tick >= v.Tick)) return;
        var expected = i.Seats.Where(x => x.Member.Connected && !x.Member.Resyncing && (i.Streams[x.Slot] is not { } s || s.FromTick <= tick))
            .Select(x => x.Slot).ToList();
        if (expected.Count < 2 || !expected.All(d.ContainsKey) || expected.Select(s => d[s]).Distinct().Count() == 1) return;
        i.Voted.Add(tick);
        while (i.Voted.Count > 64) i.Voted.Remove(i.Voted.Min());
        Vote(i, tick, d, expected, now);
    }

    /// <summary>
    /// A fingerprint mismatch (SEAMLESS.md 6.9): the majority wins. Without one (two members) the member whose engine no
    /// resync's replay contradicted wins, then the longer member (before, the longer member always won, and a desync on
    /// its machine resynced the healthy partner again and again).
    /// </summary>
    private void Vote(HubInstance i, uint tick, Dictionary<int, ulong> d, List<int> slots, long now)
    {
        ++i.Desyncs;
        Count("desync_votes");
        // A member rebuilt from a checkpoint never judges (6.13.4, round J's review): it is no independent engine (its donor's
        // state, a field the rebuild missed), so the majority is taken among the others; it joins one only as the last resort.
        var rebuilt = slots.Where(s => i.Slots[s] is { RebuiltFromCheckpoint: true }).ToList();
        var judges = slots.Except(rebuilt).ToList();
        if (judges.Count == 0) judges = slots;
        var groups = judges.GroupBy(s => d[s]).OrderByDescending(g => g.Count()).ToList();
        ulong winner;
        if (groups[0].Count() >= 2 && (groups.Count == 1 || groups[1].Count() < groups[0].Count())) winner = groups[0].Key;
        else
        {
            var trusted = judges.Select(s => i.Slots[s]!).OrderBy(x => x.ReplayMismatches).ThenBy(x => x.InstanceSince).First();
            winner = d[trusted.Slot];
        }
        _log.LogWarning("Room {Room} instance {Inst}: net fingerprints differ at tick {Tick}", Code, i.Id, tick);
        Note("desync", i, ("tick", tick), ("fingerprints", slots.ToDictionary(s => s.ToString(), s => d[s].ToString("x16"))),
            ("rebuilt", rebuilt.ToArray()));
        // A desync soon after a rebuild from a checkpoint, or one a rebuilt member is part of (at any time): that rebuild may
        // have missed a field (6.13.4).
        if (rebuilt.Count > 0) MarkSuspect(i, tick, "desync_rebuilt");
        else if (i.RebuildTick is { } rebuiltAt && tick <= rebuiltAt + (uint)_inst.CheckpointSuspectTicks) MarkSuspect(i, tick, "desync");
        foreach (var s in slots.Where(s => d[s] != winner))
        {
            Resync(i, s, now);
            if (i.Ended) return;
        }
    }

    /// <summary>
    /// A member's fingerprint differs from the one a resync's replay of the log gave for that tick: its engine is the one
    /// off. It loses the next two-member vote and is resynced itself.
    /// </summary>
    private void ReplayDisagrees(HubInstance i, HubMember m, uint tick, long now)
    {
        ++m.ReplayMismatches;
        Count("replay_disagrees");
        Note("replay_disagrees", i, ("slot", m.Slot), ("member", m.Index), ("tick", tick));
        Resync(i, m.Slot, now);
    }

    /// <summary>RESYNC and the state for a member whose engine is off (6.9), within the limits.</summary>
    private void Resync(HubInstance i, int s, long now)
    {
        if (i.Slots[s] is not { } loser) return;
        // Not at a barrier: the visit ends there (a RESYNC that reached a member parked after its BARRIER threw it back
        // into the stage it had left).
        if (i.Barrier is not null)
        {
            Count("resync_at_barrier");
            return;
        }
        // One resync at a time per member and at most one per Instances:ResyncCooldownSeconds: the state is the heaviest
        // frame the relay sends (a member that keeps forging fingerprints cannot flood its partner).
        if (loser.Resyncing || loser.PendingState is not null || (loser.ResyncAt != 0 && Ms(now - loser.ResyncAt) < _inst.ResyncCooldownSeconds * 1000.0))
        {
            Count("resync_throttled");
            return;
        }
        // Instances:ResyncLimit resyncs in Instances:ResyncWindowMinutes: one more lost vote and the member continues alone
        // (it leaves the instance by LEAVE; its client sees it and plays on from its own state until its next barrier).
        while (loser.ResyncTimes.Count > 0 && Ms(now - loser.ResyncTimes.Peek()) > _inst.ResyncWindowMinutes * 60_000.0) loser.ResyncTimes.Dequeue();
        if (loser.ResyncTimes.Count >= Math.Max(1, _inst.ResyncLimit))
        {
            Count("resync_limit");
            Note("resync_limit", i, ("slot", s), ("member", loser.Index), ("resyncs", loser.ResyncTimes.Count));
            StampLeave(i, s, now);
            return;
        }
        var catchUp = HubRules.CatchUpSeconds(i.StreamEnd, loser.Rate == 0 ? _inst.DefaultReplayRate : loser.Rate);
        if (catchUp > _inst.MaxCatchUpSeconds || !i.Snapshot.Complete)
        {
            // Beyond the limit the member continues alone.
            Note("resync_too_long", i, ("slot", s));
            StampLeave(i, s, now);
            return;
        }
        loser.Peer?.Send(new ResyncMsg(i.Id, i.Visit).Frame());
        loser.Resyncing = true;
        loser.ResyncAt = now;
        loser.ResyncTimes.Enqueue(now);
        // Round J (6.13.4): from the newest agreed checkpoint (the ticks since it, under half a second), else the classic way.
        var cp = CheckpointPathOpen(i) ? i.Checkpoints.LastOrDefault(c => c.Agreed && UsableCheckpoint(i, c)) : null;
        loser.ResyncFromCheckpoint = cp is not null;
        SendState(loser, i, StatePurpose.Resync, s, cp);
        Note("resync", i, ("slot", s), ("member", loser.Index), ("checkpoint", cp?.Tick ?? 0));
    }

    private void OnState(HubMember m, StateMsg msg, long now)
    {
        if (Mine(m, msg.Inst, msg.Visit) is not { } i) return;
        // The flags and the context come from one member, the connected one that has been in the instance longest (then
        // the lowest slot; a joiner still catching up sends none): every engine computes them alike, and another member's
        // STATE marked the shared instance boss-locked or not joinable, or changed its context, at will.
        if (i.Seats.Where(x => x.Member.Connected).OrderBy(x => x.Member.InstanceSince).ThenBy(x => x.Slot).Select(x => x.Member).FirstOrDefault() is { } reporter &&
            reporter != m)
        {
            Count("state_ignored");
            return;
        }
        i.Joinable = (msg.Flags & StateMsg.Joinable) != 0 && !i.LogFull;
        // A new instance is *starting* until its stage group is first joinable (19.7.6).
        if (i.Joinable && i.World is { StartingUntil: not 0 } starting) starting.StartingUntil = 0;
        i.MembershipSafe = (msg.Flags & StateMsg.MembershipSafe) != 0;
        var boss = (msg.Flags & StateMsg.BossLock) != 0;
        if (boss != i.BossLock) Note(boss ? "boss_lock" : "boss_clear", i);
        i.BossLock = boss;
        i.Reason = msg.Reason;
        i.ContextHash = msg.ContextHash;
        // The entry context: the first report after the load end, joinable or not (review of round J: from the first joinable
        // report, a visit that ran from its entry into a boss took the boss kill's context - a quest flag - as its entry, and
        // an arrival with the old context joined after the kill and took that progress).
        if ((msg.Flags & StateMsg.LoadEnded) != 0 && i.EntryContext is null) i.EntryContext = msg.ContextHash;
        CheckHolds(now);
    }

    // Barrier ------------------------------------------------------------------------------------------------------------

    private void OnBarrier(HubMember m, BarrierMsg msg, long now)
    {
        // Protocol 49: the member's own settings (its Game Start difficulty, its Online > Settings): a NEW from home makes them its
        // instance's, a DISSOLVE rebases its own story on that difficulty.
        if (msg.Settings is { } own) m.Settings = own;
        OnBarrierWorld(m, msg, now);
    }

    private void OnBarrierWorld(HubMember m, BarrierMsg msg, long now)
    {
        var abandons = m.Arrival is not null || (m.Joining is { } joining && (joining.Target.Id != msg.Inst || joining.Target.Visit != msg.Visit));
        var known = m.Instance is { Ended: false } own && own.Visit == msg.Visit && (own.Id == msg.Inst || m.PreviousInstanceId == msg.Inst) && m.Slot >= 0;
        if (abandons || (m.Joining is null && !known))
        {
            // A BARRIER that is decided alone may send a whole state (a join): at most one such decision per
            // Instances:LoneBarrierMs and none while the member's last state is still being built (an honest client
            // loads a stage in between; one member's flood of 4,276 BARRIER built states for minutes, 6.7).
            if (m.PendingState is not null || (m.LoneBarrierAt != 0 && Ms(now - m.LoneBarrierAt) < _inst.LoneBarrierMs))
            {
                Count("barrier_throttled");
                return;
            }
            m.LoneBarrierAt = now;
        }
        // A BARRIER while the server still parks the member (its five-second timeout ran out: it went on as NEW on its
        // own and reached its next barrier), or while it catches up on a target the BARRIER does not name (an observer's
        // replay reaching the target's stage change names the target): the member left that wait.
        if (abandons)
        {
            Count("barrier_abandoned");
            Note("barrier_abandoned", m.Instance, ("member", m.Index));
            DetachLocked(m, now, leaveNow: true);
        }
        if (m.Joining is not null)
        {
            Count("barrier_while_parked");
            return;
        }
        var i = m.Instance;
        if (i is null || i.Ended || i.Visit != msg.Visit || (i.Id != msg.Inst && m.PreviousInstanceId != msg.Inst) || m.Slot < 0)
        {
            // A straggler whose group was decided without it, or a member the server lost track of: it arrives alone and
            // leaves whatever instance the server still had it in (a slot left filled there made a zombie instance that
            // joiners were sent to). Its engine's slot: the BARRIER's only present slot, else the server's.
            var slot = BitOperations.PopCount(msg.PresentMask) == 1 ? BitOperations.TrailingZeroCount(msg.PresentMask) : Math.Max(0, m.Slot);
            var stage = i?.Stage ?? m.LoneStage ?? "";
            DetachLocked(m, now, leaveNow: true);
            var lone = new HubInstance(0) { Stage = stage, Visit = msg.Visit, ContextHash = msg.ContextHash, Ended = true, Detached = i?.Detached == true };
            var g = new ArrivalGroup { From = lone, BarrierTick = msg.Tick, Stage = msg.Stage, Point = msg.Point, Context = msg.ContextHash, ParkedAt = now, Alive = msg.AliveMask,
                PlanNew = msg.PlanId == BarrierMsg.PlanNewInstance };
            g.Members.Add(new Arrival(m, slot, msg.Carry));
            m.Arrival = g;
            Note("barrier_alone", null, ("member", m.Index), ("to", msg.Stage));
            Decide(g, now);
            return;
        }
        if (i.Barrier is null)
        {
            i.Barrier = new BarrierCollect(msg.Tick, msg.Stage, msg.Point, msg.AliveMask, msg.PresentMask, msg.ContextHash, now);
            // The server stops sealing the old visit; ticks sealed after B are discarded by every member (and by the
            // log once the barrier holds for the whole group: a member that never reports it played them, 6.9).
            i.SealStop = msg.Tick;
            i.Sealing = false;
            Note("barrier", i, ("tick", msg.Tick), ("to", msg.Stage), ("point", msg.Point));
        }
        else if (i.Barrier.Tick != msg.Tick || i.Barrier.Stage != msg.Stage || i.Barrier.Point != msg.Point || i.Barrier.Present != msg.PresentMask)
        {
            ++i.Barrier.Mismatches;
            ++i.Desyncs;
            Count("barrier_mismatch");
            Note("barrier_mismatch", i, ("slot", m.Slot), ("tick", msg.Tick), ("to", msg.Stage));
        }
        i.Barrier.Add(m.Slot, m, msg);
        if (i.Seats.Where(x => x.Member.Connected).All(x => i.Barrier.Reports.ContainsKey(x.Slot))) Park(i, now);
    }

    /// <summary>Every connected member of the instance reported its barrier (or the wait ran out): the group parks.</summary>
    private void Park(HubInstance i, long now)
    {
        var b = i.Barrier!;
        var reporters = b.Reports.OrderBy(kv => kv.Key).Where(kv => kv.Value.Member.Instance == i && kv.Value.Member.Slot == kv.Key && i.Slots[kv.Key] == kv.Value.Member)
            .ToList();
        if (reporters.Count == 0)
        {
            // Nobody of this instance reported (the reporters moved on, a fork took them): the barrier is none of the
            // members' that are left, they play on. An instance with filled slots never ends here (it froze its last
            // member for good, without LEAVE or LEAD_FROM).
            if (i.Count == 0) EndInstance(i, now);
            else ResumeAfterBarrier(i, now);
            return;
        }
        if (i.Log.Count > b.Tick + 1 && b.Tick + 1 >= i.Log.PrefixCount) i.Log.TruncateTo(b.Tick + 1);
        foreach (var k in i.Ahead.Keys.Where(k => k > b.Tick).ToList()) i.Ahead.Remove(k);
        var g = new ArrivalGroup { From = i, BarrierTick = b.Tick, Stage = b.Stage, Point = b.Point, Context = b.Context, ParkedAt = now, Alive = b.Alive,
            PlanNew = b.Messages.Values.Any(x => x.PlanId == BarrierMsg.PlanNewInstance) };
        foreach (var (slot, (member, carry)) in reporters)
        {
            g.Members.Add(new Arrival(member, slot, carry));
            member.Arrival = g;
        }
        // Members that did not report (gone) are not part of the arrival; they arrive alone when their BARRIER comes.
        foreach (var (s, x) in i.Seats.ToList())
            if (!b.Reports.ContainsKey(s))
            {
                i.Slots[s] = null;
                x.PreviousInstanceId = i.Id;
                if (x.Instance == i)
                {
                    x.Instance = null;
                    x.Slot = -1;
                }
            }
        Decide(g, now);
    }

    /// <summary>A barrier that is nobody's any more (6.7): the instance's members play on, sealing resumes where it stopped,
    /// shifted by the stall; a member left alone leads on its own.</summary>
    private void ResumeAfterBarrier(HubInstance i, long now)
    {
        i.Barrier = null;
        i.SealStop = null;
        if (i.Mode == InstanceMode.Sealed && i.S0 != 0 && !i.SoloPending)
        {
            i.S0 += Math.Max(0, now - Due(i, i.NextSeal));
            i.Sealing = true;
            if (i.Count == 1) ScheduleLeadFrom(i, i.NextSeal + 1);
            Note("barrier_dropped", i, ("from", i.NextSeal));
            _sealer?.Invoke();
        }
        else if (i.Mode == InstanceMode.Sealed) TryStartSealing(i, now);
    }

    /// <summary>The barrier is complete when every connected member of the instance reported it.</summary>
    private void CheckBarrier(HubInstance i, long now)
    {
        if (i.Ended || i.Barrier is not { } b) return;
        if (!b.Reports.Any(kv => i.Slots[kv.Key] == kv.Value.Member))
        {
            if (i.Count > 0) ResumeAfterBarrier(i, now);
            return;
        }
        if (i.Seats.Where(x => x.Member.Connected).All(x => b.Reports.ContainsKey(x.Slot))) Park(i, now);
    }

    /// <summary>
    /// A barrier that only some connected members reported within Instances:BarrierReportSeconds: their engines executed a
    /// NEXTSTAGE the others did not (a desync, or a forged BARRIER). The reporters arrive as a group of their own (a new
    /// instance); the others play on: sealing resumes where it stopped, shifted by the stall, and the reporters leave by
    /// LEAVE events (6.9: a member that cannot stay in sync continues alone).
    /// </summary>
    private void SplitBarrier(HubInstance i, long now)
    {
        var b = i.Barrier!;
        var reporters = b.Reports.OrderBy(kv => kv.Key).Where(kv => kv.Value.Member.Instance == i && kv.Value.Member.Slot == kv.Key).ToList();
        if (reporters.Count == 0)
        {
            ResumeAfterBarrier(i, now);
            return;
        }
        ++i.Desyncs;
        Count("barrier_split");
        var lone = new HubInstance(0) { Stage = i.Stage, Visit = i.Visit, ContextHash = b.Context, Ended = true };
        var g = new ArrivalGroup { From = lone, BarrierTick = b.Tick, Stage = b.Stage, Point = b.Point, Context = b.Context, ParkedAt = now, Alive = b.Alive };
        i.Barrier = null;
        i.SealStop = null;
        // The stall moves the schedule: the next tick is due now, as if the stalled time had not been.
        i.S0 += Math.Max(0, now - Due(i, i.NextSeal));
        i.Sealing = true;
        foreach (var (slot, (member, carry)) in reporters)
        {
            g.Members.Add(new Arrival(member, slot, carry));
            StampLeave(i, slot, now);
            member.Arrival = g;
            member.PreviousInstanceId = i.Id;
        }
        Note("barrier_split", i, ("reporters", reporters.Select(r => r.Key).ToArray()), ("tick", b.Tick));
        _log.LogWarning("Room {Room} instance {Inst}: only some members reached the barrier at tick {Tick}; they go on alone", Code, i.Id, b.Tick);
        _sealer?.Invoke();
        if (g.Members.Count > 0) Decide(g, now);
    }

    /// <summary>
    /// The decision of SEAMLESS.md 3.4 in the world (19.6.3, 19.7, 19.30.3): into a hub stage a DISSOLVE (home); an instance that
    /// ended (or a stage group nobody's any more) a DISSOLVE redirected home; an arrival from home the world's matching (a
    /// co-arrival, a starting instance, round I1a's automatic join of the first candidate, a guest's NEW, or a new instance);
    /// inside an instance 3.4 among the instance's own stage groups (co-arrival, JOIN with holds, a new stage group of it).
    /// </summary>
    private void Decide(ArrivalGroup g, long now)
    {
        var w = WorldOfGroup(g);
        g.World = w;
        // (A stage group of an instance whose members hold no seat in it any more, removed ones: their engines hold the host's
        // story, so they go home like a detached group, 19.6.6.)
        var seatless = w is null && g.From.World is not null;
        if (g.From.Detached || seatless || w is { Ending: true } || (w is not null && !_worlds.ContainsKey(w.Wid)))
        {
            Dissolve(g, now, redirectHome: true);
            return;
        }
        // (Members removed while the group gathered at the barrier go home alone; the others decide on, 19.6.6.)
        if (SendRemovedHome(g, w, now)) return;
        if (StageNames.IsHub(g.Stage))
        {
            Dissolve(g, now, redirectHome: false);
            return;
        }
        if (w is not null && g.Members.Any(a => a.Member == w.Host) && (g.Alive & PresentOf(g)) == 0 && !w.Host.GaveUp)
        {
            // M3 (19.6.5): a barrier with no living present slot is the host's stage group's game over whatever stage it names (after a
            // HOME the exit prompt's names the field, stag0101 point 2), outside a give-up copy: the host was defeated; before round I3
            // his instance ends at once, and the barrier goes home like every barrier of an ended instance.
            Count("host_defeated_elsewhere");
            EndWorld(w, EndReason.HostDefeated, now);
            Dissolve(g, now, redirectHome: true);
            return;
        }
        if (w is not null) ArrivedInside(w, g);
        // A base that holds for announced groups takes this one as a co-arrival (one it does not wait for only where the
        // ones it waits for still fit): inside an instance only its own groups; from home only fresh bases this group may see
        // (19.7.6: "visible to them"), never a group of a running instance (that one is a candidate instead).
        var holder = _holds.FirstOrDefault(h => h.Hold == HoldKind.CoArrival && h != g && SameStage(h.Stage, g.Stage) && h.Context == g.Context &&
                                                h.World == w && (w is not null || (!g.PlanNew && MaySeeBase(h, g))) &&
                                                h.Size + g.Members.Count + OutstandingSize(h, g) <= Capacity);
        if (holder is not null)
        {
            holder.CoArrivals.Add(g);
            foreach (var a in g.Members) a.Member.Arrival = holder;
            holder.Awaited.RemoveAll(x => g.Members.Any(a => a.Member == x));
            Note("co_arrival", holder.From, ("members", g.Members.Select(a => a.Member.Index).ToArray()));
            if (holder.Awaited.Count == 0) FinishCoArrival(holder, now);
            return;
        }
        // Announced groups on their way to the same stage with the same context: hold for those that fit, the earliest
        // announced first (before, five arrivals at once left the first one alone: the base held only when every
        // announced member fit). Groups another base already waits for are that base's.
        var announced = _members.Values.Where(x => g.Members.All(a => a.Member != x) && x.Heading is { } h && SameStage(h.Stage, g.Stage) &&
                                                   Ms(now - h.At) <= _inst.HeadingValidMs && x.Arrival is null && x.Joining is null &&
                                                   h.Context == g.Context && x.Instance is not null && x.Connected &&
                                                   (w is not null ? x.World == w : x.World is null && MaySeeHost(g.Members[0].Member, g, x)) &&
                                                   !_holds.Any(o => o.Hold == HoldKind.CoArrival && o.Awaited.Contains(x))).ToList();
        var awaited = new List<HubMember>();
        var room = Capacity - g.Members.Count;
        foreach (var group in announced.GroupBy(x => x.Instance!).OrderBy(grp => grp.Min(x => x.Heading!.At)).ThenBy(grp => grp.Min(x => x.Index)))
        {
            var size = Math.Max(group.Key.Count, group.Count());
            if (size > room) continue;
            awaited.AddRange(group);
            room -= size;
        }
        if (awaited.Count > 0 && _inst.CoArrivalHoldMs > 0)
        {
            g.Hold = HoldKind.CoArrival;
            g.HoldUntil = now + FromMs(_inst.CoArrivalHoldMs);
            g.Awaited.AddRange(awaited);
            _holds.Add(g);
            SendWait(g, WaitReason.CoArrival, awaited.Select(x => x.Name), _inst.CoArrivalHoldMs / 1000.0, 0, now);
            Note("hold_co_arrival", g.From, ("awaiting", awaited.Select(x => x.Index).ToArray()));
            return;
        }
        if (w is null) FreshJoinOrNew(g, now);
        else JoinOrNew(g, now);
    }

    /// <summary>The arriving group's slots in the engine that reported the barrier.</summary>
    private static byte PresentOf(ArrivalGroup g)
    {
        var mask = 0;
        foreach (var a in g.Members) mask |= 1 << a.Slot;
        return (byte)mask;
    }

    /// <summary>A fresh member <paramref name="x"/> may see the instance <paramref name="host"/> would make with this base group: no
    /// block with any of them, and the host's default visibility lets it see it (19.7.6).</summary>
    private static bool MaySeeHost(HubMember host, ArrivalGroup g, HubMember x)
    {
        if (g.Members.Any(a => Matching.Blocked(a.Member, x))) return false;
        if (host.DataHash != x.DataHash) return false;
        return host.DefaultVisibility switch
        {
            Visibility.Public => true,
            Visibility.Friends => x.Staff || x.Friends.Contains(host.AccountId),
            _ => x.Staff,
        };
    }

    private static bool SameStage(string a, string b) => string.Equals(a, b, StringComparison.OrdinalIgnoreCase);

    /// <summary>The members a holding base still waits for, without the arriving group's (by their instances' sizes).</summary>
    private static int OutstandingSize(ArrivalGroup holder, ArrivalGroup arriving) =>
        holder.Awaited.Where(x => arriving.Members.All(a => a.Member != x)).GroupBy(x => x.Instance)
            .Sum(grp => Math.Max(grp.Key?.Count ?? 0, grp.Count()));

    private void JoinOrNew(ArrivalGroup g, long now)
    {
        var size = g.Members.Count;
        // (19.6.3: only stage groups of the arriving group's instance are candidates; a member never lands in another instance's.)
        var candidates = _instances.Values.Where(x => !x.Ended && x.Logged && !x.Departing && !x.LogFull && x.Barrier is null && x != g.From &&
                                                      SameStage(x.Stage, g.Stage) && x.World is not null && x.World == g.World).ToList();
        if (candidates.Count == 0)
        {
            New(g, now, BarrierNotice.None, []);
            return;
        }
        var sameContext = candidates.Where(x => HubRules.ContextMatches(x, g.Context)).ToList();
        var fits = sameContext.Where(x => x.FreeSlots >= size).ToList();
        var slowest = g.Members.Select(a => a.Member.Rate == 0 ? (double)_inst.DefaultReplayRate : a.Member.Rate).DefaultIfEmpty(_inst.DefaultReplayRate).Min();
        var ranked = fits.Select(x => (Inst: x, CatchUp: HubRules.CatchUpSeconds(CatchUpTicks(x, now), slowest)))
            .Where(x => x.CatchUp <= _inst.MaxCatchUpSeconds)
            .OrderByDescending(x => x.Inst.Count).ThenBy(x => x.CatchUp).ToList();
        if (ranked.Count == 0)
        {
            var notice = sameContext.Count == 0 ? BarrierNotice.OtherContext : fits.Count == 0 ? BarrierNotice.Full : BarrierNotice.TooLongToCatchUp;
            var names = (notice == BarrierNotice.OtherContext ? candidates : notice == BarrierNotice.Full ? sameContext : fits)
                .SelectMany(x => x.Members).Select(x => x.Name).Distinct().ToList();
            New(g, now, notice, names);
            return;
        }
        JoinTarget(g, ranked[0].Inst, now);
    }

    /// <summary>A JOIN of <paramref name="target"/> with 3.4's holds: a boss lock waits for the choice, a target not joinable yet holds
    /// up to Instances:JoinHoldSeconds.</summary>
    private void JoinTarget(ArrivalGroup g, HubInstance target, long now)
    {
        g.Target = target;
        if (target.BossLock)
        {
            if (g.Choice == BarrierChoiceMsg.Alone)
            {
                New(g, now, BarrierNotice.WentAlone, target.Members.Select(x => x.Name).ToList());
                return;
            }
            g.Hold = HoldKind.Boss;
            if (!_holds.Contains(g)) _holds.Add(g);
            SendWait(g, WaitReason.BossLock, target.Members.Select(x => x.Name), 0, g.Choice is null ? BarrierWaitMsg.ChoiceNeeded : (byte)0, now);
            Note("hold_boss", g.From, ("target", target.Id));
            return;
        }
        if (!target.Joinable || !target.Snapshot.Complete)
        {
            if (g.Hold != HoldKind.Join)
            {
                g.Hold = HoldKind.Join;
                g.HoldUntil = now + FromMs(_inst.JoinHoldSeconds * 1000.0);
                if (!_holds.Contains(g)) _holds.Add(g);
                Note("hold_join", g.From, ("target", target.Id), ("reason", target.Reason.ToString()));
            }
            SendWait(g, target.Snapshot.Complete ? WaitReason.NotJoinable : WaitReason.Snapshot, target.Members.Select(x => x.Name),
                Math.Max(0, Ms(g.HoldUntil - now) / 1000.0), 0, now);
            return;
        }
        _holds.Remove(g);
        Join(g, target, now);
    }

    /// <summary>The ticks a joiner replays: from a checkpoint its load ticks and at most two checkpoint periods (6.13.5), else the
    /// whole visit.</summary>
    private uint CatchUpTicks(HubInstance x, long now)
    {
        var current = CurrentTick(x, now);
        return CheckpointPathOpen(x) && x.AgreedLoadEnd is { } k ? Math.Min(current, k + 1200u) : current;
    }

    /// <summary>The target's current tick: sealed = the next tick it seals; solo-led = its log plus the time since the last LOG.</summary>
    private uint CurrentTick(HubInstance x, long now) => x.Mode == InstanceMode.Sealed
        ? x.NextSeal
        : x.Log.Count + (uint)Math.Max(0, Ms(now - x.LastLogAt) / 1000.0 / Wire.TickSeconds);

    private void SendWait(ArrivalGroup g, WaitReason reason, IEnumerable<string> names, double seconds, byte flags, long now)
    {
        g.LastWaitSent = now;
        var frame = new BarrierWaitMsg(g.From.Id, reason, (ushort)Math.Ceiling(seconds), names.Distinct().Take(8).ToList(), flags).Frame();
        foreach (var a in g.Members) a.Member.Peer?.Send(frame);
    }

    private void OnChoice(HubMember m, BarrierChoiceMsg msg, long now)
    {
        if (m.Arrival is not { } g || g.Hold != HoldKind.Boss) return;
        g.Choice = msg.Choice;
        Note("boss_choice", g.From, ("member", m.Index), ("alone", msg.Choice == BarrierChoiceMsg.Alone));
        if (msg.Choice == BarrierChoiceMsg.Alone)
        {
            _holds.Remove(g);
            New(g, now, BarrierNotice.WentAlone, g.Target?.Members.Select(x => x.Name).ToList() ?? []);
        }
    }

    /// <summary>Re-checks the held barriers (after a STATE, a snapshot, every housekeeping pass).</summary>
    private void CheckHolds(long now)
    {
        foreach (var g in _holds.ToList())
        {
            if (!_holds.Contains(g)) continue;
            // (A member removed while the arrival waits goes home now, 19.6.6; the bar is checked again at every pass.)
            if (SendRemovedHome(g, g.World ?? g.Target?.World, now)) continue;
            switch (g.Hold)
            {
                case HoldKind.CoArrival:
                    g.Awaited.RemoveAll(x => !x.Connected || x.Heading is null || Ms(now - x.Heading.At) > _inst.HeadingValidMs + _inst.CoArrivalHoldMs);
                    if (now >= g.HoldUntil || g.Awaited.Count == 0)
                    {
                        if (g.CoArrivals.Count > 0) FinishCoArrival(g, now);
                        else
                        {
                            _holds.Remove(g);
                            g.Hold = HoldKind.None;
                            if (g.Fresh) FreshJoinOrNew(g, now);
                            else JoinOrNew(g, now);
                        }
                    }
                    break;
                case HoldKind.Join:
                    if (g.Target is null || g.Target.Ended || g.Target.Barrier is not null || g.Target.FreeSlots < g.Members.Count ||
                        g.Target.World is null || g.Target.World.Ending)
                    {
                        _holds.Remove(g);
                        g.Hold = HoldKind.None;
                        Redecide(g, now);
                    }
                    else if (!HubRules.ContextMatches(g.Target, g.Context))
                    {
                        // The target's play changed its context while the arrival waited (a quest step): it may not join
                        // any more; JoinOrNew finds another instance or answers NEW (OtherContext).
                        _holds.Remove(g);
                        g.Hold = HoldKind.None;
                        Count("hold_context_changed");
                        Redecide(g, now);
                    }
                    else if (g.Target.Joinable && g.Target.Snapshot.Complete && !g.Target.BossLock)
                    {
                        _holds.Remove(g);
                        Join(g, g.Target, now);
                    }
                    else if (now >= g.HoldUntil)
                    {
                        _holds.Remove(g);
                        New(g, now, BarrierNotice.HoldExpired, g.Target.Members.Select(x => x.Name).ToList());
                    }
                    else if (Ms(now - g.LastWaitSent) >= 1000)
                        SendWait(g, g.Target.Snapshot.Complete ? WaitReason.NotJoinable : WaitReason.Snapshot, g.Target.Members.Select(x => x.Name),
                            Ms(g.HoldUntil - now) / 1000.0, 0, now);
                    break;
                case HoldKind.Boss:
                    if (g.Target is null || g.Target.Ended || g.Target.Barrier is not null || g.Target.FreeSlots < g.Members.Count ||
                        g.Target.World is null || g.Target.World.Ending)
                    {
                        _holds.Remove(g);
                        g.Hold = HoldKind.None;
                        Redecide(g, now);
                    }
                    else if (!g.Target.BossLock && g.Choice == BarrierChoiceMsg.Wait)
                    {
                        _holds.Remove(g);
                        g.Hold = HoldKind.None;
                        Redecide(g, now);
                    }
                    break;
            }
        }
    }

    /// <summary>The way home a DISSOLVE takes when its instance ended, or for a guest whose barrier leads into the briefing room
    /// (19.8.3): the lobby's point next to the gate.</summary>
    public const string HomeStage = "stagML11";
    public const byte HomePoint = 3;

    /// <summary>
    /// DISSOLVE (3.4, 19.8.3): every member home, each on its own story. BARRIER_OK carries the member's own keeper's newest revision
    /// rebased on its own difficulty and its own settings; the story mode is *replace* for a member whose engine held another's story
    /// (it comes from a field and is not the host), *merge* for the host and for moves between hub stages. A guest whose barrier leads
    /// into stagML02 (the host's briefing room; round I1: every guest counts as not in step) and every member of an instance that
    /// ended get a redirect to stagML11 point 3. The host's DISSOLVE ends his instance: on purpose (host_left), or defeated
    /// (host_defeated, M3: his slot not alive in the barrier, outside a give-up copy), at once before round I3.
    /// </summary>
    private void Dissolve(ArrivalGroup g, long now, bool redirectHome)
    {
        var fromField = g.From.Stage.Length > 0 && !StageNames.IsHub(g.From.Stage);
        foreach (var a in g.Members)
        {
            var m = a.Member;
            m.Arrival = null;
            m.HomeWish = null; // (home now)
            var w = m.World;
            var isHost = w is not null && w.Host == m;
            var redirect = "";
            byte redirectPoint = 0;
            if (redirectHome && !SameStage(g.Stage, HomeStage) || (w is not null && !isHost && SameStage(g.Stage, "stagML02")))
            {
                redirect = HomeStage;
                redirectPoint = HomePoint;
            }
            if (isHost)
            {
                var defeated = ((g.Alive >> a.Slot) & 1) == 0 && !m.GaveUp;
                EndWorld(w!, defeated ? EndReason.HostDefeated : EndReason.HostLeft, now);
            }
            var own = OwnKeeper(m);
            if (!_worlds.Values.Any(x => x.Keeper == own && !x.Ending)) own.SetDifficulty(m.Settings.Difficulty);
            // Merge only where the engine holds the own story for sure: the host's, and a move between hub stages. A member
            // coming from a field, or from the title or a fresh session (the local way home after a lost instance, 19.6.10),
            // gets its own story as a replace.
            var mode = isHost || (g.From.Stage.Length > 0 && !fromField) ? StoryMode.Merge : StoryMode.Replace;
            var hub = NewInstance(InstanceMode.Hub, redirect.Length > 0 ? redirect : g.Stage, 1, g.Context, now);
            var map = new sbyte[Wire.MaxSlots];
            Array.Fill(map, (sbyte)-1);
            map[a.Slot] = 0;
            DetachFromOld(g.From, m);
            Place(m, hub, 0, now);
            var world = new BarrierWorld(0, 0, Visibility.Public, ArrivalKind.None, m.Settings, m.Settings.Hash(Wire.WorldProtocol, m.DataHash), m.Id,
                (byte)m.Box, mode, redirect, redirectPoint);
            m.Peer?.Send(new BarrierOkMsg(g.From.Id, g.From.Visit, BarrierDecision.Dissolve, hub.Id, hub.Visit, g.Stage, g.Point, 0, map,
                InstanceMode.Hub, 0, [], own.Story.Revision, own.Story.ToBlob()) { World = world }.Frame());
            Note("dissolve", hub, ("member", m.Id), ("account", m.AccountId), ("from", g.From.Id), ("mode", mode.ToString()), ("wid", w?.Wid ?? 0));
            if (redirect.Length > 0)
                Note("redirect", hub, ("wid", w?.Wid ?? 0), ("account", m.AccountId), ("member", m.Id), ("stage", redirect.ToLowerInvariant()), ("point", redirectPoint));
            if (w is not null) FreeSeat(w, m, w.Ending ? "ended" : "home", now);
        }
        EndIfEmpty(g.From, now);
    }

    private void DetachFromOld(HubInstance from, HubMember m)
    {
        if (!from.Ended && m.Slot >= 0 && from.Slots[m.Slot] == m) from.Slots[m.Slot] = null;
        m.PreviousInstanceId = from.Id;
    }

    private void EndIfEmpty(HubInstance i, long now)
    {
        if (!i.Ended && i.Count == 0 && i.Id != 0) EndInstance(i, now);
    }

    /// <summary>A base that held for co-arrivals continues as NEW with them (SEAMLESS.md 3.4 step 2).</summary>
    private void FinishCoArrival(ArrivalGroup holder, long now)
    {
        _holds.Remove(holder);
        holder.Hold = HoldKind.None;
        New(holder, now, BarrierNotice.None, []);
    }

    /// <summary>
    /// NEW (SEAMLESS.md 3.4 step 4): the group's own engine continues into the stage; the instance keeps its id with the
    /// next visit (or a new id for a member without a live instance); co-arrivals of a held base come with it.
    /// </summary>
    private void New(ArrivalGroup g, long now, BarrierNotice notice, IReadOnlyList<string> noticeNames)
    {
        _holds.Remove(g);
        // A fresh arrival whose automatic join did not work out (the hold ran out, Go alone at a boss lock) plays its own instance:
        // never a second stage group next to the chosen instance's (19.7.7).
        if (g.Arrival is ArrivalKind.AutoJoin or ArrivalKind.Starting && g.World is { } chosen)
        {
            foreach (var x in g.Members.Select(a => a.Member).Concat(g.CoArrivals.SelectMany(c => c.Members).Select(a => a.Member)))
                if (chosen.Seats.ContainsKey(x)) FreeSeat(chosen, x, "left", now);
            g.World = null;
            g.Arrival = ArrivalKind.None;
        }
        if (g.World is null)
        {
            g.World = CreateWorld(g.Members[0].Member, g.Stage, g.Context, now);
            g.Arrival = ArrivalKind.NewInstance;
            foreach (var a in g.Members) TakeSeat(g.World, a.Member, now, "new_group");
            foreach (var c in g.CoArrivals.SelectMany(c => c.Members)) TakeSeat(g.World, c.Member, now, "coarrival");
        }
        var w = g.World;
        var reuse = !g.From.Ended && g.From.Id != 0 && g.From.Members.All(x => g.Members.Any(a => a.Member == x));
        // The visit the members parked in (BARRIER_OK names it; a reused instance's visit counts on below).
        var (fromId, fromVisit) = (g.From.Id, g.From.Visit);
        var inst = reuse ? g.From : NewInstance(InstanceMode.SoloLed, g.Stage, 0, g.Context, now);
        var oldObservers = reuse ? inst.Observers.ToList() : [];
        var oldStage = inst.Stage;
        if (reuse) ResetForVisit(inst, now);

        var everyone = g.Members.Select(a => (Key: a.Member, Current: a.Slot, Preferred: a.Member.PreferredSlot)).ToList();
        var coMembers = g.CoArrivals.SelectMany(c => c.Members).ToList();
        foreach (var c in coMembers) everyone.Add((c.Member, -1, c.Member.PreferredSlot));
        // Observers of this instance whose target stage is entered again become co-arrivals of the new visit (6.6).
        var followers = new List<HubMember>();
        foreach (var o in oldObservers)
        {
            if (SameStage(oldStage, g.Stage) && everyone.Count < Capacity && o.Joining is not null)
            {
                followers.Add(o);
                everyone.Add((o, -1, o.PreferredSlot));
            }
        }
        var map = HubRules.SlotMap(everyone);

        inst.Stage = g.Stage;
        inst.ContextHash = g.Context;
        inst.ArrivalContext = g.Context;
        inst.EntryContext = null;
        inst.Mode = everyone.Count >= 2 ? InstanceMode.Sealed : InstanceMode.SoloLed;
        if (inst.World is { } previous && previous != w) previous.Groups.Remove(inst);
        inst.World = w;
        w.Groups.Add(inst);
        w.Visited.Add(g.Stage);
        Array.Clear(inst.Slots);
        Array.Clear(inst.Reserved);
        foreach (var (member, slot) in map)
        {
            if (member.Instance is { } old && old != inst) DetachFromOld(old, member);
            Place(member, inst, slot, now);
            member.RebuiltFromCheckpoint = false; // (the new visit's engine is the member's own load or the base's snapshot)
        }
        var uploader = g.Members.Select(a => a.Member).OrderBy(x => map[x]).First();
        inst.Snapshot.Uploader = uploader;
        var delays = map.Keys.ToDictionary(x => x, x => DelayController.FromRoundTrips(x.Rtt.All, _sealing.MaxDelayTicks));
        if (inst.Mode == InstanceMode.Sealed)
            foreach (var (member, slot) in map)
                inst.Streams[slot] = NewStream(member, slot, 0, delays[member]);

        // The base engine spawns the co-arrivals (and followers) in its load step with their carries.
        var coList = new List<CoArrival>();
        foreach (var c in coMembers) coList.Add(new CoArrival((byte)map[c.Member], c.Member.Id, c.Carry));
        foreach (var f in followers) coList.Add(new CoArrival((byte)map[f], f.Id, f.Joining!.Carry.Length > 0 ? f.Joining.Carry : FindCarry(f)));

        // Every member of the group gets the same story bytes (their engines apply them in the same load step): the host's (the
        // instance's) confirmed story; a guest's NEW replaces the engines' own stories with it (19.30.4), every other NEW merges it.
        var story = w.Keeper.Story;
        var storyBlob = story.ToBlob();
        var mode = g.Arrival == ArrivalKind.GuestNew ? StoryMode.Replace : StoryMode.Merge;
        foreach (var a in g.Members)
        {
            var m = a.Member;
            m.Arrival = null;
            var slotMap = new sbyte[Wire.MaxSlots];
            Array.Fill(slotMap, (sbyte)-1);
            foreach (var b in g.Members) slotMap[b.Slot] = (sbyte)map[b.Member];
            var arrival = g.Arrival == ArrivalKind.NewInstance && m != w.Host ? ArrivalKind.Starting : g.Arrival;
            m.Peer?.Send(new BarrierOkMsg(fromId, fromVisit, BarrierDecision.New, inst.Id, inst.Visit, g.Stage, g.Point, (byte)map[m], slotMap,
                inst.Mode, (byte)(inst.Mode == InstanceMode.Sealed ? delays[m] : 0), coList, story.Revision, storyBlob, notice, noticeNames)
                { World = WorldPart(w, arrival, mode) }.Frame());
        }
        var coArrival = g.Arrival is ArrivalKind.NewInstance or ArrivalKind.Starting ? ArrivalKind.Starting : g.Arrival;
        foreach (var c in g.CoArrivals)
        {
            foreach (var a in c.Members)
            {
                a.Member.Arrival = null;
                inst.CoArrivalPending.Add(a.Member);
                CoArriveMessage(c.From, a.Member, inst, map[a.Member], delays[a.Member], g.Point, map, coArrival);
            }
            EndIfEmpty(c.From, now);
        }
        foreach (var f in followers)
        {
            var j = f.Joining!;
            f.Joining = null;
            inst.CoArrivalPending.Add(f);
            CoArriveMessage(j.Target, f, inst, map[f], delays[f], j.From.Point, map, ArrivalKind.None);
        }
        foreach (var o in oldObservers.Except(followers)) FallbackNew(o, now);
        if (!reuse) EndIfEmpty(g.From, now);
        // A stage group of an instance that ends goes home as soon as it can take HOME (19.6.5).
        if (w.Ending) SendHome(inst, inst.PresentMask, HomeReason.Ended, now, 0);
        foreach (var x in map.Keys) SendInfo(x, now);
        Note("new", inst, ("members", map.ToDictionary(kv => kv.Value.ToString(), kv => (object?)kv.Key.Index)), ("mode", inst.Mode.ToString()),
            ("co_arrivals", coList.Count), ("notice", notice.ToString()));
    }

    private static byte[] FindCarry(HubMember m) => m.Joining?.Carry ?? [];

    private void CoArriveMessage(HubInstance from, HubMember m, HubInstance inst, int slot, int delay, byte point, Dictionary<HubMember, int> map,
        ArrivalKind arrival)
    {
        var none = new sbyte[Wire.MaxSlots];
        Array.Fill(none, (sbyte)-1);
        m.Cursor = 0;
        // The other members of the new visit by slot, without carries (the co-arrival's engine comes from the base's
        // snapshot): its client knows their names (before, it showed "A player left" for its partners).
        var others = map.Where(kv => kv.Key != m).OrderBy(kv => kv.Value).Select(kv => new CoArrival((byte)kv.Value, kv.Key.Id, [])).ToList();
        var w = inst.World!;
        m.Peer?.Send(new BarrierOkMsg(from.Id, from.Visit, BarrierDecision.CoArrive, inst.Id, inst.Visit, inst.Stage, point, (byte)slot, none,
            inst.Mode, (byte)delay, others, w.Keeper.Story.Revision, w.Keeper.Story.ToBlob())
            {
                // (Arrivals from home into an instance they do not host: the host's story replaces their engines' (client 13:30).)
                World = WorldPart(w, arrival, arrival is ArrivalKind.Starting or ArrivalKind.GuestNew or ArrivalKind.AutoJoin ? StoryMode.Replace : StoryMode.Merge),
            }.Frame());
        Note("co_arrive", inst, ("member", m.Index), ("slot", slot));
    }

    private void SendCoArrivalState(HubInstance i, HubMember m)
    {
        i.CoArrivalPending.Remove(m);
        SendState(m, i, StatePurpose.CoArrival, m.Slot);
    }

    private void ResetForVisit(HubInstance i, long now)
    {
        i.Visit += 1;
        i.Checkpoints = [];
        i.Upload = null;
        i.CheckpointAskedAt = 0;
        i.StateHashes.Clear();
        i.AgreedLoadEnd = null;
        i.CheckpointSuspect = false;
        i.RebuildTick = null;
        i.Log = new TickLog();
        i.Ahead.Clear();
        i.Snapshot = new SnapshotStore();
        i.Forks = [];
        i.PendingEvents.Clear();
        i.Fingerprints.Clear();
        i.Splits.Clear();
        i.Observers.Clear();
        i.CoArrivalPending.Clear();
        Array.Clear(i.Streams);
        i.Parent = null;
        i.ForkTick = 0;
        i.Departing = false;
        i.BossLock = false;
        i.Joinable = false;
        i.MembershipSafe = false;
        i.Reason = StateReason.Loading;
        i.Barrier = null;
        i.Sealing = false;
        i.SealStop = null;
        i.LeadFrom = null;
        i.NextSeal = 0;
        i.S0 = 0;
        i.SoloJoin = null;
        i.LastLogAt = now;
        i.VisitStartedAt = now;
        i.PendingHome = null;
        i.AskedHome = null;
        i.HomeStamped = 0;
        i.HomeTries = 0;
        i.LogOwner = null;
        i.LogFull = false;
        i.Voted.Clear();
        i.ReplayFp.Clear();
        i.Departed.Clear();
        i.AskedJoin = null;
        i.SoloPending = false;
        i.SealedFrom = 0;
    }

    private HubInstance NewInstance(InstanceMode mode, string stage, uint visit, ulong context, long now)
    {
        // (The ids go on across a rebuilt hub: an engine line of the old one never meets a new instance's.)
        var id = _nextInstance;
        while (_instances.ContainsKey(id) || id == 0) id = id >= 999_999_999 ? 1 : id + 1;
        _nextInstance = id >= 999_999_999 ? 1 : id + 1;
        var i = new HubInstance(id)
        {
            Mode = mode, Stage = stage, Visit = visit, ContextHash = context, ArrivalContext = context, CreatedAt = now, LastLogAt = now, VisitStartedAt = now,
        };
        _instances[id] = i;
        return i;
    }

    private void Place(HubMember m, HubInstance i, int slot, long now)
    {
        i.Slots[slot] = m;
        if (i.Reserved[slot] == m) i.Reserved[slot] = null;
        if (m.Instance != i) m.InstanceSince = now;
        m.Instance = i;
        m.Slot = slot;
        if (!string.IsNullOrEmpty(i.Stage))
        {
            m.LoneStage = null;
            m.LoneKey = 0;
            m.GaveUp = false;
            // The engine line this visit's counters grow in: besides its own instance 0 lines the only kind of line the
            // member may report (8.7).
            AllowLine(m, i.Id, i.Visit);
        }
        m.Cursor = 0;
        m.Heading = null;
        m.Resyncing = false;
        m.JoinedVia = null;
        if (m.PreferredSlot < 0 && i.Mode != InstanceMode.Hub) m.PreferredSlot = slot;
    }

    private void EndInstance(HubInstance i, long now)
    {
        if (i.Ended) return;
        i.Ended = true;
        _instances.Remove(i.Id);
        i.World?.Groups.Remove(i);
        foreach (var o in i.Observers.ToList()) FallbackNew(o, now);
        i.Observers.Clear();
        _holds.RemoveAll(h => h.Target == i && h.Members.Count == 0);
        foreach (var h in _holds.Where(h => h.Target == i).ToList())
        {
            _holds.Remove(h);
            h.Hold = HoldKind.None;
            Redecide(h, now);
        }
        Note("end", i);
    }

    // Join -----------------------------------------------------------------------------------------------------------------

    /// <summary>JOIN (SEAMLESS.md 3.4 step 3, 6.6): slots are reserved, the members catch up as observers, READY stamps the JOIN.</summary>
    private void Join(ArrivalGroup g, HubInstance target, long now)
    {
        _holds.Remove(g);
        g.Hold = HoldKind.None;
        // (The last check before the seat: a member removed from the target's instance never joins it, 19.6.6.)
        if (SendRemovedHome(g, target.World, now)) return;
        var free =Enumerable.Range(0, Wire.MaxSlots).Select(s => target.Slots[s] is null && target.Reserved[s] is null).ToArray();
        foreach (var a in g.Members)
        {
            var m = a.Member;
            var slot = HubRules.JoinSlot(free, m.PreferredSlot);
            free[slot] = false;
            target.Reserved[slot] = m;
            DetachFromOld(g.From, m);
            m.Instance = null;
            m.Slot = -1;
            m.Arrival = null;
            var fresh = g.Arrival is ArrivalKind.AutoJoin or ArrivalKind.Starting;
            m.Joining = new JoinState(target, slot, g, a.Slot) { Carry = a.Carry, PlannedCheckpoint = CheckpointPathOpen(target), Fresh = fresh };
            target.Observers.Add(m);
            var none = new sbyte[Wire.MaxSlots];
            Array.Fill(none, (sbyte)-1);
            var w = target.World!;
            if (!w.Seats.ContainsKey(m)) TakeSeat(w, m, now, "join");
            m.Peer?.Send(new BarrierOkMsg(g.From.Id, g.From.Visit, BarrierDecision.Join, target.Id, target.Visit, g.Stage, g.Point, (byte)slot, none,
                target.Mode, (byte)DelayController.FromRoundTrips(m.Rtt.All, _sealing.MaxDelayTicks), [], w.Keeper.Story.Revision, w.Keeper.Story.ToBlob())
                { World = WorldPart(w, fresh ? g.Arrival : ArrivalKind.None, fresh ? StoryMode.Replace : StoryMode.Merge) }.Frame());
            Note("join_decided", target, ("member", m.Id), ("slot", slot), ("wid", w.Wid));
            SendInfo(m, now);
            DeliverState(m, now);
        }
        EndIfEmpty(g.From, now);
    }

    /// <summary>
    /// Sends the joiner its state once the snapshot is complete (SEAMLESS.md 6.13.5): a fresh checkpoint (younger than
    /// Instances:FreshCheckpointMs), else the donor's next one (asked now, awaited Instances:CheckpointWaitMs), else the
    /// newest stored one, else the classic path (the snapshot and every tick since).
    /// </summary>
    private void DeliverState(HubMember m, long now)
    {
        if (m.Joining is not { } j || j.StateSent || !j.Target.Snapshot.Complete) return;
        var t = j.Target;
        if (!j.Classic && CheckpointPathOpen(t))
        {
            var cp = t.Checkpoints.LastOrDefault(c => Ms(now - c.At) <= _inst.FreshCheckpointMs && UsableCheckpoint(t, c));
            if (cp is null)
            {
                if (j.CheckpointWaitUntil == 0)
                {
                    j.CheckpointWaitUntil = now + FromMs(_inst.CheckpointWaitMs);
                    AskCheckpoint(t, now);
                    return;
                }
                if (now < j.CheckpointWaitUntil) return; // the checkpoint's arrival or the housekeeping delivers
                cp = t.Checkpoints.LastOrDefault(c => UsableCheckpoint(t, c));
                if (cp is null) Count("checkpoint_wait_expired");
            }
            if (cp is not null)
            {
                j.StateSent = true;
                j.StateSentAt = now;
                j.FromCheckpoint = true;
                j.Checkpoint = cp;
                Count("checkpoint_joins");
                SendState(m, t, StatePurpose.Join, j.Slot, cp);
                return;
            }
        }
        // The classic state after a planned checkpoint join (CHECKPOINT_BAD, no checkpoint in time, a READY mismatch, a path
        // that became suspect): the ranking took the short catch-up, the whole visit may be far longer (round J's review: a
        // long visit's joiner waited well over 30 s where it got NEW before).
        if (j.PlannedCheckpoint && HubRules.CatchUpSeconds(t.StreamEnd, m.Rate == 0 ? _inst.DefaultReplayRate : m.Rate) > _inst.MaxCatchUpSeconds)
        {
            Count("classic_too_long");
            Note("classic_too_long", t, ("member", m.Index), ("ticks", t.StreamEnd));
            FallbackNew(m, now, BarrierNotice.TooLongToCatchUp);
            return;
        }
        j.StateSent = true;
        j.StateSentAt = now;
        j.FromCheckpoint = false;
        j.Checkpoint = null;
        SendState(m, t, StatePurpose.Join, j.Slot);
    }

    /// <summary>Round J: joins of this instance may take a checkpoint (the switch is on, the path is not suspect and the
    /// members told the load end).</summary>
    private bool CheckpointPathOpen(HubInstance t) =>
        _inst.CheckpointJoins && Info.Protocol >= Wire.CheckpointProtocol && !t.CheckpointSuspect && t.AgreedLoadEnd is not null &&
        t.Mode != InstanceMode.Hub;

    /// <summary>A stored checkpoint serves when its load end is the agreed one and the log still holds its load ticks.</summary>
    private static bool UsableCheckpoint(HubInstance t, StoredCheckpoint c) =>
        t.AgreedLoadEnd is { } k && c.LoadEnd == k && k <= c.Tick && k <= t.Log.Count;

    /// <summary>The donor (the member that reports STATE: connected, longest in the instance, then the lowest slot) uploads its
    /// tick state at its next state-safe tick.</summary>
    private void AskCheckpoint(HubInstance t, long now, byte flags = 0)
    {
        if (t.Upload is not null && Ms(now - t.CheckpointAskedAt) < _inst.CheckpointWaitMs * 4) return; // one upload at a time
        var donor = t.Seats.Where(x => x.Member.Connected && !x.Member.Resyncing).OrderBy(x => x.Member.InstanceSince).ThenBy(x => x.Slot)
            .Select(x => x.Member).FirstOrDefault();
        if (donor?.Peer is null) return;
        t.CheckpointAskedAt = now;
        t.Upload = null;
        t.AskedDonor = donor;
        donor.Peer.Send(new CheckpointAskMsg(t.Id, t.Visit, 0, flags).Frame());
        Count("checkpoint_asks");
        if (flags == 0) Note("checkpoint_ask", t, ("donor", donor.Index));
    }

    /// <summary>STATEHASH (6.13.4): the members' hashes of a checkpoint tick; equal reports make the load end and a stored
    /// checkpoint of that tick agreed. A solo leader's report is accepted unagreed (the joiner's own check verifies it).</summary>
    private void OnStateHash(HubMember m, StateHashMsg msg, long now)
    {
        if (Mine(m, msg.Inst, msg.Visit) is not { } i) return;
        if ((msg.Flags & StateHashMsg.LoadEndOnly) != 0)
        {
            // A solo leader's report (round J's review): no hash to compare, only the visit's load end K.
            if (i.AgreedLoadEnd is null)
            {
                i.AgreedLoadEnd = msg.LoadEnd;
                Note("load_end", i, ("k", msg.LoadEnd), ("agreed", false));
            }
            return;
        }
        if (!i.StateHashes.TryGetValue(msg.Tick, out var d)) i.StateHashes[msg.Tick] = d = [];
        if (!d.TryAdd(m.Slot, (msg.Hash, msg.LoadEnd)))
        {
            Count("statehash_repeat");
            return;
        }
        while (i.StateHashes.Count > 16) i.StateHashes.Remove(i.StateHashes.Keys.First());
        // A fork reported at or before the tick waits for its confirmation: the sides' states differ from the fork on (a
        // leaver's report may still come under the parent's id), so nothing is compared (round J's review).
        if (i.Splits.Values.Any(v => !v.Done && msg.Tick > v.Tick)) return;
        var expected = i.Seats.Where(x => x.Member.Connected && !x.Member.Resyncing && (i.Streams[x.Slot] is not { } s || s.FromTick <= msg.Tick))
            .Select(x => x.Slot).ToList();
        if (expected.Count == 0 || !expected.All(d.ContainsKey)) return;
        var values = expected.Select(s => d[s]).Distinct().ToList();
        if (values.Count > 1)
        {
            // A mismatch: the fingerprint vote (NETFP) resyncs the member that is off; the checkpoint path of the instance
            // is not trusted any more (6.13.4; round J's review: at any time, not only soon after a rebuild - two engines
            // whose tick states differ make no checkpoint to trust).
            Count("statehash_mismatch");
            Note("statehash_mismatch", i, ("tick", msg.Tick), ("hashes", expected.ToDictionary(s => s.ToString(), s => d[s].Hash.ToString("x16"))));
            MarkSuspect(i, msg.Tick, expected.Any(s => i.Slots[s] is { RebuiltFromCheckpoint: true }) ? "statehash_rebuilt" : "statehash");
            return;
        }
        var (hash, k) = values[0];
        if (i.AgreedLoadEnd is null)
        {
            i.AgreedLoadEnd = k;
            Note("load_end", i, ("k", k), ("agreed", expected.Count >= 2));
        }
        else if (i.AgreedLoadEnd != k)
        {
            Count("load_end_mismatch");
            MarkSuspect(i, msg.Tick, "load_end");
        }
        foreach (var c in i.Checkpoints.Where(c => c.Tick == msg.Tick && c.Hash == hash)) c.Agreed = expected.Count >= 2;
    }

    /// <summary>CHECKPOINT (6.13.4): the asked donor's tick state in chunks; once complete it is stored (the newest
    /// Instances:KeepCheckpoints) and the joins that wait for it get their state.</summary>
    private void OnCheckpoint(HubMember m, CheckpointMsg msg, long now)
    {
        if (Mine(m, msg.Inst, msg.Visit) is not { } i) return;
        if (msg.Offset == 0)
        {
            // Only the asked donor's upload (round J's review: any member's chunk at offset 0 replaced a running upload and
            // reserved up to 8 MB, unasked and as often as it liked).
            if (i.AskedDonor != m)
            {
                Count("checkpoint_unasked");
                return;
            }
            i.Upload = new CheckpointUpload(m, msg.Tick, msg.Hash, msg.LoadEnd, msg.Total);
        }
        if (i.Upload is not { } u || u.Donor != m || u.Tick != msg.Tick || u.Hash != msg.Hash || u.Buffer.Length != msg.Total || msg.Offset != u.Received)
        {
            Count("checkpoint_chunk_dropped");
            return;
        }
        msg.Chunk.CopyTo(u.Buffer, (int)msg.Offset);
        u.Received += (uint)msg.Chunk.Length;
        if (u.Received < u.Buffer.Length) return;
        i.Upload = null;
        i.AskedDonor = null;
        var stored = new StoredCheckpoint(u.Tick, u.Hash, u.LoadEnd, u.Buffer, now) { Donor = m };
        if (i.StateHashes.TryGetValue(u.Tick, out var d))
        {
            var expected = i.Seats.Where(x => x.Member.Connected).Select(x => x.Slot).ToList();
            stored.Agreed = expected.Count >= 2 && expected.All(s => d.TryGetValue(s, out var h) && h.Hash == u.Hash);
        }
        if (i.AgreedLoadEnd is null)
        {
            // (The donor's STATEHASH may come later; its load end stands until the members tell theirs.)
            i.AgreedLoadEnd = u.LoadEnd;
            Note("load_end", i, ("k", u.LoadEnd), ("agreed", false));
        }
        i.Checkpoints.Add(stored);
        while (i.Checkpoints.Count > Math.Max(1, _inst.KeepCheckpoints)) i.Checkpoints.RemoveAt(0);
        Count("checkpoints");
        Count("checkpoint_bytes", stored.Bytes.Length);
        Note("checkpoint", i, ("tick", u.Tick), ("bytes", stored.Bytes.Length), ("donor", m.Index),
            ("ms", Math.Round(Ms(now - i.CheckpointAskedAt))));
        foreach (var o in i.Observers.ToList())
            if (o.Joining is { } oj && oj.Target == i && !oj.StateSent) DeliverState(o, now);
    }

    /// <summary>CHECKPOINT_BAD (6.13.5): the joiner's rebuild failed; it gets the classic state.</summary>
    private void OnCheckpointBad(HubMember m, CheckpointBadMsg msg, long now)
    {
        if (m.Joining is null && m.Resyncing && Mine(m, msg.Inst, msg.Visit) is { } own)
        {
            Count("checkpoint_bad");
            Note("checkpoint_bad", own, ("member", m.Index), ("tick", msg.Tick), ("reason", msg.Reason.ToString()), ("section", msg.Section), ("resync", true));
            MarkSuspect(own, msg.Tick, "resync_bad");
            m.ResyncFromCheckpoint = false;
            SendState(m, own, StatePurpose.Resync, m.Slot);
            return;
        }
        if (m.Joining is not { } j || j.Target.Id != msg.Inst || j.Target.Visit != msg.Visit) return;
        Count("checkpoint_bad");
        Note("checkpoint_bad", j.Target, ("member", m.Index), ("tick", msg.Tick), ("reason", msg.Reason.ToString()), ("section", msg.Section));
        _log.LogWarning("Room {Room} instance {Inst}: a rebuild from the checkpoint of tick {Tick} failed ({Reason}, {Section})", Code,
            j.Target.Id, msg.Tick, msg.Reason, msg.Section);
        j.Classic = true;
        j.StateSent = false;
        DeliverState(m, now);
    }

    /// <summary>The instance's checkpoint path is not trusted any more: its joins and resyncs take the classic path.</summary>
    private void MarkSuspect(HubInstance i, uint tick, string why)
    {
        if (i.CheckpointSuspect) return;
        i.CheckpointSuspect = true;
        Count("checkpoint_suspect");
        Note("checkpoint_suspect", i, ("tick", tick), ("why", why), ("rebuild", i.RebuildTick ?? 0));
        _log.LogWarning("Room {Room} instance {Inst}: checkpoint path suspect ({Why} at tick {Tick})", Code, i.Id, why, tick);
    }

    /// <summary>
    /// Sends the member the instance's state (snapshot, forks, every record so far). The records are captured under
    /// the lock; the blob (every block decompressed) is built outside it when the relay runs the job (SEAMLESS.md 6.6:
    /// a join of a long log must not hold up the room and the sealing thread). Nothing is pumped to the member until
    /// the state went out (<see cref="FinishState"/>), so the stream follows it in order.
    /// </summary>
    private bool SendState(HubMember m, HubInstance i, StatePurpose purpose, int slot, StoredCheckpoint? checkpoint = null)
    {
        if (m.Peer is not { } peer) return false;
        if (checkpoint is null) m.RebuiltFromCheckpoint = false; // (a classic state replaces the member's engine)
        var end = i.StreamEnd;
        var ahead = new List<byte[]>();
        for (var t = i.Log.Count; t < end && i.Ahead.TryGetValue(t, out var rec); ++t) ahead.Add(rec);
        // Every member has a budget of state bytes per minute (Relay:StateBytesPerMinute): an honest client needs a join, a
        // co-arrival or a resync now and then; a flood of arrivals is closed as abuse instead of building state after state.
        var estimate = checkpoint is null
            ? (i.Snapshot.Bytes?.Length ?? 0) + i.Log.TotalRawBytes + ahead.Sum(r => (long)r.Length)
            : (i.Snapshot.Bytes?.Length ?? 0) + checkpoint.Bytes.Length + 64L * (checkpoint.LoadEnd + Math.Max(0, (long)end - checkpoint.Tick));
        if (!TakeStateBudget(m, estimate, Stopwatch.GetTimestamp()))
        {
            Count("state_refused");
            Note("state_refused", i, ("member", m.Index), ("bytes", estimate));
            _log.LogWarning("Room {Room}: member {Member} asked for more state transfers than its budget; closing it", Code, m.Index);
            peer.Close(CloseReason.Abuse, "Too many state transfers.");
            return false;
        }
        var job = new StateJob(this, m, peer, i, i.Visit, purpose, (byte)slot, i.Snapshot.Bytes ?? [], [.. i.Forks], i.Log.Capture(), ahead,
            checkpoint is null ? end : Math.Max(end, checkpoint.Tick)) { Checkpoint = checkpoint, AgreedLoadEnd = i.AgreedLoadEnd ?? 0 };
        m.PendingState = job;
        if (_stateRunner is null)
        {
            job.Build();
            FinishState(job);
        }
        else _stateRunner(job);
        return true;
    }

    /// <summary>Takes <paramref name="bytes"/> from the member's state budget; false when it is spent (a burst of a minute's
    /// allowance, refilled over a minute; a single transfer larger than the whole budget passes when the budget is full).</summary>
    private bool TakeStateBudget(HubMember m, long bytes, long now)
    {
        var cap = (double)Math.Max(1, _relay.StateBytesPerMinute);
        if (double.IsNaN(m.StateTokens)) m.StateTokens = cap;
        else m.StateTokens = Math.Min(cap, m.StateTokens + Ms(now - m.StateStamp) / 60_000.0 * cap);
        m.StateStamp = now;
        if (m.StateTokens < bytes && m.StateTokens < cap) return false;
        m.StateTokens -= bytes;
        return true;
    }

    /// <summary>Sends a built state (under the room lock): its chunks, then the stream from its end.</summary>
    public void FinishState(StateJob job)
    {
        var m = job.Member;
        if (m.PendingState != job)
        {
            Count("state_superseded");
            return;
        }
        m.PendingState = null;
        var i = job.Instance;
        var current = m.Peer == job.Peer && !i.Ended && i.Visit == job.Visit && (m.Instance == i || m.Joining?.Target == i);
        if (!current || job.Blob is not { } blob)
        {
            Count("state_dropped");
            return;
        }
        for (var off = 0; off < blob.Length || off == 0; off += Wire.MaxChunkBytes)
        {
            var n = Math.Min(Wire.MaxChunkBytes, blob.Length - off);
            m.Peer?.Send(new StateDataMsg(i.Id, i.Visit, (uint)blob.Length, (uint)off, blob.AsSpan(off, n).ToArray()).Frame(), bulk: true);
            if (blob.Length == 0) break;
        }
        m.Cursor = job.End;
        Count("state_bytes", blob.Length);
        Note("state", i, ("member", m.Index), ("purpose", job.Purpose.ToString()), ("bytes", blob.Length), ("ticks", job.End),
            ("checkpoint", job.Checkpoint?.Tick ?? 0));
        Pump(i, m);
    }

    private void OnReady(HubMember m, ReadyMsg msg, long now)
    {
        if (m.Joining is null && m.Resyncing && Mine(m, msg.Inst, msg.Visit) is { } own)
        {
            // A resync's end: the member never left the instance and sends its pads again (no JOIN, 6.9).
            m.Resyncing = false;
            if (msg.Rate > 0) m.Rate = Math.Min(msg.Rate, MaxReplayRate);
            // Its first pads after the resync go out at once and come late by design: the delay rule does not count them.
            if (m.Slot >= 0 && own.Streams[m.Slot] is { } resynced)
                resynced.GraceUntil = Math.Max(resynced.GraceUntil, own.NextSeal + (uint)resynced.Delay.Current + 12);
            Count("resync_ready");
            var rebuilt = (msg.Flags & ReadyMsg.FromCheckpoint) != 0;
            m.RebuiltFromCheckpoint = rebuilt;
            Note("resync_ready", own, ("member", m.Index), ("tick", msg.Tick), ("fp", msg.NetFp.ToString("x16")), ("checkpoint", rebuilt));
            // Its reports from before the resync are its old engine's: they vote on nothing any more.
            foreach (var reports in own.Fingerprints.Values) reports.Remove(m.Slot);
            if (rebuilt)
            {
                // A rebuild from a checkpoint is not independent of the members (6.13.4): it never judges; a desync soon
                // after it makes the checkpoint path suspect.
                own.RebuildTick = msg.Tick;
                return;
            }
            // The replay's fingerprint is what the log gives at that tick: a member that reported another one has the
            // engine that is off (with two members the vote cannot tell; before, the healthy one was resynced again and
            // again). Its own reports of that tick are checked as they come (OnNetFp).
            if (msg.Tick % 60 == 0 && msg.NetFp != 0)
            {
                own.ReplayFp[msg.Tick] = msg.NetFp;
                while (own.ReplayFp.Count > 16) own.ReplayFp.Remove(own.ReplayFp.Keys.First());
                if (own.Fingerprints.TryGetValue(msg.Tick, out var reported))
                    foreach (var (s, hash) in reported.ToList())
                        if (s != m.Slot && hash != msg.NetFp && own.Slots[s] is { } other && !own.Ended) ReplayDisagrees(own, other, msg.Tick, now);
            }
            return;
        }
        if (m.Joining is not { } j || j.Target.Id != msg.Inst || j.Target.Visit != msg.Visit || j.Ready) return;
        var target = j.Target;
        var fromCheckpoint = (msg.Flags & ReadyMsg.FromCheckpoint) != 0;
        if (msg.Rate > 0 && !fromCheckpoint) m.Rate = Math.Min(msg.Rate, MaxReplayRate); // (a few ticks measure no rate)
        if (msg.Carry.Length > 0) j.Carry = msg.Carry;
        // Protocol 48 (6.13.5): a READY from a checkpoint names the checkpoint's check tick or a later multiple of 60. Any other
        // tick is none the relay can check (before the check tick: fewer than Wire.ReadyCheckTicks replayed steps; off the
        // raster: nobody reports it); the client never sends one, so the joiner gets the classic state (lane jready's review:
        // before, such a READY was taken unchecked at once).
        if (fromCheckpoint && Info.Protocol >= Wire.ReadyCheckProtocol && j.Checkpoint is { } named && msg.Tick != named.CheckTick &&
            (msg.Tick < named.CheckTick || msg.Tick % 60 != 0))
        {
            Count("ready_offgrid");
            Note("ready_offgrid", target, ("member", m.Index), ("tick", msg.Tick), ("check", named.CheckTick));
            j.PendingReady = null;
            j.Classic = true;
            j.ReadyWaitExpired = false;
            j.StateSent = false;
            DeliverState(m, now);
            return;
        }
        // A fork not confirmed yet at or before the READY's tick (6.7; TryVote and OnStateHash wait alike): from its tick the
        // two sides' engines differ, a leaver's report (a leaver donor's check fingerprint too) still comes under this id, and
        // the side the joiner keeps (the one that keeps the instance id) may be the other one; the READY waits until the fork is
        // confirmed or turned down (TryFork checks it again), at most Instances:ReadyFingerprintWaitMs (lane jready's review:
        // a leaver donor's check fingerprint made a healthy rebuild's READY a mismatch).
        var forkOpen = target.Splits.Values.Any(v => !v.Done && msg.Tick >= v.Tick);
        // What the READY's fingerprint is compared with: the members' NETFP of its tick (the most common one), else, in a
        // protocol 48 room, the donor's fingerprint of the checkpoint's check tick (its NETFP Wire.ReadyCheckTicks after the
        // checkpoint; only while the donor is in this instance: after a fork between the two its side is the other one).
        ulong? reference = !forkOpen && target.Fingerprints.TryGetValue(msg.Tick, out var fps) && fps.Count > 0
            ? fps.Values.GroupBy(x => x).OrderByDescending(x => x.Count()).First().Key
            : null;
        var checkTick = fromCheckpoint && Info.Protocol >= Wire.ReadyCheckProtocol && j.Checkpoint is { } used && msg.Tick == used.CheckTick &&
                        used.Donor?.Instance == target;
        if (reference is null && !forkOpen && checkTick && j.Checkpoint!.CheckFp is { } donorFp) reference = donorFp;
        // A READY from a checkpoint names its check tick or a later multiple of 60 (protocol 48; before, the first multiple of
        // 60 it replayed): when its fingerprint has not come yet (a sealed tick reaches the members and the joiner at about the
        // same time), the READY waits for it (OnNetFp), at most Instances:ReadyFingerprintWaitMs (round J's review: unchecked, a
        // rebuild that missed a field only showed at the next vote after the JOIN, a resync screen right after joining).
        // (A second call for the READY that waits keeps its wait's start.)
        if (!j.ReadyWaitExpired && reference is null && (forkOpen || fromCheckpoint && (msg.Tick % 60 == 0 || checkTick)))
        {
            if (j.PendingReady is null)
            {
                j.PendingReady = msg;
                j.PendingReadyAt = now;
                Count("ready_deferred");
            }
            return;
        }
        j.PendingReady = null;
        // (Nothing to compare with: a tick without fingerprints; a donor that left the instance before its check tick, or
        // that a fork moved to the other side: such a READY is taken at once, there is nothing to wait for; a field the rebuild
        // missed shows at the first vote after the JOIN, 6.13.4.)
        if (fromCheckpoint && reference is null && !j.ReadyWaitExpired) Count("ready_without_reference");
        if (reference is { } common)
        {
            if (common != msg.NetFp)
            {
                ++j.Attempts;
                Count("ready_mismatch");
                Note("ready_mismatch", target, ("member", m.Index), ("tick", msg.Tick), ("attempt", j.Attempts), ("checkpoint", fromCheckpoint));
                // A rebuild from a checkpoint that does not match: the classic path next (and the checkpoint path is suspect).
                if (fromCheckpoint)
                {
                    MarkSuspect(target, msg.Tick, "ready");
                    j.Classic = true;
                    j.ReadyWaitExpired = false;
                }
                if (j.Attempts >= 2 && !fromCheckpoint)
                {
                    FallbackNew(m, now, BarrierNotice.JoinFailed);
                    return;
                }
                j.StateSent = false;
                DeliverState(m, now);
                return;
            }
        }
        j.Ready = true;
        m.RebuiltFromCheckpoint = fromCheckpoint;
        if (fromCheckpoint)
        {
            target.RebuildTick = msg.Tick;
            Count("ready_from_checkpoint");
        }
        Note("ready", target, ("member", m.Index), ("tick", msg.Tick), ("rate", msg.Rate), ("checkpoint", fromCheckpoint),
            ("checked", reference is not null), ("from", fromCheckpoint ? j.Checkpoint?.Tick ?? 0 : 0));
        StampJoin(target, m, now);
    }

    /// <summary>Stamps the JOIN of a joiner that sent READY (sealed: at a future tick; solo-led: JOIN_REQ to the leader).</summary>
    private void StampJoin(HubInstance target, HubMember m, long now)
    {
        var j = m.Joining!;
        if (j.Stamped) return;
        var d = DelayController.FromRoundTrips(m.Rtt.All, _sealing.MaxDelayTicks);
        if (target.Mode == InstanceMode.Sealed && target.Sealing && target.Barrier is null)
        {
            var at = StampTick(target);
            target.AddEvent(at, TickEvent.Join(j.Slot, j.From.Point, j.Carry));
            target.AddEvent(at, TickEvent.Delay(j.Slot, d));
            target.Streams[j.Slot] = NewStream(m, j.Slot, at, d);
            foreach (var st in target.Streams)
                st?.GraceThrough(at + (uint)st.Delay.Current + SlotStream.JoinGraceTicks);
            j.Stamped = true;
            BecomeMember(target, m, j);
            Note("join", target, ("member", m.Index), ("slot", j.Slot), ("tick", at));
        }
        else if (target.Mode == InstanceMode.SoloLed && target.Barrier is null && target.Leader is { Connected: true } leader && target.SoloJoin is null)
        {
            var ld = DelayController.FromRoundTrips(leader.Rtt.All, _sealing.MaxDelayTicks);
            target.SoloJoin = new PendingSoloJoin(m, j.Slot, j.From.Point, j.Carry, d, ld, now);
            target.AskedJoin = target.SoloJoin;
            j.Stamped = true;
            ++j.JoinRequests;
            leader.Peer!.Send(new JoinReqMsg(target.Id, target.Visit, (byte)j.Slot, (byte)ld).Frame());
            Note("join_req", target, ("member", m.Index), ("slot", j.Slot));
        }
        // Otherwise (a sealed start that waits for hashes, another JOIN_REQ in flight) the housekeeping tries again.
    }

    private void BecomeMember(HubInstance target, HubMember m, JoinState j)
    {
        target.Observers.Remove(m);
        target.Reserved[j.Slot] = null;
        var cursor = m.Cursor;
        m.Joining = null;
        target.Slots[j.Slot] = m;
        m.Instance = target;
        m.Slot = j.Slot;
        m.Cursor = cursor;
        m.InstanceSince = Stopwatch.GetTimestamp();
        m.JoinedVia = j;
        m.GaveUp = false;
        if (m.PreferredSlot < 0) m.PreferredSlot = j.Slot;
        // The joined visit's engine line, as Place gives it (8.7): the joiner's counters grow in it like its partners'
        // (a line keeps the largest growth reported, so the same line from two members does not count twice).
        if (!string.IsNullOrEmpty(target.Stage)) AllowLine(m, target.Id, target.Visit);
        if (target.World is { } w) MarkInfoDirty(w);
    }

    private void OnJoinAt(HubMember m, JoinAtMsg msg, long now)
    {
        if (Mine(m, msg.Inst, msg.Visit) is not { Mode: InstanceMode.SoloLed } i || i.Leader != m) return;
        if (i.AskedHome is { } home && i.SoloJoin is null && i.AskedJoin is null)
        {
            // JOIN_REQ kind home (19.6.5): from J the stage group is sealed with its leader alone; SEALED(J) carries HOME and
            // LEAD_FROM(J + 1), so the leader leads alone again after its departure.
            i.AskedHome = null;
            var hc = CurrentTick(i, now);
            var hj = Math.Max(msg.Tick, i.Log.Count);
            var hr = Math.Max(now + (long)((double)Math.Max(0, (long)hj - hc) * Wire.TickSeconds * Freq) - FromMs(m.Rtt.Median / 2), now + TickTs);
            i.S0 = hr - (long)(hj * Wire.TickSeconds * Freq);
            i.NextSeal = hj;
            i.SealedFrom = hj;
            i.LogOwner = m;
            i.Mode = InstanceMode.Sealed;
            i.Streams[m.Slot] = NewStream(m, m.Slot, hj, DelayController.FromRoundTrips(m.Rtt.All, _sealing.MaxDelayTicks));
            m.Cursor = hj;
            i.AddEvent(hj, TickEvent.Home(home.Mask, home.Reason));
            i.HomeStamped |= home.Mask;
            ScheduleLeadFrom(i, hj + 1);
            Count("home_stamped");
            Note("home", i, ("mask", home.Mask), ("reason", home.Reason.ToString()), ("tick", hj), ("wid", i.World?.Wid ?? 0), ("solo_leader", m.Id));
            i.Sealing = true;
            _sealer?.Invoke();
            return;
        }
        if ((i.SoloJoin ?? i.AskedJoin) is not { } p)
        {
            Count("join_at_ignored"); // never asked
            return;
        }
        // The joiner went between JOIN_REQ and JOIN_AT (BYE, a lost connection, its own barrier, the tries ran out): the
        // leader's client already samples with delay and waits for SEALED(J). The instance seals J with the leader alone
        // and turns solo-led after it (LEAD_FROM J + 1), so the leader leads alone again (before, it froze for good).
        var orphan = i.SoloJoin is null || p.Joiner.Joining is not { } waiting || waiting.Target != i;
        i.AskedJoin = null;
        var current = CurrentTick(i, now);
        var at = Math.Max(msg.Tick, i.Log.Count);
        // S(J) where the leader reaches J, less its one-way time, so SEALED(J) arrives as it gets there.
        var reach = now + (long)((double)Math.Max(0, (long)at - current) * Wire.TickSeconds * Freq) - FromMs(m.Rtt.Median / 2);
        reach = Math.Max(reach, now + TickTs);
        i.S0 = reach - (long)(at * Wire.TickSeconds * Freq);
        i.NextSeal = at;
        i.SealedFrom = at;
        i.LogOwner = m;
        i.Mode = InstanceMode.Sealed;
        i.SoloJoin = null;
        i.Streams[m.Slot] = NewStream(m, m.Slot, at, p.LeaderDelay);
        m.Cursor = at;
        if (orphan)
        {
            ScheduleLeadFrom(i, at + 1);
            Count("join_at_orphan");
            Note("join_at_orphan", i, ("tick", at), ("solo_leader", m.Index));
        }
        else
        {
            i.Streams[p.Slot] = NewStream(p.Joiner, p.Slot, at, p.JoinerDelay);
            i.AddEvent(at, TickEvent.Join(p.Slot, p.Point, p.Carry));
            i.AddEvent(at, TickEvent.Delay(p.Slot, p.JoinerDelay));
            i.AddEvent(at, TickEvent.Delay(m.Slot, p.LeaderDelay));
            foreach (var st in i.Streams)
                st?.GraceThrough(at + (uint)st.Delay.Current + SlotStream.JoinGraceTicks);
            BecomeMember(i, p.Joiner, p.Joiner.Joining!);
            Note("join", i, ("member", p.Joiner.Index), ("slot", p.Slot), ("tick", at), ("solo_leader", m.Index));
        }
        i.Sealing = true;
        StampPendingHome(i, now);
        _sealer?.Invoke();
    }

    /// <summary>
    /// A joiner whose target went away (or whose READY failed twice) restores its own load snapshot and enters as NEW: a fresh
    /// arrival's automatic join (19.7.1) becomes an own instance on its own story (its seat in the target's goes); a member inside
    /// an instance a new stage group of it (an instance that ends sends it home at once).
    /// </summary>
    private void FallbackNew(HubMember m, long now, BarrierNotice notice = BarrierNotice.None, JoinState? joined = null)
    {
        // (joined: a joiner the server made a member already, whose engine never got its JOIN: a gap before J.)
        if ((m.Joining ?? joined) is not { } j) return;
        j.Target.Observers.Remove(m);
        if (j.Target.Reserved[j.Slot] == m) j.Target.Reserved[j.Slot] = null;
        if (j.Target.SoloJoin?.Joiner == m) j.Target.SoloJoin = null;
        m.Joining = null;
        m.JoinedVia = null;
        // A fresh arrival's JOIN replaced its engine's story with the host's, its fallback load included (client 13:30): the
        // fallback is a new stage group of the target's instance (a guest's NEW), never an own instance on that story; an
        // instance that ends sends it home at once.
        var target = j.Target.World;
        WorldInstance w;
        var arrival = ArrivalKind.None;
        if (target is not null && _worlds.ContainsKey(target.Wid))
        {
            w = target;
            if (!w.Seats.ContainsKey(m) && !w.Ending) TakeSeat(w, m, now, "new_group");
        }
        else
        {
            w = CreateWorld(m, j.From.Stage, j.From.Context, now);
            arrival = ArrivalKind.NewInstance;
        }
        var inst = NewInstance(InstanceMode.SoloLed, j.From.Stage, 1, j.From.Context, now);
        Place(m, inst, 0, now);
        inst.World = w;
        w.Groups.Add(inst);
        w.Visited.Add(inst.Stage);
        inst.Snapshot.Uploader = m;
        var map = new sbyte[Wire.MaxSlots];
        Array.Fill(map, (sbyte)-1);
        map[j.OldSlot] = 0;
        m.Peer?.Send(new BarrierOkMsg(j.Target.Id, j.Target.Visit, BarrierDecision.New, inst.Id, inst.Visit, inst.Stage, j.From.Point, 0, map,
            InstanceMode.SoloLed, 0, [], w.Keeper.Story.Revision, w.Keeper.Story.ToBlob(),
            notice == BarrierNotice.None ? BarrierNotice.HoldExpired : notice, []) { World = WorldPart(w, arrival, StoryMode.Merge) }.Frame());
        Note("fallback_new", inst, ("member", m.Index), ("from", j.Target.Id), ("wid", w.Wid));
        if (w.Ending) SendHome(inst, inst.PresentMask, HomeReason.Ended, now, 0);
        SendInfo(m, now);
        // A joiner removed during its join (19.6.6) holds no seat (TakeSeat refused it): the HOME it owes goes to its new stage group
        // at once (JOIN_REQ home), not at the next housekeeping pass.
        if (m.HomeWish is { } wish && wish.Wid == w.Wid && !w.Seats.ContainsKey(m)) RetryHome(m, now);
    }
    // Fork -----------------------------------------------------------------------------------------------------------------

    private void OnSplit(HubMember m, SplitMsg msg, long now)
    {
        // The fork moved the reporter already (its SPLIT names the id it had before): fine.
        if (Mine(m, msg.Inst, msg.Visit) is not { Mode: InstanceMode.Sealed } i || i.Id != msg.Inst) return;
        // A fork happens in a tick every member executed: a sealed one, not long ago.
        if (msg.Tick >= i.NextSeal || i.NextSeal - msg.Tick > ForkReportTicks)
        {
            Count("split_out_of_range");
            return;
        }
        if (i.Splits.TryGetValue(msg.Tick, out var vote))
        {
            if (vote.Mask != msg.LeaverMask || vote.Kind != msg.Kind)
            {
                ++i.Desyncs;
                Count("split_mismatch");
                Note("split_mismatch", i, ("slot", m.Slot), ("tick", msg.Tick));
                return;
            }
        }
        else
        {
            i.Splits[msg.Tick] = vote = new SplitVote(msg.Tick, msg.LeaverMask, msg.Kind, now);
            while (i.Splits.Count > 16) i.Splits.Remove(i.Splits.Keys.First());
        }
        if (vote.Done) return;
        vote.Reporters.Add(m.Slot);
        TryFork(i, vote, now, expired: false);
    }

    /// <summary>A fork's report is accepted this many ticks after its tick at most.</summary>
    public const uint ForkReportTicks = 600;

    /// <summary>
    /// Forks once every connected member reported the same fork (each engine executed it), or, after
    /// Instances:ForkConfirmMs, once members of both sides did; a single member's SPLIT never moves another member.
    /// Until then the sides still share the instance's ticks (each engine has despawned the other side's players).
    /// </summary>
    private void TryFork(HubInstance i, SplitVote vote, long now, bool expired)
    {
        var present = i.PresentMask;
        var leavers = (byte)(vote.Mask & present);
        if (leavers == 0 || leavers == present)
        {
            vote.Done = true;
            Count("split_ignored");
            RecheckWaitingReadies(i, vote.Tick, now);
            return;
        }
        var everyone = i.Seats.Where(x => x.Member.Connected).All(x => vote.Reporters.Contains(x.Slot));
        var bothSides = vote.Reporters.Any(s => (leavers & (1 << s)) != 0) && vote.Reporters.Any(s => (present & ~leavers & (1 << s)) != 0);
        if (everyone || (expired && bothSides))
        {
            vote.Done = true;
            Fork(i, vote.Tick, leavers, vote.Kind, now);
            RecheckWaitingReadies(i, vote.Tick, now);
        }
        else if (expired)
        {
            vote.Done = true;
            ++i.Desyncs;
            Count("split_unconfirmed");
            Note("split_unconfirmed", i, ("tick", vote.Tick), ("reporters", vote.Reporters.ToArray()));
            // The fingerprints that waited for this fork are compared now.
            foreach (var t in i.Fingerprints.Keys.Where(t => t >= vote.Tick).ToList())
            {
                TryVote(i, t, now);
                if (i.Ended) return;
            }
            RecheckWaitingReadies(i, vote.Tick, now);
        }
    }

    /// <summary>A joiner's READY that waited for a fork's confirmation (OnReady) is checked once the fork is settled: confirmed
    /// (the parent's side; a leaver donor's check fingerprint is the other side's and no reference any more) or turned down.
    /// </summary>
    private void RecheckWaitingReadies(HubInstance i, uint forkTick, long now)
    {
        if (i.Ended) return;
        foreach (var o in i.Observers.ToList())
            if (o.Joining is { PendingReady: { } waiting } oj && oj.Target == i && waiting.Tick >= forkTick)
            {
                OnReady(o, waiting, now);
                if (i.Ended) return;
            }
    }

    /// <summary>
    /// A fork (SEAMLESS.md 6.7): the leaver side moves to a child instance that shares the parent's snapshot and every
    /// record sealed so far; from the next tick each side has its own stream (same schedule, so nobody hitches).
    /// </summary>
    private void Fork(HubInstance i, uint f, byte leavers, SplitKind kind, long now)
    {
        var from = i.NextSeal; // the first tick of the separate streams
        FlushAhead(i);
        // From the fork's tick on the two sides' engines differ: those fingerprints are no instance's any more.
        foreach (var t in i.Fingerprints.Keys.Where(t => t >= f).ToList()) i.Fingerprints.Remove(t);
        var child = NewInstance(InstanceMode.Sealed, i.Stage, i.Visit, i.ContextHash, now);
        child.Visit = i.Visit;
        child.Log = new TickLog(i.Log, i.Log.Count);
        foreach (var (t, rec) in i.Ahead) child.Ahead[t] = rec;
        child.Snapshot = i.Snapshot.Share();
        child.ArrivalContext = i.ArrivalContext;
        child.EntryContext = i.EntryContext;
        // Round J: the parent's checkpoints before the fork until the child's own (one engine line up to it). The parent keeps
        // no state of the leavers' side after the fork (round J's review: a leaver donor's upload of f + 1, sent before the
        // relay forked, would have rebuilt a later joiner of the parent with the wrong players), and no report after it.
        child.Checkpoints = i.Checkpoints.Where(c => c.Tick <= f).ToList();
        var leaverMembers = Enumerable.Range(0, Wire.MaxSlots).Where(s => (leavers & (1 << s)) != 0).Select(s => i.Slots[s]).OfType<HubMember>().ToHashSet();
        var dropped = i.Checkpoints.RemoveAll(c => c.Tick > f && c.Donor is { } donor && leaverMembers.Contains(donor));
        if (dropped > 0) Count("checkpoint_fork_dropped", dropped);
        if (i.Upload is { } upload && leaverMembers.Contains(upload.Donor)) i.Upload = null;
        if (i.AskedDonor is { } asked && leaverMembers.Contains(asked)) i.AskedDonor = null;
        foreach (var t in i.StateHashes.Keys.Where(t => t > f).ToList()) i.StateHashes.Remove(t);
        child.AgreedLoadEnd = i.AgreedLoadEnd;
        child.CheckpointSuspect = i.CheckpointSuspect;
        child.VisitStartedAt = i.VisitStartedAt;
        child.World = i.World;
        i.World?.Groups.Add(child);
        child.Parent = i.Id;
        child.ForkTick = f;
        child.Departing = kind == SplitKind.Exit;
        child.Joinable = false;
        child.Reason = kind == SplitKind.Exit ? StateReason.ForkPending : StateReason.Loading;
        child.Forks = [.. i.Forks, new ForkSide(f, leavers, kind, ForkSide.Leave)];
        i.Forks = [.. i.Forks, new ForkSide(f, leavers, kind, ForkSide.Stay)];
        child.S0 = i.S0;
        child.NextSeal = from;
        child.Sealing = i.Sealing;
        child.SealStop = null;
        var childMode = BitOperations.PopCount(leavers) >= 2 ? InstanceMode.Sealed : InstanceMode.SoloLed;
        for (var s = 0; s < Wire.MaxSlots; ++s)
        {
            if ((leavers & (1 << s)) == 0 || i.Slots[s] is not { } m) continue;
            var old = i.Streams[s];
            var stream = NewStream(m, s, from, old?.Delay.Current ?? 2);
            old?.MoveAhead(stream, from);
            if (old is not null) old.UntilTick = from;
            child.Streams[s] = stream;
            child.Slots[s] = m;
            i.Slots[s] = null;
            m.Instance = child;
            m.PreviousInstanceId = i.Id;
            AllowLine(m, child.Id, child.Visit); // (the parent's line is the member's already: it counts up to the fork)
            m.Peer?.Send(new ReassignMsg(i.Id, i.Visit, child.Id, from, childMode, child.Departing ? ReassignMsg.Departing : (byte)0).Frame());
        }
        // Pending events: JOIN stays with the instance id; LEAVE, INTENT and DELAY follow their slot; HOME's mask is split by side
        // (19.18: "GO and HOME follow their slots as LEAVE, INTENT and DELAY do").
        foreach (var (t, list) in i.PendingEvents.ToList())
        {
            var moving = list.Where(e => e.Type != Ev.Join && e.Type != Ev.LeadFrom && e.Slot >= 0 && (leavers & (1 << e.Slot)) != 0).ToList();
            foreach (var e in moving)
            {
                list.Remove(e);
                child.AddEvent(Math.Max(t, from), e);
            }
            foreach (var e in list.Where(e => e.Type == Ev.Home && (e.HomeMask & leavers) != 0).ToList())
            {
                var stay = (byte)(e.HomeMask & ~leavers & Wire.SlotMask);
                child.AddEvent(Math.Max(t, from), e.WithHomeMask(leavers));
                if (stay != 0) list[list.IndexOf(e)] = e.WithHomeMask(stay);
                else list.Remove(e);
            }
            if (list.Count == 0) i.PendingEvents.Remove(t);
        }
        child.HomeStamped = (byte)(i.HomeStamped & leavers);
        i.HomeStamped = (byte)(i.HomeStamped & ~leavers);
        if (i.PendingHome is { } ph && (ph.Mask & leavers) != 0)
        {
            child.PendingHome = ((byte)(ph.Mask & leavers), ph.Reason, ph.NotBefore);
            i.PendingHome = (ph.Mask & ~leavers) != 0 ? ((byte)(ph.Mask & ~leavers), ph.Reason, ph.NotBefore) : null;
        }
        // A barrier that waits while the fork is confirmed (a member that took the exit reached its stage change before
        // every SPLIT came, about two seconds after the fork tick): its reports go with their side. A side with reports
        // waits for its own members' (or parks at once); a side without plays on at once. (Before, the child copied the
        // stopped sealing and never started, and the parent's barrier ended its last member's instance for good.)
        if (i.Barrier is { } pb)
        {
            child.Barrier = pb.Part(leavers);
            i.Barrier = pb.Part((byte)(~leavers & 0xF));
            foreach (var side in new[] { i, child })
            {
                if (side.Barrier is { } sb)
                {
                    side.SealStop = sb.Tick;
                    side.Sealing = false;
                }
                else
                {
                    side.SealStop = null;
                    side.S0 += Math.Max(0, now - Due(side, side.NextSeal));
                    side.Sealing = true;
                }
            }
        }
        foreach (var side in new[] { i, child })
        {
            if (side.Barrier is not null) continue; // parks below, or waits for its members' reports
            if (side.Count == 1 && side.Sealing) ScheduleLeadFrom(side, from + 1);
            else if (side.Count == 1)
            {
                side.Mode = InstanceMode.SoloLed;
                Array.Clear(side.Streams);
            }
        }
        Note("fork", i, ("tick", f), ("leavers", leavers), ("kind", kind.ToString()), ("child", child.Id), ("from", from),
            ("barrier", child.Barrier is not null ? "child" : i.Barrier is not null ? "parent" : "none"));
        _sealer?.Invoke();
        CheckBarrier(child, now);
        CheckBarrier(i, now);
    }

    // Stories (SEAMLESS.md 8.3-8.7, 19.8) --------------------------------------------------------------------------------------

    /// <summary>
    /// A member's pending set (19.8.4): to the keeper of its current story owner (its instance host's, or its own at home). The
    /// owner key must be that owner's (member, box), else every value is refused with *owner* (not sent again): a STORY aimed at
    /// another story (a removed guest's modified client, a stale key after a barrier) never reaches the wrong keeper. The host's own
    /// values follow the owner rules (not spaced), a guest's the guest rules (round 59's spacing, the earned queue).
    /// </summary>
    private void OnStory(HubMember m, StoryMsg msg, long now)
    {
        var keeper = OwnerKeeper(m);
        var (owner, box) = OwnerKey(keeper, m);
        Count("story");
        // A burst, then Instances:StoryMessagesPerMinute (the client sends again what the rate refused).
        var burst = Math.Max(1, _inst.StoryBurst);
        if (m.StoryTokens < 0) m.StoryTokens = burst;
        else m.StoryTokens = Math.Min(burst, m.StoryTokens + Ms(now - m.StoryRefilledAt) / 60_000.0 * Math.Max(1, _inst.StoryMessagesPerMinute));
        m.StoryRefilledAt = now;
        StoryTake take;
        HubInstance? at = null;
        var rated = m.StoryTokens < 1;
        var wrongOwner = msg.OwnerMember != owner || msg.OwnerBox != box;
        if (rated || wrongOwner)
        {
            Count(wrongOwner ? "story_owner" : "story_rate");
            var why = wrongOwner ? StoryRefusal.Owner : StoryRefusal.Rate;
            var refused = new List<StoryRefused>();
            var d = msg.Delta;
            if (d.Phase != 0) refused.Add(new StoryRefused(StoryKeyKind.Phase, d.Phase, why));
            refused.AddRange(d.Flags.Select(f => new StoryRefused(StoryKeyKind.Flag, f, why)));
            refused.AddRange(d.Folder.Select(k => new StoryRefused(StoryKeyKind.Folder, k, why)));
            refused.AddRange(d.Counters.Select(g => new StoryRefused(StoryKeyKind.Counter, g.Counter, why)));
            take = new StoryTake(refused, false);
            if (wrongOwner) Note("story_owner", m.Instance, ("member", m.Id), ("key", msg.OwnerMember), ("owner", owner));
        }
        else
        {
            m.StoryTokens -= 1;
            // Where the member plays now decides (its stage, the visit); the frame's ids may be the ones before a fork. A parked or
            // catching-up member sends none (the client sends STORY before BARRIER).
            var (stage, visit, inst) = StoryOrigin(m, keeper, now);
            at = inst;
            var (delta, lines) = OwnLines(m, msg.Delta);
            // The lines nobody can report any more go (8.7: they never come again).
            if (keeper.LineCount > LinePruneAt) PruneStoryLines(keeper);
            take = keeper.Take(delta, stage, visit);
            if (lines.Count > 0) take = take with { Refused = [.. take.Refused, .. lines] };
        }
        if (take.Refused.Count > 0) Count("story_refused", take.Refused.Count);
        NoteHeld(m, take, at);
        // (A STORY refused for the rate or the owner changed nothing: its answer carries no story.)
        m.Peer?.Send(StoryAck(keeper, StoryAckMsg.Answer, take.Refused, withStory: !(rated || wrongOwner), receiver: m));
        if (take.Changed) StoryChanged(keeper, m);
    }

    /// <summary>The membership log names what the host's grace held (the scenarios check it).</summary>
    private void NoteHeld(HubMember m, StoryTake take, HubInstance? at)
    {
        var keys = take.Refused.Where(r => r.Reason == StoryRefusal.Held).Select(r => r.Kind.ToString().ToLowerInvariant() + ":" + r.Index).ToList();
        if (keys.Count > 0) Note("story_held", at, ("member", m.Id), ("keys", string.Join(",", keys)));
    }

    /// <summary>
    /// The earned story values that are due (SEAMLESS.md 8.7, "across owners" in the world, 19.8.4): every keeper merges what was
    /// earned on it in its member's later visit, whatever stage the member plays in then; the keeper's members get the revision.
    /// </summary>
    private void MergeEarned(long now)
    {
        foreach (var keeper in _keepers.Values.Where(k => k.EarnedCount > 0).ToList())
        {
            var changed = false;
            foreach (var account in keeper.EarnedAccounts.ToList())
            {
                if (!_byAccount.TryGetValue(account, out var m)) continue;
                var (stage, visit, inst) = StoryOrigin(m, keeper, now);
                if (visit is not { } v) continue;
                var take = keeper.MergeEarned(v, stage);
                NoteHeld(m, take, inst);
                if (!take.Changed) continue;
                changed = true;
                Count("story_earned");
                Note("story_earned", inst, ("member", m.Id), ("phase", StoryMerge.Phase(keeper.Story)), ("owner", keeper.Account));
            }
            if (changed) StoryChanged(keeper, null);
        }
    }

    /// <summary>
    /// Where a member's STORY comes from (SEAMLESS.md 8.7): its stage group's stage, visit and age while the server has it in
    /// one; while its client plays alone where the server has no stage group for it, the stage it plays in. <paramref name="keeper"/>
    /// decides whether the member is the story's owner (its own values are not spaced).
    /// </summary>
    private (string? Stage, StoryVisit? Visit, HubInstance? Inst) StoryOrigin(HubMember m, StoryKeeper keeper, long now)
    {
        if (m.Joining is not null) return (null, null, null);
        var at = Ms(now) / 1000.0;
        var owner = m.AccountId == keeper.Account;
        if (m.Instance is { Ended: false } i && m.Slot >= 0 && m.Arrival is null && !string.IsNullOrEmpty(i.Stage))
        {
            var since = Math.Max(m.InstanceSince, i.VisitStartedAt);
            return (i.Stage, new StoryVisit(m.Index, (ulong)i.Id << 32 | i.Visit, Ms(now - since) / 1000.0, m.AccountId, at, owner, true), i);
        }
        if (m.Arrival is { } a)
            return (a.Stage, new StoryVisit(m.Index, 1UL << 63 | (ulong)a.ParkedAt, Ms(now - a.ParkedAt) / 1000.0, m.AccountId, at, owner, true), null);
        if (m.LoneStage is { } lone)
            return (lone, new StoryVisit(m.Index, m.LoneKey != 0 ? m.LoneKey : 1UL << 63 | (ulong)m.LoneSince,
                Ms(now - m.LoneSince) / 1000.0, m.AccountId, at, owner, true), null);
        return (null, null, null);
    }

    /// <summary>A keeper prunes its lines once it remembers more than this many (a few per member are normal).</summary>
    private const int LinePruneAt = 64;

    /// <summary>Forgets the counter lines no member can report any more (its own lines, its instance 0 lines).</summary>
    private void PruneStoryLines(StoryKeeper keeper)
    {
        var reportable = new HashSet<(uint, uint)>();
        foreach (var m in _members.Values)
        {
            reportable.UnionWith(m.StoryLines);
            foreach (var v in m.LoneLineVisits) reportable.Add((0x8000_0000u | (uint)m.Index, v));
        }
        var removed = keeper.PruneLines(k => reportable.Contains((k.Inst, k.Visit)));
        if (removed > 0) Count("story_lines_pruned", removed);
    }

    private static void AllowLine(HubMember m, uint inst, uint visit)
    {
        if (!m.StoryLines.Add((inst, visit))) return;
        m.StoryLineOrder.Enqueue((inst, visit));
        while (m.StoryLineOrder.Count > HubMember.StoryLineLimit) m.StoryLines.Remove(m.StoryLineOrder.Dequeue());
    }

    /// <summary>
    /// The counter lines a member may report (SEAMLESS.md 8.7): the engine lines the server put it in (the last
    /// <see cref="HubMember.StoryLineLimit"/>: a growth sent again after the barrier still counts), and lines of instance 0
    /// (a group that went on alone) as its own namespace, <see cref="HubMember.LoneLineLimit"/> of them a session. Other
    /// lines are refused (limit): a client could otherwise invent lines and multiply a counter's growth.
    /// </summary>
    private (StoryDelta Delta, List<StoryRefused> Refused) OwnLines(HubMember m, StoryDelta delta)
    {
        var refused = new List<StoryRefused>();
        if (delta.Counters.Count == 0) return (delta, refused);
        var own = new List<CounterGrowth>(delta.Counters.Count);
        foreach (var g in delta.Counters)
        {
            if (g.Inst == 0)
            {
                if (m.LoneLineVisits.Contains(g.Visit) || m.LoneLineVisits.Count < HubMember.LoneLineLimit && m.LoneLineVisits.Add(g.Visit))
                    own.Add(g with { Inst = 0x8000_0000u | (uint)m.Index });
                else refused.Add(new StoryRefused(StoryKeyKind.Counter, g.Counter, StoryRefusal.Limit));
            }
            else if (m.StoryLines.Contains((g.Inst, g.Visit))) own.Add(g);
            else refused.Add(new StoryRefused(StoryKeyKind.Counter, g.Counter, StoryRefusal.Limit));
        }
        if (refused.Count > 0) Count("story_line_refused", refused.Count);
        return (new StoryDelta { Phase = delta.Phase, Flags = delta.Flags, Folder = delta.Folder, Counters = own }, refused);
    }

    /// <summary>A keeper's new revision: every member whose story owner it is follows (STORY_ACK), its owner's online story is saved.</summary>
    private void StoryChanged(StoryKeeper keeper, HubMember? sender)
    {
        var story = keeper.Story;
        // (One frame per owner key its receivers know the keeper by: the host's id in his instance, the owner's own at home.)
        byte[]? frame = null;
        uint frameKey = 0;
        foreach (var other in _members.Values)
        {
            if (other == sender || other.Peer is null || OwnerKeeper(other) != keeper) continue;
            var key = OwnerKey(keeper, other).Member;
            if (frame is null || frameKey != key) (frame, frameKey) = (StoryAck(keeper, 0, [], receiver: other), key);
            other.Peer.Send(frame);
        }
        Note("story", null, ("account", keeper.Account), ("box", keeper.Box), ("revision", story.Revision), ("phase", StoryMerge.Phase(story)));
        // (Only counters changed since the last save: the saver writes such revisions at most once a minute, SEAMLESS.md 8.7.)
        var countersOnly = keeper.Saved is { } before && before.Flags.AsSpan().SequenceEqual(story.Flags) && before.Folder.AsSpan().SequenceEqual(story.Folder);
        keeper.Saved = story.Clone();
        if (keeper.Account > 0) StorySaver?.Invoke(keeper.Account, story.Clone(), countersOnly);
    }

    /// <summary>STORY_ACK of a keeper's story with its owner key as <paramref name="receiver"/> knows it; without an owner key no story
    /// (a client reads owner 0 as its own: another's story must never come so).</summary>
    private byte[] StoryAck(StoryKeeper k, byte flags, IReadOnlyList<StoryRefused> refused, bool withStory = true, HubMember? receiver = null)
    {
        var (owner, box) = OwnerKey(k, receiver);
        if (owner == 0 && withStory)
        {
            Count("story_ack_without_owner");
            withStory = false;
        }
        return new StoryAckMsg(k.Story.Revision, withStory ? k.Story.ToBlob() : [], flags, refused, owner, box).Frame();
    }

    /// <summary>A keeper's story as the API and the tests see it.</summary>
    public RoomStoryState? StoryState(long accountId, int box = 1)
    {
        if (!_keepers.TryGetValue((accountId, box), out var k)) return null;
        var s = k.Story;
        return new RoomStoryState(s.Revision, StoryMerge.Phase(s), Enumerable.Range(0, StorySets.FlagCount).Where(f => s.Flags[f] != 0).ToList(),
            Enumerable.Range(0, StorySets.FolderCount).Where(f => s.Folder[f] != 0).ToList(), [.. s.Counters], "world", !k.Held, [], [], [], k.HeldCount)
        {
            Difficulty = k.Difficulty,
        };
    }

    /// <summary>
    /// The most players the account's instance had in the last <paramref name="seconds"/> (its stage group now, and every size the
    /// housekeeping saw it in since): the achievements' "players" cannot claim more. Null when the account is not in the world.
    /// </summary>
    public int? RecentInstancePlayers(long accountId, long now, double seconds = 60)
    {
        if (!_byAccount.TryGetValue(accountId, out var m)) return null;
        var most = m.Instance is { Ended: false } i ? Math.Max(1, i.Slots.Count(x => x is not null)) : 1;
        for (var n = most + 1; n < m.SeenInSize.Length; ++n)
            if (m.SeenInSize[n] != 0 && Ms(now - m.SeenInSize[n]) <= seconds * 1000) most = n;
        return most;
    }

    // Resume ---------------------------------------------------------------------------------------------------------------

    private void OnResume(HubMember m, ResumeMsg msg, long now)
    {
        if (!m.ResumeAllowed || Mine(m, msg.Inst, msg.Visit) is not { } i || m.Slot != msg.Slot)
        {
            // Not resumable (19.15): a member that still has its seat and whose engine plays on in a field of its instance is filed
            // there as continuing alone (no stage group until its next barrier, which is decided inside its instance; its
            // whereabouts are that stage).
            if (m.World is { Ending: false } w && m.Instance is null or { Mode: InstanceMode.Hub } && msg.Stage.Length > 0 && !StageNames.IsHub(msg.Stage))
            {
                m.LoneStage = msg.Stage;
                m.LoneSince = now;
                m.LoneKey = 0;
                Count("resume_filed_alone");
                Note("resume_alone", null, ("member", m.Id), ("wid", w.Wid), ("stage", msg.Stage));
                MarkInfoDirty(w);
                return;
            }
            Count("resume_refused");
            return;
        }
        m.ResumeAllowed = false; // once per connection
        if (i.Mode == InstanceMode.Sealed)
        {
            // Only the ticks lost with the old connection come again (a reconnect's gap); the cursor never moves forward. A host's
            // window covers his grace (19.6.7: World:HostMaxResumeTicks).
            var max = m.World?.Host == m ? Math.Max(_inst.MaxResumeTicks, _world.HostMaxResumeTicks) : _inst.MaxResumeTicks;
            var from = Math.Min(msg.LastTick + 1, i.StreamEnd);
            if (from < m.Cursor && m.Cursor - from > (uint)Math.Max(0, max))
            {
                Count("resume_refused");
                return;
            }
            if (from < m.Cursor) m.Cursor = from;
            Pump(i, m, bulk: true);
        }
        if (m.World is { HostAway: true } hw && hw.Host == m) HostBack(hw, now);
        Note("resume", i, ("member", m.Index), ("last", msg.LastTick));
    }

    // Streams to members -------------------------------------------------------------------------------------------------

    private void PumpAll(HubInstance i)
    {
        if (i.Mode == InstanceMode.Sealed)
            foreach (var m in i.Members) Pump(i, m);
        foreach (var o in i.Observers) Pump(i, o);
        // Members taken out that still play get the ticks up to their LEAVE (then they continue alone).
        foreach (var (d, until) in i.Departed.ToList())
        {
            if (d.Instance is not null || d.Joining is not null || d.Arrival is not null || d.Peer is null || d.Cursor > until)
            {
                i.Departed.Remove(d);
                continue;
            }
            Pump(i, d, until: until);
        }
    }

    /// <summary>Sends the member every contiguous record from its cursor (SEALED to members, LIVE to observers), up to
    /// <paramref name="until"/> when given.</summary>
    private void Pump(HubInstance i, HubMember m, bool bulk = false, uint until = uint.MaxValue)
    {
        if (m.Peer is null || m.PendingState is not null) return;
        var observer = m.Joining is not null;
        if (observer && !m.Joining!.StateSent) return;
        var end = i.StreamEnd;
        if (i.SealStop is { } stop) end = Math.Min(end, stop + 1);
        if (until != uint.MaxValue) end = Math.Min(end, until + 1);
        while (m.Cursor < end)
        {
            var count = Math.Min(end - m.Cursor, 600u);
            var records = i.Records(m.Cursor, m.Cursor + count).ToList();
            if (records.Count == 0) break;
            m.Peer.Send(TicksMsg.Frame(observer ? Msg.Live : Msg.Sealed, i.Id, i.Visit, m.Cursor, records), bulk: observer || bulk);
            m.Cursor += (uint)records.Count;
        }
    }

    // Housekeeping ---------------------------------------------------------------------------------------------------------

    /// <summary>Pings, holds, lag and reconnect limits, the earned story values, the world's part (the relay calls it a few times
    /// per second).</summary>
    public void Housekeep(long now)
    {
        {
            // One PING a second per member, spread over the passes (a member's own second; M4: the whole world's pings in one pass
            // held the lock for milliseconds), with the low margin cached per stream (DelayController.PingMargin).
            _lastPing = now;
            foreach (var m in _members.Values)
            {
                if (m.Peer is null) continue;
                if (m.NextPingAt == 0) m.NextPingAt = now + FromMs(250.0 * (m.Id % 4));
                if (now < m.NextPingAt) continue;
                m.NextPingAt = now + FromMs(1000);
                m.PingNonce = ++_pingNonce;
                m.PingSentAt = now;
                var st = m.Instance is { } i && m.Slot >= 0 ? i.Streams[m.Slot] : null;
                var margin = st?.Delay.PingMargin() is { } low ? (short)Math.Clamp(low, short.MinValue, short.MaxValue) : (short)0;
                var d = st?.Delay.Current ?? DelayController.FromRoundTrips(m.Rtt.All, _sealing.MaxDelayTicks);
                m.Peer.Send(new PingMsg(m.PingNonce, (ulong)(now * 1000.0 / Freq), margin, (byte)d).Frame());
                // What the member's network indicator and online.log show (round 62; older clients skip the frame).
                var (rttMedian, rttSpread) = DelayController.RoundTrip(m.Rtt.All);
                m.Peer.Send(new NetStatMsg((ushort)Math.Clamp(Math.Round(rttMedian), 0, ushort.MaxValue), (ushort)Math.Clamp(Math.Round(rttSpread), 0, ushort.MaxValue),
                    (byte)d, margin, (uint)Math.Min(uint.MaxValue, st?.Sealed ?? 0), (uint)Math.Min(uint.MaxValue, st?.Filled ?? 0),
                    (uint)Math.Min(uint.MaxValue, st?.Late ?? 0), st is null ? (byte)0 : NetStatMsg.InStream).Frame());
            }
        }
        foreach (var i in _instances.Values.ToList())
        {
            if (i.Ended) continue;
            // One instance's fault never stops the room's housekeeping (pings, roster, timeouts of the others).
            try
            {
                HousekeepInstance(i, now);
            }
            catch (Exception e)
            {
                Fault(e, "housekeeping of instance " + i.Id);
            }
        }
        try
        {
            CheckHolds(now);
        }
        catch (Exception e)
        {
            Fault(e, "the barrier holds");
        }
        try
        {
            MergeEarned(now);
        }
        catch (Exception e)
        {
            Fault(e, "the earned story values");
        }
        CachedViews = Views();
        foreach (var i in _instances.Values)
        {
            if (i.Ended) continue;
            var n = i.Slots.Count(x => x is not null);
            foreach (var x in i.Slots)
                if (x is not null && n < x.SeenInSize.Length) x.SeenInSize[n] = now;
        }
        try
        {
            HousekeepWorld(now);
        }
        catch (Exception e)
        {
            Fault(e, "the world's housekeeping");
        }
    }

    /// <summary>
    /// The log of an instance sealed from J (JOIN_AT) can never reach J: its solo leader left (crashed, lost its
    /// connection past the window) before its LOG did, or that LOG lags far behind the sealed ticks. Nobody can be sent the
    /// ticks from J (before, the joiner waited on its loading screen for good and the sealed ticks piled up in memory).
    /// The joiners' engines never got their JOIN: they restore their own load snapshot and play the stage as NEW. A
    /// leader still there is closed (its client waits for ticks that cannot come).
    /// </summary>
    private void LostGap(HubInstance i, long now, bool ownerGone)
    {
        Count("gap_lost");
        Note("gap_lost", i, ("log", i.Log.Count), ("sealed_from", i.SealedFrom), ("ahead", i.Ahead.Count), ("owner_gone", ownerGone));
        _log.LogWarning("Room {Room} instance {Inst}: the log before J ({J}) never came; its joiners play on their own", Code, i.Id, i.SealedFrom);
        i.Ahead.Clear();
        i.PendingEvents.Clear();
        Array.Clear(i.Streams);
        i.Sealing = false;
        i.LeadFrom = null;
        i.SoloPending = false;
        i.Departed.Clear();
        foreach (var o in i.Observers.ToList()) FallbackNew(o, now, BarrierNotice.JoinFailed);
        var owner = i.LogOwner;
        foreach (var (s, x) in i.Seats.ToList())
        {
            i.Slots[s] = null;
            if (x.Instance == i)
            {
                x.Instance = null;
                x.Slot = -1;
            }
            if (x == owner)
            {
                x.Peer?.Close(CloseReason.Protocol, "The log before the join never came.");
                continue;
            }
            if (x.JoinedVia is { } joined) FallbackNew(x, now, BarrierNotice.JoinFailed, joined);
            else x.Peer?.Close(CloseReason.Protocol, "The instance's log broke off.");
        }
        EndInstance(i, now);
    }

    private void HousekeepInstance(HubInstance i, long now)
    {
        // Round J: an upload that does not finish (its donor left the instance or the connection, a chunk was dropped) no longer
        // blocks the periodic checkpoints for the rest of the visit (round J's review).
        if (i.Upload is { } upload && (upload.Donor.Instance != i || !upload.Donor.Connected || Ms(now - i.CheckpointAskedAt) > _inst.CheckpointWaitMs * 4))
        {
            Count("checkpoint_upload_abandoned");
            i.Upload = null;
            i.AskedDonor = null;
        }
        // Round J: while joinable, the donor uploads a checkpoint now and then (resyncs, a donor that does not answer); not in a
        // solo-led visit (round J's review: nobody to resync, and a join asks for a fresh one anyway).
        if (_inst.CheckpointPeriodicMs > 0 && i.Joinable && i.Mode != InstanceMode.SoloLed && CheckpointPathOpen(i) && i.Upload is null &&
            i.Barrier is null && Ms(now - Math.Max(i.CheckpointAskedAt, i.Checkpoints.LastOrDefault()?.At ?? 0)) >= _inst.CheckpointPeriodicMs)
            AskCheckpoint(i, now, CheckpointAskMsg.Periodic);
        // Round J: joins that waited for the donor's checkpoint long enough take the newest stored one or the classic path.
        foreach (var o in i.Observers.ToList())
        {
            if (o.Joining is not { } oj || oj.Target != i) continue;
            if (!oj.StateSent && oj.CheckpointWaitUntil != 0 && now >= oj.CheckpointWaitUntil)
                DeliverState(o, now);
            else if (oj.PendingReady is { } pending && Ms(now - oj.PendingReadyAt) >= _inst.ReadyFingerprintWaitMs)
            {
                // The members' fingerprint of that tick did not come: the READY is taken unchecked (as before).
                Count("ready_unchecked");
                oj.PendingReady = null;
                oj.ReadyWaitExpired = true;
                OnReady(o, pending, now);
            }
            else if (oj.StateSent && !oj.Ready && oj.StateSentAt != 0 && Ms(now - oj.StateSentAt) > (_inst.MaxCatchUpSeconds + 30) * 1000.0)
            {
                // A joiner that never sends READY (round J's review: after a READY mismatch's classic state a client kept
                // waiting in 'Joining...' for good): its own stage as NEW.
                Count("join_ready_timeout");
                Note("join_ready_timeout", i, ("member", o.Index));
                FallbackNew(o, now, BarrierNotice.JoinFailed);
            }
            if (i.Ended) return;
        }
        // Barrier reports that do not come.
        if (i.Barrier is { } b && Ms(now - b.FirstAt) >= _inst.BarrierReportSeconds * 1000.0 && !i.Members.Any(x => x.Arrival is not null))
        {
            if (i.Seats.Any(x => x.Member.Connected && !b.Reports.ContainsKey(x.Slot)) && i.Mode == InstanceMode.Sealed) SplitBarrier(i, now);
            else Park(i, now); // (no reporter left: the others play on)
        }
        if (i.Ended) return;
        // Forks reported but not confirmed by every member within Instances:ForkConfirmMs.
        foreach (var vote in i.Splits.Values.Where(v => !v.Done && Ms(now - v.FirstAt) >= _inst.ForkConfirmMs).ToList())
            if (!i.Ended && i.Mode == InstanceMode.Sealed) TryFork(i, vote, now, expired: true);
        if (i.Ended) return;
        // Sealed from J while the solo leader's LOG has not reached J: the gap closes only with that LOG. Gone leader, or
        // more than Instances:MaxAheadSeconds of sealed ticks waiting: it never will.
        if (i.Mode == InstanceMode.Sealed && i.Log.Count < i.SealedFrom)
        {
            var ownerGone = i.LogOwner is not { } owner || owner.Instance != i;
            if (ownerGone || i.Ahead.Count > _inst.MaxAheadSeconds / Wire.TickSeconds) LostGap(i, now, ownerGone);
            if (i.Ended) return;
        }
        // A sealed visit whose members' snapshot hashes do not all come: the silent ones leave it (6.8).
        if (i.Mode == InstanceMode.Sealed && !i.Sealing && !i.SoloPending && i.Barrier is null && Ms(now - i.VisitStartedAt) >= _inst.SnapHashSeconds * 1000.0)
        {
            foreach (var (s, x) in i.Seats.ToList())
                if (!i.Snapshot.Hashes.ContainsKey(s) && x.Connected && !i.Ended)
                {
                    Count("snaphash_timeout");
                    Note("snaphash_timeout", i, ("member", x.Index), ("slot", s));
                    StampLeave(i, s, now);
                }
            if (!i.Ended) TryStartSealing(i, now);
        }
        // Lag and disconnects in sealed instances.
        if (i.Mode == InstanceMode.Sealed && i.Sealing)
        {
            // When every connected member lags, nobody plays ahead of them: the whole group (or this server) stalled, and
            // nobody is taken out for lag. Taking them all out ended the instance before any of them got the ticks up to its
            // LEAVE, and every client waited for those ticks for good (round 56 verification: A1 on a loaded machine).
            var everyoneLags = i.Seats.Where(x => x.Member.Connected)
                .All(x => i.Streams[x.Slot] is { } xs && xs.FillStreak >= SlotStream.LagFills);
            foreach (var (s, m) in i.Seats.ToList())
            {
                var st = i.Streams[s];
                if (st is null) continue;
                // A resyncing member sends no pads while it catches up (bounded by the catch-up limit).
                if (m.Resyncing && Ms(now - m.ResyncAt) > (_inst.MaxCatchUpSeconds + 30) * 1000.0) m.Resyncing = false;
                var lagging = st.FillStreak >= SlotStream.LagFills;
                if (lagging != m.Lagging)
                {
                    m.Lagging = lagging;
                    if (m.World is { } lw)
                    {
                        foreach (var o in lw.Members)
                            if (o != m) o.Peer?.Send(Notice(o, lagging ? WorldNoticeKind.Lagging : WorldNoticeKind.LagOver, m, 0, ""));
                        MarkInfoDirty(lw);
                    }
                }
                // The host in his grace (19.6.7): his slot fills with neutral pads until the grace's end, no lagOut, no reconnect LEAVE.
                var inGrace = m.World is { HostAway: true } hw && hw.Host == m;
                var gone = !inGrace && !m.Connected && Ms(now - m.DisconnectedAt) >= _relay.ReconnectSeconds * 1000.0;
                var lagOut = !inGrace && lagging && !everyoneLags && !m.Resyncing && Ms(now - st.FillStreakSince) >= _relay.LagLeaveSeconds * 1000.0;
                if (gone || lagOut)
                {
                    Note(gone ? "reconnect_timeout" : "lag_leave", i, ("member", m.Index));
                    StampLeave(i, s, now);
                    if (i.Ended) return;
                }
            }
        }
        else if (i.Mode == InstanceMode.SoloLed)
        {
            foreach (var (s, m) in i.Seats.ToList())
                if (!m.Connected && Ms(now - m.DisconnectedAt) >= _relay.ReconnectSeconds * 1000.0 && !(m.World is { HostAway: true } sw && sw.Host == m))
                {
                    Note("reconnect_timeout", i, ("member", m.Index));
                    i.Slots[s] = null;
                    if (m.Instance == i)
                    {
                        m.Instance = null;
                        m.Slot = -1;
                    }
                    EndIfEmpty(i, now);
                }
            if (i.Ended) return;
            if (i.SoloJoin is { } sj && Ms(now - sj.AskedAt) > 5000)
            {
                // The leader did not answer JOIN_REQ: ask again, a few times; then the joiner plays the stage as NEW (the
                // loading screen waits at most about 30 s, 3.4).
                i.SoloJoin = null;
                Count("join_req_timeout");
                if (sj.Joiner.Joining is { } jj)
                {
                    jj.Stamped = false;
                    if (jj.JoinRequests >= Math.Max(1, _inst.JoinRequestTries))
                    {
                        Note("join_req_failed", i, ("member", sj.Joiner.Index));
                        FallbackNew(sj.Joiner, now, BarrierNotice.JoinFailed);
                    }
                }
            }
        }
        // Joiners that are ready but could not be stamped yet (the target was starting, another JOIN_REQ ran).
        foreach (var o in i.Observers.ToList())
            if (o.Joining is { Ready: true, Stamped: false } oj && oj.Target == i)
                StampJoin(i, o, now);
    }

    // Views ----------------------------------------------------------------------------------------------------------------

    public IReadOnlyList<InstanceView> Views() => _instances.Values.Where(i => !i.Ended).OrderBy(i => i.Id).Select(i => new InstanceView(i.Id, i.Stage, i.Visit,
        i.Mode switch { InstanceMode.Hub => "hub", InstanceMode.SoloLed => "solo", _ => "sealed" },
        Enumerable.Range(0, Wire.MaxSlots).Where(s => i.Slots[s] is not null)
            .Select(s => new InstanceMemberView(i.Slots[s]!.AccountId, i.Slots[s]!.Name, s, i.Slots[s]!.Connected, i.Slots[s]!.Lagging)).ToList(),
        i.Departing, i.BossLock, i.Joinable, i.Mode == InstanceMode.Sealed ? i.NextSeal : i.Log.Count, i.Parent)).ToList();
}

/// <summary>
/// A state transfer captured under the room lock (<see cref="RoomHub"/>): the snapshot, the fork list and the records
/// up to <see cref="End"/>. <see cref="Build"/> runs without the lock; <see cref="RoomHub.FinishState"/> sends it.
/// </summary>
public sealed class StateJob(RoomHub hub, HubMember member, IHubPeer peer, HubInstance instance, uint visit, StatePurpose purpose, byte slot,
    byte[] snapshot, IReadOnlyList<ForkSide> forks, TickLogView log, IReadOnlyList<byte[]> ahead, uint end)
{
    public RoomHub Hub { get; } = hub;
    public HubMember Member { get; } = member;
    public IHubPeer Peer { get; } = peer;
    public HubInstance Instance { get; } = instance;
    public uint Visit { get; } = visit;
    public StatePurpose Purpose { get; } = purpose;
    public uint End { get; } = end;
    public byte[]? Blob { get; private set; }
    /// <summary>Round J: the checkpoint the state starts from (null: the classic state).</summary>
    public StoredCheckpoint? Checkpoint { get; init; }
    public uint AgreedLoadEnd { get; init; }

    /// <summary>A newer state for the same member replaced this one (read without the room lock: a stale answer only builds
    /// a state that is then dropped, or skips one that is dropped anyway).</summary>
    public bool Superseded
    {
        get
        {
            Interlocked.MemoryBarrier();
            return !ReferenceEquals(Member.PendingState, this);
        }
    }

    public void Build()
    {
        var records = new List<byte[]>((int)Math.Min(End, 1_000_000));
        records.AddRange(log.Range(0, log.Count));
        records.AddRange(ahead);
        if (Checkpoint is { } cp)
        {
            // The load ticks [0, K) and the ticks from T on (6.13.7): the rest of the log stays with the relay.
            var k = (int)Math.Min(cp.LoadEnd, (uint)records.Count);
            var from = (int)Math.Min(cp.Tick, (uint)records.Count);
            Blob = StateBlob.BuildWithCheckpoint(Purpose, slot, snapshot, forks, records.GetRange(0, k), cp.Tick, cp.Hash,
                cp.Agreed ? StateBlob.CheckpointAgreed : (byte)0, cp.Bytes, records.GetRange(from, records.Count - from));
            return;
        }
        Blob = StateBlob.Build(Purpose, slot, snapshot, forks, 0, records);
    }
}
