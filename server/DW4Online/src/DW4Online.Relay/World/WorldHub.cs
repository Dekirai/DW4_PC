using System.Diagnostics;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Rooms;
using DW4Online.Domain.Story;
using DW4Online.Domain.World;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;
using Microsoft.Extensions.Logging;

namespace DW4Online.Relay.Hub;

/// <summary>An event of the instance history (19.17.1), queued under the hub lock and written off it.</summary>
public abstract record WorldHistoryEvent(WorldInstance World);
public sealed record HistoryStart(WorldInstance World, InstanceHistoryStart Row) : WorldHistoryEvent(World);
public sealed record HistoryEnd(WorldInstance World, string Reason, int Peak, DateTime At) : WorldHistoryEvent(World);
public sealed record HistoryJoined(WorldInstance World, long Account, int Box, string Role, DateTime At) : WorldHistoryEvent(World);
public sealed record HistoryLeft(WorldInstance World, long Account, DateTime JoinedAt, string Reason, DateTime At) : WorldHistoryEvent(World);

/// <summary>
/// The world (SEAMLESS.md 19, round I1a): the hub generalized from one room to the whole server and not renamed (19.17.4). This
/// part holds the members by account with their member ids, the story keepers per (account, box), the user's instances
/// (<see cref="WorldInstance"/>) with their seats, area and visited stages, the matching of arrivals from home, the instance's end
/// with HOME, the host's grace, the notices and the instance info, the staff role and the history and audit queues. Every public
/// method runs under <c>lock (hub.Sync)</c>.
/// </summary>
public sealed partial class RoomHub
{
    private readonly WorldHubOptions _world;
    private readonly AreaTable _areas = AreaTable.Default;
    private readonly Dictionary<long, HubMember> _byAccount = [];
    private readonly Dictionary<uint, WorldInstance> _worlds = [];
    /// <summary>The instances by area (round 72: a fresh arrival's candidates are its area's instances only).</summary>
    private readonly Dictionary<string, List<WorldInstance>> _byArea = new(StringComparer.Ordinal);
    private readonly Dictionary<(long Account, int Box), StoryKeeper> _keepers = [];
    private readonly Dictionary<long, (WorldPlayContext Context, long At)> _recentContexts = [];
    private uint _nextMemberId = 1;
    private uint _nextWid = 1;
    private long _lastSelection;
    private Task<Dictionary<HubMember, PresenceChoice>>? _selectionJob;

    /// <summary>The instance history's queue (written off the lock).</summary>
    public Action<WorldHistoryEvent>? History { get; set; }
    /// <summary>Audit rows (event, account, actor, detail), queued off the lock.</summary>
    public Action<string, long?, long?, object?>? Audit { get; set; }
    /// <summary>The host's visibility choice, remembered as online_prefs.default_visibility (Q9), off the lock.</summary>
    public Action<long, Visibility>? RememberVisibility { get; set; }
    /// <summary>Runs the computation of the presence selection off the lock (the relay: the thread pool); null: at once (tests).</summary>
    public Func<Func<Dictionary<HubMember, PresenceChoice>>, Task<Dictionary<HubMember, PresenceChoice>>>? SelectionRunner { get; set; }

    public WorldHubOptions WorldOptions => _world;
    public AreaTable Areas => _areas;
    public IReadOnlyCollection<WorldInstance> Worlds => _worlds.Values;
    public WorldInstance? WorldInstance(uint wid) => _worlds.GetValueOrDefault(wid);
    public HubMember? MemberOf(long accountId) => _byAccount.GetValueOrDefault(accountId);
    public StoryKeeper? KeeperOf(long accountId, int box = 1) => _keepers.GetValueOrDefault((accountId, box));
    public int KeeperCount => _keepers.Count;
    /// <summary>The browser snapshot (19.17.2) as the last housekeeping pass built it: immutable, read without the lock.</summary>
    public WorldSnapshot CachedSnapshot { get; private set; } = WorldSnapshot.Empty;

    private StoryKeeper NewStoryKeeper(long account, int box, RoomStoryData story, int difficulty) => new(account, box, story, difficulty)
    {
        MinVisitSeconds = _inst.StoryMinVisitSeconds, PhaseStepSeconds = _inst.StoryPhaseStepSeconds, KeySeconds = _inst.StoryKeySeconds,
        FlagBurst = _inst.StoryFlagBurst, FlagSeconds = _inst.StoryFlagSeconds, MaxLinesPerMember = _inst.StoryLinesPerMember,
    };

    /// <summary>The member's own keeper (its bound box's story).</summary>
    private StoryKeeper OwnKeeper(HubMember m)
    {
        if (!_keepers.TryGetValue((m.AccountId, m.Box), out var k))
            _keepers[(m.AccountId, m.Box)] = k = NewStoryKeeper(m.AccountId, m.Box, new RoomStoryData(), m.Settings.Difficulty);
        return k;
    }

    /// <summary>The keeper the member's story values go to now (19.8.4): its instance host's in an instance, its own at home.</summary>
    private StoryKeeper OwnerKeeper(HubMember m) => m.World is { } w ? w.Keeper : OwnKeeper(m);

    /// <summary>
    /// The owner key (member id, box) of a keeper as <paramref name="receiver"/> knows it (19.8.4): the keeper of the receiver's instance
    /// by that instance's host's member id (the id BARRIER_OK named; the host stays with his instance until it goes, also after he
    /// left the world), the receiver's own keeper by the receiver's id; else the owner's member id while he is in the world, else the
    /// host of an instance that still runs on it. 0 only for a keeper nobody owns any more: a STORY_ACK then carries no story (a
    /// client reads owner 0 as its own key, client 13:30).
    /// </summary>
    private (uint Member, byte Box) OwnerKey(StoryKeeper k, HubMember? receiver = null)
    {
        var box = (byte)k.Box;
        if (receiver?.World is { } w && w.Keeper == k) return (w.Host.Id, box);
        if (receiver is not null && receiver.AccountId == k.Account && receiver.Box == k.Box) return (receiver.Id, box);
        if (_byAccount.TryGetValue(k.Account, out var o)) return (o.Id, box);
        foreach (var x in _worlds.Values)
            if (x.Keeper == k) return (x.Host.Id, box);
        return (0, box);
    }

    private static DateTime UtcNow => DateTime.UtcNow;

    // Admission ------------------------------------------------------------------------------------------------------------

    /// <summary>A member's relay connection completed its handshake (test form: a plain player with an empty story).</summary>
    public HubMember Connect(int index, long accountId, string name, bool owner, IHubPeer peer, long now) =>
        Connect(new HubAdmit(accountId, name), peer, now);

