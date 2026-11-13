using DW4Online.Domain.Story;
using DW4Online.Relay.Protocol;

namespace DW4Online.Relay.Hub;

/// <summary>The sending side of a connected member (a TCP connection, or a test double).</summary>
public interface IHubPeer
{
    /// <summary>Queues a frame. Bulk frames (STATE_DATA and the LIVE frames behind it) pass the state budget.</summary>
    void Send(byte[] frame, bool bulk = false);
    /// <summary>Queues a frame that may wait a few milliseconds to go out with the peer's next ones (presence, M4 (e)).</summary>
    void SendSoon(byte[] frame) => Send(frame);
    /// <summary>Sends CLOSE and closes the connection.</summary>
    void Close(CloseReason reason, string text);
}

/// <summary>The hub's identity: the world (one hub for the whole server, SEAMLESS.md 19.17.4; its code "WORLD") and its protocol.</summary>
public sealed record RoomInfo(string Code, int Protocol, ulong DataHash = 0, ulong SettingsHash = 0, int MaxPlayers = 4)
{
    public static readonly RoomInfo World = new(DW4Online.Relay.Protocol.Wire.WorldChannel, DW4Online.Relay.Protocol.Wire.WorldProtocol);
}

/// <summary>A member as the room's state lists it.</summary>
public sealed record InstanceMemberView(long AccountId, string Username, int Slot, bool Connected, bool Lagging);

/// <summary>A lockstep group as the room state, the website and the tests see it.</summary>
public sealed record InstanceView(uint Id, string Stage, uint Visit, string Mode, IReadOnlyList<InstanceMemberView> Members, bool Departing,
    bool BossLock, bool Joinable, uint Ticks, uint? Parent);

/// <summary>A room member on the relay (one connection at a time).</summary>
public sealed class HubMember(uint id, long accountId, string name)
{
    /// <summary>The member id (SEAMLESS.md 19.17.3): a u32 given at the account's first admission in a server run, kept while the
    /// account is online or holds a seat, never reused in the run. The only member identity on the wire.</summary>
    public uint Id { get; } = id;
    /// <summary>The member id as an int (the story keepers' per-member tallies and the membership log).</summary>
    public int Index => (int)Id;
    public long AccountId { get; } = accountId;
    public string Name { get; set; } = name;
    public IHubPeer? Peer { get; set; }

