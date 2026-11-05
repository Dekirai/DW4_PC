using System.Text.Json;
using DW4Online.Domain.Story;

namespace DW4Online.Tests;

/// <summary>
/// The room story rules (SEAMLESS.md 8): the embedded story sets equal data/online/story_sets.json, and the golden vectors
/// of data/online/story_golden.json (merge, pending set, rebase, application; written by tools/online/story_golden.py) give
/// the same results here as in the game client's ctest online_story.
/// </summary>
public class StoryGoldenTests
{
    private static JsonElement Load(string name)
    {
        var path = Path.Combine(AppContext.BaseDirectory, "story", name);
        return JsonDocument.Parse(File.ReadAllText(path)).RootElement;
    }

    private static int[] Ints(JsonElement e) => e.EnumerateArray().Select(x => x.GetInt32()).ToArray();

    private static RoomStoryData Room(JsonElement e)
    {
        var r = new RoomStoryData { Revision = e.GetProperty("revision").GetUInt32() };
        foreach (var f in Ints(e.GetProperty("flags"))) r.Flags[f] = 1;
        foreach (var k in Ints(e.GetProperty("folder"))) r.Folder[k] = 1;
        foreach (var p in e.GetProperty("counters").EnumerateArray()) r.Counters[p[0].GetInt32()] = p[1].GetInt32();
        return r;
    }

    private static string Describe(RoomStoryData r) =>
        $"rev {r.Revision} flags [{string.Join(",", Enumerable.Range(0, 1024).Where(i => r.Flags[i] != 0))}] folder [{string.Join(",", Enumerable.Range(0, 12).Where(i => r.Folder[i] != 0))}] " +
        $"counters [{string.Join(",", Enumerable.Range(0, 16).Where(i => r.Counters[i] != 0).Select(i => $"{i}={r.Counters[i]}"))}]";

    private static StoryValues Values(JsonElement e) => new()
    {
        Flags = new SortedDictionary<int, byte>(e.GetProperty("flags").EnumerateArray().ToDictionary(p => p[0].GetInt32(), p => (byte)p[1].GetInt32())),
        Folder = new SortedDictionary<int, byte>(e.GetProperty("folder").EnumerateArray().ToDictionary(p => p[0].GetInt32(), p => (byte)p[1].GetInt32())),
        Counters = new SortedDictionary<int, int>(e.GetProperty("counters").EnumerateArray().ToDictionary(p => p[0].GetInt32(), p => p[1].GetInt32())),
    };

    private static string Describe(StoryValues v) =>
        $"flags [{string.Join(",", v.Flags.Select(p => $"{p.Key}={p.Value}"))}] folder [{string.Join(",", v.Folder.Select(p => $"{p.Key}={p.Value}"))}] " +
        $"counters [{string.Join(",", v.Counters.Select(p => $"{p.Key}={p.Value}"))}]";