    /// <summary>
    /// A world connection completed its handshake (19.17.3): the account's member (a new member id at its first admission in the
    /// run), its social cache and its keeper, then WELCOME, the own story, the instance's story when it still plays in one, and
    /// INSTANCE_INFO. A NewSession handshake (Play on another PC) replaces a known member: its old connection gets CLOSE
    /// replaced, a hosted instance ends at once (END host_left), the seat goes. <paramref name="beforeWelcome"/> gets the member
    /// id first (the relay's "OK" line).
    /// </summary>
    public HubMember Connect(HubAdmit a, IHubPeer peer, long now, Action<uint>? beforeWelcome = null)
    {
        if (_byAccount.TryGetValue(a.AccountId, out var m) && a.NewSession && (m.Connected || m.World is not null || m.Instance is { Mode: not InstanceMode.Hub }))
        {
            Count("replaced");
            Note("replaced", m.Instance, ("member", m.Id), ("account", m.AccountId));
            m.Peer?.Close(CloseReason.Replaced, "Signed in on another PC.");
            m.Peer = null;
            m.DisconnectedAt = now;
            if (m.World is { } hosted && hosted.Host == m) EndWorld(hosted, EndReason.HostLeft, now);
            DetachLocked(m, now, leaveNow: true);
            if (m.World is { } w) FreeSeat(w, m, "left", now);
        }
        if (m is null)
        {
            m = new HubMember(_nextMemberId++, a.AccountId, a.Name);
            _members[m.Index] = m;
            _byAccount[a.AccountId] = m;
            m.LastInputOrPong = now;
        m.NextPingAt = now + FromMs(250.0 * (m.Id % 4)); // (one a second, spread over the housekeeping passes)
            Note("member", null, ("member", m.Id), ("account", m.AccountId), ("name", m.Name));
        }
        ApplyAdmit(m, a);
        if (!_keepers.TryGetValue((m.AccountId, m.Box), out var own))
            _keepers[(m.AccountId, m.Box)] = own = NewStoryKeeper(m.AccountId, m.Box, a.Story, m.Settings.Difficulty);
        own.IdleSince = 0;
        beforeWelcome?.Invoke(m.Id);
        if (m.Peer is { } old && old != peer) old.Close(CloseReason.Replaced, "A newer connection of this account replaced this one.");
        // Resumable only in a stage group of the member's live instance: WELCOME then names it (wid). A client whose WELCOME names
        // no instance or another takes the local way home (19.6.10) and sends no RESUME; a slot kept for it here (an instance that
        // is ending, a seat that went with a removal, a detached group) would stop its stage group's sealing at that way home's
        // BARRIER.
        var resumable = m.Instance is { Ended: false } i && m.DisconnectedAt != 0 && m.Joining is null && m.Arrival is null && i.Barrier is null &&
                        m.Slot >= 0 && i.Slots[m.Slot] == m && i.Mode != InstanceMode.Hub &&
                        m.World is { Ending: false } seat && _worlds.ContainsKey(seat.Wid) && i.World == seat;
        m.Peer = peer;
        m.DisconnectedAt = 0;
        m.LastHeard = now;
        m.LastInputOrPong = now;
        m.NextPingAt = now + FromMs(250.0 * (m.Id % 4)); // (one a second, spread over the housekeeping passes)
        m.ResumeAllowed = resumable;
        m.Known.Clear();
        if (!resumable)
        {
            MarkLone(m);
            DetachLocked(m, now, leaveNow: true);
            var hub = NewInstance(InstanceMode.Hub, "", 1, 0, now);
            Place(m, hub, 0, now);
        }
        var inst = m.Instance!;
        var flags = (byte)((resumable ? WelcomeMsg.Resumable : 0) | WelcomeMsg.PresenceV2 | WelcomeMsg.Checkpoints | WelcomeMsg.World |
                           (m.Staff ? WelcomeMsg.Staff : 0));
        peer.Send(new WelcomeMsg(m.Id, inst.Id, inst.Visit, (byte)m.Slot, inst.Mode, inst.Stage,
            resumable && m.Cursor > 0 ? m.Cursor - 1 : WelcomeMsg.NoTick, (byte)(inst.Streams[m.Slot]?.Delay.Current ?? 0),
            (ushort)_inst.MaxCatchUpSeconds, flags, m.World is { Ending: false } live ? live.Wid : 0, (byte)m.Box).Frame());
        // The own confirmed story (the title applies it at its Game Start, 19.8.2), then the instance's when the member still plays in
        // one (an ending instance's story is nobody's to play on: WELCOME names no instance then).
        peer.Send(StoryAck(own, 0, [], receiver: m));
        if (m.World is { } world)
        {
            if (!world.Ending) peer.Send(StoryAck(world.Keeper, 0, [], receiver: m));
            if (world.Host == m && world.HostAway) HostBack(world, now);
            MarkInfoDirty(world);
        }
        SendInfo(m, now);
        Note("connect", inst, ("member", m.Id), ("account", m.AccountId), ("resumed", resumable), ("wid", m.World?.Wid ?? 0));
        return m;
    }

    private static void ApplyAdmit(HubMember m, HubAdmit a)
    {
        m.Name = a.Name;
        m.Staff = a.Staff;
        m.Box = Math.Max(1, a.Box);
        m.DataHash = a.DataHash;
        m.DefaultVisibility = a.DefaultVisibility;
        m.Friends = a.Friends.ToHashSet();
        m.Blocks = a.Blocks.ToHashSet();
        m.BlockedBy = a.BlockedBy.ToHashSet();
        m.Admission = a.Handle ?? m.Admission;
        m.LeftWorld = false;
    }

    /// <summary>
    /// The live re-check (19.14): the admission ran again off the lock after IWorldNotifier.AccountChanged / PrefsChanged /
    /// FriendsChanged / BlocksChanged. Null: the account may not play any more (locked, banned, deleted, a newer session): its
    /// connection gets CLOSE account and it leaves the world (a hosted instance ends). <paramref name="handle"/> and
    /// <paramref name="seq"/> name the re-check (<see cref="AdmissionHandles"/>): a result for an admission the member no longer has (a
    /// newer handshake came meanwhile) or one that a newer re-check's result overtook is dropped, a refusal included.
    /// </summary>
    public void Readmitted(long accountId, HubAdmit? a, long now, object? handle = null, long seq = 0)
    {
        if (!_byAccount.TryGetValue(accountId, out var m)) return;
        if (handle is not null && !Equals(m.Admission, handle))
        {
            Count("readmit_stale");
            return;
        }
        if (seq != 0)
        {
            if (seq <= m.RecheckApplied)
            {
                Count("readmit_overtaken");
                return;
            }
            m.RecheckApplied = seq;
        }
        if (a is null)
        {
            Count("readmit_refused");
            Note("readmit_refused", m.Instance, ("member", m.Id), ("account", accountId));
            m.Peer?.Close(CloseReason.Account, "This account can no longer play online.");
            m.Peer = null;
            LeaveWorld(m, "left", now);
            return;
        }
        var wasStaff = m.Staff;
        ApplyAdmit(m, a with { Box = m.Box, DataHash = m.DataHash });
        if (wasStaff != m.Staff) Note("role", m.Instance, ("member", m.Id), ("staff", m.Staff));
        if (m.World is { } w) MarkInfoDirty(w);
    }

    /// <summary>The members whose admission should run again for a change of these accounts (it and, for a friendship or block, the
    /// other side when that one is online): the admission's handle and the re-check's number (<see cref="Readmitted"/> applies the
    /// newest only).</summary>
    public IReadOnlyList<(long Account, object Handle, long Seq)> AdmissionHandles(params long[] accounts)
    {
        var list = new List<(long, object, long)>();
        foreach (var a in accounts.Distinct())
            if (_byAccount.TryGetValue(a, out var m) && m.Admission is { } handle)
                list.Add((a, handle, ++m.RecheckSeq));
        return list;
    }

    /// <summary>
    /// The member leaves the world (BYE, POST /world/leave, its connection gone beyond World:MemberTimeoutSeconds, its account
    /// closed): its stage group goes on without it, its seat frees, a hosted instance ends (19.6.5).
    /// </summary>
    public void LeaveWorld(HubMember m, string reason, long now)
    {
        if (m.LeftWorld && !_members.ContainsKey(m.Index)) return;
        RememberContext(m, now);
        if (m.World is { } w && w.Host == m)
            EndWorld(w, reason == "lost" ? EndReason.HostLost : EndReason.HostLeft, now);
        DetachLocked(m, now, leaveNow: true);
        if (m.World is { } seat) FreeSeat(seat, m, reason, now);
        foreach (var r in m.ChosenBy.Keys) r.Peer?.Send(GoneFigure(m));
        m.ChosenBy.Clear();
        m.Choice = PresenceChoice.Empty;
        m.HomeWish = null;
        _members.Remove(m.Index);
        _byAccount.Remove(m.AccountId);
        m.LeftWorld = true;
        if (_keepers.TryGetValue((m.AccountId, m.Box), out var k) && !_worlds.Values.Any(x => x.Keeper == k)) k.IdleSince = now;
        Note("member_left", null, ("member", m.Id), ("account", m.AccountId), ("reason", reason));
    }

    /// <summary>A presence that hides a figure at once (the member left the stage or the world).</summary>
    private static byte[] GoneFigure(HubMember m) =>
        new PresenceOutMsg(m.Id, new PresenceMsg("", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)).Frame();

    /// <summary>POST /world/leave: the member's connection closes (CLOSE left) and it leaves the world. False: not in it.</summary>
    public bool Leave(long accountId, long now)
    {
        if (!_byAccount.TryGetValue(accountId, out var m)) return false;
        m.Peer?.Close(CloseReason.Left, "You left the world.");
        m.Peer = null;
        LeaveWorld(m, "left", now);
        return true;
    }

    // Instances --------------------------------------------------------------------------------------------------------------

    private uint NextWid()
    {
        var id = _nextWid;
        while (_worlds.ContainsKey(id) || id == 0) id = id >= 999_999_999 ? 1 : id + 1;
        _nextWid = id >= 999_999_999 ? 1 : id + 1;
        return id;
    }

