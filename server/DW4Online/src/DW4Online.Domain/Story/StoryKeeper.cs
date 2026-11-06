namespace DW4Online.Domain.Story;

/// <summary>
/// The story of one (account, box) in the world (SEAMLESS.md 19.8.1): the account's online story rebased on its difficulty, the
/// pending sets of the members who play on it checked for plausibility (the sender's stage must set the flag or change the
/// counter, the phase rises at most one step, the clear flag only of the story's difficulty and only after the last briefing,
/// counter growth within limits), the per-visit and per-member rules with the earned values that wait for them
/// (<see cref="MergeEarned"/>), and the host's grace (<see cref="Hold"/>): values that pass are held and merged when the host
/// returns, discarded when his instance ends. The owner's own values are not spaced (the spacing guards his story against a
/// guest's client). Round 59's owner-away machinery is retired (19.8.5): no dropped sets, no epochs, no "not started" (a new
/// account's story simply starts at zero). Not thread-safe: the relay calls it under the hub lock. One keeper and one writer per
/// (account, box) in the relay (19.17.3: one world session per account).
/// </summary>
public sealed class StoryKeeper
{
    /// <summary>The clear flags: stagBS06 sets FLGset(SYSgetDifficulty() + 406, 1), so a story takes only its own difficulty's.</summary>
    public const int FirstClearFlag = RoomStoryKeeper.FirstClearFlag;

    private readonly StorySets _sets;
    private readonly Dictionary<StoryLineKey, int> _lines = [];
    // Per member: its current visit and what that visit gave (one phase step, one key item).
    private readonly Dictionary<int, VisitTally> _visits = [];
    // Per account: when its last phase step and its last key item counted (relay clock, seconds).
    private readonly Dictionary<long, (double Phase, double Key)> _lastSteps = [];
    // Per account: a guest's flag tokens (FlagBurst, one more every FlagSeconds) and when they were counted.
    private readonly Dictionary<long, (double Tokens, double At)> _flagTokens = [];
    // Per account: the counter lines it opened on this story (Instances:StoryLinesPerMember).
    private readonly Dictionary<long, int> _linesOpened = [];
    // Per account and counter: the growth taken (merged or held) from it (MaxSessionGrowth).
    private readonly Dictionary<(long Account, int Counter), long> _grown = [];
    // Per account: earned values that passed every check and wait only for the one-per-visit rule or the spacing; merged when due.
    private readonly Dictionary<long, Earned> _earned = [];
    // Held while the host is in his grace (19.8.5).
    private int _heldPhase;
    private readonly SortedSet<int> _heldFlags = [];
    private readonly SortedSet<int> _heldFolder = [];
    private readonly Dictionary<StoryLineKey, int> _heldLines = [];
    // Held growth of lines nobody can report any more (PruneLines): merged with the rest when the hold ends with a merge.
    private readonly SortedDictionary<int, long> _heldGrowth = [];
    private uint _mergeLine;

    private sealed class Earned
    {
        public int Phase { get; set; }
        public SortedSet<int> Keys { get; } = [];
        public SortedSet<int> Flags { get; } = [];
        public bool IsEmpty => Phase == 0 && Keys.Count == 0 && Flags.Count == 0;
    }

    private sealed class VisitTally(ulong key)
    {
        public ulong Key { get; } = key;
        public int PhaseSteps { get; set; }
        public int Keys { get; set; }
    }

    public StoryKeeper(long account, int box, RoomStoryData story, int difficulty, StorySets? sets = null)
    {
        _sets = sets ?? StorySets.Default;
        Account = account;
        Box = box;
        Difficulty = Math.Clamp(difficulty, 0, StorySets.Difficulties - 1);
        Story = story.Clone();
        StoryMerge.Rebase(Story, Difficulty, _sets);
    }

