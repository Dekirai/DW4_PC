using DW4Online.Domain.Story;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Relay.World
{
    /// <summary>
    /// A member's gate-form plan (SEAMLESS.md 19.7.4): the PLAN the relay answered with OPTIONS, the member's CHOICE for it, and PLAN's
    /// throttle (one a second). The BARRIER that names the plan id takes the choice; a disconnect, a new PLAN or that BARRIER ends it.
    /// </summary>
    public sealed class PromptPlan
    {
        /// <summary>The open plan's id (0: none).</summary>
        public ushort Plan { get; set; }
        public PlanPurpose Purpose { get; set; }
        public string Stage { get; set; } = "";
        public byte Point { get; set; }
        public ulong Context { get; set; }
        /// <summary>The member's answer for the open plan (null: none yet, or nothing to choose).</summary>
        public ChoiceMsg? Choice { get; set; }
        /// <summary>The last PLAN taken (the throttle: at most one a second, 19.7.4).</summary>
        public long LastPlanAt { get; set; }
        /// <summary>The last OPTIONS payload sent for the open plan and when (the refresh compares it).</summary>
        public byte[]? Options { get; set; }
        public long OptionsAt { get; set; }
        /// <summary>Rows were offered (an overlay is open on the client until its CHOICE).</summary>
        public bool Offered { get; set; }
        /// <summary>The host names of the rows offered (the refreshed prompt names a chosen instance that ended meanwhile).</summary>
        public Dictionary<uint, string> Hosts { get; set; } = [];

        /// <summary>The member chose in the gate form (a row or *New instance*): no automatic co-arrival base waits for it, and it joins
        /// no foreign base (19.7.6: "did not choose otherwise in a prompt").</summary>
        public bool Chose => Plan != 0 && Choice is not null;

        public void Clear()
        {
            Plan = 0;
            Choice = null;
            Options = null;
            Offered = false;
            Hosts = [];
        }
    }
}

namespace DW4Online.Relay.Hub
{
    /// <summary>
    /// Round I1b, the arrival prompt (SEAMLESS.md 19.7.3-19.7.7, decision 40): PLAN answered by OPTIONS (the gate form), the prompt hold
    /// of a fresh arrival's barrier (the loading-screen form, HoldKind.Prompt, no time limit), CHOICE with its re-check at the decision,
    /// and the refresh of an open prompt at most once a second. Candidates, the order and the preselection are round I1a's
    /// (<see cref="Candidates(ArrivalGroup, long)"/>, <see cref="Matching"/>).
    /// </summary>
    public sealed partial class RoomHub
    {
        // PLAN (the gate form) ---------------------------------------------------------------------------------------------

        /// <summary>A member at home holds its confirming press and asks what the departure would find (19.7.4).</summary>
        private void OnPlan(HubMember m, PlanMsg msg, long now)
        {
            var p = m.Prompt;
            if (p.LastPlanAt != 0 && Ms(now - p.LastPlanAt) < 1000.0)
            {
                // (At most one a second, like BROWSE: the client's press goes out after World:PlanWaitMs without an answer.)
                Count("plan_throttled");
                return;
            }
            p.LastPlanAt = now;
            if (_world.TestDropPlans)
            {
                Count("plan_dropped");
                Note("plan_dropped", m.Instance, ("member", m.Id), ("plan", msg.Plan), ("stage", msg.Stage.ToLowerInvariant()));
                return;
            }
            // Only from home: a member in an instance moves inside it and is never prompted (decision 41).
            if (m.World is not null || m.Instance is not { Mode: InstanceMode.Hub } home || StageNames.IsHub(msg.Stage) || !_world.ArrivalPrompt)
            {
                Count("plan_ignored");
                p.Clear();
                m.Peer?.Send(new OptionsMsg(msg.Plan, 0, 0, msg.Stage, OptionsMsg.Nothing, m.DefaultVisibility, OptionsMsg.NewInstance, Rtt(m), 0, "",
                    []).Frame());
                return;
            }
            p.Clear();
            p.Plan = msg.Plan;
            p.Purpose = msg.Purpose;
            p.Stage = msg.Stage;
            p.Point = msg.Point;
            p.Context = msg.Context;
            var g = PlanGroup(m, home, msg.Stage, msg.Point, msg.Context, now);
            var rows = Candidates(g, now);
            var starting = rows.FirstOrDefault(r => r.World.StartingUntil != 0 && r.Group is not null && r.Choosable);
            OptionsMsg options;
            if (starting is not null)
                options = new OptionsMsg(msg.Plan, 0, 0, msg.Stage.ToLowerInvariant(), OptionsMsg.Starting, m.DefaultVisibility, OptionsMsg.NewInstance,
                    Rtt(m), 0, "", []);
            else if (rows.Count == 0)
                options = new OptionsMsg(msg.Plan, 0, 0, msg.Stage.ToLowerInvariant(), OptionsMsg.Nothing, m.DefaultVisibility, OptionsMsg.NewInstance,
                    Rtt(m), 0, "", []);
            else
            {
                options = BuildOptions(m, msg.Plan, 0, 0, msg.Stage, rows, 0, "", 0);
                p.Offered = true;
            }
            p.Options = RefreshKey(options);
            p.OptionsAt = now;
            p.Hosts = options.Rows.ToDictionary(r => r.Wid, r => r.HostName);
            m.Peer?.Send(options.Frame());
            Note("plan", m.Instance, ("member", m.Id), ("plan", msg.Plan), ("purpose", msg.Purpose.ToString().ToLowerInvariant()),
                ("stage", msg.Stage.ToLowerInvariant()), ("point", msg.Point), ("rows", RowsText(options)), ("preselected", PreselectedText(options)),
                ("flags", options.Flags));
        }