    // The world (round I1) ------------------------------------------------------------------------------------------------
    /// <summary>The ticket role is staff (moderator or admin), re-checked live (19.14).</summary>
    public bool Staff { get; set; }
    /// <summary>The bound box (1 until round D).</summary>
    public int Box { get; set; } = 1;
    /// <summary>The client's game data hash (the handshake's): a matching filter (19.17.3).</summary>
    public ulong DataHash { get; set; }
    /// <summary>The member's OWN settings, from its newest BARRIER (protocol 49): a NEW from home makes them the instance's.</summary>
    public DW4Online.Domain.Rooms.RoomSettings Settings { get; set; } = DW4Online.Domain.Rooms.RoomSettings.Default;
    /// <summary>online_prefs.default_visibility (Q9: the last choice remembered).</summary>
    public Visibility DefaultVisibility { get; set; }
    /// <summary>The social cache (loaded outside the lock, replaced as a whole on a change: the selection reads it off the lock).</summary>
    public IReadOnlySet<long> Friends { get; set; } = new HashSet<long>();
    public IReadOnlySet<long> Blocks { get; set; } = new HashSet<long>();
    public IReadOnlySet<long> BlockedBy { get; set; } = new HashSet<long>();
    /// <summary>The admission's handle for the live re-check (the relay calls the admission again with it).</summary>
    public object? Admission { get; set; }
    /// <summary>The live re-checks started for this member (each one gets the next number) and the newest one applied: a result that
    /// comes after a newer one's is dropped (19.14).</summary>
    public long RecheckSeq { get; set; }
    public long RecheckApplied { get; set; }
    /// <summary>INSTANCE_OP's token bucket (<see cref="RoomHub.InstanceOpBurst"/>, refilled at one a second) and when it was refilled.</summary>
    public double InstanceOpTokens { get; set; } = double.NaN;
    public long InstanceOpTokensAt { get; set; }
    /// <summary>REPORT's token bucket (<see cref="RoomHub.ReportBurst"/>, refilled at <see cref="RoomHub.ReportsPerMinute"/>; round I1b).</summary>
    public double ReportTokens { get; set; } = double.NaN;
    public long ReportTokensAt { get; set; }
    /// <summary>A HOME the relay owes this member (its *Leave instance* or its removal, 19.6.4 / 19.6.6), asked again in its next stage
    /// group of that instance when a barrier overtook it; gone at the member's DISSOLVE or the instance's end.</summary>
    public HomeWish? HomeWish { get; set; }
    /// <summary>The instance the member has a seat in (null: home). Its stage group is <see cref="Instance"/> (null while parked,
    /// joining or continuing alone).</summary>
    public DW4Online.Relay.World.WorldInstance? World { get; set; }
    /// <summary>The member asked to give up (INTENT 2) in this visit: a barrier with alive mask 0 of its copy is on purpose (M3).</summary>
    public bool GaveUp { get; set; }
    /// <summary>The last INPUT or PONG (a host silent this long while his slot fills is in his grace, 19.6.7).</summary>
    public long LastInputOrPong { get; set; }
    /// <summary>The members this receiver was told about by MEMBER_INFO, with the flags it was told.</summary>
    public Dictionary<uint, byte> Known { get; } = [];
    /// <summary>The account and name of every member this receiver was told about by MEMBER_INFO since its connection (cleared with
    /// <see cref="Known"/>): REPORT resolves only these (round I1b review: a member id the client never learnt is "not available",
    /// whatever that member does now, so REPORT tells nobody's name or presence).</summary>
    public Dictionary<uint, (long Account, string Name)> Introduced { get; } = [];
    /// <summary>The ghost figures this receiver draws (the presence selection, 19.13).</summary>
    public DW4Online.Relay.World.PresenceChoice Choice { get; set; } = DW4Online.Relay.World.PresenceChoice.Empty;
    /// <summary>The receivers that draw this sender, and whether as one of their nearest.</summary>
    public Dictionary<HubMember, bool> ChosenBy { get; } = [];
    /// <summary>PRESENCE2 frames sent by this member (the farther receivers get every second one).</summary>
    public long PresenceSeq { get; set; }
    /// <summary>The receiver's presence budget (World:PresenceBytesPerSecond): bytes left and when refilled.</summary>
    public double PresenceBudget { get; set; } = double.NaN;
    public long PresenceBudgetAt { get; set; }
    /// <summary>When the member's connection went (0: connected); it leaves the world after World:MemberTimeoutSeconds.</summary>
    public bool LeftWorld { get; set; }
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
    /// <summary>Round I1b (19.7.4): the gate form's plan of this member (PLAN, its OPTIONS and CHOICE) and PLAN's throttle.</summary>
    public DW4Online.Relay.World.PromptPlan Prompt { get; } = new();

    public uint Rate { get; set; }
    public Heading? Heading { get; set; }
    public PresenceMsg? Presence { get; set; }
    public long PresenceAt { get; set; }
    /// <summary>The member sends PRESENCE2 (round 60): it is sent PRESENCE2 too. Its last one (null: PRESENCE).</summary>
    public bool PresenceV2 { get; set; }
    public Presence2Msg? Presence2 { get; set; }
    /// <summary>PRESENCE2's token bucket (<see cref="RoomHub.PresenceRate"/> a second, <see cref="RoomHub.PresenceBurst"/>).</summary>
    public double PresenceTokens { get; set; }
    public long PresenceTokensAt { get; set; }
    public bool Lagging { get; set; }

