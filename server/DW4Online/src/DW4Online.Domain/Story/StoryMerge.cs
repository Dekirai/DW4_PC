using System.Buffers.Binary;

namespace DW4Online.Domain.Story;

/// <summary>
/// The room story (SEAMLESS.md 8.4): BASE_FLAG (1024 bytes), BASE_FLAGFOLDER (12) and BASE_COUNTER (16 x i32) of the room
/// owner's online story, with its revision. On the wire (STORY_ACK, BARRIER_OK) it is the 1,100-byte story blob: flags,
/// folder, counters little endian.
/// </summary>
public sealed class RoomStoryData
{
    public const int BlobBytes = StorySets.FlagCount + StorySets.FolderCount + StorySets.CounterCount * 4;

    public byte[] Flags { get; } = new byte[StorySets.FlagCount];
    public byte[] Folder { get; } = new byte[StorySets.FolderCount];
    public int[] Counters { get; } = new int[StorySets.CounterCount];
    public uint Revision { get; set; }

    public RoomStoryData Clone()
    {
        var c = new RoomStoryData { Revision = Revision };
        Flags.CopyTo(c.Flags, 0);
        Folder.CopyTo(c.Folder, 0);
        Counters.CopyTo(c.Counters, 0);
        return c;
    }

    /// <summary>A new game's story: no flag, no key item, every counter 0 (an account without an online story, or one the
    /// online character's reset emptied).</summary>
    public bool IsEmpty => !Flags.AsSpan().ContainsAnyExcept((byte)0) && !Folder.AsSpan().ContainsAnyExcept((byte)0) &&
                           !Counters.AsSpan().ContainsAnyExcept(0);

    public bool SameValues(RoomStoryData o) => Flags.AsSpan().SequenceEqual(o.Flags) && Folder.AsSpan().SequenceEqual(o.Folder) &&
                                               Counters.AsSpan().SequenceEqual(o.Counters);

    public byte[] ToBlob()
    {
        var b = new byte[BlobBytes];
        Flags.CopyTo(b, 0);
        Folder.CopyTo(b, StorySets.FlagCount);
        for (var i = 0; i < Counters.Length; ++i)
            BinaryPrimitives.WriteInt32LittleEndian(b.AsSpan(StorySets.FlagCount + StorySets.FolderCount + 4 * i), Counters[i]);
        return b;
    }

    public static RoomStoryData FromBlob(ReadOnlySpan<byte> b, uint revision)
    {
        if (b.Length != BlobBytes) throw new InvalidDataException($"a story blob has {b.Length} bytes, not {BlobBytes}");
        var s = new RoomStoryData { Revision = revision };
        b[..StorySets.FlagCount].CopyTo(s.Flags);
        b.Slice(StorySets.FlagCount, StorySets.FolderCount).CopyTo(s.Folder);
        for (var i = 0; i < s.Counters.Length; ++i)
            s.Counters[i] = BinaryPrimitives.ReadInt32LittleEndian(b.Slice(StorySets.FlagCount + StorySets.FolderCount + 4 * i, 4));
        return s;
    }

    /// <summary>The counters as stored (online_story.counters): 16 x i32 little endian.</summary>
    public byte[] CounterBytes()
    {
        var b = new byte[StorySets.CounterCount * 4];
        for (var i = 0; i < Counters.Length; ++i) BinaryPrimitives.WriteInt32LittleEndian(b.AsSpan(4 * i), Counters[i]);
        return b;
    }

    public static RoomStoryData FromColumns(uint revision, byte[] flags, byte[] folder, byte[] counters)
    {
        var s = new RoomStoryData { Revision = revision };
        flags.AsSpan(0, Math.Min(flags.Length, StorySets.FlagCount)).CopyTo(s.Flags);
        folder.AsSpan(0, Math.Min(folder.Length, StorySets.FolderCount)).CopyTo(s.Folder);
        for (var i = 0; i < s.Counters.Length && 4 * i + 4 <= counters.Length; ++i)
            s.Counters[i] = BinaryPrimitives.ReadInt32LittleEndian(counters.AsSpan(4 * i, 4));
        return s;
    }
}

/// <summary>The growth of an additive counter in one engine line (the instance and visit it was made in).</summary>
public readonly record struct CounterGrowth(int Counter, uint Inst, uint Visit, int Growth);

/// <summary>The key of an engine line's growth per counter: the room takes each line's cumulative growth once.</summary>
public readonly record struct StoryLineKey(int Counter, uint Inst, uint Visit);

