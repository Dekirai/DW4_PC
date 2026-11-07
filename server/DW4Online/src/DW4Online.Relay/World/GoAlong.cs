using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;

namespace DW4Online.Relay.World
{
    /// <summary>
    /// A go-along offer (round I1b, SEAMLESS.md 19.30.5): one per instance and dungeon, opened when a member walks (or is taken along)
    /// from a field into a dungeon stage. It follows its entrants inside the dungeon (its target: the stage of the first entrant still
    /// in it and the point its stage group entered through) and closes when the last of them left the dungeon or the instance ends.
    /// </summary>
    public sealed class GoOffer(ushort id, string dungeon, WorldInstance world, long openedAt)
    {
        public ushort Id { get; } = id;
        /// <summary>The areas.json dungeon key (dng204, zone_x, dngq1, ...).</summary>
        public string Dungeon { get; } = dungeon;
        public WorldInstance World { get; } = world;
        public long OpenedAt { get; set; } = openedAt;
        public bool Open { get; set; } = true;
        public long ClosedAt { get; set; }
        /// <summary>The entrants, the first entrant first (by a mat, or by going along).</summary>
        public List<HubMember> Entrants { get; } = [];
        /// <summary>Every member the offer named: a re-entry of one of them within World:OfferReopenSeconds after the close reopens it.</summary>
        public HashSet<HubMember> Named { get; } = [];
        /// <summary>Entrants whose way out of the dungeon was a game over (the barrier's alive bit clear): "Kim was defeated in ...".</summary>
        public HashSet<HubMember> LeftDefeated { get; } = [];
        /// <summary>The target as last told: the first entrant's stage and its stage group's entrance point.</summary>
        public string Stage { get; set; } = "";
        public byte Point { get; set; }
        /// <summary>The entrants' barrier context (a receiver needs it: a quest's context, 3.7).</summary>
        public ulong Context { get; set; }
        /// <summary>Members that got OFFER open (the row is on their *Instance* page).</summary>
        public HashSet<HubMember> Receivers { get; } = [];
        /// <summary>Members at a barrier, loading or away when it opened: a late notice once they are in a field.</summary>
        public HashSet<HubMember> Late { get; } = [];
        /// <summary>Members in a dungeon when it opened: the row without a notice once they come out into a field.</summary>
        public HashSet<HubMember> Silent { get; } = [];
        /// <summary>The members the exit port asked on the mat for an entry (HEADING's asked mask): no notice for that entry.</summary>
        public HashSet<HubMember> Asked { get; } = [];
    }

    /// <summary>A member's go-along request (19.30.7): waiting for the target's floor and its own stage group, then stamped.</summary>
    public sealed class GoRequest(GoOffer offer, long since)
    {
        public GoOffer Offer { get; } = offer;
        /// <summary>The wait's start (a boss fight keeps it at the current time: no limit while the target is boss-locked).</summary>
        public long Since { get; set; } = since;
        public bool Stamped { get; set; }
        /// <summary>The GO's target as stamped (the offer's target at the stamp).</summary>
        public string Stage { get; set; } = "";
        public byte Point { get; set; }
        public HubInstance? StampedIn { get; set; }
        public uint StampTick { get; set; }
        public bool Boss { get; set; }
    }
}

namespace DW4Online.Relay.Hub
{
    using DW4Online.Relay.World;

    /// <summary>
    /// Round I1b's go along (SEAMLESS.md 19.30.5-19.30.7, i1b-contract.md "go 2026-11-07 16:15"): the offers (made, extended, followed,
    /// closed, reopened silently), OFFER to the members a field holds, INTENT 4 and its withdraw, the wait before the stamp (the target
    /// joinable with a living member and no boss lock, the sender's stage group sealing and membership-safe or solo-led), GO stamped
    /// sealed or through JOIN_REQ kind go, the GO's barrier (the offer's stage group first, its join hold without
    /// Instances:JoinHoldSeconds, never a second stage group next to a living one), the expiry backstop, HEADING's asked mask.
    /// Everything runs under the hub lock; offers change only at barrier decisions, STATEs, INTENTs and the housekeeping.
    /// </summary>
    public sealed partial class RoomHub
    {
        /// <summary>An instance holds at most four open offers (four members in four dungeons).</summary>
        public const int MaxOffers = 4;
        /// <summary>A GO not applied this many ticks after its tick expires on every engine (a constant of the event, 19.30.7).</summary>
        public const uint GoExpiryTicks = 300;