    /// <summary>The story's owner (the host when an instance runs on it).</summary>
    public long Account { get; }
    /// <summary>The box (1 until round D).</summary>
    public int Box { get; }
    public RoomStoryData Story { get; private set; }
    public int Difficulty { get; private set; }
    /// <summary>The host's grace (19.6.7, 19.8.5): values that pass are held, not merged.</summary>
    public bool Held { get; private set; }
    /// <summary>The story is being replaced from outside (a reset or a staff restore, 19.8.1): every value is refused until
    /// <see cref="Replace"/> loads the new one.</summary>
    public bool Replacing { get; set; }
    /// <summary>The last revision handed to the saver (the relay compares it to save counter-only revisions less often).</summary>
    public RoomStoryData? Saved { get; set; }
    /// <summary>When the keeper's owner left the world and nothing plays on it any more (the relay drops it some minutes later).</summary>
    public long IdleSince { get; set; }

    public double MinVisitSeconds { get; init; } = 10;
    public double PhaseStepSeconds { get; init; } = 300;
    public double KeySeconds { get; init; } = 60;
    public int FlagBurst { get; init; } = 8;
    public double FlagSeconds { get; init; } = 30;
    public int MaxLinesPerMember { get; init; } = 256;
    public StoryRefusal EarnedAnswer { get; init; } = StoryRefusal.Queued;

    public int HeldCount => (_heldPhase > 0 ? 1 : 0) + _heldFlags.Count + _heldFolder.Count + _heldLines.Count + _heldGrowth.Count;
    public int EarnedCount => _earned.Values.Sum(e => (e.Phase != 0 ? 1 : 0) + e.Keys.Count + e.Flags.Count);
    public bool HasEarned(int member, long account) => _earned.ContainsKey(AccountOf(member, account));
    /// <summary>The accounts with earned values waiting (the relay merges them in their members' visits).</summary>
    public IEnumerable<long> EarnedAccounts => _earned.Keys;
    public int LineCount => _lines.Count + _heldLines.Count;

    /// <summary>
    /// The story plays at another difficulty from now on (the owner's own Game Start chose it; only while nothing plays on the
    /// story): the working set is that difficulty's (StoryMerge.Rebase), the copies stay.
    /// </summary>
    public void SetDifficulty(int difficulty)
    {
        difficulty = Math.Clamp(difficulty, 0, StorySets.Difficulties - 1);
        if (difficulty == Difficulty) return;
        Difficulty = difficulty;
        StoryMerge.Rebase(Story, Difficulty, _sets);
    }

    /// <summary>The story as it would be at another difficulty (the DISSOLVE's replace payload, 19.8.3), a copy.</summary>
    public RoomStoryData RebasedCopy(int difficulty)
    {
        var copy = Story.Clone();
        StoryMerge.Rebase(copy, Math.Clamp(difficulty, 0, StorySets.Difficulties - 1), _sets);
        return copy;
    }

    /// <summary>The story was replaced from outside (19.8.1): the new one, nothing earned, held or counted of the old.</summary>
    public void Replace(RoomStoryData story)
    {
        Story = story.Clone();
        StoryMerge.Rebase(Story, Difficulty, _sets);
        _lines.Clear();
        _visits.Clear();
        _earned.Clear();
        _heldPhase = 0;
        _heldFlags.Clear();
        _heldFolder.Clear();
        _heldLines.Clear();
        _heldGrowth.Clear();
        Held = false;
        Replacing = false;
        Saved = Story.Clone();
    }