/// <summary>A member's pending set (STORY): a phase claim (0 none, 1-7), persistent flags, keys, counter growth.</summary>
public sealed class StoryDelta
{
    public byte Phase { get; set; }
    public List<int> Flags { get; init; } = [];
    public List<int> Folder { get; init; } = [];
    public List<CounterGrowth> Counters { get; init; } = [];
    public bool IsEmpty => Phase == 0 && Flags.Count == 0 && Folder.Count == 0 && Counters.Count == 0;
}

/// <summary>Why the server did not take a key (STORY_ACK; the client's engine::StoryRefusal).</summary>
public enum StoryRefusal : byte
{
    None = 0,
    /// <summary>Policy drop: refused and put into the room's dropped set.</summary>
    OwnerAway = 1,
    /// <summary>Policy hold: kept until the owner returns (not a loss).</summary>
    Held = 2,
    /// <summary>The sender's instance stage never sets it.</summary>
    Stage = 3,
    /// <summary>More than one phase step beyond the room's.</summary>
    PhaseJump = 4,
    /// <summary>Too many STORY messages (the client sends it again).</summary>
    Rate = 5,
    /// <summary>The sender is in no stage instance (the title, a UI stage).</summary>
    NotMember = 6,
    /// <summary>Never merged (a temporary, derived or unknown value).</summary>
    NotStory = 7,
    /// <summary>A counter growth beyond the plausible, or a counter line this member was never in.</summary>
    Limit = 8,
    /// <summary>Not yet (the client sends it again): the sender's visit is younger than Instances:StoryMinVisitSeconds, or it
    /// already gave its one phase step or key item, or the member's last phase step or key item is too recent.</summary>
    Wait = 9,
    /// <summary>The room owner's online story has not started (SEAMLESS.md 8.6): an owner without a story starts at zero, so
    /// what members do before the owner's own game enters a stage is dropped like the owner-away policy drop.</summary>
    NotStarted = 10,
    /// <summary>Earned (round G, SEAMLESS.md 8.8): the value passed every check and waits only for the one-per-visit rule or a
    /// guest's spacing; the keeper merges it when it is due (<see cref="RoomStoryKeeper.MergeEarned"/>), so the client does
    /// not send it again (like Held, without its notice). Protocol 47; the keeper answered Wait before.</summary>
    Queued = 11,
}

public enum StoryKeyKind : byte { Phase = 0, Flag = 1, Folder = 2, Counter = 3 }

/// <summary>One refused key of a STORY_ACK.</summary>
public readonly record struct StoryRefused(StoryKeyKind Kind, int Index, StoryRefusal Reason);

/// <summary>Story keys: a dropped set (8.6) as BARRIER_OK carries it. Blob: u16 n + n x u16 flags | u8 n + n x u8 keys | u8 n + n x u8 counters; empty = none.</summary>
public sealed class StoryKeys
{
    public List<int> Flags { get; init; } = [];
    public List<int> Folder { get; init; } = [];
    public List<int> Counters { get; init; } = [];
    public bool IsEmpty => Flags.Count == 0 && Folder.Count == 0 && Counters.Count == 0;

    public byte[] ToBlob()
    {
        if (IsEmpty) return [];
        var b = new List<byte>(4 + 2 * Flags.Count + Folder.Count + Counters.Count);
        b.Add((byte)Flags.Count);
        b.Add((byte)(Flags.Count >> 8));
        foreach (var f in Flags)
        {
            b.Add((byte)f);
            b.Add((byte)(f >> 8));
        }
        b.Add((byte)Folder.Count);
        foreach (var k in Folder) b.Add((byte)k);
        b.Add((byte)Counters.Count);
        foreach (var c in Counters) b.Add((byte)c);
        return b.ToArray();
    }