    /// <summary>
    /// A NEW from home makes an instance (19.6.1): its host is the base group's first member (whose snapshot every co-arriving
    /// engine restores: it holds his story and settings), the visibility his default, the settings his own (from his BARRIER), the
    /// story his keeper rebased on his difficulty, the area the stage's (19.30.2), the stage its first visited one.
    /// </summary>
    private WorldInstance CreateWorld(HubMember host, string stage, ulong context, long now)
    {
        var keeper = OwnKeeper(host);
        if (!_worlds.Values.Any(x => x.Keeper == keeper)) keeper.SetDifficulty(host.Settings.Difficulty);
        keeper.IdleSince = 0;
        var w = new WorldInstance(NextWid())
        {
            Host = host, Keeper = keeper, DataHash = host.DataHash, Visibility = host.DefaultVisibility, LoggedVisibility = host.DefaultVisibility,
            Settings = host.Settings,
            Area = _areas.AreaOf(stage), FirstStage = stage.ToLowerInvariant(), Context = context, CreatedAt = now, StartedUtc = UtcNow,
            StartingUntil = now + FromMs(_world.StartingSeconds * 1000.0),
        };
        w.Unlisted = _worlds.Count(x => !x.Value.Unlisted) >= _world.MaxInstances;
        if (w.Unlisted) Count("instances_unlisted");
        w.Visited.Add(stage);
        _worlds[w.Wid] = w;
        if (w.Area is { } area)
        {
            if (!_byArea.TryGetValue(area, out var list)) _byArea[area] = list = [];
            list.Add(w);
        }
        Count("instances");
        Note("instance_start", null, ("wid", w.Wid), ("host", host.AccountId), ("member", host.Id), ("area", w.Area ?? ""), ("stage", w.FirstStage),
            ("visibility", Visibilities.Text(w.Visibility)), ("unlisted", w.Unlisted), ("context", w.Context.ToString("x16")));
        History?.Invoke(new HistoryStart(w, new InstanceHistoryStart(host.AccountId, host.Box, w.FirstStage, Visibilities.Text(w.Visibility),
            w.Settings.Difficulty, StoryMerge.Phase(keeper.Story), w.Settings.Canonical(Wire.WorldProtocol, w.DataHash), w.StartedUtc)));
        TakeSeat(w, host, now, "new_group");
        return w;
    }

    /// <summary>A member takes a seat (19.3): its story owner is the host's from now on; staff who play are audited.</summary>
    private void TakeSeat(WorldInstance w, HubMember m, long now, string how)
    {
        if (w.Seats.ContainsKey(m)) return;
        if (m.World is { } other && other != w) FreeSeat(other, m, "left", now);
        var role = w.Host == m ? "host" : m.Staff ? "staff" : "guest";
        w.Seats[m] = new Seat(m, now, UtcNow, role);
        m.World = w;
        w.PeakPlayers = Math.Max(w.PeakPlayers, w.Seats.Count);
        History?.Invoke(new HistoryJoined(w, m.AccountId, m.Box, role, UtcNow));
        if (role == "staff")
            Audit?.Invoke("staff_join", w.HostAccount, m.AccountId, new { wid = w.Wid, visibility = Visibilities.Text(w.Visibility), members = w.Seats.Count });
        Note("instance_join", null, ("wid", w.Wid), ("account", m.AccountId), ("member", m.Id), ("role", role), ("how", how));
        foreach (var x in w.Members)
            if (x != m) x.Peer?.Send(Notice(x, WorldNoticeKind.Joined, m, 0, ""));
        MarkInfoDirty(w);
    }

    /// <summary>A member's seat frees (it went home, left the world, was removed): the instance ends when nobody is left.</summary>
    private void FreeSeat(WorldInstance w, HubMember m, string reason, long now)
    {
        if (!w.Seats.Remove(m, out var seat)) return;
        if (m.World == w) m.World = null;
        GoSeatFreed(w, m, now);
        History?.Invoke(new HistoryLeft(w, m.AccountId, seat.SinceUtc, reason, UtcNow));
        Note("instance_leave", null, ("wid", w.Wid), ("account", m.AccountId), ("member", m.Id), ("reason", reason));
        foreach (var x in w.Members) x.Peer?.Send(Notice(x, WorldNoticeKind.Left, m, 0, ""));
        if (m.Connected) SendInfo(m, now);
        MarkInfoDirty(w);
        if (w.Seats.Count == 0)
        {
            if (!w.Ending) EndWorld(w, EndReason.Empty, now);
            RemoveWorld(w, now);
        }
    }

    private void RemoveWorld(WorldInstance w, long now)
    {
        if (!_worlds.Remove(w.Wid)) return;
        if (w.Area is { } area && _byArea.TryGetValue(area, out var list)) list.Remove(w);
        if (w.Keeper.Held) w.Keeper.Release(merge: false);
        foreach (var g in w.Groups) g.World = null;
        w.Groups.Clear();
        if (!_byAccount.ContainsKey(w.HostAccount) && !_worlds.Values.Any(x => x.Keeper == w.Keeper)) w.Keeper.IdleSince = now;
        Note("instance_removed", null, ("wid", w.Wid));
    }

    /// <summary>
    /// The end of an instance (19.6.5): joins and travels to it are refused from now on; every member gets WORLD_NOTICE ended
    /// (the reason); every stage group gets HOME(every present slot) at least World:EndNoticeTicks ahead (sealed: stamped; solo-led:
    /// JOIN_REQ kind home when due); parked groups get their DISSOLVE redirected home at their decision; held story values are
    /// discarded (19.8.5). The members' seats free as their DISSOLVEs come.
    /// </summary>
    public void EndWorld(WorldInstance w, EndReason reason, long now)
    {
        if (w.Ending) return;
        FlushVisibility(w);
        w.Ending = true;
        w.EndReason = reason;
        w.EndedAt = now;
        w.HostAwaySince = 0;
        w.GraceUntil = 0;
        if (w.Keeper.Held) w.Keeper.Release(merge: false);
        Count("instances_ended");
        Note("instance_end", null, ("wid", w.Wid), ("reason", EndReasons.Text(reason)));
        History?.Invoke(new HistoryEnd(w, EndReasons.Text(reason), w.PeakPlayers, UtcNow));
        foreach (var x in w.Members) x.Peer?.Send(Notice(x, WorldNoticeKind.Ended, w.Host, (uint)reason, ""));
        // (Round I1b: the offers close, waiting requests end; HOME and the end's redirect win over a stamped GO, 19.30.8.)
        UpdateOffers(w, now);
        var ahead = (uint)Math.Max(0, _world.EndNoticeTicks);
        foreach (var g in w.Groups.ToList())
            if (!g.Ended) SendHome(g, g.PresentMask, HomeReason.Ended, now, ahead);
        // Arrivals that wait for one of its stage groups decide again (fresh ones: elsewhere or an own instance).
        foreach (var h in _holds.Where(h => h.Target?.World == w).ToList())
        {
            _holds.Remove(h);
            h.Hold = HoldKind.None;
            Redecide(h, now);
        }
        MarkInfoDirty(w);
    }

    /// <summary>
    /// HOME for <paramref name="mask"/> of a stage group (19.6.5): sealed and sealing: stamped at the stamp tick plus
    /// <paramref name="ahead"/>; sealed before its sealing starts: when it starts; solo-led: JOIN_REQ kind home to its leader
    /// once due (its JOIN_AT seals HOME). A group at a barrier gets nothing: its decision sends the members home.
    /// </summary>
    private void SendHome(HubInstance g, byte mask, HomeReason reason, long now, uint ahead)
    {
        // (Never a slot whose HOME is stamped or asked of the solo leader already: a second ask, the client's kept *Leave instance*
        // beside the relay's RetryHome, would stamp a second HOME.)
        mask = (byte)(mask & g.PresentMask & ~g.HomeStamped & ~(g.AskedHome?.Mask ?? 0));
        if (mask == 0 || g.Ended || g.Barrier is not null) return;
        if (g.Mode == InstanceMode.Sealed && g.Sealing)
        {
            var at = StampTick(g) + ahead;
            g.AddEvent(at, TickEvent.Home(mask, reason));
            g.HomeStamped |= mask;
            Count("home_stamped");
            Note("home", g, ("mask", mask), ("reason", reason.ToString()), ("tick", at), ("wid", g.World?.Wid ?? 0));
            return;
        }
        var notBefore = now + (long)(ahead * Wire.TickSeconds * Freq);
        g.PendingHome = g.PendingHome is { } p ? ((byte)(p.Mask | mask), reason, Math.Min(p.NotBefore, notBefore)) : (mask, reason, notBefore);
        if (g.Mode == InstanceMode.SoloLed) TrySoloHome(g, now);
    }

