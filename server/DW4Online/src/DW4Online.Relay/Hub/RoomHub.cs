using System.Diagnostics;
using System.Numerics;
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
    private readonly Dictionary<int, HubMember> _members = [];
    private readonly Dictionary<uint, HubInstance> _instances = [];
    private readonly List<ArrivalGroup> _holds = [];
    private uint _nextInstance = 1;
    private uint _pingNonce;
    private long _lastPing;
    private long _lastRoster;

    public RoomHub(RoomInfo info, RelayOptions relay, SealingOptions sealing, InstanceOptions instances, ILogger log, IMembershipLog? mlog = null)
    {
        Info = info;
        _relay = relay;
        _sealing = sealing;
        _inst = instances;
        _log = log;
        _mlog = mlog;
    }

    public object Sync => this;
    public RoomInfo Info { get; set; }
    public string Code => Info.Code;
    public bool Empty => _members.Values.All(m => !m.Connected) && _instances.Count == 0;
    public IReadOnlyCollection<HubMember> Members => _members.Values;
    public HubInstance? Instance(uint id) => _instances.GetValueOrDefault(id);
    public IReadOnlyCollection<HubInstance> Instances => _instances.Values;
    /// <summary>Counters for the tests and the diagnostics.</summary>
    public Dictionary<string, long> Counters { get; } = [];

    private int Capacity => Math.Clamp(_inst.PlayersPerInstance, 1, Wire.MaxSlots);
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
        if (!resumable)
        {
            DetachLocked(m, now, leaveNow: true);
            var hub = NewInstance(InstanceMode.Hub, "", 1, 0, now);
            Place(m, hub, 0, now);
        }
        var inst = m.Instance!;
        peer.Send(new WelcomeMsg((byte)m.Index, inst.Id, inst.Visit, (byte)m.Slot, inst.Mode, inst.Stage,
            resumable && m.Cursor > 0 ? m.Cursor - 1 : WelcomeMsg.NoTick, (byte)(inst.Streams[m.Slot]?.Delay.Current ?? 0),
            (ushort)_inst.MaxCatchUpSeconds, resumable ? WelcomeMsg.Resumable : (byte)0).Frame());
        foreach (var other in _members.Values)
        {
            peer.Send(new RoomMsg(RoomNotice.MemberJoined, (byte)other.Index, (ulong)other.AccountId, other.Name, Flags(other)).Frame());
            if (other != m) other.Peer?.Send(new RoomMsg(RoomNotice.Connected, (byte)m.Index, (ulong)m.AccountId, m.Name, Flags(m)).Frame());
        }
        Note("connect", inst, ("member", m.Index), ("resumed", resumable));
        return m;
    }

    private static byte Flags(HubMember m) => m.Owner ? RoomMsg.Owner : (byte)0;

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
            DetachLocked(m, now, leaveNow: true);
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
            if (i.Barrier is { } b) b.Reports.Remove(m.Slot);
            if (i.Mode == InstanceMode.Sealed && i.Sealing && leaveNow) StampLeave(i, m.Slot, now);
            else
            {
                i.Slots[m.Slot] = null;
                if (i.Mode == InstanceMode.Sealed && !i.Sealing) i.Streams[m.Slot] = null; // before tick 0: as if it never came
                else if (i.Streams[m.Slot] is { } s) s.UntilTick = i.NextSeal;
                if (i.Count == 0) EndInstance(i, now);
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
            case Msg.Story: Count("story_ignored"); break; // round S2
            case Msg.Presence: OnPresence(m, PresenceMsg.Parse(p), now); break;
            case Msg.Resume: OnResume(m, ResumeMsg.Parse(p), now); break;
            case Msg.Rate: m.Rate = Math.Min(RateMsg.Parse(p).StepsPerSecond, 1_000_000u); break;
            case Msg.Pong: OnPong(m, PongMsg.Parse(p), now); break;
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
            switch (s.Offer(tick, msg.Pads[k], i.NextSeal, margin))
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
        var next = i.Log.Count;
        var first = msg.FirstTick;
        if (first > next) throw new ProtocolException("LOG skips ticks");
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

    private void OnPresence(HubMember m, PresenceMsg msg, long now)
    {
        if (m.PresenceAt != 0 && Ms(now - m.PresenceAt) < 150)
        {
            Count("presence_dropped");
            return;
        }
        m.Presence = msg;
        m.PresenceAt = now;
        var frame = new PresenceOutMsg((byte)m.Index, msg).Frame();
        foreach (var o in _members.Values)
        {
            if (o == m || o.Peer is null || o.Presence is null) continue;
            if (!string.Equals(o.Presence.Stage, msg.Stage, StringComparison.OrdinalIgnoreCase)) continue;
            if (o.Instance is not null && m.Instance is not null && o.Instance == m.Instance && o.Instance.Mode != InstanceMode.Hub) continue;
            o.Peer.Send(frame);
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
                    i.Mode = InstanceMode.SoloLed;
                    i.Sealing = false;
                    i.LeadFrom = null;
                    i.LastLogAt = now;
                    Array.Clear(i.Streams);
                    Note("lead_from", i, ("tick", i.NextSeal));
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
            foreach (var key in i.PendingEvents.Keys.Where(k => k <= t).ToList())
            {
                rec.Events.AddRange(i.PendingEvents[key]);
                i.PendingEvents.Remove(key);
            }
        }
        var bytes = rec.ToBytes();
        if (i.Log.Count == t) i.Log.Append(bytes);
        else i.Ahead[t] = bytes;
        FlushAhead(i);
        i.NextSeal = t + 1;
        if (t % 60 == 59) EvaluateDelays(i, t);
        return bytes;
    }

    private static TickRecord Rebuild(TickRecord r, byte mask)
    {
        var n = new TickRecord { Mask = mask };
        Array.Copy(r.Pads, n.Pads, Wire.MaxSlots);
        return n;
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
                Note("delay", i, ("slot", st.Slot), ("d", d), ("tick", t + 2));
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
            foreach (var o in i.Observers.ToList()) DeliverState(o, now);
            foreach (var mem in i.CoArrivalPending.ToList()) SendCoArrivalState(i, mem);
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
        if (i.Mode != InstanceMode.Sealed || i.Sealing || i.Barrier is not null) return;
        if (i.Members.Any(x => !i.Snapshot.Hashes.ContainsKey(x.Slot) && x.Connected)) return;
        var rtt = i.Members.Select(x => x.Rtt.Max).DefaultIfEmpty(0).Max();
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
        if (!i.Fingerprints.TryGetValue(msg.Tick, out var d)) i.Fingerprints[msg.Tick] = d = [];
        d[m.Slot] = msg.Hash;
        while (i.Fingerprints.Count > 64) i.Fingerprints.Remove(i.Fingerprints.Keys.First());
        var expected = i.Members.Where(x => x.Connected && (i.Streams[x.Slot] is not { } s || s.FromTick <= msg.Tick)).Select(x => x.Slot).ToList();
        if (expected.Count < 2 || !expected.All(d.ContainsKey) || expected.Select(s => d[s]).Distinct().Count() == 1) return;
        Vote(i, msg.Tick, d, expected, now);
    }

    /// <summary>A fingerprint mismatch (SEAMLESS.md 6.9): the majority wins, with two members the longer member.</summary>
    private void Vote(HubInstance i, uint tick, Dictionary<int, ulong> d, List<int> slots, long now)
    {
        ++i.Desyncs;
        Count("desync_votes");
        var groups = slots.GroupBy(s => d[s]).OrderByDescending(g => g.Count()).ToList();
        ulong winner;
        if (groups[0].Count() >= 2 && (groups.Count == 1 || groups[1].Count() < groups[0].Count())) winner = groups[0].Key;
        else
        {
            var longest = slots.Select(s => i.Slots[s]!).OrderBy(x => x.InstanceSince).First();
            winner = d[longest.Slot];
        }
        _log.LogWarning("Room {Room} instance {Inst}: net fingerprints differ at tick {Tick}", Code, i.Id, tick);
        Note("desync", i, ("tick", tick), ("fingerprints", slots.ToDictionary(s => s.ToString(), s => d[s].ToString("x16"))));
        foreach (var s in slots.Where(s => d[s] != winner))
        {
            var loser = i.Slots[s]!;
            var catchUp = HubRules.CatchUpSeconds(i.StreamEnd, loser.Rate == 0 ? _inst.DefaultReplayRate : loser.Rate);
            if (catchUp > _inst.MaxCatchUpSeconds || !i.Snapshot.Complete)
            {
                // Beyond the limit the member continues alone (round S4 completes the path; S1 stamps the LEAVE).
                Note("resync_too_long", i, ("slot", s));
                StampLeave(i, s, now);
                continue;
            }
            loser.Peer?.Send(new ResyncMsg(i.Id, i.Visit).Frame());
            SendState(loser, i, StatePurpose.Resync, s);
            Note("resync", i, ("slot", s));
        }
    }

    private void OnState(HubMember m, StateMsg msg, long now)
    {
        if (Mine(m, msg.Inst, msg.Visit) is not { } i) return;
        i.Joinable = (msg.Flags & StateMsg.Joinable) != 0;
        i.MembershipSafe = (msg.Flags & StateMsg.MembershipSafe) != 0;
        var boss = (msg.Flags & StateMsg.BossLock) != 0;
        if (boss != i.BossLock) Note(boss ? "boss_lock" : "boss_clear", i);
        i.BossLock = boss;
        i.Reason = msg.Reason;
        i.ContextHash = msg.ContextHash;
        CheckHolds(now);
    }

    // Barrier ------------------------------------------------------------------------------------------------------------

    private void OnBarrier(HubMember m, BarrierMsg msg, long now)
    {
        if (m.Arrival is not null || m.Joining is not null)
        {
            Count("barrier_while_parked");
            return;
        }
        var i = m.Instance;
        if (i is null || i.Ended || i.Visit != msg.Visit || (i.Id != msg.Inst && m.PreviousInstanceId != msg.Inst) || m.Slot < 0)
        {
            // A straggler whose group was decided without it, or a member the server lost track of: it arrives alone.
            var lone = new HubInstance(0) { Stage = i?.Stage ?? "", Visit = msg.Visit, ContextHash = msg.ContextHash, Ended = true };
            var g = new ArrivalGroup { From = lone, BarrierTick = msg.Tick, Stage = msg.Stage, Point = msg.Point, Context = msg.ContextHash, ParkedAt = now };
            g.Members.Add(new Arrival(m, Math.Max(0, m.Slot), msg.Carry));
            m.Instance = null;
            m.Slot = -1;
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
        i.Barrier.Reports[m.Slot] = (m, msg.Carry);
        if (i.Members.Where(x => x.Connected).All(x => i.Barrier.Reports.ContainsKey(x.Slot))) Park(i, now);
    }

    /// <summary>Every connected member of the instance reported its barrier (or the wait ran out): the group parks.</summary>
    private void Park(HubInstance i, long now)
    {
        var b = i.Barrier!;
        if (i.Log.Count > b.Tick + 1 && b.Tick + 1 >= i.Log.PrefixCount) i.Log.TruncateTo(b.Tick + 1);
        foreach (var k in i.Ahead.Keys.Where(k => k > b.Tick).ToList()) i.Ahead.Remove(k);
        var g = new ArrivalGroup { From = i, BarrierTick = b.Tick, Stage = b.Stage, Point = b.Point, Context = b.Context, ParkedAt = now };
        foreach (var (slot, (member, carry)) in b.Reports.OrderBy(kv => kv.Key))
        {
            if (member.Instance != i || member.Slot != slot) continue;
            g.Members.Add(new Arrival(member, slot, carry));
            member.Arrival = g;
        }
        // Members that did not report are not part of the arrival; they arrive alone when their BARRIER comes.
        foreach (var x in i.Members.ToList())
            if (!b.Reports.ContainsKey(x.Slot))
            {
                i.Slots[x.Slot] = null;
                x.PreviousInstanceId = i.Id;
            }
        if (g.Members.Count == 0)
        {
            EndInstance(i, now);
            return;
        }
        Decide(g, now);
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
        ++i.Desyncs;
        Count("barrier_split");
        var lone = new HubInstance(0) { Stage = i.Stage, Visit = i.Visit, ContextHash = b.Context, Ended = true };
        var g = new ArrivalGroup { From = lone, BarrierTick = b.Tick, Stage = b.Stage, Point = b.Point, Context = b.Context, ParkedAt = now };
        var reporters = b.Reports.OrderBy(kv => kv.Key).Where(kv => kv.Value.Member.Instance == i && kv.Value.Member.Slot == kv.Key).ToList();
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
        // A base that holds for announced groups takes this one as a co-arrival.
        var holder = _holds.FirstOrDefault(h => h.Hold == HoldKind.CoArrival && h != g && SameStage(h.Stage, g.Stage) && h.Context == g.Context &&
                                                h.Size + g.Members.Count <= Capacity);
        if (holder is not null)
        {
            holder.CoArrivals.Add(g);
            foreach (var a in g.Members) a.Member.Arrival = holder;
            holder.Awaited.RemoveAll(x => g.Members.Any(a => a.Member == x));
            Note("co_arrival", holder.From, ("members", g.Members.Select(a => a.Member.Index).ToArray()));
            if (holder.Awaited.Count == 0) FinishCoArrival(holder, now);
            return;
        }
        // Announced groups on their way to the same stage with the same context: hold for them.
        var awaited = _members.Values.Where(x => g.Members.All(a => a.Member != x) && x.Heading is { } h && SameStage(h.Stage, g.Stage) &&
                                                 Ms(now - h.At) <= _inst.HeadingValidMs && x.Arrival is null && x.Joining is null &&
                                                 h.Context == g.Context && x.Instance is not null && x.Connected).ToList();
        var awaitedSize = awaited.Select(x => x.Instance).Distinct().Sum(xi => xi!.Count);
        if (awaited.Count > 0 && g.Members.Count + awaitedSize <= Capacity && _inst.CoArrivalHoldMs > 0)
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

    private void JoinOrNew(ArrivalGroup g, long now)
    {
        var size = g.Members.Count;
        var candidates = _instances.Values.Where(x => !x.Ended && x.Logged && !x.Departing && x.Barrier is null && x != g.From &&
                                                      SameStage(x.Stage, g.Stage)).ToList();
        if (candidates.Count == 0)
        {
            New(g, now, BarrierNotice.None, []);
            return;
        }
        var sameContext = candidates.Where(x => x.ContextHash == g.Context).ToList();
        var fits = sameContext.Where(x => x.FreeSlots >= size).ToList();
        var slowest = g.Members.Select(a => a.Member.Rate == 0 ? (double)_inst.DefaultReplayRate : a.Member.Rate).DefaultIfEmpty(_inst.DefaultReplayRate).Min();
        var ranked = fits.Select(x => (Inst: x, CatchUp: HubRules.CatchUpSeconds(CurrentTick(x, now), slowest)))
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
                InstanceMode.Hub, 0, [], 0, [], []).Frame());
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
                inst.Streams[slot] = new SlotStream(slot, 0, delays[member]) { Delay = { Max = _sealing.MaxDelayTicks, LateTarget = _sealing.LateTarget } };

        // The base engine spawns the co-arrivals (and followers) in its load step with their carries.
        var coList = new List<CoArrival>();
        foreach (var c in coMembers) coList.Add(new CoArrival((byte)map[c.Member], (byte)c.Member.Index, c.Carry));
        foreach (var f in followers) coList.Add(new CoArrival((byte)map[f], (byte)f.Index, f.Joining!.Carry.Length > 0 ? f.Joining.Carry : FindCarry(f)));

        foreach (var a in g.Members)
        {
            var m = a.Member;
            m.Arrival = null;
            var slotMap = new sbyte[Wire.MaxSlots];
            Array.Fill(slotMap, (sbyte)-1);
            foreach (var b in g.Members) slotMap[b.Slot] = (sbyte)map[b.Member];
            m.Peer?.Send(new BarrierOkMsg(g.From.Id, g.From.Visit, BarrierDecision.New, inst.Id, inst.Visit, g.Stage, g.Point, (byte)map[m], slotMap,
                inst.Mode, (byte)(inst.Mode == InstanceMode.Sealed ? delays[m] : 0), coList, 0, [], [], notice, noticeNames).Frame());
        }
        foreach (var c in g.CoArrivals)
        {
            foreach (var a in c.Members)
            {
                a.Member.Arrival = null;
                inst.CoArrivalPending.Add(a.Member);
                CoArriveMessage(c.From, a.Member, inst, map[a.Member], delays[a.Member], g.Point);
            }
            EndIfEmpty(c.From, now);
        }
        foreach (var f in followers)
        {
            var j = f.Joining!;
            f.Joining = null;
            inst.CoArrivalPending.Add(f);
            CoArriveMessage(j.Target, f, inst, map[f], delays[f], j.From.Point);
        }
        foreach (var o in oldObservers.Except(followers)) FallbackNew(o, now);
        if (!reuse) EndIfEmpty(g.From, now);
        Note("new", inst, ("members", map.ToDictionary(kv => kv.Value.ToString(), kv => (object?)kv.Key.Index)), ("mode", inst.Mode.ToString()),
            ("co_arrivals", coList.Count), ("notice", notice.ToString()));
    }

    private static byte[] FindCarry(HubMember m) => m.Joining?.Carry ?? [];

    private void CoArriveMessage(HubInstance from, HubMember m, HubInstance inst, int slot, int delay, byte point)
    {
        var none = new sbyte[Wire.MaxSlots];
        Array.Fill(none, (sbyte)-1);
        m.Cursor = 0;
        m.Peer?.Send(new BarrierOkMsg(from.Id, from.Visit, BarrierDecision.CoArrive, inst.Id, inst.Visit, inst.Stage, point, (byte)slot, none,
            inst.Mode, (byte)delay, [], 0, [], []).Frame());
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
    }

    private HubInstance NewInstance(InstanceMode mode, string stage, uint visit, ulong context, long now)
    {
        var id = _nextInstance;
        while (_instances.ContainsKey(id) || id == 0) id = id >= 999_999_999 ? 1 : id + 1;
        _nextInstance = id >= 999_999_999 ? 1 : id + 1;
        var i = new HubInstance(id) { Mode = mode, Stage = stage, Visit = visit, ContextHash = context, CreatedAt = now, LastLogAt = now };
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
        m.Cursor = 0;
        m.Heading = null;
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
                target.Mode, (byte)DelayController.FromRoundTrips(m.Rtt.All, _sealing.MaxDelayTicks), [], 0, [], []).Frame());
            Note("join_decided", target, ("member", m.Index), ("slot", slot));
            DeliverState(m, now);
        }
        EndIfEmpty(g.From, now);
    }

    /// <summary>Sends the joiner the snapshot and every tick since (STATE_DATA) once the snapshot is complete.</summary>
    private void DeliverState(HubMember m, long now)
    {
        if (m.Joining is not { } j || j.StateSent || !j.Target.Snapshot.Complete) return;
        j.StateSent = true;
        SendState(m, j.Target, StatePurpose.Join, j.Slot);
    }

    private void SendState(HubMember m, HubInstance i, StatePurpose purpose, int slot)
    {
        var end = i.StreamEnd;
        var blob = StateBlob.Build(purpose, (byte)slot, i.Snapshot.Bytes ?? [], i.Forks, 0, i.Records(0, end).ToList());
        for (var off = 0; off < blob.Length || off == 0; off += Wire.MaxChunkBytes)
        {
            var n = Math.Min(Wire.MaxChunkBytes, blob.Length - off);
            m.Peer?.Send(new StateDataMsg(i.Id, i.Visit, (uint)blob.Length, (uint)off, blob.AsSpan(off, n).ToArray()).Frame(), bulk: true);
            if (blob.Length == 0) break;
        }
        m.Cursor = end;
        Count("state_bytes", blob.Length);
        Note("state", i, ("member", m.Index), ("purpose", purpose.ToString()), ("bytes", blob.Length), ("ticks", end));
    }

    private void OnReady(HubMember m, ReadyMsg msg, long now)
    {
        if (m.Joining is not { } j || j.Target.Id != msg.Inst || j.Target.Visit != msg.Visit || j.Ready) return;
        var target = j.Target;
        if (msg.Rate > 0) m.Rate = msg.Rate;
        if (msg.Carry.Length > 0) j.Carry = msg.Carry;
        if (target.Fingerprints.TryGetValue(msg.Tick, out var fps) && fps.Count > 0)
        {
            var common = fps.Values.GroupBy(x => x).OrderByDescending(x => x.Count()).First().Key;
            if (common != msg.NetFp)
            {
                ++j.Attempts;
                Count("ready_mismatch");
                Note("ready_mismatch", target, ("member", m.Index), ("tick", msg.Tick), ("attempt", j.Attempts));
                if (j.Attempts >= 2)
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
        Note("ready", target, ("member", m.Index), ("tick", msg.Tick), ("rate", msg.Rate));
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
            target.Streams[j.Slot] = new SlotStream(j.Slot, at, d) { Delay = { Max = _sealing.MaxDelayTicks, LateTarget = _sealing.LateTarget } };
            j.Stamped = true;
            BecomeMember(target, m, j);
            Note("join", target, ("member", m.Index), ("slot", j.Slot), ("tick", at));
        }
        else if (target.Mode == InstanceMode.SoloLed && target.Barrier is null && target.Leader is { Connected: true } leader && target.SoloJoin is null)
        {
            var ld = DelayController.FromRoundTrips(leader.Rtt.All, _sealing.MaxDelayTicks);
            target.SoloJoin = new PendingSoloJoin(m, j.Slot, j.From.Point, j.Carry, d, ld, now);
            j.Stamped = true;
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
        if (m.PreferredSlot < 0) m.PreferredSlot = j.Slot;
    }

    private void OnJoinAt(HubMember m, JoinAtMsg msg, long now)
    {
        if (Mine(m, msg.Inst, msg.Visit) is not { Mode: InstanceMode.SoloLed } i || i.SoloJoin is not { } p || i.Leader != m) return;
        var current = CurrentTick(i, now);
        var at = Math.Max(msg.Tick, i.Log.Count);
        // S(J) where the leader reaches J, less its one-way time, so SEALED(J) arrives as it gets there.
        var reach = now + (long)((double)Math.Max(0, (long)at - current) * Wire.TickSeconds * Freq) - FromMs(m.Rtt.Median / 2);
        reach = Math.Max(reach, now + TickTs);
        i.S0 = reach - (long)(at * Wire.TickSeconds * Freq);
        i.NextSeal = at;
        i.SealedFrom = at;
        i.Mode = InstanceMode.Sealed;
        i.SoloJoin = null;
        i.Streams[m.Slot] = new SlotStream(m.Slot, at, p.LeaderDelay) { Delay = { Max = _sealing.MaxDelayTicks, LateTarget = _sealing.LateTarget } };
        i.Streams[p.Slot] = new SlotStream(p.Slot, at, p.JoinerDelay) { Delay = { Max = _sealing.MaxDelayTicks, LateTarget = _sealing.LateTarget } };
        i.AddEvent(at, TickEvent.Join(p.Slot, p.Point, p.Carry));
        i.AddEvent(at, TickEvent.Delay(p.Slot, p.JoinerDelay));
        i.AddEvent(at, TickEvent.Delay(m.Slot, p.LeaderDelay));
        m.Cursor = at;
        if (p.Joiner.Joining is { } j) BecomeMember(i, p.Joiner, j);
        i.Sealing = true;
        Note("join", i, ("member", p.Joiner.Index), ("slot", p.Slot), ("tick", at), ("solo_leader", m.Index));
        _sealer?.Invoke();
    }

    /// <summary>A joiner whose target went away (or whose READY failed twice) restores its own load snapshot and enters as NEW.</summary>
    private void FallbackNew(HubMember m, long now, BarrierNotice notice = BarrierNotice.None)
    {
        if (m.Joining is not { } j) return;
        j.Target.Observers.Remove(m);
        if (j.Target.Reserved[j.Slot] == m) j.Target.Reserved[j.Slot] = null;
        m.Joining = null;
        var inst = NewInstance(InstanceMode.SoloLed, j.From.Stage, 1, j.From.Context, now);
        Place(m, inst, 0, now);
        inst.Snapshot.Uploader = m;
        var map = new sbyte[Wire.MaxSlots];
        Array.Fill(map, (sbyte)-1);
        map[j.OldSlot] = 0;
        m.Peer?.Send(new BarrierOkMsg(j.Target.Id, j.Target.Visit, BarrierDecision.New, inst.Id, inst.Visit, inst.Stage, j.From.Point, 0, map,
            InstanceMode.SoloLed, 0, [], 0, [], [], notice == BarrierNotice.None ? BarrierNotice.HoldExpired : notice, []).Frame());
        Note("fallback_new", inst, ("member", m.Index), ("from", j.Target.Id));
    }

    // Fork -----------------------------------------------------------------------------------------------------------------

    private void OnSplit(HubMember m, SplitMsg msg, long now)
    {
        if (Mine(m, msg.Inst, msg.Visit) is not { Mode: InstanceMode.Sealed } i)
        {
            // The other side's member may already have moved the reporter into the child (fork processed): fine.
            return;
        }
        if (i.Splits.TryGetValue(msg.Tick, out var seen))
        {
            if (seen.Mask != msg.LeaverMask || seen.Kind != msg.Kind)
            {
                ++i.Desyncs;
                Count("split_mismatch");
                Note("split_mismatch", i, ("slot", m.Slot), ("tick", msg.Tick));
            }
            return;
        }
        var present = i.PresentMask;
        var leavers = (byte)(msg.LeaverMask & present);
        if (leavers == 0 || leavers == present)
        {
            Count("split_ignored");
            return;
        }
        i.Splits[msg.Tick] = (msg.LeaverMask, msg.Kind);
        Fork(i, msg.Tick, leavers, msg.Kind, now);
    }

    /// <summary>
    /// A fork (SEAMLESS.md 6.7): the leaver side moves to a child instance that shares the parent's snapshot and every
    /// record sealed so far; from the next tick each side has its own stream (same schedule, so nobody hitches).
    /// </summary>
    private void Fork(HubInstance i, uint f, byte leavers, SplitKind kind, long now)
    {
        var from = i.NextSeal; // the first tick of the separate streams
        FlushAhead(i);
        var child = NewInstance(InstanceMode.Sealed, i.Stage, i.Visit, i.ContextHash, now);
        child.Visit = i.Visit;
        child.Log = new TickLog(i.Log, i.Log.Count);
        foreach (var (t, rec) in i.Ahead) child.Ahead[t] = rec;
        child.Snapshot = i.Snapshot.ShareComplete();
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
            var stream = new SlotStream(s, from, old?.Delay.Current ?? 2) { Delay = { Max = _sealing.MaxDelayTicks, LateTarget = _sealing.LateTarget } };
            old?.MoveAhead(stream, from);
            if (old is not null) old.UntilTick = from;
            child.Streams[s] = stream;
            child.Slots[s] = m;
            i.Slots[s] = null;
            m.Instance = child;
            m.PreviousInstanceId = i.Id;
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
        foreach (var side in new[] { i, child })
        {
            if (side.Count == 1 && side.Sealing) ScheduleLeadFrom(side, from + 1);
            else if (side.Count == 1)
            {
                side.Mode = InstanceMode.SoloLed;
                Array.Clear(side.Streams);
            }
        }
        Note("fork", i, ("tick", f), ("leavers", leavers), ("kind", kind.ToString()), ("child", child.Id), ("from", from));
        _sealer?.Invoke();
    }

    // Resume ---------------------------------------------------------------------------------------------------------------

    private void OnResume(HubMember m, ResumeMsg msg, long now)
    {
        if (Mine(m, msg.Inst, msg.Visit) is not { } i || m.Slot != msg.Slot)
        {
            Count("resume_refused");
            return;
        }
        if (i.Mode == InstanceMode.Sealed)
        {
            m.Cursor = Math.Min(msg.LastTick + 1, i.StreamEnd);
            Pump(i, m);
        }
        Note("resume", i, ("member", m.Index), ("last", msg.LastTick));
    }

    // Streams to members -------------------------------------------------------------------------------------------------

    private void PumpAll(HubInstance i)
    {
        if (i.Mode == InstanceMode.Sealed)
            foreach (var m in i.Members) Pump(i, m);
        foreach (var o in i.Observers) Pump(i, o);
    }

    /// <summary>Sends the member every contiguous record from its cursor (SEALED to members, LIVE to observers).</summary>
    private void Pump(HubInstance i, HubMember m)
    {
        if (m.Peer is null) return;
        var observer = m.Joining is not null;
        if (observer && !m.Joining!.StateSent) return;
        var end = i.StreamEnd;
        if (i.SealStop is { } stop) end = Math.Min(end, stop + 1);
        while (m.Cursor < end)
        {
            var count = Math.Min(end - m.Cursor, 600u);
            var records = i.Records(m.Cursor, m.Cursor + count).ToList();
            if (records.Count == 0) break;
            m.Peer.Send(TicksMsg.Frame(observer ? Msg.Live : Msg.Sealed, i.Id, i.Visit, m.Cursor, records), bulk: observer);
            m.Cursor += (uint)records.Count;
        }
    }

    // Housekeeping ---------------------------------------------------------------------------------------------------------

    /// <summary>Pings, holds, lag and reconnect limits, the roster (the relay calls it a few times per second).</summary>
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
            }
        }
        foreach (var i in _instances.Values.ToList())
        {
            if (i.Ended) continue;
            // Barrier reports that do not come.
            if (i.Barrier is { } b && Ms(now - b.FirstAt) >= _inst.BarrierReportSeconds * 1000.0 && !i.Members.Any(x => x.Arrival is not null))
            {
                if (i.Members.Any(x => x.Connected && !b.Reports.ContainsKey(x.Slot)) && i.Mode == InstanceMode.Sealed) SplitBarrier(i, now);
                else if (b.Reports.Count > 0) Park(i, now);
            }
            // Lag and disconnects in sealed instances.
            if (i.Mode == InstanceMode.Sealed && i.Sealing)
            {
                foreach (var m in i.Members.ToList())
                {
                    var st = i.Streams[m.Slot];
                    if (st is null) continue;
                    var lagging = st.FillStreak >= SlotStream.LagFills;
                    if (lagging != m.Lagging)
                    {
                        m.Lagging = lagging;
                        foreach (var o in _members.Values)
                            o.Peer?.Send(new RoomMsg(lagging ? RoomNotice.Lagging : RoomNotice.LagOk, (byte)m.Index, (ulong)m.AccountId, m.Name, Flags(m)).Frame());
                    }
                    var gone = !m.Connected && Ms(now - m.DisconnectedAt) >= _relay.ReconnectSeconds * 1000.0;
                    var lagOut = lagging && Ms(now - st.FillStreakSince) >= _relay.LagLeaveSeconds * 1000.0;
                    if (gone || lagOut)
                    {
                        Note(gone ? "reconnect_timeout" : "lag_leave", i, ("member", m.Index));
                        StampLeave(i, m.Slot, now);
                    }
                }
            }
            else if (i.Mode == InstanceMode.SoloLed)
            {
                foreach (var m in i.Members.ToList())
                    if (!m.Connected && Ms(now - m.DisconnectedAt) >= _relay.ReconnectSeconds * 1000.0)
                    {
                        Note("reconnect_timeout", i, ("member", m.Index));
                        i.Slots[m.Slot] = null;
                        m.Instance = null;
                        m.Slot = -1;
                        EndIfEmpty(i, now);
                    }
                if (i.SoloJoin is { } sj && Ms(now - sj.AskedAt) > 5000)
                {
                    // The leader did not answer JOIN_REQ: ask again.
                    i.SoloJoin = null;
                    if (sj.Joiner.Joining is { } jj) jj.Stamped = false;
                    Count("join_req_timeout");
                }
            }
            // Joiners that are ready but could not be stamped yet (the target was starting, another JOIN_REQ ran).
            foreach (var o in i.Observers.ToList())
                if (o.Joining is { Ready: true, Stamped: false } oj && oj.Target == i)
                    StampJoin(i, o, now);
        }
        CheckHolds(now);
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

    // Views ----------------------------------------------------------------------------------------------------------------

    public IReadOnlyList<InstanceView> Views() => _instances.Values.Where(i => !i.Ended).OrderBy(i => i.Id).Select(i => new InstanceView(i.Id, i.Stage, i.Visit,
        i.Mode switch { InstanceMode.Hub => "hub", InstanceMode.SoloLed => "solo", _ => "sealed" },
        Enumerable.Range(0, Wire.MaxSlots).Where(s => i.Slots[s] is not null)
            .Select(s => new InstanceMemberView(i.Slots[s]!.AccountId, i.Slots[s]!.Name, s, i.Slots[s]!.Connected, i.Slots[s]!.Lagging)).ToList(),
        i.Departing, i.BossLock, i.Joinable, i.Mode == InstanceMode.Sealed ? i.NextSeal : i.Log.Count, i.Parent)).ToList();
}
