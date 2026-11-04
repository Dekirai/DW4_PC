namespace DW4Online.Domain.Story;

/// <summary>What happens to story progress while the room owner is away (Rooms:StoryWhileOwnerAway, SEAMLESS.md 8.6).</summary>
public enum StoryPolicy
{
    /// <summary>Refused and recorded in the room's dropped set; the next barrier removes it from the engines (default).</summary>
    Drop,
    /// <summary>Kept by the server and merged when the owner returns; the engines keep the values meanwhile.</summary>
    Hold,
    /// <summary>Merged at once, as with the owner present.</summary>
    Apply,
}

public static class StoryPolicies
{
    public static StoryPolicy Parse(string? text) => text?.Trim().ToLowerInvariant() switch
    {
        "hold" => StoryPolicy.Hold,
        "apply" => StoryPolicy.Apply,
        _ => StoryPolicy.Drop,
    };
}

/// <summary>The answer to one STORY: the refused keys and whether the room story changed (a new revision).</summary>
public sealed record StoryTake(IReadOnlyList<StoryRefused> Refused, bool Changed);

/// <summary>
/// The sender's visit as the relay knows it (SEAMLESS.md 8.7): the member, a key that changes with every visit (an
/// instance visit, or a visit the member plays alone), how long the member has been in it, the member's account (the
/// per-member rules count per account, so leaving and joining the room again starts nothing new) and the relay's clock in
/// seconds. The per-visit rules (<see cref="RoomStoryKeeper.Take"/>) count per member and visit. <paramref name="Owner"/>:
/// the sender is the room owner, whose own steps are not spaced (the spacing guards the owner's story against a guest's
/// client).
/// </summary>
public readonly record struct StoryVisit(int Member, ulong Key, double Seconds, long Account = 0, double At = 0, bool Owner = false);

/// <summary>
/// The room story of one room session (SEAMLESS.md 8.3-8.7): the owner's online story rebased on the room's difficulty,
/// the members' pending sets checked for plausibility (the sender's instance stage must set the flag or change the
/// counter, the phase rises at most one step, the clear flag only of the room's difficulty and only after the last
/// briefing, counter growth within limits), the per-visit and per-member rules (with the
/// earned values that wait for them, <see cref="MergeEarned"/>), and the owner-away policy: merged, dropped (the dropped
/// set every BARRIER_OK carries; a dropped value counts again only from an instance whose visit began after the drop, that
/// is from an engine the barrier cleaned and that set it anew) or held until the owner returns. A room whose owner has no
/// online story yet starts at zero: until the owner's own game enters a stage (<see cref="OwnerStarted"/>) everything is
/// dropped with the reason NotStarted, whatever the policy. Not thread-safe: the relay calls it under the room lock. It
/// lives as long as the room (the relay keeps it when a room's hub is rebuilt), so its dropped set, epochs, held values,
/// earned values and per-member rules hold for the whole room session.
/// </summary>
public sealed class RoomStoryKeeper
{
    /// <summary>
    /// The largest growth one engine line may report: COUNTER 1 counts the bits donated in stagML13's junk shop (a wallet
    /// and a bank of at most 9,999,999 each), COUNTER 7 stag0001's training attempts.
    /// </summary>
    public static int MaxGrowth(int counter) => counter == 7 ? 100_000 : 20_000_000;

    /// <summary>The clear flags: stagBS06 sets FLGset(SYSgetDifficulty() + 406, 1), so a room sets only its own difficulty's.</summary>
    public const int FirstClearFlag = 406;

    private readonly StorySets _sets;
    private readonly Dictionary<StoryLineKey, int> _lines = [];
    // Per member: its current visit and what that visit gave (one phase step, one key item).
    private readonly Dictionary<int, VisitTally> _visits = [];
    // Per account: when its last phase step and its last key item counted (relay clock, seconds).
    private readonly Dictionary<long, (double Phase, double Key)> _lastSteps = [];
    // Per account: the counter lines it opened in this room session (Instances:StoryLinesPerMember).
    private readonly Dictionary<long, int> _linesOpened = [];
    // Per account: earned values (SEAMLESS.md 8.7) that passed every check and wait only for the one-per-visit rule or the
    // spacing, with the epoch of the visit that sent them; merged when due (MergeEarned), whatever stage the member is in.
    private readonly Dictionary<long, Earned> _earned = [];