        private string? DungeonOf(string? stage) => _areas.Find(stage) is { IsDungeon: true } s ? s.Dungeon ?? s.Stage : null;

        private bool IsField(string? stage) => !string.IsNullOrEmpty(stage) && _areas.Find(stage) is { IsHub: false, IsDungeon: false };

        /// <summary>Where a member of an instance is: its parked barrier's target, its join's target, its stage group (settled: in a stage
        /// group of the instance, not parked, not joining).</summary>
        private static (string Stage, byte Point, HubInstance? Group, bool Settled) PlaceOf(HubMember m)
        {
            if (m.Arrival is { } a) return (a.Stage, a.Point, null, false);
            if (m.Joining is { } j) return (j.Target.Stage, j.Target.EntryPoint, j.Target, false);
            if (m.Instance is { Ended: false } i && m.Slot >= 0 && i.Slots[m.Slot] == m) return (i.Stage, i.EntryPoint, i, i.Mode != InstanceMode.Hub);
            if (m.LoneStage is { } lone) return (lone, 0, null, false);
            return ("", 0, null, false);
        }

        /// <summary>The offer's entrants still in its dungeon, the first entrant first.</summary>
        private List<HubMember> InDungeon(GoOffer o) =>
            o.Entrants.Where(e => e.World == o.World && DungeonOf(PlaceOf(e).Stage) == o.Dungeon).ToList();

        /// <summary>
        /// A barrier inside an instance (before its decision): an entrant's way out of its dungeon (a game over noted); an entry from a
        /// field into a dungeon makes, extends or silently reopens that dungeon's offer; a stamped GO's barrier (its stage and point)
        /// marks the arriving group and joins the offer's entrants silently. Called again for a redecided group: idempotent.
        /// </summary>
        private void GoAtBarrier(WorldInstance w, ArrivalGroup g, long now)
        {
            var toDungeon = DungeonOf(g.Stage);
            var fromDungeon = DungeonOf(g.From.Stage);
            foreach (var o in w.Offers.Where(o => o.Open && o.Dungeon == fromDungeon && o.Dungeon != toDungeon))
                foreach (var a in g.Members.Where(a => o.Entrants.Contains(a.Member)))
                {
                    if (((g.Alive >> a.Slot) & 1) == 0) o.LeftDefeated.Add(a.Member);
                    else o.LeftDefeated.Remove(a.Member);
                }
            var members = g.Members.Select(a => a.Member).ToList();
            // The GO's own barrier (19.30.7 step 4): matched by its stage and point.
            var goers = members.Where(x => x.Go is { Stamped: true } r && SameStage(r.Stage, g.Stage) && r.Point == g.Point).ToList();
            if (goers.Count > 0)
            {
                g.Go ??= goers[0].Go!.Offer;
                foreach (var x in goers)
                {
                    Note("go_barrier", g.From, ("wid", w.Wid), ("member", x.Id), ("offer", x.Go!.Offer.Id), ("stage", g.Stage.ToLowerInvariant()));
                    x.Go = null;
                }
            }
            if (toDungeon is not null && IsField(g.From.Stage) && !StageNames.IsHub(g.Stage))
            {
                var silent = g.Go is not null;
                var o = w.Offers.FirstOrDefault(x => x.Open && x.Dungeon == toDungeon);
                if (o is null)
                {
                    var reopen = w.Offers.Where(x => !x.Open && x.Dungeon == toDungeon && members.Any(x.Named.Contains) &&
                                                     Ms(now - x.ClosedAt) <= _world.OfferReopenSeconds * 1000.0)
                        .OrderByDescending(x => x.ClosedAt).FirstOrDefault();
                    if (reopen is not null) ReopenOffer(reopen, members, g, now);
                    else if (w.Offers.Count(x => x.Open) < MaxOffers) OpenOffer(w, toDungeon, members, g, silent, now);
                    else Count("offers_full");
                }
                else
                {
                    var newcomers = members.Where(x => !o.Entrants.Contains(x)).ToList();
                    if (newcomers.Count > 0) ExtendOffer(o, newcomers, g, silent, now);
                }
            }
            UpdateOffers(w, now);
        }