    public uint PingNonce { get; set; }
    public long PingSentAt { get; set; }
    /// <summary>When the member's next PING is due (one a second, spread over the housekeeping passes).</summary>
    public long NextPingAt { get; set; }
    public long LastHeard { get; set; }
    public RttSamples Rtt { get; } = new();
    /// <summary>The delay rule of the member's newest sealed stream: the next one keeps its bar (a value that proved too low
    /// for this member's link and game, SEAMLESS.md 6.4), since the link does not change with the stage.</summary>
    public DelayController? LastDelay { get; set; }

    /// <summary>This connection may send RESUME once (its WELCOME said resumable).</summary>
    public bool ResumeAllowed { get; set; }
    /// <summary>A fingerprint vote sent RESYNC: the member catches up from the state until its READY (no JOIN follows).</summary>
    public bool Resyncing { get; set; }
    /// <summary>Round J: the running resync's state came from a checkpoint.</summary>
    public bool ResyncFromCheckpoint { get; set; }
    /// <summary>Round J's review: the member's engine was rebuilt from a checkpoint (its READY said so) and has not had a
    /// classic state or a new visit since. It is not independent of its donor: it never makes a majority in a fingerprint
    /// vote, and a desync or STATEHASH mismatch it is part of makes the instance's checkpoint path suspect (6.13.4).</summary>
    public bool RebuiltFromCheckpoint { get; set; }
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
    /// <summary>The stage the member plays alone in while the server has no instance with a stage for it (taken out of its
    /// group, its connection lost in a lobby or at a barrier, a reconnect without resuming), and since when: its STORY counts
    /// against that stage (SEAMLESS.md 8.7).</summary>
    public string? LoneStage { get; set; }
    public long LoneSince { get; set; }
    /// <summary>The visit key the member's lone play counts under (the instance visit it went on from alone; 0: from
    /// <see cref="LoneSince"/>), so a reconnect without resuming is no new visit for the per-visit rules.</summary>
    public ulong LoneKey { get; set; }
    /// <summary>When the housekeeping last saw the member in an instance of n players (index n, 1..4): the achievements'
    /// players claim is capped by it (<see cref="RoomHub.RecentInstancePlayers"/>).</summary>
    public long[] SeenInSize { get; } = new long[Wire.MaxSlots + 1];
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
    /// <summary>Round I1b (19.30.7): the member's go-along request (waiting or stamped), one at a time.</summary>
    public DW4Online.Relay.World.GoRequest? Go { get; set; }
    /// <summary>Round I1b (19.30.5): the members the exit port asked on the mat for the member's newest HEADING (no notice for them).</summary>
    public List<HubMember> HeadingAsked { get; set; } = [];
    /// <summary>The departure (stage, point) <see cref="HeadingAsked"/> was given for, until the member's next barrier: a HEADING of the
    /// same departure again keeps them (the engine announces a copy's NEXTSTAGE once more when the port's fork left the member alone in it,
    /// with no asked slots).</summary>
    public (string Stage, byte Point)? HeadingAskedFor { get; set; }
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
    /// <summary>Round J: the checkpoint the join waits for (the relay asked the donor) until this time (0: not waiting).</summary>
    public long CheckpointWaitUntil { get; set; }
    /// <summary>Round J: the joiner's state came from a checkpoint; the classic path is the fallback.</summary>
    public bool FromCheckpoint { get; set; }
    /// <summary>The checkpoint the joiner's state came from (protocol 48: its READY is checked at its check tick).</summary>
    public StoredCheckpoint? Checkpoint { get; set; }
    /// <summary>Round J: the next state is the classic one (a checkpoint failed).</summary>
    public bool Classic { get; set; }
    /// <summary>READY arrived (the joiner caught up).</summary>
    public bool Ready { get; set; }
    /// <summary>When the state went out (0: not yet): a joiner without READY falls back to NEW after the catch-up limit plus
    /// half a minute (round J's review: a READY mismatch's classic state left a client that never sent READY again).</summary>
    public long StateSentAt { get; set; }
    /// <summary>Round J's review: the ranking took the checkpoint path's short catch-up; a fallback to the classic state
    /// checks the whole visit's catch-up time again.</summary>
    public bool PlannedCheckpoint { get; set; }
    /// <summary>A READY from a checkpoint whose tick the members have not reported yet: checked when their NETFP comes (the
    /// donor's of the check tick in a protocol 48 room; round J's review: READY of T - 1 met no fingerprint and went
    /// unchecked); also any READY at or after a fork the relay has not confirmed yet (checked once it is settled, TryFork).
    /// </summary>
    public ReadyMsg? PendingReady { get; set; }
    public long PendingReadyAt { get; set; }
    /// <summary>The members' fingerprint of the pending READY's tick never came in time: it is taken unchecked.</summary>
    public bool ReadyWaitExpired { get; set; }
    /// <summary>The JOIN is a fresh arrival's automatic join (19.7.1): its BARRIER_OK replaced the engine's story with the host's.</summary>
    public bool Fresh { get; init; }
    /// <summary>The story owner key (member id, box) the JOIN's BARRIER_OK named: the target instance's host, whose story the joiner's
    /// engine holds from then on (its fallback load included) until a replace on its way home (fix-019).</summary>
    public (uint Member, byte Box) Owner { get; init; }
    /// <summary>The instance whose story and settings that BARRIER_OK gave the joiner's engine (also after the instance is gone: a
    /// fallback from nobody's instance plays on it until its way home, fix-019's refix).</summary>
    public DW4Online.Relay.World.WorldInstance? World { get; init; }
    /// <summary>The JOIN is stamped (sealed) or JOIN_REQ went to the solo leader.</summary>
    public bool Stamped { get; set; }
}