        /// <summary>The arrival a plan would be: the member alone from its hub stage group (a party's in round I4).</summary>
        private static ArrivalGroup PlanGroup(HubMember m, HubInstance home, string stage, byte point, ulong context, long now)
        {
            var g = new ArrivalGroup { From = home, BarrierTick = 0, Stage = stage, Point = point, Context = context, ParkedAt = now };
            g.Members.Add(new Arrival(m, Math.Max(0, m.Slot), []));
            return g;
        }

        // CHOICE ---------------------------------------------------------------------------------------------------------

        /// <summary>The prompt's answer: for the gate form's open plan (kept until the BARRIER names it), or at a held barrier (decided
        /// now, 19.7.7).</summary>
        private void OnPromptChoice(HubMember m, ChoiceMsg msg, long now)
        {
            Note("choice", m.Instance, ("member", m.Id), ("plan", msg.Plan), ("choice", msg.Choice.ToString().ToLowerInvariant()), ("wid", msg.Wid),
                ("visibility", Visibilities.Text(msg.Visibility)), ("inst", msg.Inst), ("visit", msg.Visit));
            if (msg.Choice is ChoiceKind.Travel or ChoiceKind.Return)
            {
                // (Round I3 / I4: no travel rows and no return hold yet.)
                Count("choice_refused");
                Note("choice_refused", null, ("member", m.Id), ("wid", msg.Wid), ("why", "not_offered"));
                return;
            }
            if (msg.Plan != 0)
            {
                var p = m.Prompt;
                if (p.Plan != msg.Plan || msg.Choice == ChoiceKind.Back)
                {
                    // (A gate form's B goes to the game: no barrier, nothing to decide. An unknown plan: nothing open.)
                    if (p.Plan == msg.Plan) p.Clear();
                    Count("choice_ignored");
                    return;
                }
                p.Choice = msg;
                if (msg.Choice == ChoiceKind.New) RememberChoice(m, msg.Visibility);
                return;
            }
            if (m.Arrival is not { Hold: HoldKind.Prompt } g || g.From.Id != msg.Inst || g.From.Visit != msg.Visit)
            {
                Count("choice_ignored");
                return;
            }
            _holds.Remove(g);
            g.Hold = HoldKind.None;
            g.PromptChoice = msg;
            if (msg.Choice == ChoiceKind.New) RememberChoice(m, msg.Visibility);
            if (!DecideChoice(g, now)) FreshJoinOrNew(g, now);
        }

        /// <summary>*New instance* with a visibility: the member's remembered default from now on (Q9), written off the lock.</summary>
        private void RememberChoice(HubMember m, Visibility v)
        {
            if (m.DefaultVisibility == v) return;
            m.DefaultVisibility = v;
            RememberVisibility?.Invoke(m.AccountId, v);
        }