        private void OpenOffer(WorldInstance w, string dungeon, List<HubMember> entrants, ArrivalGroup g, bool silent, long now)
        {
            var id = w.NextOfferId;
            while (w.Offers.Any(x => x.Id == id) || id == 0) id = (ushort)(id + 1);
            w.NextOfferId = (ushort)(id == ushort.MaxValue ? 1 : id + 1);
            var o = new GoOffer(id, dungeon, w, now) { Stage = g.Stage, Point = g.Point, Context = g.Context };
            o.Entrants.AddRange(entrants);
            o.Named.UnionWith(entrants);
            foreach (var x in entrants) o.Asked.UnionWith(x.HeadingAsked);
            w.Offers.Add(o);
            Count("offers_opened");
            Note("offer_open", null, ("wid", w.Wid), ("offer", o.Id), ("dungeon", dungeon), ("entrants", entrants.Select(x => x.Id).ToArray()),
                ("stage", g.Stage.ToLowerInvariant()), ("point", g.Point));
            foreach (var x in w.Members.ToList())
                if (!o.Entrants.Contains(x)) OfferToMember(o, x, silent ? (byte)0 : OfferMsg.Notice, now);
        }

        /// <summary>A member who goes into the same dungeon again within World:OfferReopenSeconds after the offer that named him closed: open
        /// again on the *Instance* pages, with no notice and no closing line for anyone (19.30.5).</summary>
        private void ReopenOffer(GoOffer o, List<HubMember> entrants, ArrivalGroup g, long now)
        {
            o.Open = true;
            o.ClosedAt = 0;
            o.Entrants.Clear();
            o.Entrants.AddRange(entrants);
            o.Named.UnionWith(entrants);
            o.LeftDefeated.Clear();
            o.Receivers.Clear();
            o.Late.Clear();
            o.Silent.Clear();
            o.Stage = g.Stage;
            o.Point = g.Point;
            o.Context = g.Context;
            Count("offers_reopened");
            Note("offer_reopen", null, ("wid", o.World.Wid), ("offer", o.Id), ("entrants", entrants.Select(x => x.Id).ToArray()));
            foreach (var x in o.World.Members.ToList())
                if (!o.Entrants.Contains(x)) OfferToMember(o, x, OfferMsg.Reopened, now);
        }

        /// <summary>A second group goes into the open offer's dungeon: it joins the entrants and the notice shows again, with the new names,
        /// to the members that did not go along (not for a re-entry of a member it named, nor for one who came by *Go along*).</summary>
        private void ExtendOffer(GoOffer o, List<HubMember> newcomers, ArrivalGroup g, bool silent, long now)
        {
            var reentry = newcomers.All(o.Named.Contains);
            o.Entrants.AddRange(newcomers);
            o.Named.UnionWith(newcomers);
            var asked = newcomers.SelectMany(x => x.HeadingAsked).ToHashSet();
            Note("offer_extend", null, ("wid", o.World.Wid), ("offer", o.Id), ("entrants", o.Entrants.Select(x => x.Id).ToArray()),
                ("silent", silent || reentry));
            foreach (var x in newcomers)
            {
                // (Over for them: they are in the dungeon now.)
                if (o.Receivers.Remove(x)) SendOffer(o, x, OfferOp.Closed, 0, OfferClose.Gone);
                o.Late.Remove(x);
                o.Silent.Remove(x);
            }
            foreach (var x in o.Receivers.ToList())
                SendOffer(o, x, OfferOp.Changed, silent || reentry || asked.Contains(x) ? (byte)0 : OfferMsg.Notice);
            _ = g;
            _ = now;
        }