/// <summary>A checkpoint the relay keeps (round J): the donor's tick state at Tick, with its hash and the load end.</summary>
public sealed record StoredCheckpoint(uint Tick, ulong Hash, uint LoadEnd, byte[] Bytes, long At)
{
    public bool Agreed { get; set; }
    /// <summary>The member that uploaded it (a fork drops the parent's checkpoints of leaver donors after the fork tick).</summary>
    public HubMember? Donor { get; init; }
    /// <summary>Protocol 48: the tick a READY from this checkpoint is checked at, after <see cref="Wire.ReadyCheckTicks"/>
    /// replayed steps (Tick is the first tick not in the state).</summary>
    public uint CheckTick => Tick + Wire.ReadyCheckTicks - 1;
    /// <summary>Protocol 48: the donor's net fingerprint of <see cref="CheckTick"/> (its NETFP of that tick; null until it
    /// came). After a fork between the checkpoint and its check tick it is the donor's side's (the parent and the child share
    /// this object): a READY is compared with it only while the donor is in the joiner's target and no fork at or before
    /// the check tick waits for its confirmation (RoomHub.OnReady).</summary>
    public ulong? CheckFp { get; set; }
}

/// <summary>A donor's checkpoint arriving in chunks.</summary>
public sealed class CheckpointUpload(HubMember donor, uint tick, ulong hash, uint loadEnd, uint total)
{
    public HubMember Donor { get; } = donor;
    public uint Tick { get; } = tick;
    public ulong Hash { get; } = hash;
    public uint LoadEnd { get; } = loadEnd;
    public byte[] Buffer { get; } = new byte[total];
    public uint Received { get; set; }
}

