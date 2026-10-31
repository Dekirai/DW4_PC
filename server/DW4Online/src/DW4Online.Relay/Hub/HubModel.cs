using DW4Online.Domain.Story;
using DW4Online.Relay.Protocol;

namespace DW4Online.Relay.Hub;

/// <summary>The sending side of a connected member (a TCP connection, or a test double).</summary>
public interface IHubPeer
{
    /// <summary>Queues a frame. Bulk frames (STATE_DATA and the LIVE frames behind it) pass the state budget.</summary>
    void Send(byte[] frame, bool bulk = false);
    /// <summary>Sends CLOSE and closes the connection.</summary>
    void Close(CloseReason reason, string text);
}

/// <summary>What the room service tells the relay about a room when a member connects (the story: when the room has an owner account).</summary>
public sealed record RoomInfo(string Code, int Protocol, ulong DataHash, ulong SettingsHash, int MaxPlayers, RoomStoryInit? Story = null);

/// <summary>
/// The room story's start (SEAMLESS.md 8): the owner's account and online story as stored, the room's difficulty (the
/// story is rebased on it), the owner-away policy and whether the owner is in the room.
/// </summary>
public sealed record RoomStoryInit(long OwnerAccountId, RoomStoryData Story, int Difficulty, StoryPolicy Policy, bool OwnerPresent);

/// <summary>A member as the room's state lists it.</summary>
public sealed record InstanceMemberView(long AccountId, string Username, int Slot, bool Connected, bool Lagging);

/// <summary>A lockstep group as the room state, the website and the tests see it.</summary>
public sealed record InstanceView(uint Id, string Stage, uint Visit, string Mode, IReadOnlyList<InstanceMemberView> Members, bool Departing,
    bool BossLock, bool Joinable, uint Ticks, uint? Parent);

/// <summary>A room member on the relay (one connection at a time).</summary>
public sealed class HubMember(int index, long accountId, string name, bool owner)
{
    public int Index { get; } = index;
    public long AccountId { get; } = accountId;
    public string Name { get; } = name;
    public bool Owner { get; set; } = owner;
    public IHubPeer? Peer { get; set; }
    public long DisconnectedAt { get; set; }
    public bool Connected => Peer is not null;

    /// <summary>The instance the member plays in (or is parked at the barrier of).</summary>
    public HubInstance? Instance { get; set; }
    public int Slot { get; set; } = -1;
    /// <summary>The id the member's instance had before a fork moved it (its frames in flight still name it).</summary>
    public uint PreviousInstanceId { get; set; }
    /// <summary>The slot of its first instance in the room, kept when free (SEAMLESS.md 3.6).</summary>
    public int PreferredSlot { get; set; } = -1;
    /// <summary>When the member entered its current instance (the longer member wins a two-member vote).</summary>
    public long InstanceSince { get; set; }

    /// <summary>The next tick of its instance's stream this member has not been sent yet.</summary>
    public uint Cursor { get; set; }
    /// <summary>Set while the member catches up into an instance (observer, LIVE frames).</summary>
    public JoinState? Joining { get; set; }
    /// <summary>Set while the member is part of a parked arrival group that waits for a decision.</summary>
    public ArrivalGroup? Arrival { get; set; }

    public uint Rate { get; set; }
    public Heading? Heading { get; set; }
    public PresenceMsg? Presence { get; set; }
    public long PresenceAt { get; set; }
    public bool Lagging { get; set; }

    public uint PingNonce { get; set; }
    public long PingSentAt { get; set; }
    public long LastHeard { get; set; }
    public RttSamples Rtt { get; } = new();

    /// <summary>This connection may send RESUME once (its WELCOME said resumable).</summary>
    public bool ResumeAllowed { get; set; }
    /// <summary>A fingerprint vote sent RESYNC: the member catches up from the state until its READY (no JOIN follows).</summary>
    public bool Resyncing { get; set; }
    /// <summary>When the last RESYNC went out (at most one per Instances:ResyncCooldownSeconds).</summary>
    public long ResyncAt { get; set; }
    /// <summary>A state transfer for this member is being built outside the room lock: nothing is pumped to it meanwhile.</summary>
    public StateJob? PendingState { get; set; }