        /// <summary>The offer for a member that is not one of its entrants (19.30.5): OFFER open now in a field (the notice unless the port
        /// asked it), later otherwise (a late notice after a barrier, a load or the host's grace; the row without a notice for a member in a
        /// dungeon when it opened); nothing for another context.</summary>
        private void OfferToMember(GoOffer o, HubMember x, byte flags, long now)
        {
            if (x.World != o.World || x.LeftWorld) return;
            var p = PlaceOf(x);
            if (DungeonOf(p.Stage) is not null)
            {
                o.Silent.Add(x);
                return;
            }
            if (!x.Connected || !p.Settled)
            {
                if ((flags & OfferMsg.Notice) != 0) o.Late.Add(x);
                else o.Silent.Add(x);
                return;
            }
            if (!IsField(p.Stage) || p.Group is null || !HubRules.ContextMatches(p.Group, o.Context)) return;
            if (o.Asked.Contains(x)) flags = (byte)(flags & ~OfferMsg.Notice);
            o.Receivers.Add(x);
            SendOffer(o, x, OfferOp.Open, flags);
            _ = now;
        }

        private void SendOffer(GoOffer o, HubMember x, OfferOp op, byte flags, OfferClose close = OfferClose.None)
        {
            if (x.Peer is null) return;
            var inside = InDungeon(o);
            var names = (inside.Count > 0 ? inside : o.Entrants).Take(Wire.MaxSlots).ToList();
            foreach (var e in names) Introduce(x, e);
            if (op != OfferOp.Closed && x.Go is { } r && r.Offer == o)
                flags |= (byte)((r.Stamped ? OfferMsg.Stamped : OfferMsg.Waiting) | (r.Boss ? OfferMsg.Boss : 0));
            x.Peer.Send(new OfferMsg(o.Id, op, flags, names.Select(e => e.Id).ToList(), o.Stage.ToLowerInvariant(),
                (byte)Math.Clamp(_world.AnswerNoticeSeconds, 0, 255), close).Frame());
            if ((flags & OfferMsg.Notice) != 0)
                Note("offer_notice", null, ("wid", o.World.Wid), ("offer", o.Id), ("member", x.Id), ("late", (flags & OfferMsg.Late) != 0));
        }

        private void CloseOffer(GoOffer o, OfferClose reason, long now)
        {
            if (!o.Open) return;
            o.Open = false;
            o.ClosedAt = now;
            Count("offers_closed");
            Note("offer_close", null, ("wid", o.World.Wid), ("offer", o.Id), ("reason", reason.ToString().ToLowerInvariant()));
            foreach (var x in o.Receivers.ToList()) SendOffer(o, x, OfferOp.Closed, 0, reason);
            o.Receivers.Clear();
            o.Late.Clear();
            o.Silent.Clear();
            // Waiting requests end; a GO already stamped still runs and lands in a new stage group of the instance there (19.30.8).
            foreach (var x in o.World.Members.ToList())
                if (x.Go is { Stamped: false } r && r.Offer == o) EndGo(x, reason == OfferClose.Ended ? GoRefusal.Ended : GoRefusal.Closed, now);
        }