    public static StoryKeys FromBlob(ReadOnlySpan<byte> b)
    {
        var k = new StoryKeys();
        if (b.IsEmpty) return k;
        var at = 0;
        void Need(int n, ReadOnlySpan<byte> s)
        {
            if (s.Length - at < n) throw new InvalidDataException("a story key set is cut short");
        }
        Need(2, b);
        var nf = b[0] | b[1] << 8;
        at = 2;
        if (nf > StorySets.FlagCount) throw new InvalidDataException("a story key set holds too many flags");
        Need(2 * nf, b);
        for (var i = 0; i < nf; ++i, at += 2)
        {
            var f = b[at] | b[at + 1] << 8;
            if (f >= StorySets.FlagCount) throw new InvalidDataException("a story key set names a flag beyond 1023");
            k.Flags.Add(f);
        }
        Need(1, b);
        var nk = b[at++];
        if (nk > StorySets.FolderCount) throw new InvalidDataException("a story key set holds too many keys");
        Need(nk, b);
        for (var i = 0; i < nk; ++i)
        {
            if (b[at] >= StorySets.FolderCount) throw new InvalidDataException("a story key set names a key beyond 11");
            k.Folder.Add(b[at++]);
        }
        Need(1, b);
        var nc = b[at++];
        if (nc > StorySets.CounterCount) throw new InvalidDataException("a story key set holds too many counters");
        Need(nc, b);
        for (var i = 0; i < nc; ++i)
        {
            if (b[at] >= StorySets.CounterCount) throw new InvalidDataException("a story key set names a counter beyond 15");
            k.Counters.Add(b[at++]);
        }
        if (at != b.Length) throw new InvalidDataException("a story key set has trailing bytes");
        return k;
    }
}

/// <summary>An engine's story values as it keeps them: sparse, an absent key reads 0 (the fake clients' engine).</summary>
public sealed class StoryValues
{
    public SortedDictionary<int, byte> Flags { get; init; } = [];
    public SortedDictionary<int, byte> Folder { get; init; } = [];
    public SortedDictionary<int, int> Counters { get; init; } = [];
}

/// <summary>The outcome of a merge: the story, the keys refused by the rules, whether anything changed.</summary>
public sealed record StoryMergeResult(RoomStoryData Story, IReadOnlyList<StoryRefused> Refused, bool Changed);

/// <summary>
/// The story rules of SEAMLESS.md 8.2-8.6, as the client's src/engine/online_story.cpp has them; both are held to the
/// golden vectors of data/online/story_golden.json (StoryGoldenTests, the ctest online_story).
/// </summary>
public static class StoryMerge
{
    /// <summary>The phase index 0-7 of a story (the furthest phase flag set).</summary>
    public static int Phase(RoomStoryData s, StorySets? sets = null)
    {
        sets ??= StorySets.Default;
        for (var i = sets.Phase.Count; i >= 1; --i)
            if (s.Flags[sets.Phase[i - 1]] != 0) return i;
        return 0;
    }

    public static int Phase(StoryValues v, StorySets? sets = null)
    {
        sets ??= StorySets.Default;
        for (var i = sets.Phase.Count; i >= 1; --i)
            if (v.Flags.GetValueOrDefault(sets.Phase[i - 1]) != 0) return i;
        return 0;
    }

    /// <summary>The pending set of an engine against the confirmed story, without counters (8.3).</summary>
    public static StoryDelta Pending(RoomStoryData confirmed, StoryValues engine, StorySets? sets = null)
    {
        sets ??= StorySets.Default;
        var d = new StoryDelta();
        var pe = Phase(engine, sets);
        if (pe > Phase(confirmed, sets)) d.Phase = (byte)pe;
        foreach (var (f, v) in engine.Flags)
            if (v != 0 && sets.IsPersistent(f) && confirmed.Flags[f] == 0) d.Flags.Add(f);
        foreach (var (k, v) in engine.Folder)
            if (v != 0 && sets.IsKey(k) && confirmed.Folder[k] == 0) d.Folder.Add(k);
        return d;
    }