        /// <summary>The gate form's answer of the plan the BARRIER named (a fresh arrival of one member): the arrival's choice.</summary>
        private void TakePlanChoice(ArrivalGroup g)
        {
            if (g.Members.Count != 1) return;
            var p = g.Members[0].Member.Prompt;
            if (g.PlanId != 0 && p.Plan == g.PlanId && p.Choice is { } c)
            {
                g.PromptChoice = c;
                foreach (var (wid, name) in p.Hosts) g.PromptHosts[wid] = name;
            }
            if (g.PlanId != 0 && p.Plan == g.PlanId) p.Clear(); // (the plan is done: its barrier came)
        }

        /// <summary>
        /// The arrival's choice checked again at the decision (19.7.7): *New instance* a new instance with the chosen visibility; a row
        /// that is still a choosable candidate a JOIN at its stage group (with 3.4's holds; a boss lock waits for *Wait* / *Go alone*) or a
        /// guest's NEW; a row gone full, ended or no longer open holds the barrier with a refreshed prompt that names it; *Back* (only at a
        /// held barrier from home) the DISSOLVE redirect to the hub stage it left. False: no choice to decide.
        /// </summary>
        private bool DecideChoice(ArrivalGroup g, long now)
        {
            if (g.PromptChoice is not { } c) return false;
            var m = g.Members[0].Member;
            switch (c.Choice)
            {
                case ChoiceKind.New:
                    Note("prompt_new", g.From, ("member", m.Id), ("visibility", Visibilities.Text(c.Visibility)));
                    // (The member's default is the chosen visibility now: CreateWorld takes it.)
                    m.DefaultVisibility = c.Visibility;
                    New(g, now, BarrierNotice.None, []);
                    return true;
                case ChoiceKind.Back:
                    if (!g.Fresh || !StageNames.IsHub(g.From.Stage) && g.From.Stage.Length > 0)
                    {
                        Count("choice_refused");
                        g.PromptChoice = null;
                        return false;
                    }
                    var point = c.BackPoint == ChoiceMsg.DefaultBackPoint ? HomePoint : c.BackPoint;
                    Note("prompt_back", g.From, ("member", m.Id), ("stage", HomeStage.ToLowerInvariant()), ("point", point));
                    Dissolve(g, now, redirectHome: true, backPoint: point);
                    return true;
                case ChoiceKind.Join:
                    var rows = Candidates(g, now);
                    var row = rows.FirstOrDefault(r => r.World.Wid == c.Wid);
                    if (row is null || !row.Choosable)
                    {
                        // (Full, ended or no longer open meanwhile: the barrier waits with a refreshed prompt, 19.7.7.)
                        var w0 = _worlds.GetValueOrDefault(c.Wid);
                        var notice = w0 is null || w0.Ending ? OptionsMsg.NoticeEnded : row?.Why == "full" ? OptionsMsg.NoticeFull : OptionsMsg.NoticeClosed;
                        Count("choice_refused");
                        Note("choice_refused", g.From, ("member", m.Id), ("wid", c.Wid), ("why", row?.Why is { Length: > 0 } why ? why : "gone"));
                        HoldPrompt(g, rows, notice, w0?.Host.Name ?? g.PromptHosts.GetValueOrDefault(c.Wid) ?? "", now);
                        return true;
                    }
                    var w = row.World;
                    var starting = w.StartingUntil != 0 && row.Group is not null;
                    g.Arrival = starting ? ArrivalKind.Starting : row.Group is null ? ArrivalKind.GuestNew : ArrivalKind.AutoJoin;
                    g.World = w;
                    Note("candidates", null, ("members", g.Members.Select(a => a.Member.Id).ToArray()), ("stage", g.Stage),
                        ("rows", rows.Select(r => $"{r.World.Wid}:{r.State}:{r.Here}{(r.Why.Length > 0 ? ":" + r.Why : "")}").ToArray()),
                        ("chosen", w.Wid), ("by", "choice"));
                    if (row.Group is null)
                    {
                        // A guest's NEW (19.30.4): a new stage group of the instance at the stage; the host's story replaces the engines'.
                        foreach (var a in g.Members) TakeSeat(w, a.Member, now, "new_group");
                        foreach (var x in g.CoArrivals.SelectMany(x => x.Members)) TakeSeat(w, x.Member, now, "coarrival");
                        New(g, now, BarrierNotice.None, []);
                        return true;
                    }
                    foreach (var a in g.Members) TakeSeat(w, a.Member, now, "join");
                    JoinTarget(g, row.Group, now);
                    return true;
                default:
                    g.PromptChoice = null;
                    return false;
            }
        }