    private sealed class Earned
    {
        public int Phase { get; set; }
        public long PhaseEpoch { get; set; }
        public SortedDictionary<int, long> Keys { get; } = [];
        public bool IsEmpty => Phase == 0 && Keys.Count == 0;
    }

    private sealed class VisitTally(ulong key)
    {
        public ulong Key { get; } = key;
        public int PhaseSteps { get; set; }
        public int Keys { get; set; }
    }
    // Dropped keys with the epoch of their last drop and why (owner away, or the story had not started).
    private readonly SortedDictionary<int, (long Epoch, StoryRefusal Why)> _droppedFlags = [];
    private readonly SortedDictionary<int, (long Epoch, StoryRefusal Why)> _droppedFolder = [];
    private readonly SortedDictionary<int, (long Epoch, StoryRefusal Why)> _droppedCounters = [];
    // Held while the owner is away (policy hold).
    private int _heldPhase;
    private readonly SortedSet<int> _heldFlags = [];
    private readonly SortedSet<int> _heldFolder = [];
    private readonly Dictionary<StoryLineKey, int> _heldLines = [];
    // Held growth of lines nobody can report any more (PruneLines): merged with the rest when the owner returns.
    private readonly SortedDictionary<int, long> _heldGrowth = [];
    private uint _mergeLine;

    /// <param name="started">False for a room whose owner has no online story yet (an empty one): it starts at zero, and
    /// nothing counts before the owner's own game enters a stage (<see cref="OwnerStarted"/>).</param>
    public RoomStoryKeeper(RoomStoryData ownerStory, int difficulty, StoryPolicy policy, bool ownerPresent, StorySets? sets = null, bool started = true)
    {
        _sets = sets ?? StorySets.Default;
        Difficulty = Math.Clamp(difficulty, 0, StorySets.Difficulties - 1);
        Policy = policy;
        OwnerPresent = ownerPresent;
        Started = started;
        Story = ownerStory.Clone();
        // The room plays at its difficulty: the working set is that difficulty's, as gameStart.lua restores it.
        StoryMerge.Rebase(Story, Difficulty, _sets);
    }

    public RoomStoryData Story { get; private set; }
    public int Difficulty { get; }
    public StoryPolicy Policy { get; }
    public bool OwnerPresent { get; private set; }
    /// <summary>The owner's online story has begun: it had one when the room opened, or the owner's game entered a stage since.</summary>
    public bool Started { get; private set; }
    /// <summary>
    /// Phase steps, flags, key items and counter growth count only from a visit at least this old (Instances:
    /// StoryMinVisitSeconds; the client sends them again): a client that hops from stage to stage gets nothing from a visit
    /// it did not play.
    /// </summary>
    public double MinVisitSeconds { get; init; } = 10;
    /// <summary>A guest's phase steps count at least this far apart (Instances:StoryPhaseStepSeconds; the original's steps
    /// follow whole dungeons). The rest waits (reason Wait) and is merged when due. The owner's own steps are not spaced:
    /// a new game's phase 2 (the first briefing) comes a few minutes after phase 1 (the lobby's first load).</summary>
    public double PhaseStepSeconds { get; init; } = 300;
    /// <summary>A guest's key items count at least this far apart (Instances:StoryKeySeconds; one pickup at a time); not the owner's.</summary>
    public double KeySeconds { get; init; } = 60;
    /// <summary>The counter lines one member may open in a room session (Instances:StoryLinesPerMember); more are refused (limit).</summary>
    public int MaxLinesPerMember { get; init; } = 256;
    /// <summary>Rises with every drop; an instance remembers the epoch its visit began in.</summary>
    public long Epoch { get; private set; }
    public int HeldCount => (_heldPhase > 0 ? 1 : 0) + _heldFlags.Count + _heldFolder.Count + _heldLines.Count + _heldGrowth.Count;
    /// <summary>Earned values waiting for their visit or the spacing (phase steps and key items), over all accounts.</summary>
    public int EarnedCount => _earned.Values.Sum(e => (e.Phase != 0 ? 1 : 0) + e.Keys.Count);
    /// <summary>Whether the member (its account; a member without one by its index) has earned values waiting (<see cref="MergeEarned"/>).</summary>
    public bool HasEarned(int member, long account) => _earned.ContainsKey(AccountOf(member, account));
    /// <summary>The engine lines the keeper remembers (taken and held).</summary>
    public int LineCount => _lines.Count + _heldLines.Count;

    /// <summary>The dropped set of this session (BARRIER_OK).</summary>
    public StoryKeys Dropped => new()
    {
        Flags = [.. _droppedFlags.Keys],
        Folder = [.. _droppedFolder.Keys],
        Counters = [.. _droppedCounters.Keys],
    };