    /// <summary>
    /// One member's pending set. <paramref name="stage"/> is the sender's stage now (null or a UI stage: nothing counts). With
    /// <paramref name="visit"/> (the relay always gives it) the per-visit and per-member rules apply (round 59: everything only
    /// from a visit at least <see cref="MinVisitSeconds"/> old, one phase step and one key item per member and visit, a guest's
    /// phase steps <see cref="PhaseStepSeconds"/> and key items <see cref="KeySeconds"/> apart, a guest's flags
    /// <see cref="FlagBurst"/> then one per <see cref="FlagSeconds"/>, at most <see cref="MaxLinesPerMember"/> counter lines);
    /// what waits only for the one-per-visit rule or the spacing is earned (<see cref="MergeEarned"/>).
    /// </summary>
    public StoryTake Take(StoryDelta delta, string? stage, StoryVisit? visit = null)
    {
        var refused = new List<StoryRefused>();
        if (Replacing)
        {
            RefuseAll(delta, StoryRefusal.Wait, refused);
            return new StoryTake(refused, false);
        }
        if (string.IsNullOrEmpty(stage) || stage.StartsWith("stagis", StringComparison.OrdinalIgnoreCase))
        {
            RefuseAll(delta, StoryRefusal.NotMember, refused);
            return new StoryTake(refused, false);
        }
        var ok = new StoryDelta();
        var phase = StoryMerge.Phase(Story, _sets);
        var reached = Math.Max(phase, Held ? _heldPhase : 0);
        if (delta.Phase != 0)
        {
            var limit = reached + 1;
            if (delta.Phase > _sets.Phase.Count) refused.Add(new(StoryKeyKind.Phase, delta.Phase, StoryRefusal.NotStory));
            else if (delta.Phase <= phase) { }
            else if (delta.Phase > limit) refused.Add(new(StoryKeyKind.Phase, delta.Phase, StoryRefusal.PhaseJump));
            else if (!_sets.SetsFlag(stage, _sets.Phase[delta.Phase - 1])) refused.Add(new(StoryKeyKind.Phase, delta.Phase, StoryRefusal.Stage));
            else ok.Phase = delta.Phase;
        }
        foreach (var f in delta.Flags.Distinct())
        {
            if (!_sets.IsPersistent(f)) refused.Add(new(StoryKeyKind.Flag, f, StoryRefusal.NotStory));
            else if (Story.Flags[f] != 0) { }
            else if (!_sets.SetsFlag(stage, f)) refused.Add(new(StoryKeyKind.Flag, f, StoryRefusal.Stage));
            else if (f is >= FirstClearFlag and < FirstClearFlag + StorySets.Difficulties && f != FirstClearFlag + Difficulty)
                refused.Add(new(StoryKeyKind.Flag, f, StoryRefusal.Stage)); // another difficulty's clear
            else if (f == FirstClearFlag + Difficulty && reached < _sets.Phase.Count - 1)
                refused.Add(new(StoryKeyKind.Flag, f, StoryRefusal.PhaseJump)); // the clear only after the last briefing (round 59)
            else ok.Flags.Add(f);
        }
        foreach (var k in delta.Folder.Distinct())
        {
            if (!_sets.IsKey(k)) refused.Add(new(StoryKeyKind.Folder, k, StoryRefusal.NotStory));
            else if (Story.Folder[k] != 0) { }
            else if (!_sets.GivesKey(stage, k)) refused.Add(new(StoryKeyKind.Folder, k, StoryRefusal.Stage));
            else ok.Folder.Add(k);
        }
        foreach (var g in delta.Counters)
        {
            if (!_sets.IsAdditive(g.Counter)) refused.Add(new(StoryKeyKind.Counter, g.Counter, StoryRefusal.NotStory));
            else if (g.Growth < 0 || g.Growth > RoomStoryKeeper.MaxGrowth(g.Counter)) refused.Add(new(StoryKeyKind.Counter, g.Counter, StoryRefusal.Limit));
            else if (!_sets.ChangesCounter(stage, g.Counter)) refused.Add(new(StoryKeyKind.Counter, g.Counter, StoryRefusal.Stage));
            else if (g.Growth > _lines.GetValueOrDefault(new StoryLineKey(g.Counter, g.Inst, g.Visit))) ok.Counters.Add(g);
        }
        if (ok.IsEmpty) return new StoryTake(refused, false);
        var charges = new List<(long Account, int Counter, long Growth)>();
        if (visit is { } v) PerVisit(ok, v, refused, charges);
        if (ok.IsEmpty) return new StoryTake(refused, false);
        foreach (var (account, counter, growth) in charges) _grown[(account, counter)] = _grown.GetValueOrDefault((account, counter)) + growth;
        return new StoryTake(refused, Apply(ok, refused));
    }