    /// <summary>
    /// A delta into the room story (no plausibility, no owner policy): the phase rises when at most one step above the
    /// room's, flags and keys are ORed, a counter grows by the line's growth beyond what <paramref name="lines"/> took;
    /// then the copies of <paramref name="difficulty"/> are derived and the revision rises when anything changed.
    /// </summary>
    public static StoryMergeResult Merge(RoomStoryData room, StoryDelta delta, int difficulty, Dictionary<StoryLineKey, int> lines, StorySets? sets = null)
    {
        sets ??= StorySets.Default;
        var s = room.Clone();
        var refused = new List<StoryRefused>();
        var changed = false;
        if (delta.Phase != 0)
        {
            var pr = Phase(room, sets);
            if (delta.Phase > sets.Phase.Count) refused.Add(new StoryRefused(StoryKeyKind.Phase, delta.Phase, StoryRefusal.NotStory));
            else if (delta.Phase > pr)
            {
                if (delta.Phase <= pr + 1)
                {
                    SetPhase(s, delta.Phase, sets);
                    changed = true;
                }
                else refused.Add(new StoryRefused(StoryKeyKind.Phase, delta.Phase, StoryRefusal.PhaseJump));
            }
        }
        var seen = new HashSet<int>();
        foreach (var f in delta.Flags)
        {
            if (!seen.Add(f)) continue;
            if (!sets.IsPersistent(f))
            {
                refused.Add(new StoryRefused(StoryKeyKind.Flag, f, StoryRefusal.NotStory));
                continue;
            }
            if (s.Flags[f] == 0)
            {
                s.Flags[f] = 1;
                changed = true;
            }
        }
        seen.Clear();
        foreach (var k in delta.Folder)
        {
            if (!seen.Add(k)) continue;
            if (!sets.IsKey(k))
            {
                refused.Add(new StoryRefused(StoryKeyKind.Folder, k, StoryRefusal.NotStory));
                continue;
            }
            if (s.Folder[k] == 0)
            {
                s.Folder[k] = 1;
                changed = true;
            }
        }
        foreach (var g in delta.Counters)
        {
            if (!sets.IsAdditive(g.Counter))
            {
                refused.Add(new StoryRefused(StoryKeyKind.Counter, g.Counter, StoryRefusal.NotStory));
                continue;
            }
            if (g.Growth < 0)
            {
                refused.Add(new StoryRefused(StoryKeyKind.Counter, g.Counter, StoryRefusal.Limit));
                continue;
            }
            var key = new StoryLineKey(g.Counter, g.Inst, g.Visit);
            var taken = lines.GetValueOrDefault(key);
            lines[key] = taken;
            if (g.Growth <= taken) continue;
            s.Counters[g.Counter] = (int)Math.Min((long)s.Counters[g.Counter] + (g.Growth - taken), int.MaxValue);
            lines[key] = g.Growth;
            changed = true;
        }
        if (changed)
        {
            DeriveCopies(s, difficulty, sets);
            ++s.Revision;
        }
        return new StoryMergeResult(s, refused, changed);
    }

    /// <summary>The copies of <paramref name="difficulty"/> from the working set and the keys (stagIS12's start.lua).</summary>
    public static void DeriveCopies(RoomStoryData s, int difficulty, StorySets? sets = null)
    {
        sets ??= StorySets.Default;
        if (difficulty is < 0 or >= StorySets.Difficulties) return;
        for (var i = 0; i < sets.Phase.Count; ++i) s.Flags[sets.PhaseCopies[difficulty][i]] = s.Flags[sets.Phase[i]] != 0 ? (byte)1 : (byte)0;
        foreach (var g in sets.Working)
            for (var j = 0; j < g.Count; ++j)
                s.Flags[g.Copies[difficulty] + j] = s.Flags[g.First + j] != 0 ? (byte)1 : (byte)0;
        for (var k = 0; k < sets.Keys; ++k) s.Flags[sets.KeyCopies[difficulty] + k] = s.Folder[k] != 0 ? (byte)1 : (byte)0;
    }

    /// <summary>
    /// Whether a stored story shows the training was played (round G, SEAMLESS.md 8.8; stories saved before flag 503 was
    /// story never hold it): 503 itself, phase 3 or later in the working set or any difficulty's copies, COUNTER 7 above 0
    /// (a training attempt), any key item or key copy, or a training proof flag (<see cref="StorySets.ProvesTraining"/>)
    /// or one of its copies. An owner in phase 2 who never left the lobby keeps 503 = 0, as he would offline.
    /// </summary>
    public static bool PlayedTraining(RoomStoryData s, StorySets? sets = null)
    {
        sets ??= StorySets.Default;
        if (s.Flags[sets.TrainingFlag] != 0) return true;
        for (var i = 2; i < sets.Phase.Count; ++i)
        {
            if (s.Flags[sets.Phase[i]] != 0) return true;
            for (var d = 0; d < StorySets.Difficulties; ++d)
                if (s.Flags[sets.PhaseCopies[d][i]] != 0) return true;
        }
        if (s.Counters[7] > 0) return true;
        for (var k = 0; k < sets.Keys; ++k)
        {
            if (s.Folder[k] != 0) return true;
            for (var d = 0; d < StorySets.Difficulties; ++d)
                if (s.Flags[sets.KeyCopies[d] + k] != 0) return true;
        }
        foreach (var f in sets.Persistent)
        {
            if (!sets.ProvesTraining(f)) continue;
            if (s.Flags[f] != 0) return true;
            foreach (var g in sets.Working)
                if (f >= g.First && f < g.First + g.Count)
                    for (var d = 0; d < StorySets.Difficulties; ++d)
                        if (s.Flags[g.Copies[d] + f - g.First] != 0) return true;
        }
        return false;
    }

