using System.Diagnostics;
using System.Numerics;
using DW4Online.Domain.Rooms;
using DW4Online.Domain.Story;
using DW4Online.Relay.Protocol;
using Microsoft.Extensions.Logging;

namespace DW4Online.Relay.Hub;

/// <summary>Receives the membership events of a room (diagnostics, the online test stack).</summary>
public interface IMembershipLog
{
    void Write(string room, IReadOnlyDictionary<string, object?> entry);
}

/// <summary>
/// The relay side of one room (SEAMLESS.md 3, 6): its members' connections, the instances (solo-led and sealed lockstep
/// groups), server-paced sealing, events, barrier decisions, forks, the snapshot and log store and state delivery. The
/// server never simulates. Every public method runs under <c>lock (hub.Sync)</c>; sending only queues frames.
/// </summary>
public sealed class RoomHub
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
    private readonly RoomSessionState _session;
    private uint _pingNonce;
    private long _lastPing;
    private long _lastRoster;

    /// <param name="stateRunner">Builds state transfers outside the room lock (the relay: on the thread pool, then
    /// <see cref="FinishState"/> under the lock); null builds them at once (the tests).</param>
    /// <param name="session">What the room keeps while its hub is rebuilt (the relay drops an empty hub while the room stays
    /// open): its story keeper and the next instance id. Null: a room of its own (the tests).</param>
    public RoomHub(RoomInfo info, RelayOptions relay, SealingOptions sealing, InstanceOptions instances, ILogger log, IMembershipLog? mlog = null,
        Action<StateJob>? stateRunner = null, RoomSessionState? session = null)
    {
        Info = info;
        _session = session ?? new RoomSessionState();
        _relay = relay;
        _sealing = sealing;
        _inst = instances;
        _log = log;
        _mlog = mlog;
        _stateRunner = stateRunner;
    }

    /// <summary>Saves a new revision of the room story as the owner's online story (the account, a copy, whether only counters
    /// changed); set by the relay.</summary>
    public Action<long, RoomStoryData, bool>? StorySaver { get; set; }

    /// <summary>The room story of this session (SEAMLESS.md 8.3-8.7), from the owner's story the first connection brought; it
    /// stays with the room when the hub is rebuilt.</summary>
    public RoomStoryKeeper Story => _session.Story ??= NewKeeper(Info.Story, _inst);

    /// <summary>A room's story keeper from its start (the owner's story, difficulty, policy and presence), with the options'
    /// per-visit and per-member rules. Without a start (relay-only tests): an empty, started story.</summary>
    public static RoomStoryKeeper NewKeeper(RoomStoryInit? init, InstanceOptions inst) => init is { } s
        ? new RoomStoryKeeper(s.Story, s.Difficulty, s.Policy, s.OwnerPresent, started: s.Started) { MinVisitSeconds = inst.StoryMinVisitSeconds,
            PhaseStepSeconds = inst.StoryPhaseStepSeconds, KeySeconds = inst.StoryKeySeconds, FlagBurst = inst.StoryFlagBurst,
            FlagSeconds = inst.StoryFlagSeconds, MaxLinesPerMember = inst.StoryLinesPerMember }
        : new RoomStoryKeeper(new RoomStoryData(), 0, StoryPolicy.Drop, ownerPresent: true) { MinVisitSeconds = inst.StoryMinVisitSeconds,
            PhaseStepSeconds = inst.StoryPhaseStepSeconds, KeySeconds = inst.StoryKeySeconds, FlagBurst = inst.StoryFlagBurst,
            FlagSeconds = inst.StoryFlagSeconds, MaxLinesPerMember = inst.StoryLinesPerMember };

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

    /// <summary>A member's relay connection completed its handshake.</summary>
    public HubMember Connect(int index, long accountId, string name, bool owner, IHubPeer peer, long now)
    {
        if (!_members.TryGetValue(index, out var m) || m.AccountId != accountId)
        {
            if (m is not null) RemoveMemberLocked(m, CloseReason.Replaced, "Another account took this place in the room.", now);
            m = new HubMember(index, accountId, name, owner);
            _members[index] = m;
            foreach (var other in _members.Values)
                if (other != m) other.Peer?.Send(new RoomMsg(RoomNotice.MemberJoined, (byte)m.Index, (ulong)m.AccountId, m.Name, Flags(m)).Frame());
        }
        m.Owner = owner;
        if (m.Peer is { } old && old != peer) old.Close(CloseReason.Replaced, "A newer connection of this account replaced this one.");
        var resumable = m.Instance is { Ended: false } i && m.DisconnectedAt != 0 && m.Joining is null && m.Arrival is null && i.Barrier is null &&
                        m.Slot >= 0 && i.Slots[m.Slot] == m && i.Mode != InstanceMode.Hub;
        m.Peer = peer;
        m.DisconnectedAt = 0;
        m.LastHeard = now;
        m.ResumeAllowed = resumable;
        if (!resumable)
        {
            MarkLone(m);
            DetachLocked(m, now, leaveNow: true);
            var hub = NewInstance(InstanceMode.Hub, "", 1, 0, now);
            Place(m, hub, 0, now);
        }
        var inst = m.Instance!;
        peer.Send(new WelcomeMsg((byte)m.Index, inst.Id, inst.Visit, (byte)m.Slot, inst.Mode, inst.Stage,
            resumable && m.Cursor > 0 ? m.Cursor - 1 : WelcomeMsg.NoTick, (byte)(inst.Streams[m.Slot]?.Delay.Current ?? 0),
            (ushort)_inst.MaxCatchUpSeconds, (byte)((resumable ? WelcomeMsg.Resumable : 0) | WelcomeMsg.PresenceV2 | (Info.Protocol >= Wire.CheckpointProtocol ? WelcomeMsg.Checkpoints : 0))).Frame());
        // The confirmed room story to start from (the title applies it at game start; SEAMLESS.md 8.5).
        peer.Send(new StoryAckMsg(Story.Story.Revision, Story.Story.ToBlob(), 0, []).Frame());
        foreach (var other in _members.Values)
        {
            peer.Send(new RoomMsg(RoomNotice.MemberJoined, (byte)other.Index, (ulong)other.AccountId, other.Name, Flags(other)).Frame());
            if (other != m) other.Peer?.Send(new RoomMsg(RoomNotice.Connected, (byte)m.Index, (ulong)m.AccountId, m.Name, Flags(m)).Frame());
        }
        Note("connect", inst, ("member", m.Index), ("resumed", resumable));
        return m;
    }

    private static byte Flags(HubMember m) => m.Owner ? RoomMsg.Owner : (byte)0;

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

    /// <summary>A member's connection closed (its slot stays filled for Relay:ReconnectSeconds in a logged instance).</summary>
    public void Disconnect(HubMember m, IHubPeer peer, long now, bool graceful = false)
    {
        if (m.Peer != peer) return;
        m.Peer = null;
        m.DisconnectedAt = now;
        foreach (var other in _members.Values)
            other.Peer?.Send(new RoomMsg(RoomNotice.Disconnected, (byte)m.Index, (ulong)m.AccountId, m.Name, Flags(m)).Frame());
        Note("disconnect", m.Instance, ("member", m.Index), ("graceful", graceful));
        if (graceful || m.Joining is not null || m.Arrival is not null || m.Instance is null || m.Instance.Mode == InstanceMode.Hub || m.Instance.Barrier is not null)
        {
            // (A lobby or a parked barrier: the client plays on there alone, its STORY after the reconnect counts there.)
            if (!graceful) MarkLone(m);
            DetachLocked(m, now, leaveNow: true);
        }
        else if (m.Instance is { Mode: InstanceMode.Sealed, Sealing: false, Barrier: null, Ended: false } waiting)
            TryStartSealing(waiting, now); // (its hash is not awaited from a gone member; the reconnect window runs from tick 0)
    }

    /// <summary>The member left the room (leave, kick, timeout, a staff lock): its connection closes, its slot leaves at once.</summary>
    public void RemoveMember(long accountId, CloseReason reason, string text, long now)
    {
        if (_members.Values.FirstOrDefault(x => x.AccountId == accountId) is { } m) RemoveMemberLocked(m, reason, text, now);
    }

    private void RemoveMemberLocked(HubMember m, CloseReason reason, string text, long now)
    {
        m.Peer?.Close(reason, text);
        m.Peer = null;
        DetachLocked(m, now, leaveNow: true);
        _members.Remove(m.Index);
        foreach (var other in _members.Values)
            other.Peer?.Send(new RoomMsg(RoomNotice.MemberLeft, (byte)m.Index, (ulong)m.AccountId, m.Name, Flags(m)).Frame());
        Note("member_left", null, ("member", m.Index), ("reason", reason.ToString()));
    }

    /// <summary>The room closed: every connection gets CLOSE, every instance ends.</summary>
    public void CloseAll(CloseReason reason, string text)
    {
        foreach (var m in _members.Values.ToList())
        {
            m.Peer?.Close(reason, text);
            m.Peer = null;
        }
        _members.Clear();
        _instances.Clear();
        _holds.Clear();
    }

    /// <summary>Owner present / away (the story follows the owner, SEAMLESS.md 8.6).</summary>
    public void OwnerPresence(bool present)
    {
        var owner = _members.Values.FirstOrDefault(m => m.Owner);
        foreach (var m in _members.Values)
            m.Peer?.Send(new RoomMsg(present ? RoomNotice.OwnerPresent : RoomNotice.OwnerAway, (byte)(owner?.Index ?? 0xFF),
                (ulong)(owner?.AccountId ?? 0), owner?.Name ?? "", RoomMsg.Owner).Frame());
        Note("owner", null, ("present", present), ("held", Story.HeldCount));
        // The hold policy merges what was kept when the owner returns.
        if (Story.SetOwnerPresent(present)) StoryChanged(null);
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

    /// <summary>
    /// A member's presence to the room members in the same stage outside its sealed instance (name tags, and in a hub
    /// stage ghost figures): PRESENCE2 to those that send it, PRESENCE to the others. A member that just left a stage
    /// (another stage, or hidden while it loads) is also sent to those still in the stage it left, so its tag and figure
    /// go at once instead of after the two seconds a stale one stays.
    /// </summary>
    private void FanPresence(HubMember m, PresenceMsg msg, Presence2Msg? msg2, long now)
    {
        var left = m.Presence?.Stage;
        if (string.IsNullOrEmpty(left) || string.Equals(left, msg.Stage, StringComparison.OrdinalIgnoreCase)) left = null;
        m.Presence = msg;
        m.Presence2 = msg2;
        m.PresenceAt = now;
        byte[]? v1 = null, v2 = null;
        foreach (var o in _members.Values)
        {
            if (o == m || o.Peer is null || o.Presence is null) continue;
            if (!string.Equals(o.Presence.Stage, msg.Stage, StringComparison.OrdinalIgnoreCase) &&
                (left is null || !string.Equals(o.Presence.Stage, left, StringComparison.OrdinalIgnoreCase))) continue;
            if (o.Instance is not null && m.Instance is not null && o.Instance == m.Instance && o.Instance.Mode != InstanceMode.Hub) continue;
            if (msg2 is not null && o.PresenceV2) o.Peer.Send(v2 ??= new Presence2OutMsg((byte)m.Index, msg2).Frame());
            else o.Peer.Send(v1 ??= new PresenceOutMsg((byte)m.Index, msg).Frame());
        }
    }

    private void OnPong(HubMember m, PongMsg msg, long now)
    {
        if (msg.Nonce != m.PingNonce || m.PingSentAt == 0) return;
        m.Rtt.Add(Ms(now - m.PingSentAt));
        m.PingSentAt = 0;
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
        if (!i.Fingerprints.TryGetValue(msg.Tick, out var d)) i.Fingerprints[msg.Tick] = d = [];
        // One report per member and tick: a second one (a forged NETFP for a tick already compared equal) started a vote
        // against its partner whenever it liked.
        if (!d.TryAdd(m.Slot, msg.Hash))
        {
            Count("netfp_repeat");
            return;
        }
        while (i.Fingerprints.Count > 64) i.Fingerprints.Remove(i.Fingerprints.Keys.First());
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
        var groups = slots.GroupBy(s => d[s]).OrderByDescending(g => g.Count()).ToList();
        ulong winner;
        if (groups[0].Count() >= 2 && (groups.Count == 1 || groups[1].Count() < groups[0].Count())) winner = groups[0].Key;
        else
        {
            var trusted = slots.Select(s => i.Slots[s]!).OrderBy(x => x.ReplayMismatches).ThenBy(x => x.InstanceSince).First();
            winner = d[trusted.Slot];
        }
        _log.LogWarning("Room {Room} instance {Inst}: net fingerprints differ at tick {Tick}", Code, i.Id, tick);
        Note("desync", i, ("tick", tick), ("fingerprints", slots.ToDictionary(s => s.ToString(), s => d[s].ToString("x16"))));
        // A desync soon after a rebuild from a checkpoint: that rebuild may have missed a field (6.13.4).
        if (i.RebuildTick is { } rebuilt && tick <= rebuilt + (uint)_inst.CheckpointSuspectTicks) MarkSuspect(i, tick, "desync");
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
        i.MembershipSafe = (msg.Flags & StateMsg.MembershipSafe) != 0;
        var boss = (msg.Flags & StateMsg.BossLock) != 0;
        if (boss != i.BossLock) Note(boss ? "boss_lock" : "boss_clear", i);
        i.BossLock = boss;
        i.Reason = msg.Reason;
        i.ContextHash = msg.ContextHash;
        if (i.Joinable && i.EntryContext is null) i.EntryContext = msg.ContextHash;
        CheckHolds(now);
    }

    // Barrier ------------------------------------------------------------------------------------------------------------

    private void OnBarrier(HubMember m, BarrierMsg msg, long now)
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
            var stage = i?.Stage ?? "";
            DetachLocked(m, now, leaveNow: true);
            var lone = new HubInstance(0) { Stage = stage, Visit = msg.Visit, ContextHash = msg.ContextHash, Ended = true };
            var g = new ArrivalGroup { From = lone, BarrierTick = msg.Tick, Stage = msg.Stage, Point = msg.Point, Context = msg.ContextHash, ParkedAt = now };
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
        var g = new ArrivalGroup { From = i, BarrierTick = b.Tick, Stage = b.Stage, Point = b.Point, Context = b.Context, ParkedAt = now };
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
        var g = new ArrivalGroup { From = lone, BarrierTick = b.Tick, Stage = b.Stage, Point = b.Point, Context = b.Context, ParkedAt = now };
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

    /// <summary>The decision of SEAMLESS.md 3.4: DISSOLVE, co-arrival, JOIN (with holds), NEW.</summary>
    private void Decide(ArrivalGroup g, long now)
    {
        if (StageNames.IsHub(g.Stage))
        {
            Dissolve(g, now);
            return;
        }
        // A base that holds for announced groups takes this one as a co-arrival (one it does not wait for only where the
        // ones it waits for still fit).
        var holder = _holds.FirstOrDefault(h => h.Hold == HoldKind.CoArrival && h != g && SameStage(h.Stage, g.Stage) && h.Context == g.Context &&
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
        JoinOrNew(g, now);
    }

    private static bool SameStage(string a, string b) => string.Equals(a, b, StringComparison.OrdinalIgnoreCase);

    /// <summary>The members a holding base still waits for, without the arriving group's (by their instances' sizes).</summary>
    private static int OutstandingSize(ArrivalGroup holder, ArrivalGroup arriving) =>
        holder.Awaited.Where(x => arriving.Members.All(a => a.Member != x)).GroupBy(x => x.Instance)
            .Sum(grp => Math.Max(grp.Key?.Count ?? 0, grp.Count()));

    private void JoinOrNew(ArrivalGroup g, long now)
    {
        var size = g.Members.Count;
        var candidates = _instances.Values.Where(x => !x.Ended && x.Logged && !x.Departing && !x.LogFull && x.Barrier is null && x != g.From &&
                                                      SameStage(x.Stage, g.Stage)).ToList();
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
        var target = ranked[0].Inst;
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
                            JoinOrNew(g, now);
                        }
                    }
                    break;
                case HoldKind.Join:
                    if (g.Target is null || g.Target.Ended || g.Target.Barrier is not null || g.Target.FreeSlots < g.Members.Count)
                    {
                        _holds.Remove(g);
                        g.Hold = HoldKind.None;
                        JoinOrNew(g, now);
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
                    if (g.Target is null || g.Target.Ended || g.Target.Barrier is not null || g.Target.FreeSlots < g.Members.Count)
                    {
                        _holds.Remove(g);
                        g.Hold = HoldKind.None;
                        JoinOrNew(g, now);
                    }
                    else if (!g.Target.BossLock && g.Choice == BarrierChoiceMsg.Wait)
                    {
                        _holds.Remove(g);
                        g.Hold = HoldKind.None;
                        JoinOrNew(g, now);
                    }
                    break;
            }
        }
    }

    private void Dissolve(ArrivalGroup g, long now)
    {
        foreach (var a in g.Members)
        {
            var m = a.Member;
            m.Arrival = null;
            var hub = NewInstance(InstanceMode.Hub, g.Stage, 1, g.Context, now);
            var map = new sbyte[Wire.MaxSlots];
            Array.Fill(map, (sbyte)-1);
            map[a.Slot] = 0;
            DetachFromOld(g.From, m);
            Place(m, hub, 0, now);
            m.Peer?.Send(new BarrierOkMsg(g.From.Id, g.From.Visit, BarrierDecision.Dissolve, hub.Id, hub.Visit, g.Stage, g.Point, 0, map,
                InstanceMode.Hub, 0, [], Story.Story.Revision, Story.Story.ToBlob(), Story.Dropped.ToBlob()).Frame());
            Note("dissolve", hub, ("member", m.Index), ("from", g.From.Id));
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
        Array.Clear(inst.Slots);
        Array.Clear(inst.Reserved);
        foreach (var (member, slot) in map)
        {
            if (member.Instance is { } old && old != inst) DetachFromOld(old, member);
            Place(member, inst, slot, now);
        }
        var uploader = g.Members.Select(a => a.Member).OrderBy(x => map[x]).First();
        inst.Snapshot.Uploader = uploader;
        var delays = map.Keys.ToDictionary(x => x, x => DelayController.FromRoundTrips(x.Rtt.All, _sealing.MaxDelayTicks));
        if (inst.Mode == InstanceMode.Sealed)
            foreach (var (member, slot) in map)
                inst.Streams[slot] = NewStream(member, slot, 0, delays[member]);

        // The base engine spawns the co-arrivals (and followers) in its load step with their carries.
        var coList = new List<CoArrival>();
        foreach (var c in coMembers) coList.Add(new CoArrival((byte)map[c.Member], (byte)c.Member.Index, c.Carry));
        foreach (var f in followers) coList.Add(new CoArrival((byte)map[f], (byte)f.Index, f.Joining!.Carry.Length > 0 ? f.Joining.Carry : FindCarry(f)));

        // Every member of the group gets the same story bytes (their engines apply them in the same load step).
        var story = Story.Story;
        var storyBlob = story.ToBlob();
        var dropped = Story.Dropped.ToBlob();
        foreach (var a in g.Members)
        {
            var m = a.Member;
            m.Arrival = null;
            var slotMap = new sbyte[Wire.MaxSlots];
            Array.Fill(slotMap, (sbyte)-1);
            foreach (var b in g.Members) slotMap[b.Slot] = (sbyte)map[b.Member];
            m.Peer?.Send(new BarrierOkMsg(fromId, fromVisit, BarrierDecision.New, inst.Id, inst.Visit, g.Stage, g.Point, (byte)map[m], slotMap,
                inst.Mode, (byte)(inst.Mode == InstanceMode.Sealed ? delays[m] : 0), coList, story.Revision, storyBlob, dropped, notice, noticeNames).Frame());
        }
        foreach (var c in g.CoArrivals)
        {
            foreach (var a in c.Members)
            {
                a.Member.Arrival = null;
                inst.CoArrivalPending.Add(a.Member);
                CoArriveMessage(c.From, a.Member, inst, map[a.Member], delays[a.Member], g.Point, map);
            }
            EndIfEmpty(c.From, now);
        }
        foreach (var f in followers)
        {
            var j = f.Joining!;
            f.Joining = null;
            inst.CoArrivalPending.Add(f);
            CoArriveMessage(j.Target, f, inst, map[f], delays[f], j.From.Point, map);
        }
        foreach (var o in oldObservers.Except(followers)) FallbackNew(o, now);
        if (!reuse) EndIfEmpty(g.From, now);
        Note("new", inst, ("members", map.ToDictionary(kv => kv.Value.ToString(), kv => (object?)kv.Key.Index)), ("mode", inst.Mode.ToString()),
            ("co_arrivals", coList.Count), ("notice", notice.ToString()));
    }

    private static byte[] FindCarry(HubMember m) => m.Joining?.Carry ?? [];

    private void CoArriveMessage(HubInstance from, HubMember m, HubInstance inst, int slot, int delay, byte point, Dictionary<HubMember, int> map)
    {
        var none = new sbyte[Wire.MaxSlots];
        Array.Fill(none, (sbyte)-1);
        m.Cursor = 0;
        // The other members of the new visit by slot, without carries (the co-arrival's engine comes from the base's
        // snapshot): its client knows their names (before, it showed "A player left" for its partners).
        var others = map.Where(kv => kv.Key != m).OrderBy(kv => kv.Value).Select(kv => new CoArrival((byte)kv.Value, (byte)kv.Key.Index, [])).ToList();
        m.Peer?.Send(new BarrierOkMsg(from.Id, from.Visit, BarrierDecision.CoArrive, inst.Id, inst.Visit, inst.Stage, point, (byte)slot, none,
            inst.Mode, (byte)delay, others, Story.Story.Revision, Story.Story.ToBlob(), Story.Dropped.ToBlob()).Frame());
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
        i.StoryEpoch = Story.Epoch;
        i.OwnerSeen = false;
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
        var id = _session.NextInstance;
        while (_instances.ContainsKey(id) || id == 0) id = id >= 999_999_999 ? 1 : id + 1;
        _session.NextInstance = id >= 999_999_999 ? 1 : id + 1;
        var i = new HubInstance(id)
        {
            Mode = mode, Stage = stage, Visit = visit, ContextHash = context, ArrivalContext = context, CreatedAt = now, LastLogAt = now, VisitStartedAt = now, StoryEpoch = Story.Epoch,
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
        // (A member that plays on alone in its last visit, a reconnect without resuming placed in an instance without a
        // stage, keeps that visit's epoch: no barrier has cleaned its engine since. Before round 59's verification it took
        // the current epoch, and what was dropped in that visit counted again from the same engine.)
        if (!string.IsNullOrEmpty(i.Stage) || m.LoneStage is null)
        {
            m.VisitEpoch = i.StoryEpoch;
            m.VisitOwnerSeen = i.OwnerSeen;
        }
        if (!string.IsNullOrEmpty(i.Stage))
        {
            m.LoneStage = null;
            m.LoneKey = 0;
            // The engine line this visit's counters grow in: besides its own instance 0 lines the only kind of line the
            // member may report (8.7).
            AllowLine(m, i.Id, i.Visit);
            OwnerEnters(m, i, visitBegins: i.VisitStartedAt == now);
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
        foreach (var o in i.Observers.ToList()) FallbackNew(o, now);
        i.Observers.Clear();
        _holds.RemoveAll(h => h.Target == i && h.Members.Count == 0);
        foreach (var h in _holds.Where(h => h.Target == i).ToList())
        {
            _holds.Remove(h);
            h.Hold = HoldKind.None;
            JoinOrNew(h, now);
        }
        Note("end", i);
    }

    // Join -----------------------------------------------------------------------------------------------------------------

    /// <summary>JOIN (SEAMLESS.md 3.4 step 3, 6.6): slots are reserved, the members catch up as observers, READY stamps the JOIN.</summary>
    private void Join(ArrivalGroup g, HubInstance target, long now)
    {
        _holds.Remove(g);
        g.Hold = HoldKind.None;
        var free = Enumerable.Range(0, Wire.MaxSlots).Select(s => target.Slots[s] is null && target.Reserved[s] is null).ToArray();
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
            m.Joining = new JoinState(target, slot, g, a.Slot) { Carry = a.Carry };
            target.Observers.Add(m);
            var none = new sbyte[Wire.MaxSlots];
            Array.Fill(none, (sbyte)-1);
            m.Peer?.Send(new BarrierOkMsg(g.From.Id, g.From.Visit, BarrierDecision.Join, target.Id, target.Visit, g.Stage, g.Point, (byte)slot, none,
                target.Mode, (byte)DelayController.FromRoundTrips(m.Rtt.All, _sealing.MaxDelayTicks), [], Story.Story.Revision, Story.Story.ToBlob(),
                Story.Dropped.ToBlob()).Frame());
            Note("join_decided", target, ("member", m.Index), ("slot", slot));
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
                j.FromCheckpoint = true;
                Count("checkpoint_joins");
                SendState(m, t, StatePurpose.Join, j.Slot, cp);
                return;
            }
        }
        j.StateSent = true;
        j.FromCheckpoint = false;
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
        donor.Peer.Send(new CheckpointAskMsg(t.Id, t.Visit, 0, flags).Frame());
        Count("checkpoint_asks");
        if (flags == 0) Note("checkpoint_ask", t, ("donor", donor.Index));
    }

    /// <summary>STATEHASH (6.13.4): the members' hashes of a checkpoint tick; equal reports make the load end and a stored
    /// checkpoint of that tick agreed. A solo leader's report is accepted unagreed (the joiner's own check verifies it).</summary>
    private void OnStateHash(HubMember m, StateHashMsg msg, long now)
    {
        if (Mine(m, msg.Inst, msg.Visit) is not { } i) return;
        if (!i.StateHashes.TryGetValue(msg.Tick, out var d)) i.StateHashes[msg.Tick] = d = [];
        if (!d.TryAdd(m.Slot, (msg.Hash, msg.LoadEnd)))
        {
            Count("statehash_repeat");
            return;
        }
        while (i.StateHashes.Count > 16) i.StateHashes.Remove(i.StateHashes.Keys.First());
        var expected = i.Seats.Where(x => x.Member.Connected && !x.Member.Resyncing && (i.Streams[x.Slot] is not { } s || s.FromTick <= msg.Tick))
            .Select(x => x.Slot).ToList();
        if (expected.Count == 0 || !expected.All(d.ContainsKey)) return;
        var values = expected.Select(s => d[s]).Distinct().ToList();
        if (values.Count > 1)
        {
            // A mismatch: the fingerprint vote (NETFP) resyncs the member that is off; the checkpoint path of the instance
            // is not trusted any more (6.13.4).
            Count("statehash_mismatch");
            Note("statehash_mismatch", i, ("tick", msg.Tick), ("hashes", expected.ToDictionary(s => s.ToString(), s => d[s].Hash.ToString("x16"))));
            if (i.RebuildTick is { } rebuilt && msg.Tick <= rebuilt + (uint)_inst.CheckpointSuspectTicks) MarkSuspect(i, msg.Tick, "statehash");
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
        if (msg.Offset == 0) i.Upload = new CheckpointUpload(m, msg.Tick, msg.Hash, msg.LoadEnd, msg.Total);
        if (i.Upload is not { } u || u.Donor != m || u.Tick != msg.Tick || u.Hash != msg.Hash || u.Buffer.Length != msg.Total || msg.Offset != u.Received)
        {
            Count("checkpoint_chunk_dropped");
            return;
        }
        msg.Chunk.CopyTo(u.Buffer, (int)msg.Offset);
        u.Received += (uint)msg.Chunk.Length;
        if (u.Received < u.Buffer.Length) return;
        i.Upload = null;
        var stored = new StoredCheckpoint(u.Tick, u.Hash, u.LoadEnd, u.Buffer, now);
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
        if (target.Fingerprints.TryGetValue(msg.Tick, out var fps) && fps.Count > 0)
        {
            var common = fps.Values.GroupBy(x => x).OrderByDescending(x => x.Count()).First().Key;
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
        if (fromCheckpoint)
        {
            target.RebuildTick = msg.Tick;
            Count("ready_from_checkpoint");
        }
        Note("ready", target, ("member", m.Index), ("tick", msg.Tick), ("rate", msg.Rate), ("checkpoint", fromCheckpoint));
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
        if (m.PreferredSlot < 0) m.PreferredSlot = j.Slot;
        // The joined visit's epoch (the joiner's engine is that visit's snapshot and ticks), and the owner's arrival (8.6).
        m.VisitEpoch = target.StoryEpoch;
        m.VisitOwnerSeen = target.OwnerSeen;
        // The joined visit's engine line, as Place gives it (8.7): the joiner's counters grow in it like its partners'
        // (a line keeps the largest growth reported, so the same line from two members does not count twice).
        if (!string.IsNullOrEmpty(target.Stage)) AllowLine(m, target.Id, target.Visit);
        OwnerEnters(m, target, visitBegins: false);
    }

    /// <summary>
    /// The room owner entered a visit of a stage outside the title (Place, a join; SEAMLESS.md 8.6). A room whose owner had
    /// no online story starts now (at zero). The owner's first visit begins with the start when it begins at this barrier:
    /// it gets the start's epoch (every engine of it starts from this barrier's load step, cleaned of the dropped values).
    /// A visit the owner is in counts from then on (OwnerSeen) even if it began before the start; what it offered before
    /// stays dropped.
    /// </summary>
    private void OwnerEnters(HubMember m, HubInstance i, bool visitBegins)
    {
        if (!m.Owner || string.IsNullOrEmpty(i.Stage) || i.Stage.StartsWith("stagis", StringComparison.OrdinalIgnoreCase)) return;
        if (Story.OwnerStarted())
        {
            Note("story_started", i, ("member", m.Index));
            if (visitBegins) i.StoryEpoch = Story.Epoch;
        }
        i.OwnerSeen = true;
        foreach (var x in i.Members)
        {
            x.VisitEpoch = i.StoryEpoch;
            x.VisitOwnerSeen = true;
        }
    }

    private void OnJoinAt(HubMember m, JoinAtMsg msg, long now)
    {
        if (Mine(m, msg.Inst, msg.Visit) is not { Mode: InstanceMode.SoloLed } i || i.Leader != m) return;
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
        _sealer?.Invoke();
    }

    /// <summary>A joiner whose target went away (or whose READY failed twice) restores its own load snapshot and enters as NEW.</summary>
    private void FallbackNew(HubMember m, long now, BarrierNotice notice = BarrierNotice.None, JoinState? joined = null)
    {
        // (joined: a joiner the server made a member already, whose engine never got its JOIN: a gap before J.)
        if ((m.Joining ?? joined) is not { } j) return;
        j.Target.Observers.Remove(m);
        if (j.Target.Reserved[j.Slot] == m) j.Target.Reserved[j.Slot] = null;
        if (j.Target.SoloJoin?.Joiner == m) j.Target.SoloJoin = null;
        m.Joining = null;
        m.JoinedVia = null;
        var inst = NewInstance(InstanceMode.SoloLed, j.From.Stage, 1, j.From.Context, now);
        Place(m, inst, 0, now);
        inst.Snapshot.Uploader = m;
        var map = new sbyte[Wire.MaxSlots];
        Array.Fill(map, (sbyte)-1);
        map[j.OldSlot] = 0;
        m.Peer?.Send(new BarrierOkMsg(j.Target.Id, j.Target.Visit, BarrierDecision.New, inst.Id, inst.Visit, inst.Stage, j.From.Point, 0, map,
            InstanceMode.SoloLed, 0, [], Story.Story.Revision, Story.Story.ToBlob(), Story.Dropped.ToBlob(),
            notice == BarrierNotice.None ? BarrierNotice.HoldExpired : notice, []).Frame());
        Note("fallback_new", inst, ("member", m.Index), ("from", j.Target.Id));
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
            return;
        }
        var everyone = i.Seats.Where(x => x.Member.Connected).All(x => vote.Reporters.Contains(x.Slot));
        var bothSides = vote.Reporters.Any(s => (leavers & (1 << s)) != 0) && vote.Reporters.Any(s => (present & ~leavers & (1 << s)) != 0);
        if (everyone || (expired && bothSides))
        {
            vote.Done = true;
            Fork(i, vote.Tick, leavers, vote.Kind, now);
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
        // Round J: the parent's checkpoints before the fork until the child's own (one engine line up to it).
        child.Checkpoints = i.Checkpoints.Where(c => c.Tick <= f).ToList();
        child.AgreedLoadEnd = i.AgreedLoadEnd;
        child.CheckpointSuspect = i.CheckpointSuspect;
        child.VisitStartedAt = i.VisitStartedAt;
        child.StoryEpoch = i.StoryEpoch;
        child.OwnerSeen = i.OwnerSeen;
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
        // Pending events: JOIN stays with the instance id; LEAVE, INTENT and DELAY follow their slot.
        foreach (var (t, list) in i.PendingEvents.ToList())
        {
            var moving = list.Where(e => e.Type != Ev.Join && e.Type != Ev.LeadFrom && e.Slot >= 0 && (leavers & (1 << e.Slot)) != 0).ToList();
            foreach (var e in moving)
            {
                list.Remove(e);
                child.AddEvent(Math.Max(t, from), e);
            }
            if (list.Count == 0) i.PendingEvents.Remove(t);
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

    // Room story (SEAMLESS.md 8.3-8.6) ---------------------------------------------------------------------------------------

    private void OnStory(HubMember m, StoryMsg msg, long now)
    {
        var keeper = Story;
        Count("story");
        // A burst, then Instances:StoryMessagesPerMinute (the client sends again what the rate refused).
        var burst = Math.Max(1, _inst.StoryBurst);
        if (m.StoryTokens < 0) m.StoryTokens = burst;
        else m.StoryTokens = Math.Min(burst, m.StoryTokens + Ms(now - m.StoryRefilledAt) / 60_000.0 * Math.Max(1, _inst.StoryMessagesPerMinute));
        m.StoryRefilledAt = now;
        StoryTake take;
        HubInstance? at = null;
        var rated = m.StoryTokens < 1;
        if (rated)
        {
            Count("story_rate");
            var refused = new List<StoryRefused>();
            var d = msg.Delta;
            if (d.Phase != 0) refused.Add(new StoryRefused(StoryKeyKind.Phase, d.Phase, StoryRefusal.Rate));
            refused.AddRange(d.Flags.Select(f => new StoryRefused(StoryKeyKind.Flag, f, StoryRefusal.Rate)));
            refused.AddRange(d.Folder.Select(k => new StoryRefused(StoryKeyKind.Folder, k, StoryRefusal.Rate)));
            refused.AddRange(d.Counters.Select(g => new StoryRefused(StoryKeyKind.Counter, g.Counter, StoryRefusal.Rate)));
            take = new StoryTake(refused, false);
        }
        else
        {
            m.StoryTokens -= 1;
            // Where the member plays now decides (its stage, the epoch its visit began in, the visit); the frame's ids may
            // be the ones before a fork. A parked or catching-up member sends none (the client sends STORY before BARRIER).
            var (stage, epoch, visit, inst) = StoryOrigin(m, now);
            at = inst;
            var (delta, lines) = OwnLines(m, msg.Delta);
            // The lines nobody can report any more go (8.7: they never come again).
            if (keeper.LineCount > LinePruneAt) PruneStoryLines();
            take = keeper.Take(delta, stage, epoch, visit);
            if (lines.Count > 0) take = take with { Refused = [.. take.Refused, .. lines] };
        }
        if (take.Refused.Count > 0) Count("story_refused", take.Refused.Count);
        NotePolicy(m, take, at);
        // (A STORY refused for the rate changed nothing: its answer carries no story, 21 bytes of request were 1,120 of answer.)
        m.Peer?.Send(new StoryAckMsg(keeper.Story.Revision, rated ? [] : keeper.Story.ToBlob(), StoryAckMsg.Answer, take.Refused).Frame());
        if (take.Changed) StoryChanged(m);
    }

    /// <summary>The membership log names what the owner-away policy took (the scenarios of SEAMLESS.md 16 check it).</summary>
    private void NotePolicy(HubMember m, StoryTake take, HubInstance? at)
    {
        foreach (var (why, ev) in new[] { (StoryRefusal.OwnerAway, "story_dropped"), (StoryRefusal.NotStarted, "story_dropped"),
                     (StoryRefusal.Held, "story_held") })
        {
            var keys = take.Refused.Where(r => r.Reason == why).Select(r => r.Kind.ToString().ToLowerInvariant() + ":" + r.Index).ToList();
            if (keys.Count == 0) continue;
            if (why == StoryRefusal.NotStarted) Note(ev, at, ("member", m.Index), ("keys", string.Join(",", keys)), ("reason", "not_started"));
            else Note(ev, at, ("member", m.Index), ("keys", string.Join(",", keys)));
        }
    }

    /// <summary>
    /// The earned story values that are due (SEAMLESS.md 8.7): a phase step or key item that waited only for the
    /// one-per-visit rule or a guest's spacing is merged in its member's later visit, whatever stage the member plays in
    /// then (the stage that set it may be long left, where a resend would be refused for the stage); every member gets the
    /// new revision. The client keeps sending such a value meanwhile and is answered wait, as before.
    /// </summary>
    private void MergeEarned(long now)
    {
        if (_session.Story is not { EarnedCount: > 0 } keeper) return;
        var changed = false;
        foreach (var m in _members.Values.OrderBy(x => x.Index).ToList())
        {
            if (!keeper.HasEarned(m.Index, m.AccountId)) continue;
            var (stage, _, visit, inst) = StoryOrigin(m, now);
            if (visit is not { } v) continue;
            var take = keeper.MergeEarned(v, stage);
            NotePolicy(m, take, inst);
            if (!take.Changed) continue;
            changed = true;
            Count("story_earned");
            Note("story_earned", inst, ("member", m.Index), ("phase", StoryMerge.Phase(keeper.Story)));
        }
        if (changed) StoryChanged(null);
    }

    /// <summary>
    /// Where a member's STORY comes from (SEAMLESS.md 8.7): its instance's stage, visit and age while the server has it in
    /// one; while its client plays alone where the server has no instance with a stage for it, the stage it plays in: the
    /// target of a barrier it went on from after the five seconds (its arrival is still parked here), or the stage it was
    /// taken out of its group in (lag, resync limits) or reconnected in without resuming. A member alone counts from the
    /// epoch of its last instance visit (its engine was cleaned at that barrier, not at the one it went on from).
    /// </summary>
    private (string? Stage, long Epoch, StoryVisit? Visit, HubInstance? Inst) StoryOrigin(HubMember m, long now)
    {
        if (m.Joining is not null) return (null, 0, null, null);
        var at = Ms(now) / 1000.0;
        if (m.Instance is { Ended: false } i && m.Slot >= 0 && m.Arrival is null && !string.IsNullOrEmpty(i.Stage))
        {
            var since = Math.Max(m.InstanceSince, i.VisitStartedAt);
            return (i.Stage, i.StoryEpoch, new StoryVisit(m.Index, (ulong)i.Id << 32 | i.Visit, Ms(now - since) / 1000.0, m.AccountId, at, m.Owner,
                i.OwnerSeen), i);
        }
        if (m.Arrival is { } a)
            return (a.Stage, m.VisitEpoch, new StoryVisit(m.Index, 1UL << 63 | (ulong)a.ParkedAt, Ms(now - a.ParkedAt) / 1000.0, m.AccountId, at, m.Owner,
                m.VisitOwnerSeen), null);
        if (m.LoneStage is { } lone)
            return (lone, m.VisitEpoch, new StoryVisit(m.Index, m.LoneKey != 0 ? m.LoneKey : 1UL << 63 | (ulong)m.LoneSince,
                Ms(now - m.LoneSince) / 1000.0, m.AccountId, at, m.Owner, m.VisitOwnerSeen), null);
        return (null, 0, null, null);
    }

    /// <summary>The keeper prunes its lines once it remembers more than this many (a few per member are normal).</summary>
    private const int LinePruneAt = 64;

    /// <summary>Forgets the counter lines no member can report any more (its own lines, its instance 0 lines).</summary>
    private void PruneStoryLines()
    {
        var reportable = new HashSet<(uint, uint)>();
        foreach (var m in _members.Values)
        {
            reportable.UnionWith(m.StoryLines);
            foreach (var v in m.LoneLineVisits) reportable.Add((0x8000_0000u | (uint)m.Index, v));
        }
        var removed = Story.PruneLines(k => reportable.Contains((k.Inst, k.Visit)));
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

    /// <summary>A new revision: every other member's confirmed story follows (STORY_ACK), the owner's online story is saved.</summary>
    private void StoryChanged(HubMember? sender)
    {
        var story = Story.Story;
        var frame = new StoryAckMsg(story.Revision, story.ToBlob(), 0, []).Frame();
        foreach (var other in _members.Values)
            if (other != sender) other.Peer?.Send(frame);
        Note("story", null, ("revision", story.Revision), ("phase", StoryMerge.Phase(story)));
        // (Only counters changed since the last save: the saver writes such revisions at most once a minute, SEAMLESS.md 8.7.)
        var countersOnly = _session.Saved is { } before && before.Flags.AsSpan().SequenceEqual(story.Flags) && before.Folder.AsSpan().SequenceEqual(story.Folder);
        _session.Saved = story.Clone();
        if (Info.Story is { OwnerAccountId: > 0 } s) StorySaver?.Invoke(s.OwnerAccountId, story.Clone(), countersOnly);
    }

    /// <summary>The room story as the room API shows it.</summary>
    public RoomStoryState StoryState()
    {
        var k = Story;
        var s = k.Story;
        var dropped = k.Dropped;
        return new RoomStoryState(s.Revision, StoryMerge.Phase(s), Enumerable.Range(0, StorySets.FlagCount).Where(f => s.Flags[f] != 0).ToList(),
            Enumerable.Range(0, StorySets.FolderCount).Where(f => s.Folder[f] != 0).ToList(), [.. s.Counters], k.Policy.ToString().ToLowerInvariant(),
            k.OwnerPresent, dropped.Flags, dropped.Folder, dropped.Counters, k.HeldCount)
        {
            Started = k.Started,
            Difficulty = k.Difficulty,
        };
    }

    /// <summary>
    /// The most players the account's instance had in the last <paramref name="seconds"/> (its instance now, and every
    /// size the housekeeping saw it in since): the achievements' "players" cannot claim more (a guest alone in its lobby
    /// reported four). Null when the account is no member of this room's relay.
    /// </summary>
    public int? RecentInstancePlayers(long accountId, long now, double seconds = 60)
    {
        if (_members.Values.FirstOrDefault(x => x.AccountId == accountId) is not { } m) return null;
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
            Count("resume_refused");
            return;
        }
        m.ResumeAllowed = false; // once per connection
        if (i.Mode == InstanceMode.Sealed)
        {
            // Only the ticks lost with the old connection come again (a reconnect's gap); the cursor never moves forward.
            var from = Math.Min(msg.LastTick + 1, i.StreamEnd);
            if (from < m.Cursor && m.Cursor - from > (uint)Math.Max(0, _inst.MaxResumeTicks))
            {
                Count("resume_refused");
                return;
            }
            if (from < m.Cursor) m.Cursor = from;
            Pump(i, m, bulk: true);
        }
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

    /// <summary>Pings, holds, lag and reconnect limits, the earned story values, the roster (the relay calls it a few times per second).</summary>
    public void Housekeep(long now)
    {
        if (Ms(now - _lastPing) >= 1000)
        {
            _lastPing = now;
            foreach (var m in _members.Values)
            {
                if (m.Peer is null) continue;
                m.PingNonce = ++_pingNonce;
                m.PingSentAt = now;
                var st = m.Instance is { } i && m.Slot >= 0 ? i.Streams[m.Slot] : null;
                var margin = st?.Delay.LowMargin() is { } low ? (short)Math.Clamp(low, short.MinValue, short.MaxValue) : (short)0;
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
        if (Ms(now - _lastRoster) >= 1000)
        {
            _lastRoster = now;
            var entries = _members.Values.OrderBy(m => m.Index).Select(m => new RosterEntry((byte)m.Index,
                m.Presence?.Stage ?? m.Instance?.Stage ?? "", m.Instance?.Id ?? m.Joining?.Target.Id ?? 0, m.Presence?.Level ?? 0, m.Presence?.Hp ?? 0,
                m.Presence?.HpMax ?? 0, (byte)((m.Connected ? RosterEntry.Connected : 0) | (m.Lagging ? RosterEntry.Lagging : 0) | (m.Owner ? RosterEntry.Owner : 0)))).ToList();
            var frame = new RosterMsg(entries).Frame();
            foreach (var m in _members.Values) m.Peer?.Send(frame);
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
        // Round J: while joinable, the donor uploads a checkpoint now and then (resyncs, a donor that does not answer).
        if (_inst.CheckpointPeriodicMs > 0 && i.Joinable && CheckpointPathOpen(i) && i.Upload is null && i.Barrier is null &&
            Ms(now - Math.Max(i.CheckpointAskedAt, i.Checkpoints.LastOrDefault()?.At ?? 0)) >= _inst.CheckpointPeriodicMs)
            AskCheckpoint(i, now, CheckpointAskMsg.Periodic);
        // Round J: joins that waited for the donor's checkpoint long enough take the newest stored one or the classic path.
        foreach (var o in i.Observers.ToList())
            if (o.Joining is { } oj && oj.Target == i && !oj.StateSent && oj.CheckpointWaitUntil != 0 && now >= oj.CheckpointWaitUntil)
                DeliverState(o, now);
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
                    foreach (var o in _members.Values)
                        o.Peer?.Send(new RoomMsg(lagging ? RoomNotice.Lagging : RoomNotice.LagOk, (byte)m.Index, (ulong)m.AccountId, m.Name, Flags(m)).Frame());
                }
                var gone = !m.Connected && Ms(now - m.DisconnectedAt) >= _relay.ReconnectSeconds * 1000.0;
                var lagOut = lagging && !everyoneLags && !m.Resyncing && Ms(now - st.FillStreakSince) >= _relay.LagLeaveSeconds * 1000.0;
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
                if (!m.Connected && Ms(now - m.DisconnectedAt) >= _relay.ReconnectSeconds * 1000.0)
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