    [Fact]
    public void Embedded_sets_equal_the_census_file()
    {
        var file = StorySets.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "story", "story_sets.json")));
        var s = StorySets.Default;
        Assert.Equal(file.Phase, s.Phase);
        Assert.Equal(file.Persistent, s.Persistent);
        Assert.Equal(file.Counters, s.Counters);
        Assert.Equal(file.KeyCopies, s.KeyCopies);
        Assert.Equal(file.Working.Count, s.Working.Count);
        Assert.Equal(11, s.Keys);
        Assert.Equal([1, 2, 3, 4, 5, 6, 86], s.Phase);
        Assert.True(s.IsPersistent(19) && s.IsPersistent(392) && !s.IsPersistent(23) && !s.IsPersistent(1) && !s.IsPersistent(706));
        Assert.True(s.IsCopy(706) && s.IsCopy(700) && s.IsCopy(529) && !s.IsCopy(19));
        Assert.True(s.SetsFlag("stagML02", 2) && !s.SetsFlag("stag0101", 2));
        Assert.True(s.ChangesCounter("stag0001", 7) && s.ChangesCounter("stagML13", 1) && !s.ChangesCounter("stagML13", 7));
        for (var f = 0; f < 1024; ++f) Assert.False(s.IsCopy(f) && s.IsPersistent(f), $"copy {f} is persistent");
    }

    [Fact]
    public void Golden_merge_cases()
    {
        var cases = Load("story_golden.json").GetProperty("merge").EnumerateArray().ToList();
        Assert.True(cases.Count >= 10);
        foreach (var c in cases)
        {
            var name = c.GetProperty("name").GetString();
            var d = c.GetProperty("delta");
            var delta = new StoryDelta
            {
                Phase = (byte)d.GetProperty("phase").GetInt32(),
                Flags = [.. Ints(d.GetProperty("flags"))],
                Folder = [.. Ints(d.GetProperty("folder"))],
                Counters = d.GetProperty("counters").EnumerateArray()
                    .Select(g => new CounterGrowth(g[0].GetInt32(), g[1].GetUInt32(), g[2].GetUInt32(), g[3].GetInt32())).ToList(),
            };
            var lines = c.GetProperty("lines").EnumerateArray()
                .ToDictionary(l => new StoryLineKey(l[0].GetInt32(), l[1].GetUInt32(), l[2].GetUInt32()), l => l[3].GetInt32());
            var r = StoryMerge.Merge(Room(c.GetProperty("room")), delta, c.GetProperty("difficulty").GetInt32(), lines);
            var want = c.GetProperty("expect");
            Assert.True(Describe(Room(want.GetProperty("room"))) == Describe(r.Story), $"{name}: {Describe(r.Story)}");
            Assert.True(want.GetProperty("changed").GetBoolean() == r.Changed, $"{name}: changed");
            var refused = want.GetProperty("refused").EnumerateArray()
                .Select(x => new StoryRefused((StoryKeyKind)x[0].GetInt32(), x[1].GetInt32(), (StoryRefusal)x[2].GetInt32())).ToList();
            Assert.True(refused.SequenceEqual(r.Refused), $"{name}: refused");
            var wantLines = want.GetProperty("lines").EnumerateArray()
                .ToDictionary(l => new StoryLineKey(l[0].GetInt32(), l[1].GetUInt32(), l[2].GetUInt32()), l => l[3].GetInt32());
            Assert.True(wantLines.Count == lines.Count && wantLines.All(kv => lines.GetValueOrDefault(kv.Key, -1) == kv.Value), $"{name}: lines");
        }
    }

    [Fact]
    public void Golden_pending_rebase_and_apply_cases()
    {
        var golden = Load("story_golden.json");
        foreach (var c in golden.GetProperty("pending").EnumerateArray())
        {
            var name = c.GetProperty("name").GetString();
            var d = StoryMerge.Pending(Room(c.GetProperty("confirmed")), Values(c.GetProperty("engine")));
            var want = c.GetProperty("expect");
            Assert.True(want.GetProperty("phase").GetInt32() == d.Phase, $"{name}: phase");
            Assert.True(Ints(want.GetProperty("flags")).SequenceEqual(d.Flags), $"{name}: flags");
            Assert.True(Ints(want.GetProperty("folder")).SequenceEqual(d.Folder), $"{name}: keys");
        }
        foreach (var c in golden.GetProperty("rebase").EnumerateArray())
        {
            var r = Room(c.GetProperty("room"));
            StoryMerge.Rebase(r, c.GetProperty("difficulty").GetInt32());
            Assert.True(Describe(Room(c.GetProperty("expect"))) == Describe(r), $"{c.GetProperty("name").GetString()}: {Describe(r)}");
        }
        foreach (var c in golden.GetProperty("apply").EnumerateArray())
        {
            var name = c.GetProperty("name").GetString();
            var e = Values(c.GetProperty("engine"));
            var dropped = c.GetProperty("dropped");
            var keys = new StoryKeys
            {
                Flags = [.. Ints(dropped.GetProperty("flags"))],
                Folder = [.. Ints(dropped.GetProperty("folder"))],
                Counters = [.. Ints(dropped.GetProperty("counters"))],
            };
            var target = c.TryGetProperty("target", out var t) ? t.GetString() : null;
            var removed = StoryMerge.Apply(e, Room(c.GetProperty("room")), keys, targetStage: target);
            var want = c.GetProperty("expect");
            Assert.True(Describe(Values(want.GetProperty("engine"))) == Describe(e), $"{name}: {Describe(e)}");
            Assert.True(want.GetProperty("removed").GetBoolean() == removed, $"{name}: removed");
        }
    }

    [Fact]
    public void Blobs_round_trip_with_the_clients_bytes()
    {
        var s = new RoomStoryData { Revision = 9 };
        s.Flags[1] = 1;
        s.Folder[10] = 1;
        s.Counters[1] = 3956000;
        var b = s.ToBlob();
        Assert.Equal(1100, b.Length);
        Assert.Equal([0x20, 0x5D, 0x3C, 0x00], b[1040..1044]);
        var back = RoomStoryData.FromBlob(b, 9);
        Assert.True(back.SameValues(s) && back.Revision == 9);
        Assert.Throws<InvalidDataException>(() => RoomStoryData.FromBlob(b.AsSpan(0, 1099), 0));
        var keys = new StoryKeys { Flags = [4, 392, 1023], Folder = [5], Counters = [7] };
        Assert.Equal("030004008801ff0301050107", Convert.ToHexString(keys.ToBlob()).ToLowerInvariant());
        var k2 = StoryKeys.FromBlob(keys.ToBlob());
        Assert.Equal(keys.Flags, k2.Flags);
        Assert.Equal(keys.Counters, k2.Counters);
        Assert.Empty(new StoryKeys().ToBlob());
        foreach (var bad in new byte[][] { [1, 0, 0], [1, 0, 0, 4, 0, 0], [0, 0, 1, 12, 0], [0, 0, 0, 1, 16], [0, 0, 0, 0, 9] })
            Assert.Throws<InvalidDataException>(() => StoryKeys.FromBlob(bad));
    }

    [Fact]
    public void Record_composition_replaces_only_the_story_bytes()
    {
        var record = new byte[3316];
        for (var i = 0; i < record.Length; ++i) record[i] = (byte)(i * 7);
        var s = new RoomStoryData();
        s.Flags[406] = 1;
        s.Folder[3] = 1;
        s.Counters[7] = 0x01020304;
        var o = StoryRecord.Compose(record, s);
        Assert.Equal(record[..792], o[..792]);
        Assert.Equal(record[1892..], o[1892..]);
        Assert.Equal(1, o[792 + 406]);
        Assert.Equal(0, o[792 + 405]);
        Assert.Equal(1, o[1816 + 3]);
        Assert.Equal([1, 2, 3, 4], o[(1828 + 28)..(1828 + 32)]);
        Assert.True(StoryRecord.Extract(o).SameValues(s));
    }
}