    /// <summary>
    /// The account's earned values that are due in its member's visit <paramref name="visit"/> in <paramref name="stage"/>
    /// (8.7's queue, "across owners" in the world: each keeper keeps what was earned on it and merges it when due, whatever
    /// stage the member plays in then). What the story has meanwhile is no longer waited for.
    /// </summary>
    public StoryTake MergeEarned(StoryVisit visit, string? stage)
    {
        var account = AccountOf(visit);
        if (!_earned.TryGetValue(account, out var earned)) return new StoryTake([], false);
        var known = Math.Max(StoryMerge.Phase(Story, _sets), _heldPhase);
        if (earned.Phase != 0 && earned.Phase <= known) earned.Phase = 0;
        earned.Keys.RemoveWhere(k => Story.Folder[k] != 0 || _heldFolder.Contains(k));
        earned.Flags.RemoveWhere(f => Story.Flags[f] != 0 || _heldFlags.Contains(f));
        if (earned.IsEmpty)
        {
            _earned.Remove(account);
            return new StoryTake([], false);
        }
        if (Replacing || string.IsNullOrEmpty(stage) || stage.StartsWith("stagis", StringComparison.OrdinalIgnoreCase) || visit.Seconds < MinVisitSeconds)
            return new StoryTake([], false);
        var tally = Tally(visit);
        var last = _lastSteps.TryGetValue(account, out var l) ? l : (double.NegativeInfinity, double.NegativeInfinity);
        var ok = new StoryDelta();
        if (earned.Phase != 0 && earned.Phase <= known + 1 && tally.PhaseSteps == 0 && (visit.Owner || visit.At - last.Item1 >= PhaseStepSeconds))
        {
            ok.Phase = (byte)earned.Phase;
            earned.Phase = 0;
            tally.PhaseSteps = 1;
            last.Item1 = visit.At;
        }
        if (earned.Keys.Count > 0 && tally.Keys == 0 && (visit.Owner || visit.At - last.Item2 >= KeySeconds))
        {
            var k = earned.Keys.Min;
            ok.Folder.Add(k);
            earned.Keys.Remove(k);
            tally.Keys = 1;
            last.Item2 = visit.At;
        }
        _lastSteps[account] = last;
        if (earned.Flags.Count > 0)
        {
            var tokens = visit.Owner || FlagSeconds <= 0 ? double.PositiveInfinity : FlagTokens(account, visit.At);
            foreach (var f in earned.Flags.ToList())
            {
                if (tokens < 1) break;
                tokens -= 1;
                ok.Flags.Add(f);
                earned.Flags.Remove(f);
            }
            if (!double.IsPositiveInfinity(tokens)) _flagTokens[account] = (tokens, visit.At);
        }
        if (earned.IsEmpty) _earned.Remove(account);
        if (ok.IsEmpty) return new StoryTake([], false);
        var refused = new List<StoryRefused>();
        return new StoryTake(refused, Apply(ok, refused));
    }

    /// <summary>The host's grace began: values that pass are held from now on (19.8.5).</summary>
    public void Hold() => Held = true;

    /// <summary>
    /// The grace ended: <paramref name="merge"/> true (the host is back) merges what was held, step by step; false (the instance
    /// ends) discards it (the guests' engines go home and are replaced, so nothing lingers). Returns whether the story changed.
    /// </summary>
    public bool Release(bool merge)
    {
        Held = false;
        if (HeldCount == 0) return false;
        var changed = false;
        if (merge)
        {
            for (var p = StoryMerge.Phase(Story, _sets) + 1; p <= _heldPhase; ++p)
            {
                var step = StoryMerge.Merge(Story, new StoryDelta { Phase = (byte)p }, Difficulty, _lines, _sets);
                Story = step.Story;
                changed |= step.Changed;
            }
            var rest = new StoryDelta
            {
                Flags = [.. _heldFlags],
                Folder = [.. _heldFolder],
                Counters = _heldLines.Select(kv => new CounterGrowth(kv.Key.Counter, kv.Key.Inst, kv.Key.Visit, kv.Value)).ToList(),
            };
            var once = new List<StoryLineKey>();
            foreach (var (counter, growth) in _heldGrowth)
            {
                var key = new StoryLineKey(counter, 0xFFFF_FFFFu, ++_mergeLine);
                once.Add(key);
                rest.Counters.Add(new CounterGrowth(counter, key.Inst, key.Visit, (int)Math.Min(growth, int.MaxValue)));
            }
            var r = StoryMerge.Merge(Story, rest, Difficulty, _lines, _sets);
            Story = r.Story;
            changed |= r.Changed;
            foreach (var key in once) _lines.Remove(key);
        }
        _heldPhase = 0;
        _heldFlags.Clear();
        _heldFolder.Clear();
        _heldLines.Clear();
        _heldGrowth.Clear();
        return changed;
    }