    /// <summary>STORY tokens (Instances:StoryBurst, refilled at StoryMessagesPerMinute) and when they were refilled.</summary>
    public double StoryTokens { get; set; } = -1;
    public long StoryRefilledAt { get; set; }
    /// <summary>The engine lines (instance, visit) the server put this member in (every visit it was placed in, a fork's
    /// child): besides its own instance 0 lines the only counter lines it may report, the last <see cref="StoryLineLimit"/>
    /// of them (SEAMLESS.md 8.7).</summary>
    public HashSet<(uint Inst, uint Visit)> StoryLines { get; } = [];
    public Queue<(uint Inst, uint Visit)> StoryLineOrder { get; } = new();
    public const int StoryLineLimit = 16;
    /// <summary>The visits of instance 0 (a group that went on alone) the member reported lines of, at most
    /// <see cref="LoneLineLimit"/> in a room session.</summary>
    public HashSet<uint> LoneLineVisits { get; } = [];
    public const int LoneLineLimit = 8;
    /// <summary>The story epoch of the member's last instance visit (its engine was cleaned at that barrier).</summary>
    public long VisitEpoch { get; set; }
    /// <summary>The stage the member plays alone in while the server has no instance with a stage for it (taken out of its
    /// group, its connection lost in a lobby or at a barrier, a reconnect without resuming), and since when: its STORY counts
    /// against that stage (SEAMLESS.md 8.7).</summary>
    public string? LoneStage { get; set; }
    public long LoneSince { get; set; }
    /// <summary>When the member's last BARRIER that arrived alone was decided (at most one per Instances:LoneBarrierMs).</summary>
    public long LoneBarrierAt { get; set; }
    /// <summary>The member's state budget (Relay:StateBytesPerMinute): bytes left and when it was last refilled.</summary>
    public double StateTokens { get; set; } = double.NaN;
    public long StateStamp { get; set; }
    /// <summary>When the member's resyncs went out (the last Instances:ResyncWindowMinutes; 6.9).</summary>
    public Queue<long> ResyncTimes { get; } = new();
    /// <summary>How often a resync's replay of the log disagreed with this member's own fingerprint: its engine was the one
    /// off, so it loses the next two-member vote (the longer member no longer wins by default).</summary>
    public int ReplayMismatches { get; set; }
    /// <summary>The join that made the member one of its instance, until the instance's log reaches its JOIN (a gap before J
    /// that never closes sends it back to its own stage as NEW).</summary>
    public JoinState? JoinedVia { get; set; }
}

public sealed record Heading(string Stage, byte Point, ulong Context, long At);

/// <summary>The last 120 round trips of a member (PING / PONG).</summary>
public sealed class RttSamples
{
    private readonly Queue<double> _q = new();

    public void Add(double ms)
    {
        _q.Enqueue(ms);
        while (_q.Count > 120) _q.Dequeue();
    }

    public IReadOnlyCollection<double> All => _q;
    public double Median => _q.Count == 0 ? 0 : _q.OrderBy(x => x).ElementAt(_q.Count / 2);
    public double Max => _q.Count == 0 ? 0 : _q.Max();
}

/// <summary>A joiner catching up (SEAMLESS.md 6.6): its target, reserved slot, entrance and fallback.</summary>
public sealed class JoinState(HubInstance target, int slot, ArrivalGroup from, int oldSlot)
{
    public HubInstance Target { get; } = target;
    public int Slot { get; } = slot;
    public ArrivalGroup From { get; } = from;
    /// <summary>The joiner's slot in its own (fallback) engine.</summary>
    public int OldSlot { get; } = oldSlot;
    public byte[] Carry { get; set; } = [];
    public int Attempts { get; set; }
    /// <summary>JOIN_REQ sent to the solo leader without its JOIN_AT (a few, then the joiner falls back to NEW).</summary>
    public int JoinRequests { get; set; }
    public bool StateSent { get; set; }
    /// <summary>READY arrived (the joiner caught up).</summary>
    public bool Ready { get; set; }
    /// <summary>The JOIN is stamped (sealed) or JOIN_REQ went to the solo leader.</summary>
    public bool Stamped { get; set; }
}