    /// <summary>The working set and the keys from the copies of <paramref name="difficulty"/> (gameStart.lua): a room opens with
    /// it. Flag 503 is set when the stored story shows the training was played (<see cref="PlayedTraining"/>, read before the
    /// rebase, so a Hard room of an owner who cleared Normal keeps it: 503 has no difficulty copies).</summary>
    public static void Rebase(RoomStoryData s, int difficulty, StorySets? sets = null)
    {
        sets ??= StorySets.Default;
        if (difficulty is < 0 or >= StorySets.Difficulties) return;
        var trained = PlayedTraining(s, sets);
        for (var i = 0; i < sets.Phase.Count; ++i) s.Flags[sets.Phase[i]] = s.Flags[sets.PhaseCopies[difficulty][i]] != 0 ? (byte)1 : (byte)0;
        foreach (var g in sets.Working)
            for (var j = 0; j < g.Count; ++j)
                s.Flags[g.First + j] = s.Flags[g.Copies[difficulty] + j] != 0 ? (byte)1 : (byte)0;
        for (var k = 0; k < StorySets.FolderCount; ++k)
            s.Folder[k] = k < sets.Keys && s.Flags[sets.KeyCopies[difficulty] + k] != 0 ? (byte)1 : (byte)0;
        if (trained) s.Flags[sets.TrainingFlag] = 1;
    }

    /// <summary>
    /// A barrier's room story into an engine (8.5, 8.6): the phase up to the room's, persistent flags, keys and counters the
    /// room has; then the dropped values the engine holds go back to the room's. Only keys whose value changes are touched.
    /// Returns whether a dropped value was removed. <paramref name="targetStage"/>: the barrier's target stage; one into the
    /// training field leaves flag 503 alone both ways (round G, 8.8: the retry cleared it on purpose).
    /// </summary>
    public static bool Apply(StoryValues e, RoomStoryData room, StoryKeys dropped, StorySets? sets = null, string? targetStage = null)
    {
        sets ??= StorySets.Default;
        var removed = false;
        var keep = string.Equals(targetStage, StorySets.TrainingStage, StringComparison.OrdinalIgnoreCase) ? sets.TrainingFlag : -1;
        var df = dropped.Flags.ToHashSet();
        var dk = dropped.Folder.ToHashSet();
        var dc = dropped.Counters.ToHashSet();
        var pe = Phase(e, sets);
        var pr = Phase(room, sets);
        var target = Math.Max(pe, pr);
        if (pe > pr && df.Contains(sets.Phase[pe - 1]))
        {
            target = pr;
            removed = true;
        }
        if (target != pe)
            for (var i = 1; i <= sets.Phase.Count; ++i) Put(e.Flags, sets.Phase[i - 1], (byte)(i == target ? 1 : 0));
        foreach (var f in sets.Persistent)
            if (f != keep && room.Flags[f] != 0 && e.Flags.GetValueOrDefault(f) == 0) Put(e.Flags, f, room.Flags[f]);
        foreach (var f in df.Order())
            if (f != keep && sets.IsPersistent(f) && room.Flags[f] == 0 && Put(e.Flags, f, (byte)0)) removed = true;
        for (var k = 0; k < sets.Keys; ++k)
            if (room.Folder[k] != 0 && e.Folder.GetValueOrDefault(k) == 0) Put(e.Folder, k, room.Folder[k]);
        foreach (var k in dk.Order())
            if (sets.IsKey(k) && room.Folder[k] == 0 && Put(e.Folder, k, (byte)0)) removed = true;
        foreach (var c in sets.Counters)
        {
            var rv = room.Counters[c];
            var ev = e.Counters.GetValueOrDefault(c);
            if (dc.Contains(c))
            {
                if (ev > rv) removed = true;
                Put(e.Counters, c, rv);
            }
            else if (rv > ev) Put(e.Counters, c, rv);
        }
        return removed;
    }

    private static void SetPhase(RoomStoryData s, int index, StorySets sets)
    {
        for (var i = 1; i <= sets.Phase.Count; ++i) s.Flags[sets.Phase[i - 1]] = i == index ? (byte)1 : (byte)0;
    }

    private static bool Put<T>(SortedDictionary<int, T> m, int key, T value) where T : struct, IEquatable<T>
    {
        if (!m.TryGetValue(key, out var old))
        {
            if (value.Equals(default)) return false;
            m[key] = value;
            return true;
        }
        if (old.Equals(value)) return false;
        m[key] = value;
        return true;
    }
}