    /// <summary>A solo-led stage group's due HOME: JOIN_REQ kind home to its leader (one at a time, after any JOIN in flight).</summary>
    private void TrySoloHome(HubInstance g, long now)
    {
        if (g.PendingHome is not { } p || g.Mode != InstanceMode.SoloLed || g.Barrier is not null || now < p.NotBefore) return;
        if (g.SoloJoin is not null || g.AskedJoin is not null || g.AskedHome is not null) return;
        // (Slots stamped meanwhile, a JOIN_AT home before the group led alone again, are not asked twice.)
        var open = (byte)(p.Mask & g.PresentMask & ~g.HomeStamped);
        if (open == 0)
        {
            g.PendingHome = null;
            return;
        }
        p = (open, p.Reason, p.NotBefore);
        if (g.Leader is not { Connected: true } leader) return;
        var ld = DelayController.FromRoundTrips(leader.Rtt.All, _sealing.MaxDelayTicks);
        g.AskedHome = (p.Mask, p.Reason, now);
        g.PendingHome = null;
        leader.Peer!.Send(new JoinReqMsg(g.Id, g.Visit, p.Mask, (byte)ld, JoinReqKind.Home).Frame());
        Count("home_requests");
        Note("home_req", g, ("mask", p.Mask), ("reason", p.Reason.ToString()));
    }

    /// <summary>
    /// A stage group whose last member but one left turns solo-led at <paramref name="lead"/> and seals no tick from there: a HOME
    /// stamped at or after it (the END's HOME stamped World:EndNoticeTicks ahead before the lost or leaving host's LEAVE took an
    /// earlier tick, or a guest's leave before another member's LEAVE) would never reach the remaining member, who then played on
    /// in an ended instance (W7b, W12). It waits again as the group's pending HOME for the slots still present, due when its stamp
    /// would have been sealed, and goes to the solo leader as JOIN_REQ home (<see cref="TrySoloHome"/>).
    /// </summary>
    private void RefileHomesAfterLead(HubInstance g, uint lead, long now)
    {
        foreach (var (t, list) in g.PendingEvents.Where(kv => kv.Key >= lead).ToList())
        {
            foreach (var e in list.Where(e => e.Type == Ev.Home).ToList())
            {
                list.Remove(e);
                g.HomeStamped = (byte)(g.HomeStamped & ~e.HomeMask);
                var mask = (byte)(e.HomeMask & g.PresentMask);
                if (mask == 0) continue;
                var reason = e.Payload.Length > 1 ? (HomeReason)e.Payload[1] : HomeReason.Ended;
                var notBefore = now + (long)((t - lead) * Wire.TickSeconds * Freq);
                g.PendingHome = g.PendingHome is { } p ? ((byte)(p.Mask | mask), p.Reason, Math.Min(p.NotBefore, notBefore)) : (mask, reason, notBefore);
                Count("home_refiled");
                Note("home_refiled", g, ("mask", mask), ("reason", reason.ToString()), ("tick", t), ("lead", lead), ("wid", g.World?.Wid ?? 0));
            }
            if (list.Count == 0) g.PendingEvents.Remove(t);
        }
    }

    /// <summary>A sealed stage group that starts sealing stamps the HOME that waited for it.</summary>
    private void StampPendingHome(HubInstance g, long now)
    {
        if (g.PendingHome is not { } p || g.Mode != InstanceMode.Sealed || !g.Sealing) return;
        g.PendingHome = null;
        var left = Math.Max(0, (p.NotBefore - now) / Freq / Wire.TickSeconds);
        SendHome(g, p.Mask, p.Reason, now, (uint)Math.Ceiling(left));
    }

    /// <summary>A guest leaves on purpose (INTENT leave, 19.6.4) or is removed (19.6.6): HOME for its slot; a removed member's seat
    /// and story owner switch at once (a modified client cannot keep writing the host's story).</summary>
    private void HomeMember(HubMember m, HomeReason reason, long now)
    {
        // (The HOME is owed until the member's DISSOLVE: a barrier of its stage group before the stamp drops the group's unstamped
        // HOME, so the member's next stage group of the instance is asked again, RetryHome.)
        var wid = m.World?.Wid ?? m.Instance?.World?.Wid ?? 0;
        if (wid != 0) m.HomeWish = new HomeWish(reason, wid);
        if (m.Instance is not { } g || m.Slot < 0 || g.Slots[m.Slot] != m) return;
        if (g.Barrier is not null)
        {
            Count("home_at_barrier");
            return;
        }
        if (m.HomeWish is { } wish) wish.AskedIn = (g.Id, g.Visit);
        SendHome(g, (byte)(1 << m.Slot), reason, now, 0);
    }

    /// <summary>
    /// A HOME owed to a member (<see cref="HubMember.HomeWish"/>) whose stage group took a barrier before it was stamped (a *Leave
    /// instance* while the group gathered at a barrier, a solo leader's JOIN_REQ home or a sealed group's HOME waiting for its sealing
    /// when the barrier came): asked again once in each later stage group of the instance the member plays in, unless that group holds
    /// it already. The instance's end sends everyone home anyway.
    /// </summary>
    private void RetryHome(HubMember m, long now)
    {
        if (m.HomeWish is not { } wish) return;
        if (!_worlds.TryGetValue(wish.Wid, out var w) || w.Ending)
        {
            m.HomeWish = null;
            return;
        }
        if (m.Instance is not { Ended: false } g || g.Mode == InstanceMode.Hub || g.World != w || m.Slot < 0 || g.Slots[m.Slot] != m ||
            g.Barrier is not null || m.Arrival is not null || m.Joining is not null || wish.AskedIn == (g.Id, g.Visit))
            return;
        wish.AskedIn = (g.Id, g.Visit);
        var bit = (byte)(1 << m.Slot);
        if (((g.HomeStamped | (g.PendingHome?.Mask ?? 0) | (g.AskedHome?.Mask ?? 0)) & bit) != 0) return;
        Count("home_asked_again");
        Note("home_again", g, ("member", m.Id), ("reason", wish.Reason.ToString()), ("wid", w.Wid));
        SendHome(g, bit, wish.Reason, now, 0);
    }

    // The host's grace (19.6.7) ---------------------------------------------------------------------------------------------

    /// <summary>The host's connection closed or fell silent: his slot fills with neutral pads, guests see the countdown, their story
    /// values are held, joins wait; he stays host and keeps his seat until the grace's end.</summary>
    private void StartGrace(WorldInstance w, long now, string why)
    {
        if (w.Ending || w.HostAway) return;
        w.HostAwaySince = now;
        w.GraceUntil = now + FromMs(_world.HostGraceSeconds * 1000.0);
        w.LastAwayNotice = now;
        w.Keeper.Hold();
        if (w.Host.Instance is { } g && w.Host.Slot >= 0 && g.Streams[w.Host.Slot] is { } st) st.NeutralFill = true;
        Count("host_grace");
        Note("host_away", null, ("wid", w.Wid), ("why", why));
        foreach (var x in w.Members)
            if (x != w.Host) x.Peer?.Send(Notice(x, WorldNoticeKind.HostAway, w.Host, (uint)_world.HostGraceSeconds, ""));
        MarkInfoDirty(w);
    }

    /// <summary>The host is back within his grace (RESUME, a reconnect, INPUT after silence): play on, the held values merge.</summary>
    private void HostBack(WorldInstance w, long now)
    {
        if (!w.HostAway) return;
        w.HostAwaySince = 0;
        w.GraceUntil = 0;
        if (w.Host.Instance is { } g && w.Host.Slot >= 0 && g.Streams[w.Host.Slot] is { } st) st.NeutralFill = false;
        Note("host_back", null, ("wid", w.Wid));
        foreach (var x in w.Members)
            if (x != w.Host) x.Peer?.Send(Notice(x, WorldNoticeKind.HostBack, w.Host, 0, ""));
        if (w.Keeper.Release(merge: true)) StoryChanged(w.Keeper, null);
        MarkInfoDirty(w);
    }