public enum HoldKind
{
    None,
    CoArrival,
    Join,
    Boss,
    /// <summary>Round I1b (19.7.5): a fresh arrival's barrier held for the loading-screen form of the prompt, without a time limit.</summary>
    Prompt,
    /// <summary>Round I1b's scenario hook World:TestGoBarrierHoldMs: a GO's barrier waits before its decision.</summary>
    TestGo,
    /// <summary>Decision 46 (round I1b's integration): a fresh arrival that chose an *Arriving now* row waits for that instance's stage
    /// group at the stage (Instances:JoinHoldSeconds at most, as a JOIN's hold), then its choice is decided again.</summary>
    Arriving,
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
    /// <summary>The barrier's alive mask (M3: 0 outside a give-up copy is a game over).</summary>
    public byte Alive { get; init; } = 0xFF;
    /// <summary>The members' instance (null: an arrival from home, a fresh arrival of 19.7.1).</summary>
    public DW4Online.Relay.World.WorldInstance? World { get; set; }
    public bool Fresh => World is null;
    /// <summary>The client chose *New instance* (BARRIER plan id 0xFFFF: DW4_ONLINE_AUTOCHOICE=new, client 13:30): no automatic join.</summary>
    public bool PlanNew { get; init; }
    /// <summary>Round I1b: the gate form's plan id the BARRIER named (0: none).</summary>
    public ushort PlanId { get; init; }
    /// <summary>Round I1b: the prompt's answer for this arrival (the gate form's CHOICE of the plan the BARRIER named, or the held
    /// barrier's CHOICE); null: none (yet).</summary>
    public DW4Online.Relay.Protocol.ChoiceMsg? PromptChoice { get; set; }
    /// <summary>Round I1b: the last OPTIONS payload sent for the held prompt and when (the refresh compares it).</summary>
    public byte[]? PromptOptions { get; set; }
    public long PromptOptionsAt { get; set; }
    /// <summary>Round I1b: the host names of the rows this arrival was offered (a refreshed prompt names a chosen instance that ended).</summary>
    public Dictionary<uint, string> PromptHosts { get; } = [];
    /// <summary>Round I1b: when the held prompt last took a CHOICE (at most one a second) and the newest one waiting for its turn.</summary>
    public long LastChoiceAt { get; set; }
    public DW4Online.Relay.Protocol.ChoiceMsg? PendingChoice { get; set; }
    /// <summary>The kind of arrival this decision is for its members (BARRIER_OK's arrival byte).</summary>
    public ArrivalKind Arrival { get; set; }
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
    /// <summary>World:TestJoinHoldMs (test runs only): the JOIN waits until then whatever the target's state.</summary>
    public long TestJoinHoldUntil { get; set; }
    public int Size => Members.Count + CoArrivals.Sum(c => c.Members.Count);
    /// <summary>Round I1b (19.30.7): the barrier of a stamped GO: its offer's stage group first, a JOIN hold without
    /// Instances:JoinHoldSeconds while that stage group exists, no *Go alone* at a boss lock, no rescue join.</summary>
    public DW4Online.Relay.World.GoOffer? Go { get; set; }
    /// <summary>Round I1b's scenario hook World:TestGoBarrierHoldMs held this GO barrier once already.</summary>
    public bool TestHeld { get; set; }
}

public sealed record Arrival(HubMember Member, int Slot, byte[] Carry);