        /// <summary>
        /// The offers of an instance after anything that moves its members (a barrier, a load, a leave, the housekeeping): each open
        /// offer follows its entrants (OFFER changed to its receivers when the first entrant's stage changed) and closes when none of
        /// them is in the dungeon; a receiver that entered a dungeon or left gets its close (gone); a member it waited for that is now in
        /// a field gets the offer (late notice or none); the requests are checked; closed offers are kept for the reopen time.
        /// </summary>
        private void UpdateOffers(WorldInstance w, long now)
        {
            if (w.Ending || !_worlds.ContainsKey(w.Wid))
            {
                foreach (var o in w.Offers.Where(o => o.Open).ToList()) CloseOffer(o, OfferClose.Ended, now);
                foreach (var x in w.Members.ToList())
                    if (x.Go is not null) EndGo(x, GoRefusal.Ended, now);
                return;
            }
            foreach (var o in w.Offers.ToList())
            {
                if (!o.Open)
                {
                    if (Ms(now - o.ClosedAt) > _world.OfferReopenSeconds * 1000.0) w.Offers.Remove(o);
                    continue;
                }
                o.Entrants.RemoveAll(e => e.World != w);
                var inside = InDungeon(o);
                if (inside.Count == 0)
                {
                    var defeated = o.Entrants.Count > 0 && o.Entrants.All(o.LeftDefeated.Contains);
                    CloseOffer(o, defeated ? OfferClose.Defeated : OfferClose.Left, now);
                    continue;
                }
                var (stage, point, _, _) = PlaceOf(inside[0]);
                if (!SameStage(stage, o.Stage) || point != o.Point)
                {
                    o.Stage = stage;
                    o.Point = point;
                    Note("offer_move", null, ("wid", w.Wid), ("offer", o.Id), ("stage", stage.ToLowerInvariant()), ("point", point));
                    foreach (var x in o.Receivers.ToList()) SendOffer(o, x, OfferOp.Changed, 0);
                }
                foreach (var x in o.Receivers.ToList())
                {
                    if (x.World != w)
                    {
                        o.Receivers.Remove(x);
                        continue;
                    }
                    if (DungeonOf(PlaceOf(x).Stage) is not null && !o.Entrants.Contains(x))
                    {
                        // It entered a dungeon itself (another one, or this one by its own mat: then it is an entrant already).
                        o.Receivers.Remove(x);
                        SendOffer(o, x, OfferOp.Closed, 0, OfferClose.Gone);
                    }
                }
                foreach (var x in o.Late.Concat(o.Silent).ToList())
                {
                    if (x.World != w)
                    {
                        o.Late.Remove(x);
                        o.Silent.Remove(x);
                        continue;
                    }
                    var p = PlaceOf(x);
                    if (!x.Connected || !p.Settled) continue;
                    if (DungeonOf(p.Stage) is not null)
                    {
                        // A late notice never reaches a member that arrived in a dungeon; it finds the row when it comes out.
                        if (o.Late.Remove(x)) o.Silent.Add(x);
                        continue;
                    }
                    if (!IsField(p.Stage) || p.Group is null || !HubRules.ContextMatches(p.Group, o.Context)) continue;
                    var late = o.Late.Remove(x);
                    o.Silent.Remove(x);
                    o.Receivers.Add(x);
                    SendOffer(o, x, OfferOp.Open, late && !o.Asked.Contains(x) ? (byte)(OfferMsg.Notice | OfferMsg.Late) : (byte)0);
                }
            }
            TryGo(w, now);
        }

        // Requests (19.30.7) --------------------------------------------------------------------------------------------------------