    /// <summary>The world's part of the housekeeping: the grace (its start on silence, its notices, its end), member timeouts,
    /// due HOMEs of solo-led groups, idle keepers, the instance info, the presence selection, the browser snapshot.</summary>
    private void HousekeepWorld(long now)
    {
        foreach (var w in _worlds.Values.ToList())
        {
            if (!_worlds.ContainsKey(w.Wid)) continue;
            var host = w.Host;
            if (!w.Ending && !w.HostAway && host.Connected && host.Instance is { Mode: InstanceMode.Sealed, Sealing: true } hg && host.Slot >= 0 &&
                hg.Streams[host.Slot] is { FillStreak: > 0 } && Ms(now - host.LastInputOrPong) >= _world.HostSilenceSeconds * 1000.0)
                StartGrace(w, now, "silence");
            if (w.HostAway)
            {
                if (now >= w.GraceUntil)
                {
                    EndWorld(w, EndReason.HostLost, now);
                    continue;
                }
                if (Ms(now - w.LastAwayNotice) >= _world.HostAwayNoticeSeconds * 1000.0)
                {
                    w.LastAwayNotice = now;
                    var left = (uint)Math.Ceiling(Ms(w.GraceUntil - now) / 1000.0);
                    foreach (var x in w.Members)
                        if (x != host) x.Peer?.Send(Notice(x, WorldNoticeKind.HostAway, host, left, ""));
                    MarkInfoDirty(w);
                }
            }
            if (w.VisibilityPending is { } vp && Ms(now - vp.At) >= VisibilitySettleMs) FlushVisibility(w);
            if (w.StartingUntil != 0 && (now >= w.StartingUntil || w.Groups.Any(g => g.Joinable))) w.StartingUntil = 0;
            var players = Math.Min(w.Seats.Count, Wire.MaxSlots);
            w.SeenWithPlayers[players] = now;
            // Round I1b: the offers follow their entrants, late notices, the requests' waits (once a pass).
            if (w.Offers.Count > 0 || w.Members.Any(x => x.Go is not null)) UpdateOffers(w, now);
            foreach (var g in w.Groups.ToList())
                if (g.PendingHome is not null)
                {
                    if (g.Mode == InstanceMode.Sealed) StampPendingHome(g, now);
                    else TrySoloHome(g, now);
                }
                else if (g.AskedHome is { } asked && Ms(now - asked.AskedAt) > 5000)
                {
                    // The leader did not answer JOIN_REQ home: ask again, a few times; then the group is nobody's (19.6.6).
                    g.AskedHome = null;
                    g.HomeTries++;
                    Count("home_req_timeout");
                    if (g.HomeTries >= Math.Max(1, _inst.JoinRequestTries)) Detach(g, now);
                    else g.PendingHome = (asked.Mask, asked.Reason, now);
                }
        }
        // Members whose connection is gone leave the world after the member timeout (a host in his grace after it plus the margin).
        foreach (var m in _members.Values.ToList())
        {
            if (m.Connected || m.DisconnectedAt == 0) continue;
            var limit = _world.MemberTimeoutSeconds * 1000.0;
            if (m.World is { HostAway: true } w && w.Host == m) limit = Math.Max(limit, Ms(w.GraceUntil - m.DisconnectedAt) + _world.MemberTimeoutMarginSeconds * 1000.0);
            if (Ms(now - m.DisconnectedAt) >= limit) LeaveWorld(m, "lost", now);
        }
        foreach (var m in _members.Values)
            if (m.HomeWish is not null) RetryHome(m, now);
        foreach (var (key, k) in _keepers.ToList())
            if (k.IdleSince != 0 && Ms(now - k.IdleSince) >= _world.KeeperIdleMinutes * 60_000.0 && !_byAccount.ContainsKey(key.Account) &&
                !_worlds.Values.Any(x => x.Keeper == k))
                _keepers.Remove(key);
        foreach (var (a, c) in _recentContexts.ToList())
            if (Ms(now - c.At) > 60_000) _recentContexts.Remove(a);
        foreach (var w in _worlds.Values)
            if (w.InfoDirty) SendInfoAll(w, now);
        HousekeepSelection(now);
        CachedSnapshot = BuildSnapshot(now);
    }

    /// <summary>A stage group that no longer answers its HOME (19.6.6): nobody's instance from now on (no joins, no story), its next
    /// barrier a DISSOLVE home with the redirect; its members' seats free.</summary>
    private void Detach(HubInstance g, long now)
    {
        if (g.World is not { } w) return;
        Count("groups_detached");
        Note("detached", g, ("wid", w.Wid));
        g.World = null;
        g.Detached = true;
        w.Groups.Remove(g);
        foreach (var m in g.Members.ToList())
            if (m.World == w) FreeSeat(w, m, w.Ending ? "ended" : "removed", now);
    }

    // Instance operations (INSTANCE_OP, the admin endpoints) ----------------------------------------------------------------------

    /// <summary>INSTANCE_OP a member may send at once; then one a second (19.16: a flood reaches neither the guests' toasts nor the
    /// audit log). More are counted and dropped; INSTANCE_INFO tells the client the state that holds.</summary>
    public const int InstanceOpBurst = 3;
    /// <summary>A visibility change's audit row and the host's remembered default wait this long for the next change (one row for a burst).</summary>
    public const double VisibilitySettleMs = 2000;

    private void OnInstanceOp(HubMember m, InstanceOpMsg msg, long now)
    {
        m.InstanceOpTokens = double.IsNaN(m.InstanceOpTokens) ? InstanceOpBurst
            : Math.Min(InstanceOpBurst, m.InstanceOpTokens + Ms(now - m.InstanceOpTokensAt) / 1000.0);
        m.InstanceOpTokensAt = now;
        if (m.InstanceOpTokens < 1)
        {
            Count("instance_op_throttled");
            return;
        }
        m.InstanceOpTokens -= 1;
        if (m.World is not { Ending: false } w)
        {
            Count("instance_op_ignored");
            return;
        }
        var isHost = w.Host == m;
        switch (msg.Op)
        {
            case InstanceOpKind.Visibility when isHost || m.Staff:
                SetVisibility(w, (Visibility)msg.Argument, m, now);
                break;
            case InstanceOpKind.End when isHost:
                EndWorld(w, EndReason.HostLeft, now);
                break;
            case InstanceOpKind.End when m.Staff:
                Audit?.Invoke("staff_end_instance", w.HostAccount, m.AccountId, new { wid = w.Wid });
                EndWorld(w, EndReason.Staff, now);
                break;
            case InstanceOpKind.Remove when isHost || m.Staff:
                if (w.Members.FirstOrDefault(x => x.Id == msg.Argument) is { } target && target != w.Host)
                {
                    // (Audit rows of 19.17.1: a staff removal staff_remove, the host's instance_remove.)
                    Audit?.Invoke(isHost ? "instance_remove" : "staff_remove", target.AccountId, m.AccountId, new { wid = w.Wid });
                    Remove(w, target, isHost ? HomeReason.Removed : HomeReason.Staff, now);
                }
                break;
            default:
                Count("instance_op_refused");
                break;
        }
    }

    private void SetVisibility(WorldInstance w, Visibility v, HubMember by, long now)
    {
        if (w.Visibility == v) return;
        w.Visibility = v;
        Note("visibility", null, ("wid", w.Wid), ("visibility", Visibilities.Text(v)), ("by", by.Id));
        // (The audit row and the remembered default are written once the choice settled, FlushVisibility.)
        w.VisibilityPending = (by, now);
        if (by == w.Host)
        {
            by.DefaultVisibility = v;
            w.HostChoicePending = v;
        }
        foreach (var x in w.Members) x.Peer?.Send(Notice(x, WorldNoticeKind.VisibilityChanged, by, (uint)v, ""));
        MarkInfoDirty(w);
    }

    /// <summary>A settled visibility change (or one at the instance's end): its audit row when the visibility differs from the last
    /// one written, and the host's own last choice as his remembered default (Q9), both off the lock.</summary>
    private void FlushVisibility(WorldInstance w)
    {
        if (w.VisibilityPending is not { } p) return;
        w.VisibilityPending = null;
        if (w.Visibility != w.LoggedVisibility)
        {
            w.LoggedVisibility = w.Visibility;
            Audit?.Invoke("instance_visibility", w.HostAccount, p.By.AccountId, new { wid = w.Wid, visibility = Visibilities.Text(w.Visibility) });
        }
        if (w.HostChoicePending is { } choice)
        {
            w.HostChoicePending = null;
            RememberVisibility?.Invoke(w.HostAccount, choice);
        }
    }

