using System.Text.Json;

namespace DW4Online.Domain.Story;

/// <summary>A group of the story working set gameStart.lua restores from the save room's copies (one copy per difficulty).</summary>
public sealed record WorkingGroup(int First, int Count, int[] Copies);

/// <summary>
/// The story sets of SEAMLESS.md 8.2: data/online/story_sets.json, written by tools/online/story_census.py from the
/// decompiled stage scripts and embedded in this assembly (the game client compiles the same data in;
/// tests/online_story_tests.cpp and StorySetsTests compare both with the file). Phase (one-hot, the furthest wins),
/// persistent flags (OR), keys 0-10 (OR), additive counters (per engine line); the save room copies of the working set
/// are derived, never taken from a member. The setter table names the stages whose scripts set a flag (the plausibility
/// check of 8.4).
/// </summary>
public sealed class StorySets
{
    public const int FlagCount = 1024, FolderCount = 12, CounterCount = 16, Difficulties = 3;

    private readonly bool[] _phase = new bool[FlagCount];
    private readonly bool[] _persistent = new bool[FlagCount];
    private readonly bool[] _copy = new bool[FlagCount];

    public IReadOnlyList<int> Phase { get; }
    public IReadOnlyList<int[]> PhaseCopies { get; }
    public IReadOnlyList<WorkingGroup> Working { get; }
    public IReadOnlyList<int> Persistent { get; }
    public int Keys { get; }
    public IReadOnlyList<int> KeyCopies { get; }
    public IReadOnlyList<int> Counters { get; }
    /// <summary>Flag -> the stages (lower case) whose scripts set it to 1; phase and persistent flags only.</summary>
    public IReadOnlyDictionary<int, IReadOnlySet<string>> FlagSetters { get; }
    /// <summary>Additive counter -> the stages (lower case) that change it.</summary>
    public IReadOnlyDictionary<int, IReadOnlySet<string>> CounterSetters { get; }

    private StorySets(IReadOnlyList<int> phase, IReadOnlyList<int[]> phaseCopies, IReadOnlyList<WorkingGroup> working,
        IReadOnlyList<int> persistent, int keys, IReadOnlyList<int> keyCopies, IReadOnlyList<int> counters,
        IReadOnlyDictionary<int, IReadOnlySet<string>> flagSetters, IReadOnlyDictionary<int, IReadOnlySet<string>> counterSetters)
    {
        Phase = phase;
        PhaseCopies = phaseCopies;
        Working = working;
        Persistent = persistent;
        Keys = keys;
        KeyCopies = keyCopies;
        Counters = counters;
        FlagSetters = flagSetters;
        CounterSetters = counterSetters;
        foreach (var f in phase) _phase[f] = true;
        foreach (var f in persistent) _persistent[f] = true;
        for (var d = 0; d < Difficulties; ++d)
        {
            foreach (var f in phaseCopies[d]) _copy[f] = true;
            foreach (var g in working)
                for (var j = 0; j < g.Count; ++j) _copy[g.Copies[d] + j] = true;
            for (var k = 0; k <= keys; ++k) _copy[keyCopies[d] + k] = true;
        }
    }

    /// <summary>The embedded data/online/story_sets.json.</summary>
    public static StorySets Default { get; } = LoadEmbedded();

    public bool IsPhaseFlag(int flag) => flag is >= 0 and < FlagCount && _phase[flag];
    public bool IsPersistent(int flag) => flag is >= 0 and < FlagCount && _persistent[flag];
    public bool IsKey(int key) => key >= 0 && key < Keys;
    public bool IsAdditive(int counter) => Counters.Contains(counter);
    /// <summary>A derived save room copy (phase, working set or key copy of any difficulty).</summary>
    public bool IsCopy(int flag) => flag is >= 0 and < FlagCount && _copy[flag];

    /// <summary>Whether a script of <paramref name="stage"/> sets the flag (case does not matter).</summary>
    public bool SetsFlag(string stage, int flag) => FlagSetters.TryGetValue(flag, out var s) && s.Contains(stage.ToLowerInvariant());

    public bool ChangesCounter(string stage, int counter) =>
        CounterSetters.TryGetValue(counter, out var s) && s.Contains(stage.ToLowerInvariant());

    private static StorySets LoadEmbedded()
    {
        using var stream = typeof(StorySets).Assembly.GetManifestResourceStream("DW4Online.Domain.Story.story_sets.json")
                           ?? throw new InvalidOperationException("story_sets.json is not embedded");
        using var reader = new StreamReader(stream);
        return Parse(reader.ReadToEnd());
    }

    public static StorySets Parse(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var r = doc.RootElement;
        if (r.GetProperty("format").GetString() != "dw4-story-sets/1") throw new InvalidDataException("unknown story sets format");
        static int[] Ints(JsonElement e) => e.EnumerateArray().Select(x => x.GetInt32()).ToArray();
        static IReadOnlyDictionary<int, IReadOnlySet<string>> Setters(JsonElement e) => e.EnumerateObject().ToDictionary(
            p => int.Parse(p.Name, System.Globalization.CultureInfo.InvariantCulture),
            p => (IReadOnlySet<string>)p.Value.EnumerateArray().Select(s => s.GetString()!.ToLowerInvariant()).ToHashSet(StringComparer.Ordinal));
        var phaseCopies = r.GetProperty("phaseCopies").EnumerateArray().Select(Ints).ToArray();
        var working = r.GetProperty("working").EnumerateArray()
            .Select(g => new WorkingGroup(g.GetProperty("first").GetInt32(), g.GetProperty("count").GetInt32(), Ints(g.GetProperty("copies")))).ToArray();
        var setters = r.GetProperty("setters");
        return new StorySets(Ints(r.GetProperty("phase")), phaseCopies, working, Ints(r.GetProperty("persistent")), r.GetProperty("keys").GetInt32(),
            Ints(r.GetProperty("keyCopies")), Ints(r.GetProperty("counters")), Setters(setters.GetProperty("flags")), Setters(setters.GetProperty("counters")));
    }
}