    /// <summary>The owner's game entered a stage (the relay placed the owner in one outside the title): from now on the story
    /// counts. True when this started it.</summary>
    public bool OwnerStarted()
    {
        if (Started) return false;
        Started = true;
        return true;
    }

    /// <summary>
    /// One member's pending set. <paramref name="stage"/> is the sender's stage now (null or a UI stage: the title's
    /// character select holds a record's own story); <paramref name="visitEpoch"/> the <see cref="Epoch"/> its visit began
    /// in. Counter lines must already be the sender's own (the relay refuses lines it never put the member in and names
    /// instance 0 lines by the member). With <paramref name="visit"/> (the relay always gives it) the per-visit and
    /// per-member rules apply: everything only from a visit at least <see cref="MinVisitSeconds"/> old, one phase step and
    /// one key item per member and visit, a guest's phase steps <see cref="PhaseStepSeconds"/> and key items
    /// <see cref="KeySeconds"/> apart, at most <see cref="MaxLinesPerMember"/> counter lines per member (the rest waits,
    /// reason Wait; lines beyond the limit are refused). A phase step or key item that waits only for the one-per-visit
    /// rule or the spacing is earned: the keeper keeps it and merges it when it is due (<see cref="MergeEarned"/>), so it is
    /// not lost when the member leaves the stage that set it (round 61 design, 8.8 "Lag").
    /// </summary>
    public StoryTake Take(StoryDelta delta, string? stage, long visitEpoch, StoryVisit? visit = null)
    {
        var refused = new List<StoryRefused>();
        if (string.IsNullOrEmpty(stage) || stage.StartsWith("stagis", StringComparison.OrdinalIgnoreCase))
        {
            RefuseAll(delta, StoryRefusal.NotMember, refused);
            return new StoryTake(refused, false);
        }
        // Plausibility (8.4).
        var ok = new StoryDelta();
        var roomPhase = StoryMerge.Phase(Story, _sets);
        var reached = Math.Max(roomPhase, Policy == StoryPolicy.Hold && !OwnerPresent ? _heldPhase : 0);
        if (delta.Phase != 0)
        {
            var limit = reached + 1;
            if (delta.Phase > _sets.Phase.Count) refused.Add(new(StoryKeyKind.Phase, delta.Phase, StoryRefusal.NotStory));
            else if (delta.Phase <= roomPhase) { }
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
                // The clear comes from the last boss, after the last briefing (phase 6): a guest that BARRIERed into stagBS06
                // of a younger story got the owner's clear, and with it the next difficulty's rooms, in ten seconds (round 59).
                refused.Add(new(StoryKeyKind.Flag, f, StoryRefusal.PhaseJump));
            else ok.Flags.Add(f);
        }
        foreach (var k in delta.Folder.Distinct())
        {
            if (!_sets.IsKey(k)) refused.Add(new(StoryKeyKind.Folder, k, StoryRefusal.NotStory));
            else if (Story.Folder[k] == 0) ok.Folder.Add(k); // key items come from item pickups anywhere: no stage table
        }
        foreach (var g in delta.Counters)
        {
            if (!_sets.IsAdditive(g.Counter)) refused.Add(new(StoryKeyKind.Counter, g.Counter, StoryRefusal.NotStory));
            else if (g.Growth < 0 || g.Growth > MaxGrowth(g.Counter)) refused.Add(new(StoryKeyKind.Counter, g.Counter, StoryRefusal.Limit));
            else if (!_sets.ChangesCounter(stage, g.Counter)) refused.Add(new(StoryKeyKind.Counter, g.Counter, StoryRefusal.Stage));
            else if (g.Growth > _lines.GetValueOrDefault(new StoryLineKey(g.Counter, g.Inst, g.Visit))) ok.Counters.Add(g);
        }
        // A dropped value counts again only from an engine that set it anew: one whose visit began after the drop (the
        // barrier removed it from every engine of that line).
        StoryRefusal? Stale(SortedDictionary<int, (long Epoch, StoryRefusal Why)> dropped, int key) =>
            dropped.TryGetValue(key, out var at) && visitEpoch < at.Epoch ? at.Why : null;
        if (ok.Phase != 0 && Stale(_droppedFlags, _sets.Phase[ok.Phase - 1]) is { } phaseWhy)
        {
            refused.Add(new(StoryKeyKind.Phase, ok.Phase, phaseWhy));
            ok.Phase = 0;
        }
        foreach (var f in ok.Flags.ToList())
            if (Stale(_droppedFlags, f) is { } why)
            {
                refused.Add(new(StoryKeyKind.Flag, f, why));
                ok.Flags.Remove(f);
            }
        foreach (var k in ok.Folder.ToList())
            if (Stale(_droppedFolder, k) is { } why)
            {
                refused.Add(new(StoryKeyKind.Folder, k, why));
                ok.Folder.Remove(k);
            }
        foreach (var g in ok.Counters.ToList())
            if (Stale(_droppedCounters, g.Counter) is { } why)
            {
                refused.Add(new(StoryKeyKind.Counter, g.Counter, why));
                ok.Counters.Remove(g);
            }
        if (ok.IsEmpty) return new StoryTake(refused, false);
        // A room whose owner has no online story yet starts at zero (the user's decision of 31.10): what members do before
        // the owner's own game is in a stage never becomes the owner's story, whatever the policy. It is dropped (the next
        // barrier removes it from the engines) before the per-visit rules, so a value that waited cannot slip in later.
        if (!Started)
        {
            Drop(ok, StoryRefusal.NotStarted, refused);
            return new StoryTake(refused, false);
        }
        if (visit is { } v) PerVisit(ok, v, visitEpoch, refused);
        if (ok.IsEmpty) return new StoryTake(refused, false);
        return new StoryTake(refused, Apply(ok, refused));
    }