    /// <summary>A removal (19.6.6): HOME for the member's slot, a bar of World:RemovedBarMinutes, the seat and the story owner at once.</summary>
    private void Remove(WorldInstance w, HubMember target, HomeReason reason, long now)
    {
        w.Bars[target.AccountId] = now + FromMs(_world.RemovedBarMinutes * 60_000.0);
        target.Peer?.Send(Notice(target, WorldNoticeKind.Removed, w.Host, 0, ""));
        HomeMember(target, reason, now);
        FreeSeat(w, target, "removed", now);
    }

    /// <summary>Admin End (POST /admin/instances/{wid}/end): END staff. False: no such instance.</summary>
    public bool StaffEnd(uint wid, long staffAccount, long now)
    {
        if (!_worlds.TryGetValue(wid, out var w) || w.Ending) return false;
        EndWorld(w, EndReason.Staff, now);
        return true;
    }

    /// <summary>Admin Remove: the member goes home (HOME staff) with the bar; removing the host ends the instance. False: not in it.</summary>
    public bool StaffRemove(uint wid, long accountId, long staffAccount, long now)
    {
        if (!_worlds.TryGetValue(wid, out var w) || !_byAccount.TryGetValue(accountId, out var m) || !w.Seats.ContainsKey(m)) return false;
        if (w.Host == m) EndWorld(w, EndReason.Staff, now);
        else Remove(w, m, HomeReason.Staff, now);
        return true;
    }

    // Story replaced from outside (19.8.1) ------------------------------------------------------------------------------------

    /// <summary>StoryReplaced, first half (under the lock): the keeper refuses merges, an instance the account hosts on that box ends
    /// (END staff). The relay then reloads the story off the lock and calls <see cref="StoryReloaded"/>.</summary>
    public bool StoryReplacing(long accountId, int box, long now)
    {
        if (!_keepers.TryGetValue((accountId, box), out var k)) return false;
        k.Replacing = true;
        foreach (var w in _worlds.Values.Where(x => x.Keeper == k && !x.Ending).ToList()) EndWorld(w, EndReason.Staff, now);
        Note("story_replacing", null, ("account", accountId), ("box", box));
        return true;
    }

    /// <summary>StoryReplaced, second half: the keeper takes the stored story; its owner's session gets a STORY_ACK of it.</summary>
    public void StoryReloaded(long accountId, int box, RoomStoryData story)
    {
        if (!_keepers.TryGetValue((accountId, box), out var k)) return;
        k.Replace(story);
        Note("story_replaced", null, ("account", accountId), ("box", box), ("revision", story.Revision));
        if (_byAccount.TryGetValue(accountId, out var m)) m.Peer?.Send(StoryAck(k, 0, [], receiver: m));
    }

    // Notices, member info, instance info ---------------------------------------------------------------------------------------

    /// <summary>MEMBER_INFO before a member's first mention to a receiver (and when its flags change), then the notice.</summary>
    private byte[] Notice(HubMember receiver, WorldNoticeKind kind, HubMember? about, uint argument, string text)
    {
        if (about is not null) Introduce(receiver, about);
        return new WorldNoticeMsg(kind, about?.Id ?? 0, about?.Name ?? "", argument, text).Frame();
    }

    /// <summary>The relation flags of <paramref name="about"/> for <paramref name="receiver"/> (MEMBER_INFO, PRESENCE out).</summary>
    private byte RelationOf(HubMember receiver, HubMember about)
    {
        var f = 0;
        if (receiver.Friends.Contains(about.AccountId)) f |= Relation.Friend;
        if (about.Staff && about.World is not null && _world.StaffTagWhenPlaying) f |= Relation.Staff;
        if (receiver.World is not null && receiver.World == about.World) f |= Relation.SameInstance;
        return (byte)f;
    }

    private void Introduce(HubMember receiver, HubMember about)
    {
        if (receiver.Peer is null || receiver == about) return;
        var flags = RelationOf(receiver, about);
        if (receiver.Known.TryGetValue(about.Id, out var told) && told == flags) return;
        receiver.Known[about.Id] = flags;
        receiver.Peer.Send(new MemberInfoMsg(about.Id, about.Name, about.Presence?.Level ?? 0, flags).Frame());
    }

    private void MarkInfoDirty(WorldInstance w) => w.InfoDirty = true;

    private void SendInfoAll(WorldInstance w, long now)
    {
        w.InfoDirty = false;
        foreach (var x in w.Members) SendInfo(x, now);
    }

    /// <summary>INSTANCE_INFO to a member: its instance (or wid 0 at home).</summary>
    private void SendInfo(HubMember m, long now)
    {
        if (m.Peer is null) return;
        if (m.World is not { Ending: false } w)
        {
            m.Peer.Send(InstanceInfoMsg.Home.Frame());
            return;
        }
        var rows = new List<InstanceInfoMember>();
        foreach (var x in w.Members.OrderBy(x => x == w.Host ? 0 : 1).ThenBy(x => x.Id))
        {
            Introduce(m, x);
            var flags = (x.Connected ? InstanceInfoMember.Connected : 0) | (x.Lagging ? InstanceInfoMember.Lagging : 0) |
                        (x.Staff && x != w.Host && _world.StaffTagWhenPlaying ? InstanceInfoMember.Staff : 0) | (x == w.Host ? InstanceInfoMember.Host : 0) |
                        (x.Instance is not null && x.Instance == m.Instance ? InstanceInfoMember.SameGroup : 0);
            rows.Add(new InstanceInfoMember(x.Id, StageOf(x).ToLowerInvariant(), x.Presence?.Level ?? 0, (byte)flags));
        }
        var grace = w.HostAway ? (ushort)Math.Clamp(Math.Ceiling(Ms(w.GraceUntil - now) / 1000.0), 0, ushort.MaxValue) : (ushort)0;
        var role = w.Host == m ? InstanceRole.Host : m.Staff ? InstanceRole.Staff : InstanceRole.Guest;
        m.Peer.Send(new InstanceInfoMsg(w.Wid, w.Host.Id, w.Visibility, w.Settings, w.Area ?? "", "", rows, (byte)w.Seats.Count, 0, 0, grace, role).Frame());
    }

    /// <summary>Where a member is (its stage group's stage, the stage it plays alone in, a parked or joining arrival's target).</summary>
    private static string StageOf(HubMember m) =>
        m.Instance is { Stage.Length: > 0 } i ? i.Stage :
        m.Joining is { } j ? j.Target.Stage :
        m.Arrival is { } a ? a.Stage :
        m.LoneStage ?? m.Presence?.Stage ?? "";

    // The arrival from home (19.7, 19.30.3) -------------------------------------------------------------------------------------

    /// <summary>The members' instance of an arrival (one stage group's members share it); null: from home.</summary>
    private static WorldInstance? WorldOfGroup(ArrivalGroup g) => g.Members.Select(a => a.Member.World).FirstOrDefault(w => w is not null);

    /// <summary>
    /// The candidates of an arrival from home at stage S (19.7.2, 19.30.3): the instances of S's area visible to every member of
    /// the group, with its context and data hash, not ending, with seats for the whole group; each with its stage group at S
    /// (JOIN), or a guest's NEW (a gate stage the host's story opened, or a visited stage), or greyed (story not here yet, the
    /// host reconnecting, too long to catch up), ordered by 19.7.3 with decision 40.
    /// </summary>
    public List<CandidateRow> Candidates(ArrivalGroup g, long now) => Candidates(g, now, out _, out _);