public enum HoldKind
{
    None,
    CoArrival,
    Join,
    Boss,
}

/// <summary>
/// The members of one instance that parked at a barrier together (or a single member), waiting for the decision of
/// SEAMLESS.md 3.4. Co-arrivals that join a holding base are listed with it.
/// </summary>
public sealed class ArrivalGroup
{
    public required HubInstance From { get; init; }
    public required uint BarrierTick { get; init; }
    public required string Stage { get; init; }
    public required byte Point { get; init; }
    public required ulong Context { get; init; }
    public required long ParkedAt { get; init; }
    /// <summary>The arriving members with their slot in the arriving engine and their carry.</summary>
    public List<Arrival> Members { get; } = [];
    public List<ArrivalGroup> CoArrivals { get; } = [];
    public HoldKind Hold { get; set; }
    public long HoldUntil { get; set; }
    public HubInstance? Target { get; set; }
    /// <summary>Members announced (HEADING) that the base waits for.</summary>
    public List<HubMember> Awaited { get; } = [];
    public byte? Choice { get; set; }
    public long LastWaitSent { get; set; }
    public int Size => Members.Count + CoArrivals.Sum(c => c.Members.Count);
}

public sealed record Arrival(HubMember Member, int Slot, byte[] Carry);

/// <summary>The barrier reports of an instance's members (the first one stops the sealing).</summary>
public sealed class BarrierCollect(uint tick, string stage, byte point, byte alive, byte present, ulong context, long firstAt)
{
    public uint Tick { get; } = tick;
    public string Stage { get; } = stage;
    public byte Point { get; } = point;
    public byte Alive { get; } = alive;
    public byte Present { get; } = present;
    public ulong Context { get; } = context;
    public long FirstAt { get; } = firstAt;
    public Dictionary<int, (HubMember Member, byte[] Carry)> Reports { get; } = [];
    /// <summary>Each report's BARRIER (a fork while the barrier waits splits the reports by side).</summary>
    public Dictionary<int, BarrierMsg> Messages { get; } = [];
    /// <summary>The reporting slots in arrival order.</summary>
    public List<int> Order { get; } = [];
    public int Mismatches { get; set; }

    public void Add(int slot, HubMember member, BarrierMsg msg)
    {
        Reports[slot] = (member, msg.Carry);
        Messages[slot] = msg;
        Order.Remove(slot);
        Order.Add(slot);
    }

    public void Remove(int slot)
    {
        Reports.Remove(slot);
        Messages.Remove(slot);
        Order.Remove(slot);
    }

    /// <summary>The reports of <paramref name="slots"/> as a barrier of their own (its fields from the first of them), or null.</summary>
    public BarrierCollect? Part(byte slots)
    {
        var mine = Order.Where(s => (slots & (1 << s)) != 0 && Messages.ContainsKey(s)).ToList();
        if (mine.Count == 0) return null;
        var first = Messages[mine[0]];
        var b = new BarrierCollect(first.Tick, first.Stage, first.Point, first.AliveMask, first.PresentMask, first.ContextHash, FirstAt);
        foreach (var s in mine) b.Add(s, Reports[s].Member, Messages[s]);
        return b;
    }
}

/// <summary>
/// The visit's load snapshot: the leader's raw bytes and every member's canonical hash. A fork's child shares the bytes
/// with its parent, also while they still upload: whichever side the uploader went to completes them for both.
/// </summary>
public sealed class SnapshotStore
{
    private sealed class Upload
    {
        public byte[]? Buffer;
        public uint Total;
        public int Received;
        public byte[]? Bytes;
        public HubMember? Uploader;
    }

    private readonly Upload _u;

    public SnapshotStore() : this(new Upload())
    {
    }