        /// <summary>INTENT 4: go along with offer <c>argument</c>, or (0) withdraw the own waiting request / report the own GO expired.</summary>
        private void OnGoIntent(HubMember m, IntentMsg msg, long now)
        {
            if (msg.Argument == 0)
            {
                if (m.Go is not { } r) return;
                if (!r.Stamped) EndGo(m, null, now, "withdrawn");
                else
                {
                    m.Go = null;
                    Note("go_end", null, ("wid", r.Offer.World.Wid), ("offer", r.Offer.Id), ("member", m.Id), ("reason", "expired"));
                    if (r.Offer.Open && r.Offer.Receivers.Contains(m)) SendOffer(r.Offer, m, OfferOp.Changed, 0);
                }
                return;
            }
            var w = m.World;
            var refuse = (GoRefusal reason, GoOffer? about) =>
            {
                Count("go_refused");
                m.Peer?.Send(Notice(m, WorldNoticeKind.GoRefused, about?.Entrants.FirstOrDefault(), (uint)reason, about?.Stage.ToLowerInvariant() ?? ""));
            };
            if (m.Go is not null)
            {
                refuse(GoRefusal.NotOffered, null); // (one go-along per member)
                return;
            }
            if (w is null || w.Ending)
            {
                refuse(w is null ? GoRefusal.NotOffered : GoRefusal.Ended, null);
                return;
            }
            var o = w.Offers.FirstOrDefault(x => x.Id == msg.Argument);
            if (o is null || !o.Open)
            {
                refuse(GoRefusal.Closed, o);
                return;
            }
            var p = PlaceOf(m);
            if (!o.Receivers.Contains(m) || DungeonOf(p.Stage) is not null || StageNames.IsHub(p.Stage))
            {
                refuse(GoRefusal.NotOffered, o);
                return;
            }
            m.Go = new GoRequest(o, now);
            Count("go_requests");
            Note("go_request", null, ("wid", w.Wid), ("offer", o.Id), ("member", m.Id));
            TryGo(w, now);
            if (m.Go is { Stamped: false }) SendOffer(o, m, OfferOp.Changed, 0);
        }

        /// <summary>A request ends without a GO: the reason as WORLD_NOTICE go refused (none for a withdraw), its row's flags cleared.</summary>
        private void EndGo(HubMember m, GoRefusal? reason, long now, string? why = null)
        {
            if (m.Go is not { } r) return;
            m.Go = null;
            var o = r.Offer;
            Note("go_end", null, ("wid", o.World.Wid), ("offer", o.Id), ("member", m.Id),
                ("reason", why ?? reason switch
                {
                    GoRefusal.Closed => "closed", GoRefusal.Defeated => "defeated", GoRefusal.Busy => "busy", GoRefusal.Moved => "moved",
                    GoRefusal.Ended => "ended", _ => "refused",
                }));
            if (reason is { } why2)
                m.Peer?.Send(Notice(m, WorldNoticeKind.GoRefused, o.Entrants.FirstOrDefault(), (uint)why2, o.Stage.ToLowerInvariant()));
            if (o.Open && o.Receivers.Contains(m)) SendOffer(o, m, OfferOp.Changed, 0);
            _ = now;
        }

        /// <summary>
        /// The wait before the stamp (19.30.7 step 2): a request is stamped when the target (the stage group of the offer's first entrant
        /// still in the dungeon) is joinable with a living member and no boss lock, and the sender's own stage group can take the event
        /// now (sealed: sealing and membership-safe, stamped like an INTENT; solo-led and membership-safe: JOIN_REQ kind go). Requests of
        /// one stage group that become ready together are stamped at one tick. It ends after World:GoAlongWaitSeconds of waiting for any
        /// reason but a boss fight, and when the sender's group arrived in a dungeon or at home.
        /// </summary>
        private void TryGo(WorldInstance w, long now)
        {
            Dictionary<HubInstance, uint>? ticks = null;
            foreach (var m in w.Members.ToList())
            {
                if (m.Go is not { } r) continue;
                if (r.Stamped)
                {
                    GoBackstop(m, r, now);
                    continue;
                }
                var o = r.Offer;
                if (!o.Open)
                {
                    EndGo(m, GoRefusal.Closed, now);
                    continue;
                }
                var place = PlaceOf(m);
                if (place.Settled && !IsField(place.Stage))
                {
                    EndGo(m, GoRefusal.Moved, now);
                    continue;
                }
                var first = InDungeon(o).FirstOrDefault();
                var target = first is null ? null : PlaceOf(first) is { Settled: true, Group: { } tg } ? tg : null;
                var boss = target?.BossLock == true;
                if (boss != r.Boss)
                {
                    r.Boss = boss;
                    if (o.Receivers.Contains(m)) SendOffer(o, m, OfferOp.Changed, 0);
                }
                if (boss) r.Since = now; // (no time limit in a boss fight)
                var targetReady = target is { Ended: false, Barrier: null, Joinable: true, BossLock: false, NoLiving: false, LogFull: false } &&
                                  target.Snapshot.Complete && target.World == w;
                var s = place.Group;
                var senderReady = m.Connected && place.Settled && s is { Ended: false, Barrier: null, MembershipSafe: true } &&
                                  ((s.Mode == InstanceMode.Sealed && s.Sealing && s.Streams[m.Slot] is not null) ||
                                   (s.Mode == InstanceMode.SoloLed && s.Leader == m && s.SoloJoin is null && s.AskedJoin is null && s.AskedHome is null &&
                                    s.AskedGo is null && s.PendingHome is null));
                if (targetReady && senderReady)
                {
                    StampGo(m, r, s!, target!, ticks ??= [], now);
                    continue;
                }
                if (!boss && Ms(now - r.Since) >= _world.GoAlongWaitSeconds * 1000.0) EndGo(m, GoRefusal.Busy, now);
            }
        }