    /// <summary>The candidates, and why an instance with a stage group at the arrival stage was none (the loading screen's notice of
    /// a NEW next to it: another context, full, too long to catch up; 3.4) with the names of its members.</summary>
    public List<CandidateRow> Candidates(ArrivalGroup g, long now, out BarrierNotice notice, out List<string> names)
    {
        notice = BarrierNotice.None;
        names = [];
        var area = _areas.AreaOf(g.Stage);
        var rows = new List<CandidateRow>();
        if (area is null) return rows;
        var otherContext = new List<WorldInstance>();
        var full = new List<WorldInstance>();
        var tooLong = new List<WorldInstance>();
        var members = g.Members.Select(a => a.Member).Concat(g.CoArrivals.SelectMany(c => c.Members).Select(a => a.Member)).ToList();
        var size = members.Count;
        var data = members[0].DataHash;
        var slowest = members.Select(x => x.Rate == 0 ? (double)_inst.DefaultReplayRate : x.Rate).DefaultIfEmpty(_inst.DefaultReplayRate).Min();
        if (!_byArea.TryGetValue(area, out var inArea)) return rows;
        foreach (var w in inArea)
        {
            if (w.Ending || w.Unlisted || w.DataHash != data) continue;
            if (!members.All(x => Matching.Visible(w, x, now))) continue;
            var atStage = w.Groups.Where(x => !x.Ended && x.Logged && !x.Departing && !x.LogFull && x.Barrier is null && SameStage(x.Stage, g.Stage)).ToList();
            if (w.FreeSeats < size)
            {
                if (atStage.Count > 0) full.Add(w);
                continue;
            }
            var group = atStage.Where(x => x.FreeSlots >= size).OrderByDescending(x => x.Count).FirstOrDefault();
            if (group is not null ? !HubRules.ContextMatches(group, g.Context) : w.Context != g.Context)
            {
                if (atStage.Count > 0) otherContext.Add(w);
                continue;
            }
            var here = w.Members.Count(x => SameStage(StageOf(x), g.Stage));
            var hostFriend = members.Any(x => x.Friends.Contains(w.HostAccount));
            var friendsInside = w.Members.Count(x => members.Any(y => y.Friends.Contains(x.AccountId)));
            var ping = w.Members.Select(x => x.Rtt.Median).DefaultIfEmpty(0).Max();
            RowState state;
            var why = "";
            if (w.HostAway)
            {
                state = RowState.Greyed;
                why = "host_away";
            }
            else if (group is not null)
            {
                if (HubRules.CatchUpSeconds(CatchUpTicks(group, now), slowest) > _inst.MaxCatchUpSeconds)
                {
                    state = RowState.Greyed;
                    why = "too_long";
                    tooLong.Add(w);
                }
                else state = group.Joinable && group.Snapshot.Complete && !group.BossLock ? RowState.Joinable : RowState.Waiting;
            }
            else if (Matching.MayEnterAsGuest(w, g.Stage, _areas)) state = RowState.GuestNew;
            else
            {
                state = RowState.Greyed;
                why = "story_not_here";
            }
            rows.Add(new CandidateRow(w, group, state, here, hostFriend, friendsInside, ping, why));
        }
        var (why2, which) = tooLong.Count > 0 ? (BarrierNotice.TooLongToCatchUp, tooLong) : full.Count > 0 ? (BarrierNotice.Full, full) :
            otherContext.Count > 0 ? (BarrierNotice.OtherContext, otherContext) : (BarrierNotice.None, []);
        notice = why2;
        names = which.SelectMany(x => x.Members).Select(x => x.Name).Distinct().ToList();
        return Matching.Order(rows);
    }

    /// <summary>
    /// The decision of a fresh arrival (from home) at a stage outside the hub after its co-arrival holds (19.7.1, 19.7.6): a
    /// starting instance at the stage that may see it and fits takes it without a prompt; else round I1a's automatic join of the
    /// first candidate by 19.7.3's order (decision 40), a JOIN where the instance has a stage group at the stage or a guest's NEW
    /// (19.30.4); else a new instance of its own.
    /// </summary>
    private void FreshJoinOrNew(ArrivalGroup g, long now)
    {
        // The choice *New instance* (plan 0xFFFF) holds whatever came before this decision (a co-arrival hold nobody joined, a
        // redecision after its target ended).
        if (g.PlanNew)
        {
            New(g, now, BarrierNotice.None, []);
            return;
        }
        var rows = Candidates(g, now, out var notice, out var names);
        var starting = rows.FirstOrDefault(r => r.World.StartingUntil != 0 && r.Group is not null && r.Choosable);
        var row = starting ?? Matching.AutoJoinRow(rows);
        Note("candidates", null, ("members", g.Members.Select(a => a.Member.Id).ToArray()), ("stage", g.Stage),
            ("rows", rows.Select(r => $"{r.World.Wid}:{r.State}:{r.Here}{(r.Why.Length > 0 ? ":" + r.Why : "")}").ToArray()),
            ("chosen", row?.World.Wid ?? 0));
        if (row is null)
        {
            New(g, now, notice, names);
            return;
        }
        var w = row.World;
        g.Arrival = starting is not null ? ArrivalKind.Starting : row.Group is null ? ArrivalKind.GuestNew : ArrivalKind.AutoJoin;
        if (row.Group is null)
        {
            // A guest's NEW (19.30.4): a new stage group of the instance at the stage; the host's story replaces the engines'.
            g.World = w;
            foreach (var a in g.Members) TakeSeat(w, a.Member, now, "new_group");
            foreach (var c in g.CoArrivals.SelectMany(c => c.Members)) TakeSeat(w, c.Member, now, "coarrival");
            New(g, now, BarrierNotice.None, []);
            return;
        }
        g.World = w;
        foreach (var a in g.Members) TakeSeat(w, a.Member, now, "join");
        JoinTarget(g, row.Group, now);
    }

    /// <summary>Decides a held arrival again (its target ended or changed): inside its instance, or as a fresh arrival.</summary>
    private void Redecide(ArrivalGroup g, long now)
    {
        if (g.Members.Count == 0) return;
        if (g.Fresh || g.Arrival is ArrivalKind.AutoJoin or ArrivalKind.Starting)
        {
            // A fresh arrival's seat from an earlier choice goes; it decides anew among the instances (or makes its own).
            if (g.World is { } chosen)
                foreach (var a in g.Members)
                    if (chosen.Seats.ContainsKey(a.Member)) FreeSeat(chosen, a.Member, "left", now);
            g.World = null;
            g.Arrival = ArrivalKind.None;
            g.Target = null;
            FreshJoinOrNew(g, now);
        }
        else JoinOrNew(g, now);
    }

    /// <summary>A co-arrival base of a fresh group may take this fresh group: no block between any of them, and the base's host's
    /// default visibility lets every member of the group see the instance it will make (19.7.6: "visible to them").</summary>
    private static bool MaySeeBase(ArrivalGroup holder, ArrivalGroup g)
    {
        var host = holder.Members[0].Member;
        var inBase = holder.Members.Select(a => a.Member).Concat(holder.CoArrivals.SelectMany(c => c.Members).Select(a => a.Member)).ToList();
        foreach (var a in g.Members)
        {
            var x = a.Member;
            if (inBase.Any(y => Matching.Blocked(x, y))) return false;
            var ok = host.DefaultVisibility switch
            {
                Visibility.Public => true,
                Visibility.Friends => x.Staff || x.Friends.Contains(host.AccountId),
                _ => x.Staff,
            };
            if (!ok) return false;
        }
        return host.DataHash == g.Members[0].Member.DataHash;
    }

    /// <summary>The world part of BARRIER_OK for a member arriving in an instance (19.18): the story owner is the instance's host (his
    /// member id also after he left the world while the instance ends).</summary>
    private static BarrierWorld WorldPart(WorldInstance w, ArrivalKind arrival, StoryMode mode) =>
        new(w.Wid, w.Host.Id, w.Visibility, arrival, w.Settings, w.SettingsHash, w.Host.Id, (byte)w.Keeper.Box, mode);

    /// <summary>A barrier inside an instance: the visited set, the area check (invariant 9, 19.30.2) and the "went to" notice.</summary>
    private void ArrivedInside(WorldInstance w, ArrivalGroup g)
    {
        if (StageNames.IsHub(g.Stage)) return;
        if (w.Area is not null && _areas.AreaOf(g.Stage) is not null && !_areas.InScope(w.Area, g.Stage) && !_areas.IsHub(g.Stage))
        {
            // Not ended, not refused: a table mistake must not end a player's instance (19.30.2).
            Count("area_escape");
            Note("area_escape", null, ("wid", w.Wid), ("from", g.From.Stage), ("to", g.Stage));
            _log.LogWarning("World instance {Wid}: a barrier from {From} to {To} leaves the area {Area}", w.Wid, g.From.Stage, g.Stage, w.Area);
        }
        if (!SameStage(g.From.Stage, g.Stage))
            foreach (var a in g.Members)
                foreach (var x in w.Members)
                    if (x != a.Member) x.Peer?.Send(Notice(x, WorldNoticeKind.WentTo, a.Member, 0, g.Stage.ToLowerInvariant()));
        MarkInfoDirty(w);
    }

    // Play context, snapshot ----------------------------------------------------------------------------------------------------

    /// <summary>The achievements' context (19.8.9): null when the account was not in the world within the last minute.</summary>
    public WorldPlayContext? PlayContext(long accountId, long now)
    {
        if (!_byAccount.TryGetValue(accountId, out var m))
            return _recentContexts.TryGetValue(accountId, out var c) && Ms(now - c.At) <= 60_000 ? c.Context : null;
        return ContextOf(m, now);
    }