    private SnapshotStore(Upload upload) => _u = upload;

    public HubMember? Uploader { get => _u.Uploader; set => _u.Uploader = value; }
    public byte[]? Bytes => _u.Bytes;
    public bool Complete => _u.Bytes is not null;
    public Dictionary<int, ulong> Hashes { get; } = [];
    public bool HashMismatch { get; set; }

    /// <summary>Whether two stores hold the same upload (a parent and its fork children).</summary>
    public bool SharesWith(SnapshotStore other) => ReferenceEquals(_u, other._u);

    /// <summary>
    /// Adds a chunk; true when the snapshot just completed. A visit has one upload: its first chunk fixes the total, and
    /// the buffer grows with the chunks that came (a new total per 17-byte frame made the relay allocate 4 MiB each).
    /// </summary>
    public bool AddChunk(uint total, uint offset, byte[] chunk)
    {
        if (Complete) return false;
        if (total > StateBlob.MaxSnapshot) throw new ProtocolException("snapshot too large");
        if (_u.Buffer is null)
        {
            if (offset != 0) throw new ProtocolException("snapshot does not start at offset 0");
            _u.Total = total;
            _u.Buffer = new byte[Math.Min(total, (uint)Math.Max(chunk.Length, 1))];
            _u.Received = 0;
        }
        else if (total != _u.Total) throw new ProtocolException("the snapshot's total changed during its upload");
        if (offset != _u.Received) throw new ProtocolException("snapshot chunks out of order");
        if (offset + (uint)chunk.Length > total) throw new ProtocolException("snapshot chunk beyond its total");
        var end = (int)offset + chunk.Length;
        if (end > _u.Buffer.Length)
        {
            var grown = new byte[Math.Min(total, (uint)Math.Max(end, _u.Buffer.Length * 2))];
            _u.Buffer.AsSpan(0, _u.Received).CopyTo(grown);
            _u.Buffer = grown;
        }
        chunk.CopyTo(_u.Buffer, (int)offset);
        _u.Received += chunk.Length;
        if (_u.Received < total) return false;
        _u.Bytes = _u.Buffer;
        _u.Buffer = null;
        return true;
    }

    /// <summary>The snapshot shared with a fork's child (it starts from the same visit; an upload in flight completes both).</summary>
    public SnapshotStore Share()
    {
        var s = new SnapshotStore(_u);
        foreach (var (k, v) in Hashes) s.Hashes[k] = v;
        return s;
    }
}

/// <summary>A fork reported by members (SPLIT): it forks once every connected member confirmed it, or after a short wait
/// once members of both sides did (a single member's report never moves another member, 6.7).</summary>
public sealed class SplitVote(uint tick, byte mask, SplitKind kind, long firstAt)
{
    public uint Tick { get; } = tick;
    public byte Mask { get; } = mask;
    public SplitKind Kind { get; } = kind;
    public long FirstAt { get; } = firstAt;
    public HashSet<int> Reporters { get; } = [];
    public bool Done { get; set; }
}

/// <summary>An instance: a lockstep group of 1-4 members in one stage (SEAMLESS.md 3.2).</summary>
public sealed class HubInstance(uint id)
{
    public uint Id { get; } = id;
    public string Stage { get; set; } = "";
    public uint Visit { get; set; }
    public ulong ContextHash { get; set; }
    public InstanceMode Mode { get; set; }
    public bool Ended { get; set; }
    public HubMember?[] Slots { get; } = new HubMember?[Wire.MaxSlots];
    /// <summary>Slots promised to joiners that are still catching up.</summary>
    public HubMember?[] Reserved { get; } = new HubMember?[Wire.MaxSlots];
    public SlotStream?[] Streams { get; } = new SlotStream?[Wire.MaxSlots];
    public uint? Parent { get; set; }
    public uint ForkTick { get; set; }
    public bool Departing { get; set; }
    public bool BossLock { get; set; }
    public bool Joinable { get; set; }
    public bool MembershipSafe { get; set; }
    public StateReason Reason { get; set; } = StateReason.Loading;
    /// <summary>The room story's drop epoch when this visit began (a fork's child keeps its parent's: one engine line).</summary>
    public long StoryEpoch { get; set; }