    /// <summary>Forgets the engine lines no member can report any more (SEAMLESS.md 8.7); a held line's extra growth stays held.</summary>
    public int PruneLines(Func<StoryLineKey, bool> reportable)
    {
        var removed = 0;
        foreach (var (key, growth) in _heldLines.Where(kv => !reportable(kv.Key)).ToList())
        {
            var extra = growth - (long)_lines.GetValueOrDefault(key);
            if (extra > 0) _heldGrowth[key.Counter] = Math.Min(_heldGrowth.GetValueOrDefault(key.Counter) + extra, int.MaxValue);
            _heldLines.Remove(key);
            ++removed;
        }
        foreach (var key in _lines.Keys.Where(k => !reportable(k)).ToList())
        {
            _lines.Remove(key);
            ++removed;
        }
        return removed;
    }

    /// <summary>Values that passed every check: held during the host's grace, else merged. True when the story changed.</summary>
    private bool Apply(StoryDelta ok, List<StoryRefused> refused)
    {
        if (Held)
        {
            _heldPhase = Math.Max(_heldPhase, ok.Phase);
            foreach (var f in ok.Flags) _heldFlags.Add(f);
            foreach (var k in ok.Folder) _heldFolder.Add(k);
            foreach (var g in ok.Counters)
            {
                var key = new StoryLineKey(g.Counter, g.Inst, g.Visit);
                _heldLines[key] = Math.Max(_heldLines.GetValueOrDefault(key), g.Growth);
            }
            RefuseAll(ok, StoryRefusal.Held, refused);
            return false;
        }
        var r = StoryMerge.Merge(Story, ok, Difficulty, _lines, _sets);
        Story = r.Story;
        refused.AddRange(r.Refused);
        return r.Changed;
    }

    private static long AccountOf(StoryVisit v) => AccountOf(v.Member, v.Account);

    private static long AccountOf(int member, long account) => account != 0 ? account : -1 - member;

    private VisitTally Tally(StoryVisit v)
    {
        if (!_visits.TryGetValue(v.Member, out var tally) || tally.Key != v.Key) _visits[v.Member] = tally = new VisitTally(v.Key);
        return tally;
    }