    /// <summary>The play context (web 14:10): at home Wid 0, IsHost true, Players 1 and the member's own settings; in an instance the
    /// most players it had in the last minute.</summary>
    private WorldPlayContext ContextOf(HubMember m, long now)
    {
        if (m.World is not { } w) return new WorldPlayContext(0, null, true, 1, m.Settings.RandomizerActive, m.DataHash, m.Settings.Difficulty);
        var players = Math.Max(w.Seats.Count, 1);
        for (var n = players + 1; n < w.SeenWithPlayers.Length; ++n)
            if (w.SeenWithPlayers[n] != 0 && Ms(now - w.SeenWithPlayers[n]) <= 60_000) players = n;
        return new WorldPlayContext(w.Wid, w.HistoryId, w.Host == m, players, w.Settings.RandomizerActive, w.DataHash, w.Settings.Difficulty);
    }

    private void RememberContext(HubMember m, long now) => _recentContexts[m.AccountId] = (ContextOf(m, now), now);

    private WorldSnapshot BuildSnapshot(long now)
    {
        var rows = new List<WorldInstanceRow>(_worlds.Count);
        foreach (var w in _worlds.Values.OrderBy(x => x.Wid))
        {
            var members = w.Members.OrderBy(x => x == w.Host ? 0 : 1).ThenBy(x => x.Id)
                .Select(x => new WorldMemberRow(x.AccountId, x.Id, x.Name, w.Seats[x].Role, StageOf(x).ToLowerInvariant(), x.Connected, x.Lagging)).ToList();
            rows.Add(new WorldInstanceRow(w.Wid, w.HistoryId, w.HostAccount, w.Host.Name, w.Keeper.Box, w.Area ?? "",
                StageOf(w.Host).ToLowerInvariant(), Visibilities.Text(w.Visibility), w.DataHash, w.Settings.Difficulty,
                w.Settings.Canonical(Wire.WorldProtocol, w.DataHash), w.Randomizer, "", w.StartedUtc, w.StartingUntil != 0, w.Ending, w.HostAway,
                members, []));
        }
        var online = _members.Values.Where(m => m.Connected).OrderBy(m => m.Id)
            .Select(m => new WorldOnlineRow(m.AccountId, m.Id, m.Name, StageOf(m).ToLowerInvariant(), m.World is { Ending: false } w ? w.Wid : 0, m.Staff))
            .ToList();
        return new WorldSnapshot(UtcNow, online.Count, rows) { Online = online };
    }

    // Presence (19.13) ---------------------------------------------------------------------------------------------------------

    /// <summary>
    /// Once a second: the presence selection of every receiver in a hub stage, computed off the lock from an immutable copy of
    /// the positions (19.17.4 (c)) and swapped in under the lock at the next pass (its per-sender index with it).
    /// </summary>
    private void HousekeepSelection(long now)
    {
        if (_selectionJob is { IsCompleted: true } done)
        {
            _selectionJob = null;
            if (done.IsCompletedSuccessfully) ApplySelection(done.Result);
        }
        if (_selectionJob is not null || Ms(now - _lastSelection) < 1000) return;
        _lastSelection = now;
        var samples = new List<PresenceSample>();
        var previous = new Dictionary<HubMember, PresenceChoice>();
        foreach (var m in _members.Values)
        {
            if (m.Peer is null || m.Presence is not { } p || p.Stage.Length == 0 || !StageNames.IsHub(p.Stage) || m.World is not null) continue;
            if (p.Stage.StartsWith("stagIS", StringComparison.OrdinalIgnoreCase)) continue; // the menu stages show nobody
            samples.Add(new PresenceSample(m, m.AccountId, p.Stage, p.X, p.Z, m.Friends, m.Blocks, m.BlockedBy));
            previous[m] = m.Choice;
        }
        var cap = Math.Max(0, _world.HubFigures);
        var near = Math.Max(0, _world.NearFigures);
        Dictionary<HubMember, PresenceChoice> Run() => PresenceSelection.Compute(samples, previous, cap, near);
        if (SelectionRunner is { } runner) _selectionJob = runner(Run);
        else ApplySelection(Run());
    }

    private void ApplySelection(Dictionary<HubMember, PresenceChoice> choices)
    {
        foreach (var m in _members.Values) m.ChosenBy.Clear();
        foreach (var m in _members.Values)
        {
            var before = m.Choice;
            m.Choice = choices.GetValueOrDefault(m) ?? PresenceChoice.Empty;
            // Figures no longer drawn go at once (their last presence would stay two seconds otherwise).
            foreach (var gone in before.All)
                if (!m.Choice.All.Contains(gone)) m.Peer?.Send(GoneFigure(gone));
        }
        foreach (var (receiver, choice) in choices)
        {
            if (!_members.ContainsKey(receiver.Index)) continue;
            foreach (var sender in choice.All) sender.ChosenBy[receiver] = choice.Near.Contains(sender);
        }
        Count("presence_selections");
    }

    /// <summary>
    /// A member's presence (19.13): in a hub stage to the receivers that chose it (the nearest every packet, the others every second
    /// one, within each receiver's presence budget); in a field only to the own instance's members in the same stage and another
    /// stage group (their name tags). A member that left a stage is hidden at once from those that drew it there.
    /// </summary>
    private void FanWorldPresence(HubMember m, PresenceMsg msg, Presence2Msg? msg2, long now)
    {
        // One frame per relation value and kind for this sender's message, not one per receiver (M4: the CPU per frame).
        Array.Clear(_presenceFrames);
        var left = m.Presence?.Stage;
        var moved = !string.IsNullOrEmpty(left) && !SameStage(left, msg.Stage);
        m.Presence = msg;
        m.Presence2 = msg2;
        m.PresenceAt = now;
        var seq = ++m.PresenceSeq;
        if (moved)
        {
            foreach (var r in m.ChosenBy.Keys)
                if (r.Presence is not { } rp || !SameStage(rp.Stage, msg.Stage)) SendPresence(r, m, msg, msg2, now);
            if (StageNames.IsHub(msg.Stage) == false) m.ChosenBy.Clear();
        }
        if (msg.Stage.Length > 0 && StageNames.IsHub(msg.Stage))
        {
            if (m.World is not null) return;
            foreach (var (r, near) in m.ChosenBy)
            {
                if (r.Peer is null || r.Presence is not { } rp || !SameStage(rp.Stage, msg.Stage)) continue;
                if (!near && (seq & 1) == 0)
                {
                    Count("presence_tier_skipped");
                    continue;
                }
                SendPresence(r, m, msg, msg2, now);
            }
            return;
        }
        if (m.World is not { } w) return;
        foreach (var r in w.Members)
        {
            if (r == m || r.Peer is null || r.Presence is null) continue;
            if (!SameStage(r.Presence.Stage, msg.Stage) && !(moved && left is not null && SameStage(r.Presence.Stage, left))) continue;
            if (r.Instance is not null && r.Instance == m.Instance) continue;
            SendPresence(r, m, msg, msg2, now);
        }
    }

    private readonly byte[]?[] _presenceFrames = new byte[]?[32];

    private void SendPresence(HubMember r, HubMember m, PresenceMsg msg, Presence2Msg? msg2, long now)
    {
        Introduce(r, m);
        var relation = RelationOf(r, m);
        var v2 = msg2 is not null && r.PresenceV2;
        ref var cached = ref _presenceFrames[(relation & 15) | (v2 ? 16 : 0)];
        var frame = cached ??= v2 ? new Presence2OutMsg(m.Id, msg2!, relation).Frame() : new PresenceOutMsg(m.Id, msg, relation).Frame();
        // The receiver's presence budget (World:PresenceBytesPerSecond), apart from the game frames' bucket.
        var cap = Math.Max(1, _world.PresenceBytesPerSecond);
        r.PresenceBudget = double.IsNaN(r.PresenceBudget) ? cap : Math.Min(cap, r.PresenceBudget + Ms(now - r.PresenceBudgetAt) / 1000.0 * cap);
        r.PresenceBudgetAt = now;
        if (r.PresenceBudget < frame.Length)
        {
            Count("presence_budget_dropped");
            return;
        }
        r.PresenceBudget -= frame.Length;
        r.Peer!.SendSoon(frame);
        Count("presence_out");
    }
}