        private void StampGo(HubMember m, GoRequest r, HubInstance s, HubInstance target, Dictionary<HubInstance, uint> ticks, long now)
        {
            r.Stamped = true;
            r.Stage = target.Stage;
            r.Point = target.EntryPoint;
            r.StampedIn = s;
            string how;
            if (s.Mode == InstanceMode.Sealed)
            {
                if (!ticks.TryGetValue(s, out var t)) ticks[s] = t = StampTick(s);
                s.AddEvent(t, TickEvent.Go(m.Slot, r.Point, r.Stage));
                r.StampTick = t;
                how = "sealed";
            }
            else
            {
                var ld = DelayController.FromRoundTrips(m.Rtt.All, _sealing.MaxDelayTicks);
                s.AskedGo = (m.Slot, r.Stage, r.Point, now);
                m.Peer?.Send(new JoinReqMsg(s.Id, s.Visit, (byte)m.Slot, (byte)ld, JoinReqKind.Go, r.Stage, r.Point).Frame());
                how = "join_req";
            }
            Count("go_stamped");
            Note("go_stamped", s, ("wid", r.Offer.World.Wid), ("offer", r.Offer.Id), ("member", m.Id), ("slot", m.Slot), ("tick", r.StampTick),
                ("stage", r.Stage.ToLowerInvariant()), ("point", r.Point), ("how", how), ("waited_ms", (long)Ms(now - r.Since)));
            if (r.Offer.Open && r.Offer.Receivers.Contains(m)) SendOffer(r.Offer, m, OfferOp.Changed, 0);
        }

        /// <summary>
        /// A stamped GO that never led to its barrier (19.30.7 step 3): the client withdraws after its expiry (INTENT 4, 0); else the request
        /// is cleared once the stage group's stream is sealed five seconds past the expiry tick without the member's barrier, when the member
        /// left that stage group otherwise, or when a JOIN_REQ go went unanswered (asked again a few times first).
        /// </summary>
        private void GoBackstop(HubMember m, GoRequest r, long now)
        {
            if (r.StampedIn is not { } s) return;
            if (m.Arrival is not null || m.Joining is not null) return; // (at its barrier: GoAtBarrier takes it)
            // (A fork before the GO applied: the GO followed its slot into the child, 19.18.)
            if (m.Instance is { Ended: false } child && child != s && child.Parent == s.Id) r.StampedIn = s = child;
            var why = "";
            if (s.Ended || m.Instance != s) why = "moved";
            else if (s.AskedGo is { } ag)
            {
                if (Ms(now - ag.AskedAt) > 5000)
                {
                    s.AskedGo = null;
                    r.Stamped = false; // (asked again at the next ready pass)
                    Count("go_req_timeout");
                }
                return;
            }
            else if (s.Mode == InstanceMode.Sealed && r.StampTick != 0 && s.NextSeal > r.StampTick + GoExpiryTicks + (uint)(5.0 / Wire.TickSeconds))
                why = "expired";
            if (why.Length == 0) return;
            m.Go = null;
            Count("go_backstop");
            Note("go_end", s, ("wid", r.Offer.World.Wid), ("offer", r.Offer.Id), ("member", m.Id), ("reason", why));
            if (r.Offer.Open && r.Offer.Receivers.Contains(m)) SendOffer(r.Offer, m, OfferOp.Changed, 0);
        }

