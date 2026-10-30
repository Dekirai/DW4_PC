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
/// The room story of one room session (SEAMLESS.md 8.3-8.6): the owner's online story rebased on the room's difficulty,
/// the members' pending sets checked for plausibility (the sender's instance stage must set the flag or change the
/// counter, the phase rises at most one step, counter growth within limits) and taken by the owner-away policy: merged,
/// dropped (the dropped set every BARRIER_OK carries; a dropped value counts again only from an instance whose visit began
/// after the drop, that is from an engine the barrier cleaned and that set it anew) or held until the owner returns. Not
/// thread-safe: the relay calls it under the room lock.
/// </summary>
public sealed class RoomStoryKeeper
{
    /// <summary>The largest growth one engine line may report: COUNTER 1 counts donated bits, COUNTER 7 stag0001's attempts.</summary>
    public static int MaxGrowth(int counter) => counter == 7 ? 100_000 : 100_000_000;

    private readonly StorySets _sets;
    private readonly Dictionary<StoryLineKey, int> _lines = [];
    // Dropped keys with the epoch of their last drop.
    private readonly SortedDictionary<int, long> _droppedFlags = [];
    private readonly SortedDictionary<int, long> _droppedFolder = [];
    private readonly SortedDictionary<int, long> _droppedCounters = [];
    // Held while the owner is away (policy hold).
    private int _heldPhase;
    private readonly SortedSet<int> _heldFlags = [];
    private readonly SortedSet<int> _heldFolder = [];
    private readonly Dictionary<StoryLineKey, int> _heldLines = [];

    public RoomStoryKeeper(RoomStoryData ownerStory, int difficulty, StoryPolicy policy, bool ownerPresent, StorySets? sets = null)
    {
        _sets = sets ?? StorySets.Default;
        Difficulty = Math.Clamp(difficulty, 0, StorySets.Difficulties - 1);
        Policy = policy;
        OwnerPresent = ownerPresent;
        Story = ownerStory.Clone();
        // The room plays at its difficulty: the working set is that difficulty's, as gameStart.lua restores it.
        StoryMerge.Rebase(Story, Difficulty, _sets);
    }

    public RoomStoryData Story { get; private set; }
    public int Difficulty { get; }
    public StoryPolicy Policy { get; }
    public bool OwnerPresent { get; private set; }
    /// <summary>Rises with every drop; an instance remembers the epoch its visit began in.</summary>
    public long Epoch { get; private set; }
    public int HeldCount => (_heldPhase > 0 ? 1 : 0) + _heldFlags.Count + _heldFolder.Count + _heldLines.Count;

    /// <summary>The dropped set of this session (BARRIER_OK).</summary>
    public StoryKeys Dropped => new()
    {
        Flags = [.. _droppedFlags.Keys],
        Folder = [.. _droppedFolder.Keys],
        Counters = [.. _droppedCounters.Keys],
    };

    /// <summary>
    /// One member's pending set. <paramref name="stage"/> is the sender's instance stage now (null or a UI stage: the
    /// title's character select holds a record's own story); <paramref name="visitEpoch"/> the <see cref="Epoch"/> its
    /// instance's visit began in. Counter lines must already name the sender's own namespace for instance 0.
    /// </summary>
    public StoryTake Take(StoryDelta delta, string? stage, long visitEpoch)
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
        if (delta.Phase != 0)
        {
            var limit = Math.Max(roomPhase, Policy == StoryPolicy.Hold && !OwnerPresent ? _heldPhase : 0) + 1;
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
        bool Stale(SortedDictionary<int, long> dropped, int key) => dropped.TryGetValue(key, out var at) && visitEpoch < at;
        if (ok.Phase != 0 && Stale(_droppedFlags, _sets.Phase[ok.Phase - 1]))
        {
            refused.Add(new(StoryKeyKind.Phase, ok.Phase, StoryRefusal.OwnerAway));
            ok.Phase = 0;
        }
        foreach (var f in ok.Flags.Where(f => Stale(_droppedFlags, f)).ToList())
        {
            refused.Add(new(StoryKeyKind.Flag, f, StoryRefusal.OwnerAway));
            ok.Flags.Remove(f);
        }
        foreach (var k in ok.Folder.Where(k => Stale(_droppedFolder, k)).ToList())
        {
            refused.Add(new(StoryKeyKind.Folder, k, StoryRefusal.OwnerAway));
            ok.Folder.Remove(k);
        }
        foreach (var g in ok.Counters.Where(g => Stale(_droppedCounters, g.Counter)).ToList())
        {
            refused.Add(new(StoryKeyKind.Counter, g.Counter, StoryRefusal.OwnerAway));
            ok.Counters.Remove(g);
        }
        if (ok.IsEmpty) return new StoryTake(refused, false);
        if (!OwnerPresent && Policy == StoryPolicy.Drop)
        {
            ++Epoch;
            if (ok.Phase != 0) _droppedFlags[_sets.Phase[ok.Phase - 1]] = Epoch;
            foreach (var f in ok.Flags) _droppedFlags[f] = Epoch;
            foreach (var k in ok.Folder) _droppedFolder[k] = Epoch;
            foreach (var g in ok.Counters) _droppedCounters[g.Counter] = Epoch;
            RefuseAll(ok, StoryRefusal.OwnerAway, refused);
            return new StoryTake(refused, false);
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
            return new StoryTake(refused, false);
        }
        var r = StoryMerge.Merge(Story, ok, Difficulty, _lines, _sets);
        Story = r.Story;
        refused.AddRange(r.Refused);
        return new StoryTake(refused, r.Changed);
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
        var r = StoryMerge.Merge(Story, rest, Difficulty, _lines, _sets);
        Story = r.Story;
        changed |= r.Changed;
        _heldPhase = 0;
        _heldFlags.Clear();
        _heldFolder.Clear();
        _heldLines.Clear();
        return changed;
    }

    private static void RefuseAll(StoryDelta d, StoryRefusal why, List<StoryRefused> into)
    {
        if (d.Phase != 0) into.Add(new(StoryKeyKind.Phase, d.Phase, why));
        foreach (var f in d.Flags) into.Add(new(StoryKeyKind.Flag, f, why));
        foreach (var k in d.Folder) into.Add(new(StoryKeyKind.Folder, k, why));
        foreach (var g in d.Counters) into.Add(new(StoryKeyKind.Counter, g.Counter, why));
    }
}