/// <summary>A HOME owed to a member in an instance (<see cref="HubMember.HomeWish"/>): the reason, the instance, and the stage group
/// visit it was last asked in (a barrier between the ask and the stamp drops a stage group's unstamped HOME).</summary>
public sealed class HomeWish(HomeReason reason, uint wid)
{
    public HomeReason Reason { get; } = reason;
    public uint Wid { get; } = wid;
    public (uint Inst, uint Visit) AskedIn { get; set; }
}

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
    /// <summary>The session context its engine reports now (STATE; SEAMLESS.md 3.7).</summary>
    public ulong ContextHash { get; set; }
    /// <summary>The context the visit's members arrived with (their barrier's hash, taken before the load; a fork's child
    /// keeps its parent's).</summary>
    public ulong ArrivalContext { get; set; }
    /// <summary>The context at the visit's load end K, after its own load scripts ran (the first STATE with
    /// <see cref="StateMsg.LoadEnded"/>; null before and from members without it, below protocol 47; a fork's child keeps
    /// its parent's). A dungeon's first floor sets COUNTER 2 at its load (start.lua, loader state 7), so a later arrival's
    /// barrier hash (taken in the previous stage) equals <see cref="ArrivalContext"/>, not the running context.</summary>
    public ulong? EntryContext { get; set; }
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
    /// <summary>The user's instance this stage group belongs to (null: a member's home, a hub stage group, or a stage group of
    /// nobody's instance after a detach).</summary>
    public DW4Online.Relay.World.WorldInstance? World { get; set; }
    /// <summary>HOME waiting to be stamped (mask, reason, not before): a stage group that was not sealing yet, or a solo-led one
    /// (JOIN_REQ kind home when it is due).</summary>
    public (byte Mask, HomeReason Reason, long NotBefore)? PendingHome { get; set; }
    /// <summary>JOIN_REQ kind home went to the solo leader: its JOIN_AT seals HOME (mask, reason).</summary>
    public (byte Mask, HomeReason Reason, long AskedAt)? AskedHome { get; set; }
    /// <summary>HOME stamped for these slots (they leave; a second HOME for them is not stamped again).</summary>
    public byte HomeStamped { get; set; }
    /// <summary>JOIN_REQ home went unanswered this often.</summary>
    public int HomeTries { get; set; }
    /// <summary>The stage group is nobody's (its HOME went unanswered, 19.6.6): its next barrier is a DISSOLVE home with the redirect.</summary>
    public bool Detached { get; set; }
    /// <summary>A detached stage group's former instance (fix-019's refix): its engines hold that instance's story and settings until
    /// their way home, the own story only for its former host. The STORY refusal and the achievements' play context read it.</summary>
    public DW4Online.Relay.World.WorldInstance? DetachedFrom { get; set; }
    /// <summary>Round I1b (19.18): STATE's flag 16, no present slot alive (the go-along wait and the GO's join hold read it).</summary>
    public bool NoLiving { get; set; }
    /// <summary>Round I1b (19.30.5): the point the visit's barrier entered the stage through (a go-along's target point).</summary>
    public byte EntryPoint { get; set; }
    /// <summary>Round I1b (19.30.7): JOIN_REQ kind go went to the solo leader: its JOIN_AT seals GO (slot, stage, point).</summary>
    public (int Slot, string Stage, byte Point, long AskedAt)? AskedGo { get; set; }

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

    // Round J: checkpoints (SEAMLESS.md 6.13.4)
    /// <summary>The newest checkpoints, oldest first.</summary>
    public List<StoredCheckpoint> Checkpoints { get; set; } = [];
    /// <summary>The donor's upload in progress.</summary>
    public CheckpointUpload? Upload { get; set; }
    /// <summary>When the donor was last asked (0: never).</summary>
    public long CheckpointAskedAt { get; set; }
    /// <summary>The member asked for the next upload (round J's review: only its upload is taken; an unasked one replaced a
    /// running upload and reserved up to 8 MB each time).</summary>
    public HubMember? AskedDonor { get; set; }
    /// <summary>STATEHASH reports per checkpoint tick: slot -> (hash, load end).</summary>
    public SortedDictionary<uint, Dictionary<int, (ulong Hash, uint LoadEnd)>> StateHashes { get; } = [];
    /// <summary>The visit's load end K as the members' reports agree (a solo leader's alone).</summary>
    public uint? AgreedLoadEnd { get; set; }
    /// <summary>A desync after a rebuild from a checkpoint: joins and resyncs take the classic path from now on.</summary>
    public bool CheckpointSuspect { get; set; }
    /// <summary>The tick of the last READY from a checkpoint (the suspicion window counts from it).</summary>
    public uint? RebuildTick { get; set; }

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
    /// An arrival with barrier context <paramref name="context"/> may join <paramref name="x"/> (SEAMLESS.md 3.7): the
    /// instance runs that context now, or it arrived with it and nothing of the play since its load end changed it (the
    /// stage's own load scripts did; the arrival's own load would have done the same, as a co-arrival's did). Before the
    /// load end is known only the running context counts (review of round J: an unknown entry let an arrival join after a
    /// boss kill that changed the context before the visit ever became joinable).
    /// </summary>
    public static bool ContextMatches(HubInstance x, ulong context) =>
        x.ContextHash == context || (x.ArrivalContext == context && x.EntryContext is { } entry && entry == x.ContextHash);

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