        /// <summary>JOIN_AT after JOIN_REQ kind go: from J the stage group is sealed with its leader alone; SEALED(J) carries GO and
        /// LEAD_FROM(J + 1), so the leader leads alone again (as HOME, 19.6.5).</summary>
        private bool GoJoinAt(HubInstance i, HubMember m, JoinAtMsg msg, long now)
        {
            if (i.AskedGo is not { } go || i.SoloJoin is not null || i.AskedJoin is not null) return false;
            i.AskedGo = null;
            var current = CurrentTick(i, now);
            var j = Math.Max(msg.Tick, i.Log.Count);
            var reach = Math.Max(now + (long)((double)Math.Max(0, (long)j - current) * Wire.TickSeconds * Freq) - FromMs(m.Rtt.Median / 2), now + TickTs);
            i.S0 = reach - (long)(j * Wire.TickSeconds * Freq);
            i.NextSeal = j;
            i.SealedFrom = j;
            i.LogOwner = m;
            i.Mode = InstanceMode.Sealed;
            i.Streams[m.Slot] = NewStream(m, m.Slot, j, DelayController.FromRoundTrips(m.Rtt.All, _sealing.MaxDelayTicks));
            m.Cursor = j;
            i.AddEvent(j, TickEvent.Go(go.Slot, go.Point, go.Stage));
            ScheduleLeadFrom(i, j + 1);
            if (m.Go is { } r) r.StampTick = j;
            Count("go_stamped_solo");
            Note("go_join_at", i, ("tick", j), ("slot", go.Slot), ("stage", go.Stage.ToLowerInvariant()), ("wid", i.World?.Wid ?? 0));
            i.Sealing = true;
            _sealer?.Invoke();
            return true;
        }

        /// <summary>HEADING's asked mask (19.30.5): the slots the exit port asked on the mat for this departure that stay, resolved to members
        /// against the stage group the port ran in (an exit fork's parent when the sender is already in its child).</summary>
        private void HeadingAsked(HubMember m, HeadingMsg msg)
        {
            m.HeadingAsked = [];
            if (msg.Asked == 0 || m.Instance is not { } g) return;
            var port = g.Parent is { } pid && _instances.TryGetValue(pid, out var parent) && !parent.Ended ? parent : g;
            for (var s = 0; s < Wire.MaxSlots; ++s)
                if ((msg.Asked & (1 << s)) != 0 && port.Slots[s] is { } x && x != m) m.HeadingAsked.Add(x);
        }

        /// <summary>A member's seat frees (home, left, removed): its request ends, it leaves the offers' lists and entrants.</summary>
        private void GoSeatFreed(WorldInstance w, HubMember m, long now)
        {
            m.Go = null;
            foreach (var o in w.Offers)
            {
                o.Receivers.Remove(m);
                o.Late.Remove(m);
                o.Silent.Remove(m);
                o.Asked.Remove(m);
            }
            if (_worlds.ContainsKey(w.Wid) && w.Offers.Count > 0) UpdateOffers(w, now);
        }

        /// <summary>*B: Back to the lobby* on a GO's held loading screen (19.30.7 step 4): the member's *Leave instance* from its barrier, a
        /// DISSOLVE home (the host's ends the instance first).</summary>
        private bool GoBackToLobby(HubMember m, long now)
        {
            if (m.Arrival is not { Go: not null } g || !_holds.Contains(g)) return false;
            _holds.Remove(g);
            g.Hold = HoldKind.None;
            if (m.World is { } w && w.Host == m) EndWorld(w, EndReason.HostLeft, now);
            Count("go_back_to_lobby");
            Note("go_back", g.From, ("member", m.Id), ("wid", m.World?.Wid ?? 0));
            Dissolve(g, now, redirectHome: true);
            return true;
        }
    }
}