    public TickLog Log { get; set; } = new();
    /// <summary>Sealed records beyond the log's end, waiting for the solo leader's LOG to reach them (1 to 2 members).</summary>
    public SortedDictionary<uint, byte[]> Ahead { get; } = [];
    public SnapshotStore Snapshot { get; set; } = new();
    public List<ForkSide> Forks { get; set; } = [];
    public SortedDictionary<uint, List<TickEvent>> PendingEvents { get; } = [];
    public List<HubMember> Observers { get; } = [];
    /// <summary>Co-arrivals of a fresh visit that wait for the base's snapshot (STATE_DATA purpose co-arrival).</summary>
    public HashSet<HubMember> CoArrivalPending { get; } = [];
    public SortedDictionary<uint, Dictionary<int, ulong>> Fingerprints { get; } = [];
    public BarrierCollect? Barrier { get; set; }
    public SortedDictionary<uint, SplitVote> Splits { get; } = [];
    /// <summary>Ticks whose fingerprints were voted on already (one vote per tick).</summary>
    public HashSet<uint> Voted { get; } = [];
    /// <summary>Net fingerprints of a resync's replay of the log (its READY): what every member's engine must have had.</summary>
    public SortedDictionary<uint, ulong> ReplayFp { get; } = [];
    /// <summary>Members the server took out (LEAVE) that still play: they are sent the ticks up to their LEAVE's tick, so their
    /// own client sees the LEAVE and continues alone (6.9, 6.10).</summary>
    public Dictionary<HubMember, uint> Departed { get; } = [];

    // Sealed schedule
    public long S0 { get; set; }
    public bool Sealing { get; set; }
    public uint NextSeal { get; set; }
    /// <summary>The tick of a pending LEAD_FROM: after sealing the tick before it the instance turns solo-led.</summary>
    public uint? LeadFrom { get; set; }
    /// <summary>A barrier stopped the sealing after this tick.</summary>
    public uint? SealStop { get; set; }

    // Solo-led
    /// <summary>The tick from which a solo-led instance became sealed (its leader's LOG still covers the ticks before it).</summary>
    public uint SealedFrom { get; set; }
    /// <summary>The solo leader whose LOG covers the ticks before <see cref="SealedFrom"/> (JOIN_AT's sender).</summary>
    public HubMember? LogOwner { get; set; }
    /// <summary>The solo log reached Instances:MaxVisitHours: further LOG is dropped and nobody joins.</summary>
    public bool LogFull { get; set; }
    public long LastLogAt { get; set; }
    public PendingSoloJoin? SoloJoin { get; set; }
    /// <summary>The last JOIN_REQ sent to the solo leader: its JOIN_AT is answered even when the joiner went meanwhile (the
    /// leader's client turned to delayed sampling and waits for SEALED(J)).</summary>
    public PendingSoloJoin? AskedJoin { get; set; }
    /// <summary>LEAD_FROM was sealed while the log still waits for the leader's LOG before it: the instance turns solo-led
    /// once the stream is contiguous up to it.</summary>
    public bool SoloPending { get; set; }
    /// <summary>When the visit's BARRIER_OK went out (a solo LOG cannot run ahead of the clock from here).</summary>
    public long VisitStartedAt { get; set; }

    public long CreatedAt { get; set; }
    public long Desyncs { get; set; }

    public IEnumerable<HubMember> Members => Slots.Where(m => m is not null)!;
    /// <summary>The filled slots with their members (by index: never trusts a member's own slot number).</summary>
    public IEnumerable<(int Slot, HubMember Member)> Seats
    {
        get
        {
            for (var s = 0; s < Wire.MaxSlots; ++s)
                if (Slots[s] is { } m) yield return (s, m);
        }
    }
    public int Count => Slots.Count(m => m is not null);
    public int FreeSlots => Enumerable.Range(0, Wire.MaxSlots).Count(s => Slots[s] is null && Reserved[s] is null);
    public byte PresentMask => (byte)Enumerable.Range(0, Wire.MaxSlots).Where(s => Slots[s] is not null).Sum(s => 1 << s);
    public HubMember? Leader => Slots.FirstOrDefault(m => m is not null);
    public bool Logged => Mode is InstanceMode.SoloLed or InstanceMode.Sealed;