    /// <summary>The per-visit and per-member rules (round 59, SEAMLESS.md 8.7), as RoomStoryKeeper had them.</summary>
    private void PerVisit(StoryDelta ok, StoryVisit v, List<StoryRefused> refused, List<(long, int, long)> charges)
    {
        var tally = Tally(v);
        if (v.Seconds < MinVisitSeconds)
        {
            RefuseAll(ok, StoryRefusal.Wait, refused);
            ok.Phase = 0;
            ok.Flags.Clear();
            ok.Folder.Clear();
            ok.Counters.Clear();
            return;
        }
        var account = AccountOf(v);
        var last = _lastSteps.TryGetValue(account, out var l) ? l : (double.NegativeInfinity, double.NegativeInfinity);
        if (ok.Phase != 0)
        {
            if (tally.PhaseSteps > 0 || (!v.Owner && v.At - last.Item1 < PhaseStepSeconds))
            {
                refused.Add(new(StoryKeyKind.Phase, ok.Phase, EarnedAnswer));
                var earned = EarnedOf(account);
                earned.Phase = Math.Max(earned.Phase, ok.Phase);
                ok.Phase = 0;
            }
            else
            {
                tally.PhaseSteps = 1;
                last.Item1 = v.At;
            }
        }
        var keys = ok.Folder.ToList();
        ok.Folder.Clear();
        foreach (var k in keys)
        {
            if (tally.Keys > 0 || (!v.Owner && v.At - last.Item2 < KeySeconds))
            {
                refused.Add(new(StoryKeyKind.Folder, k, EarnedAnswer));
                EarnedOf(account).Keys.Add(k);
            }
            else
            {
                ok.Folder.Add(k);
                tally.Keys = 1;
                last.Item2 = v.At;
            }
        }
        _lastSteps[account] = last;
        if (!v.Owner && FlagSeconds > 0 && ok.Flags.Count > 0)
        {
            var tokens = FlagTokens(account, v.At);
            foreach (var f in ok.Flags.ToList())
            {
                if (f == FirstClearFlag + Difficulty) continue;
                if (_heldFlags.Contains(f)) continue;
                if (tokens >= 1)
                {
                    tokens -= 1;
                    continue;
                }
                refused.Add(new(StoryKeyKind.Flag, f, EarnedAnswer));
                EarnedOf(account).Flags.Add(f);
                ok.Flags.Remove(f);
            }
            _flagTokens[account] = (tokens, v.At);
        }
        var growths = new Dictionary<int, long>();
        for (var n = 0; n < ok.Counters.Count; ++n)
        {
            var g = ok.Counters[n];
            var key = new StoryLineKey(g.Counter, g.Inst, g.Visit);
            if (!_lines.ContainsKey(key) && !_heldLines.ContainsKey(key))
            {
                var opened = _linesOpened.GetValueOrDefault(account);
                if (opened >= MaxLinesPerMember)
                {
                    refused.Add(new(StoryKeyKind.Counter, g.Counter, StoryRefusal.Limit));
                    ok.Counters.RemoveAt(n--);
                    continue;
                }
                _linesOpened[account] = opened + 1;
            }
            var before = Math.Max(_lines.GetValueOrDefault(key), _heldLines.GetValueOrDefault(key));
            var extra = (long)g.Growth - before;
            if (extra <= 0 || v.Owner) continue;
            var left = RoomStoryKeeper.MaxSessionGrowth(g.Counter) - _grown.GetValueOrDefault((account, g.Counter)) - growths.GetValueOrDefault(g.Counter);
            if (left <= 0)
            {
                refused.Add(new(StoryKeyKind.Counter, g.Counter, StoryRefusal.Limit));
                ok.Counters.RemoveAt(n--);
                continue;
            }
            if (extra > left)
            {
                ok.Counters[n] = g with { Growth = (int)(before + left) };
                extra = left;
            }
            growths[g.Counter] = growths.GetValueOrDefault(g.Counter) + extra;
        }
        foreach (var (counter, growth) in growths) charges.Add((account, counter, growth));
    }

    private double FlagTokens(long account, double at) => _flagTokens.TryGetValue(account, out var b)
        ? Math.Min(FlagBurst, b.Tokens + Math.Max(0, at - b.At) / FlagSeconds)
        : FlagBurst;

    private Earned EarnedOf(long account)
    {
        if (!_earned.TryGetValue(account, out var e)) _earned[account] = e = new Earned();
        return e;
    }

    private static void RefuseAll(StoryDelta d, StoryRefusal why, List<StoryRefused> into)
    {
        if (d.Phase != 0) into.Add(new(StoryKeyKind.Phase, d.Phase, why));
        foreach (var f in d.Flags) into.Add(new(StoryKeyKind.Flag, f, why));
        foreach (var k in d.Folder) into.Add(new(StoryKeyKind.Folder, k, why));
        foreach (var g in d.Counters) into.Add(new(StoryKeyKind.Counter, g.Counter, why));
    }
}