        // The prompt hold (the loading-screen form) ---------------------------------------------------------------------

        /// <summary>A fresh arrival with candidates and no answer: its barrier waits for the prompt, without a time limit (19.7.5).</summary>
        private void HoldPrompt(ArrivalGroup g, List<CandidateRow> rows, byte notice, string noticeName, long now)
        {
            g.PromptChoice = null;
            g.Hold = HoldKind.Prompt;
            g.Target = null;
            if (!_holds.Contains(g)) _holds.Add(g);
            SendWait(g, WaitReason.Choice, [], 0, 0, now);
            var m = g.Members[0].Member;
            var options = BuildOptions(m, 0, g.From.Id, g.From.Visit, g.Stage, rows, notice, noticeName, 0);
            var frame = options.Frame();
            g.PromptOptions = RefreshKey(options);
            g.PromptOptionsAt = now;
            g.PromptHosts.Clear();
            foreach (var r in options.Rows) g.PromptHosts[r.Wid] = r.HostName;
            foreach (var a in g.Members) a.Member.Peer?.Send(frame);
            Count("prompt_holds");
            Note("prompt_hold", g.From, ("member", m.Id), ("stage", g.Stage.ToLowerInvariant()), ("rows", RowsText(options)),
                ("preselected", PreselectedText(options)), ("notice", notice));
        }

        /// <summary>A held prompt sees its candidates again once a second: OPTIONS goes again when they changed (the client keeps its
        /// cursor on the same instance).</summary>
        private void RefreshPrompt(ArrivalGroup g, long now)
        {
            if (g.Members.Count == 0 || Ms(now - g.PromptOptionsAt) < _world.OptionsRefreshMs) return;
            g.PromptOptionsAt = now;
            var m = g.Members[0].Member;
            var rows = Candidates(g, now);
            var options = BuildOptions(m, 0, g.From.Id, g.From.Visit, g.Stage, rows, 0, "", OptionsMsg.Refresh);
            var frame = options.Frame();
            if (g.PromptOptions is { } last && last.AsSpan().SequenceEqual(frame)) return;
            g.PromptOptions = frame;
            foreach (var r in options.Rows) g.PromptHosts[r.Wid] = r.HostName;
            foreach (var a in g.Members) a.Member.Peer?.Send(frame);
            Note("options", g.From, ("member", m.Id), ("plan", 0), ("stage", g.Stage.ToLowerInvariant()), ("rows", RowsText(options)),
                ("preselected", PreselectedText(options)), ("flags", options.Flags));
        }

        /// <summary>The gate form's open plans see their candidates again once a second while their overlay is open (no CHOICE yet).</summary>
        private void RefreshPlans(long now)
        {
            foreach (var m in _byAccount.Values)
            {
                var p = m.Prompt;
                if (p.Plan == 0 || !p.Offered || p.Choice is not null) continue;
                if (!m.Connected || m.World is not null || m.Instance is not { Mode: InstanceMode.Hub } home)
                {
                    p.Clear();
                    continue;
                }
                if (Ms(now - p.OptionsAt) < _world.OptionsRefreshMs) continue;
                p.OptionsAt = now;
                var rows = Candidates(PlanGroup(m, home, p.Stage, p.Point, p.Context, now), now);
                var options = BuildOptions(m, p.Plan, 0, 0, p.Stage, rows, 0, "", OptionsMsg.Refresh);
                var frame = options.Frame();
                if (p.Options is { } last && last.AsSpan().SequenceEqual(frame)) continue;
                p.Options = frame;
                foreach (var r in options.Rows) p.Hosts[r.Wid] = r.HostName;
                m.Peer?.Send(frame);
                Note("options", m.Instance, ("member", m.Id), ("plan", p.Plan), ("stage", p.Stage.ToLowerInvariant()), ("rows", RowsText(options)),
                    ("preselected", PreselectedText(options)), ("flags", options.Flags));
            }
        }