    /// <summary>The end of the contiguous stream (log plus contiguous sealed records ahead of it).</summary>
    public uint StreamEnd
    {
        get
        {
            var end = Log.Count;
            while (Ahead.ContainsKey(end)) ++end;
            return end;
        }
    }

    public IEnumerable<byte[]> Records(uint from, uint to)
    {
        var logEnd = Log.Count;
        foreach (var r in Log.Range(from, Math.Min(to, logEnd))) yield return r;
        for (var t = Math.Max(from, logEnd); t < to && Ahead.TryGetValue(t, out var rec); ++t) yield return rec;
    }

    public void AddEvent(uint tick, TickEvent e)
    {
        if (!PendingEvents.TryGetValue(tick, out var list)) PendingEvents[tick] = list = [];
        list.Add(e);
    }
}

/// <summary>A JOIN into a solo-led instance waiting for the leader's JOIN_AT.</summary>
public sealed record PendingSoloJoin(HubMember Joiner, int Slot, byte Point, byte[] Carry, int JoinerDelay, int LeaderDelay, long AskedAt);

/// <summary>Pure rules of the instance model, unit-tested on their own.</summary>
public static class HubRules
{
    /// <summary>
    /// The slot map of a NEW (SEAMLESS.md 3.6): each member keeps its preferred slot when free, else its current slot when
    /// free, else the lowest free one; then, when slot 0 is free, the member in the lowest slot moves to slot 0.
    /// Input: (key, current slot, preferred slot) in priority order; output: key -> new slot.
    /// </summary>
    public static Dictionary<T, int> SlotMap<T>(IReadOnlyList<(T Key, int Current, int Preferred)> members) where T : notnull
    {
        if (members.Count > Wire.MaxSlots) throw new ArgumentException("at most four members", nameof(members));
        var taken = new T?[Wire.MaxSlots];
        var used = new bool[Wire.MaxSlots];
        var result = new Dictionary<T, int>();
        var rest = new List<(T Key, int Current)>();
        foreach (var (key, current, preferred) in members)
        {
            if (preferred is >= 0 and < Wire.MaxSlots && !used[preferred])
            {
                used[preferred] = true;
                result[key] = preferred;
            }
            else rest.Add((key, current));
        }
        var lowest = new List<T>();
        foreach (var (key, current) in rest)
        {
            if (current is >= 0 and < Wire.MaxSlots && !used[current])
            {
                used[current] = true;
                result[key] = current;
            }
            else lowest.Add(key);
        }
        foreach (var key in lowest)
        {
            var s = Array.IndexOf(used, false);
            used[s] = true;
            result[key] = s;
        }
        if (!used[0] && result.Count > 0)
        {
            var low = result.OrderBy(kv => kv.Value).First();
            result[low.Key] = 0;
        }
        _ = taken;
        return result;
    }

    /// <summary>The slot a joiner gets: its preferred one when free, else the lowest free one, or -1.</summary>
    public static int JoinSlot(bool[] free, int preferred)
    {
        if (preferred is >= 0 and < Wire.MaxSlots && free[preferred]) return preferred;
        return Array.IndexOf(free, true);
    }

    /// <summary>
    /// The expected catch-up in seconds: remaining ticks / (rate - 59.94), because the instance keeps running while the
    /// joiner replays (SEAMLESS.md 6.6). Infinite when the rate does not exceed the game's pace.
    /// </summary>
    public static double CatchUpSeconds(uint remainingTicks, double rate)
    {
        var pace = 1.0 / Wire.TickSeconds;
        return rate <= pace ? double.PositiveInfinity : remainingTicks / (rate - pace);
    }
}