    /// <summary>
    /// The account's earned values that are due in its member's visit <paramref name="visit"/> in <paramref name="stage"/>
    /// (SEAMLESS.md 8.7, the round 61 design's queue): the visit at least <see cref="MinVisitSeconds"/> old and not yet
    /// given its phase step or key item, a guest's spacing passed. They are taken as if sent now, under the owner-away
    /// policy of this moment (merged, held or dropped); what the room story has meanwhile, or what was dropped after the
    /// visit that earned it began (the barrier removes it from that engine), is no longer waited for. The relay calls it
    /// a few times a second for every member with earned values.
    /// </summary>
    public StoryTake MergeEarned(StoryVisit visit, string? stage)
    {
        var account = AccountOf(visit);
        if (!_earned.TryGetValue(account, out var earned)) return new StoryTake([], false);
        var known = Math.Max(StoryMerge.Phase(Story, _sets), _heldPhase);
        if (earned.Phase != 0 && (earned.Phase <= known || DroppedSince(_droppedFlags, _sets.Phase[earned.Phase - 1], earned.PhaseEpoch)))
            earned.Phase = 0;
        foreach (var (k, epoch) in earned.Keys.ToList())
            if (Story.Folder[k] != 0 || _heldFolder.Contains(k) || DroppedSince(_droppedFolder, k, epoch))
                earned.Keys.Remove(k);
        if (earned.IsEmpty)
        {
            _earned.Remove(account);
            return new StoryTake([], false);
        }
        if (!Started || string.IsNullOrEmpty(stage) || stage.StartsWith("stagis", StringComparison.OrdinalIgnoreCase) || visit.Seconds < MinVisitSeconds)
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
            var k = earned.Keys.Keys.First();
            ok.Folder.Add(k);
            earned.Keys.Remove(k);
            tally.Keys = 1;
            last.Item2 = visit.At;
        }
        _lastSteps[account] = last;
        if (earned.IsEmpty) _earned.Remove(account);
        if (ok.IsEmpty) return new StoryTake([], false);
        var refused = new List<StoryRefused>();
        return new StoryTake(refused, Apply(ok, refused));
    }

    /// <summary>The owner-away policy on values that passed every check: dropped, held or merged. True when the story changed.</summary>
    private bool Apply(StoryDelta ok, List<StoryRefused> refused)
    {
        if (!OwnerPresent && Policy == StoryPolicy.Drop)
        {
            Drop(ok, StoryRefusal.OwnerAway, refused);
            return false;
        }
        if (!OwnerPresent && Policy == StoryPolicy.Hold)
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

    private static bool DroppedSince(SortedDictionary<int, (long Epoch, StoryRefusal Why)> dropped, int key, long visitEpoch) =>
        dropped.TryGetValue(key, out var at) && visitEpoch < at.Epoch;

    private static long AccountOf(StoryVisit v) => AccountOf(v.Member, v.Account);

    private static long AccountOf(int member, long account) => account != 0 ? account : -1 - member;

    private VisitTally Tally(StoryVisit v)
    {
        if (!_visits.TryGetValue(v.Member, out var tally) || tally.Key != v.Key) _visits[v.Member] = tally = new VisitTally(v.Key);
        return tally;
    }

    /// <summary>The owner came or went (RoomService). Returning under the hold policy merges what was held; returns whether the story changed.</summary>
    public bool SetOwnerPresent(bool present)
    {
        OwnerPresent = present;
        if (!present || HeldCount == 0) return false;
        var changed = false;
        // The held phase step by step (each step was plausible when it was offered).
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
        // The growth of held lines nobody can report any more goes in under lines of its own, used once.
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
        _heldPhase = 0;
        _heldFlags.Clear();
        _heldFolder.Clear();
        _heldLines.Clear();
        _heldGrowth.Clear();
        return changed;
    }

    /// <summary>
    /// Forgets the engine lines no member can report any more (<paramref name="reportable"/> false: no member's line set
    /// holds the instance visit, SEAMLESS.md 8.7): such a line can never come again, so what it took no longer matters. A
    /// held line's growth beyond what was taken stays held (per counter). Without this the lines grew for as long as the room
    /// lived (a client looping barriers opened about a thousand a minute). Returns how many were removed.
    /// </summary>
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

    private void Drop(StoryDelta ok, StoryRefusal why, List<StoryRefused> refused)
    {
        ++Epoch;
        if (ok.Phase != 0) _droppedFlags[_sets.Phase[ok.Phase - 1]] = (Epoch, why);
        foreach (var f in ok.Flags) _droppedFlags[f] = (Epoch, why);
        foreach (var k in ok.Folder) _droppedFolder[k] = (Epoch, why);
        foreach (var g in ok.Counters) _droppedCounters[g.Counter] = (Epoch, why);
        RefuseAll(ok, why, refused);
    }

    /// <summary>
    /// The per-visit and per-member rules (SEAMLESS.md 8.7): what a STORY may take from one member's visit. The plausibility
    /// check above works per message; without these a client that BARRIERs into stagML02 and sends six STORY messages
    /// climbs six phases in milliseconds. Everything waits (reason Wait, the client sends it again) until the visit is
    /// <see cref="MinVisitSeconds"/> old (counters too: a client looping barriers opened a line per visit); a visit gives one
    /// phase step and one key item (the original's phase steps follow whole dungeons, and key items come one pickup at a
    /// time), and a guest's phase steps and key items count <see cref="PhaseStepSeconds"/> and <see cref="KeySeconds"/>
    /// apart (one patient account no longer climbs the owner's story in minutes; the owner's own steps are not spaced, a new
    /// game's phase 2 follows phase 1 within minutes); a member opens at most <see cref="MaxLinesPerMember"/> counter lines.
    /// A phase step or key item that waits only for the one-per-visit rule or the spacing is earned (<see cref="MergeEarned"/>).
    /// </summary>
    private void PerVisit(StoryDelta ok, StoryVisit v, long visitEpoch, List<StoryRefused> refused)
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
                refused.Add(new(StoryKeyKind.Phase, ok.Phase, StoryRefusal.Wait));
                var earned = EarnedOf(account);
                if (ok.Phase > earned.Phase) (earned.Phase, earned.PhaseEpoch) = (ok.Phase, visitEpoch);
                else if (ok.Phase == earned.Phase) earned.PhaseEpoch = Math.Max(earned.PhaseEpoch, visitEpoch);
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
                refused.Add(new(StoryKeyKind.Folder, k, StoryRefusal.Wait));
                var earned = EarnedOf(account);
                earned.Keys[k] = Math.Max(earned.Keys.GetValueOrDefault(k, long.MinValue), visitEpoch);
            }
            else
            {
                ok.Folder.Add(k);
                tally.Keys = 1;
                last.Item2 = v.At;
            }
        }
        _lastSteps[account] = last;
        foreach (var g in ok.Counters.ToList())
        {
            var key = new StoryLineKey(g.Counter, g.Inst, g.Visit);
            if (_lines.ContainsKey(key) || _heldLines.ContainsKey(key)) continue;
            var opened = _linesOpened.GetValueOrDefault(account);
            if (opened >= MaxLinesPerMember)
            {
                refused.Add(new(StoryKeyKind.Counter, g.Counter, StoryRefusal.Limit));
                ok.Counters.Remove(g);
            }
            else _linesOpened[account] = opened + 1;
        }
    }

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