        // OPTIONS --------------------------------------------------------------------------------------------------------

        /// <summary>The receiver's OPTIONS from the ordered candidates (19.7.3): at most eight rows, the preselection of decision 40 (the
        /// first row when it is not greyed and is the party's or a friend's or has players at the arrival stage; else *New instance*).</summary>
        private OptionsMsg BuildOptions(HubMember receiver, ushort plan, uint inst, uint visit, string stage, IReadOnlyList<CandidateRow> ordered, byte notice,
            string noticeName, byte flags)
        {
            var rows = ordered.Take(OptionsMsg.MaxRows).ToList();
            var preselected = Matching.Preselected(rows) is { } first ? (byte)rows.IndexOf(first) : OptionsMsg.NewInstance;
            var list = new List<OptionRow>(rows.Count);
            foreach (var r in rows)
            {
                var w = r.World;
                var relation = (byte)((r.HostFriend ? OptionRow.FriendHost : 0) | (r.FriendsInside > 0 ? OptionRow.FriendsIn : 0));
                var state = r.State switch
                {
                    RowState.Joinable or RowState.GuestNew => OptionState.Joinable,
                    RowState.Waiting => r.Group is { BossLock: true } ? OptionState.BossFight : OptionState.Loading,
                    _ => r.Why switch
                    {
                        "host_away" => OptionState.HostAway,
                        "too_long" => OptionState.TooLong,
                        "full" => OptionState.Full,
                        _ => OptionState.StoryNotHere,
                    },
                };
                var members = w.Members.OrderBy(x => x == w.Host ? 0 : 1).ThenBy(x => x.Id).Take(4)
                    .Select(x => new OptionMember(x.Name, x.Presence?.Level ?? 0, x.Presence?.Species ?? 0)).ToList();
                list.Add(new OptionRow(w.Wid, w.Host.Id, w.Host.Name, relation, (byte)Math.Min(4, r.FriendsInside), StageOf(w.Host).ToLowerInvariant(),
                    (byte)Math.Min(4, w.Seats.Count), (byte)Math.Min(4, r.Here), (byte)Math.Min(4, w.Seats.Count), 0, 0, StoryLabel(w), 0,
                    (byte)Math.Clamp(w.Settings.Difficulty, 0, 2), (byte)(w.Randomizer ? OptionRow.Randomizer : 0), state,
                    receiver.Staff ? (byte)w.Visibility : OptionRow.Hidden, (ushort)Math.Clamp(Math.Round(r.PingMs), 0, ushort.MaxValue), members));
            }
            return new OptionsMsg(plan, inst, visit, stage.ToLowerInvariant(), flags, receiver.DefaultVisibility, preselected, Rtt(receiver), notice,
                noticeName, list);
        }

        /// <summary>The instance's story point for a row ("Chapter 4, Hard"): the host keeper's phase and the instance's difficulty.</summary>
        private static string StoryLabel(WorldInstance w)
        {
            var phase = StoryMerge.Phase(w.Keeper.Story);
            var difficulty = w.Settings.Difficulty switch { 1 => "Hard", 2 => "Very Hard", _ => "" };
            if (phase <= 0) return difficulty;
            return difficulty.Length > 0 ? $"Chapter {phase}, {difficulty}" : $"Chapter {phase}";
        }

        /// <summary>What a refresh compares: the OPTIONS as a refresh would send it (the refresh flag, no notice line).</summary>
        private static byte[] RefreshKey(OptionsMsg o) => (o with { Flags = OptionsMsg.Refresh, Notice = 0, NoticeName = "" }).Frame();

        private static ushort Rtt(HubMember m) => (ushort)Math.Clamp(Math.Round(m.Rtt.Median), 0, ushort.MaxValue);

        private static string[] RowsText(OptionsMsg o) => o.Rows.Select(r => $"{r.Wid}:{r.State}:{r.Here}").ToArray();

        private static string PreselectedText(OptionsMsg o) => o.Preselected == OptionsMsg.NewInstance ? "new" : o.Rows[o.Preselected].Wid.ToString();
    }
}
