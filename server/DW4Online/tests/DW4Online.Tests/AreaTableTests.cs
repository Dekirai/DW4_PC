using System.Text.Json;
using DW4Online.Domain.World;

namespace DW4Online.Tests;

/// <summary>
/// The area table (data/online/areas.json, format dw4-areas/2, SEAMLESS.md 19.30.2 and 19.24): the client's table with
/// its area, kind, dungeon and gate columns explicit; every arrival stage from home (the four gate stages, the 27 Security
/// Gate stages with their 29 gates, the quest start) has exactly one area; each area's scope (the stages reachable from
/// its arrival stages without a hub stage) is pinned on the graph of all stage scripts (data/online/stage_graph.json,
/// tools/export_stage_graph.py from the decompiled area.afs scripts): Death Valley's with the Fortress (fields, decision
/// 39) and stag0205's plateau. The graph holds only literal targets; stages reached through a variable (the river
/// stag0103, the tower tops' boss stages, dng310f3, the quest dungeons' later floors) are outside it and so outside the
/// pinned scopes. The client's ctest dw4_presence_tests compares the file with presence_data.cpp's table byte for byte.
/// </summary>
public class AreaTableTests
{
    private static readonly AreaTable Table = AreaTable.Default;

    private sealed record Edge(string From, string To, int Point, string Script);

    private static (List<Edge> Edges, List<(string Stage, int Point)> Gates) Graph()
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "data", "stage_graph.json")));
        Assert.Equal("dw4-stage-graph/1", doc.RootElement.GetProperty("format").GetString());
        var edges = doc.RootElement.GetProperty("edges").EnumerateArray()
            .Select(e => new Edge(e.GetProperty("from").GetString()!, e.GetProperty("to").GetString()!, e.GetProperty("point").GetInt32(), e.GetProperty("script").GetString()!))
            .ToList();
        var gates = doc.RootElement.GetProperty("securityGates").EnumerateArray()
            .Select(g => (g.GetProperty("stage").GetString()!, g.GetProperty("point").GetInt32())).ToList();
        return (edges, gates);
    }

    [Fact]
    public void The_embedded_table_is_the_checked_in_file_with_explicit_columns()
    {
        using var stream = File.OpenRead(Path.Combine(AppContext.BaseDirectory, "data", "areas.json"));
        var file = AreaTable.Parse(stream);
        Assert.Equal(file.All, Table.All);
        Assert.Equal(98, Table.All.Count);
        Assert.Equal(Table.All.Count, Table.All.Select(s => s.Stage).Distinct().Count());
        Assert.All(Table.All, s => Assert.Equal(s.Stage.ToLowerInvariant(), s.Stage));

        // Hub and UI stages: no area, kind hub. Fields: an area, no dungeon. Dungeon and boss stages: an area and a dungeon key.
        Assert.All(Table.All.Where(s => s.Stage.StartsWith("stagis") || s.Stage.StartsWith("stagml")), s => Assert.Equal((null, "hub", null, false), (s.Area, s.Kind, s.Dungeon, s.Gate)));
        Assert.All(Table.All.Where(s => s.Kind == AreaKinds.Field), s => Assert.True(s.Area is not null && s.Dungeon is null, s.Stage));
        Assert.All(Table.All.Where(s => s.IsDungeon), s => Assert.True(s.Area is not null && s.Dungeon is not null, s.Stage));
        Assert.All(Table.All.Where(s => s.Kind == AreaKinds.Dungeon && !s.Stage.StartsWith("dngq")), s => Assert.Equal(s.Stage[..6], s.Dungeon));

        // The four gate stages after the training; the training stages are none.
        Assert.Equal(["stag0101", "stag0201", "stag0301", "stag0401"], Table.All.Where(s => s.Gate).Select(s => s.Stage).Order());
        Assert.False(Table.IsGate("stag0001"));
        // What no image key or stage name gives: the boss stages' dungeons, the Fortress's area, stag0205's.
        Assert.Equal(("death_valley", "boss", "dng103"), Kind("stagBS01"));
        Assert.Equal(("dry_land", "boss", "dng206"), Kind("stagbs02"));
        Assert.Equal(("venom_jungle", "boss", "dng310"), Kind("STAGBS03"));
        foreach (var zone in new[] { "stagbs04", "stagbs05", "stagbs06" }) Assert.Equal(("machine_pit", "boss", "zone_x"), Kind(zone));
        foreach (var fortress in new[] { "staggf01", "staggf02", "staggf03", "staggf04" }) Assert.Equal(("death_valley", "field", null), Kind(fortress));
        Assert.Equal(("dry_land", "field", null), Kind("stag0205"));
        Assert.Equal(("death_valley", "field", null), Kind("stag0001"));
        Assert.Equal("dngq4", Table.Find("dngq4b2v")!.Dungeon);
        Assert.Equal("dngq6", Table.Find("dngq6b01")!.Dungeon);

        // Names and the reader's helpers (any letter case; the title and unknown stages are home).
        Assert.Equal("Dry Land: Ancient Ruins", Table.DisplayName("DNG205B2"));
        Assert.Equal("Home Server: Main Lobby: Central Area", Table.DisplayName("StagML12"));
        Assert.Equal("Title screen", Table.DisplayName(""));
        Assert.Equal("stagxx99", Table.DisplayName("stagxx99"));
        Assert.True(Table.IsHub("stagML11") && Table.IsHub("") && Table.IsHub("stagxx99"));
        Assert.True(Table.IsDungeon("stagbs04") && Table.IsDungeon("dng101b1") && !Table.IsDungeon("staggf02"));
        Assert.Equal("Venom Jungle", AreaTable.AreaName("venom_jungle"));
        Assert.Equal(["death_valley", "dry_land", "venom_jungle", "machine_pit"], AreaTable.Areas.Select(a => a.Key));
    }

    private static (string? Area, string Kind, string? Dungeon) Kind(string stage)
    {
        var s = Table.Find(stage)!;
        return (s.Area, s.Kind, s.Dungeon);
    }

    [Fact]
    public void Other_formats_and_unknown_values_are_refused()
    {
        static AreaTable Read(string json) => AreaTable.Parse(new MemoryStream(System.Text.Encoding.UTF8.GetBytes(json)));
        Assert.Throws<FormatException>(() => Read("{\"format\":\"dw4-areas/1\",\"areas\":[]}"));
        Assert.Throws<FormatException>(() => Read("{\"format\":\"dw4-areas/2\",\"areas\":[{\"stage\":\"x\",\"name\":\"x\",\"area\":null,\"kind\":\"cave\",\"dungeon\":null,\"gate\":false}]}"));
        Assert.Throws<FormatException>(() => Read("{\"format\":\"dw4-areas/2\",\"areas\":[{\"stage\":\"x\",\"name\":\"x\",\"area\":\"moon\",\"kind\":\"field\",\"dungeon\":null,\"gate\":false}]}"));
        Assert.Single(Read("{\"format\":\"dw4-areas/2\",\"areas\":[{\"stage\":\"X\",\"name\":\"x\",\"area\":\"dry_land\",\"kind\":\"field\",\"dungeon\":null,\"gate\":true}]}").All);
    }

    /// <summary>The arrival stages from home: the gate stages, the Security Gates the lobby's return mat leads to, the quest start.</summary>
    private static Dictionary<string, SortedSet<string>> Arrivals((List<Edge> Edges, List<(string Stage, int Point)> Gates) graph)
    {
        var arrivals = new Dictionary<string, SortedSet<string>>();
        void Add(string stage)
        {
            var area = Table.AreaOf(stage) ?? throw new Xunit.Sdk.XunitException($"the arrival stage {stage} has no area");
            if (!arrivals.TryGetValue(area, out var set)) arrivals[area] = set = [];
            set.Add(stage);
        }
        foreach (var gate in Table.All.Where(s => s.Gate)) Add(gate.Stage);
        foreach (var (stage, _) in graph.Gates) Add(stage);
        Add("stag0401"); // the quest board's start (stag0401 point 3)
        return arrivals;
    }

    [Fact]
    public void Every_arrival_stage_from_home_has_exactly_one_area()
    {
        var graph = Graph();
        Assert.Equal(29, graph.Gates.Count);
        Assert.Equal(27, graph.Gates.Select(g => g.Stage).Distinct().Count());
        Assert.Equal(20, graph.Gates.Count(g => g.Stage.StartsWith("dng")));
        Assert.Equal(9, graph.Gates.Count(g => g.Stage.StartsWith("stag")));
        Assert.Equal(["stag0104", "stag0201", "stag0202", "stag0204", "stag0301", "stag0302", "stag0304"],
            graph.Gates.Where(g => g.Stage.StartsWith("stag")).Select(g => g.Stage).Distinct().Order());
        var arrivals = Arrivals(graph);
        Assert.Equal(arrivals.Values.Sum(s => s.Count), arrivals.Values.SelectMany(s => s).Distinct().Count()); // one area each
        Assert.Equal(new[] { "dng204b2", "dng205b2", "dng205b3", "dng206f2", "stag0201", "stag0202", "stag0204" }, arrivals["dry_land"]);
        // No arrival from home lands in the Fortress or on stag0205 (19.30.2): their area is read only for arrivals.
        Assert.DoesNotContain(arrivals.Values.SelectMany(s => s), s => s.StartsWith("staggf") || s == "stag0205");
        // Every stage the scripts name is in the table.
        Assert.All(graph.Edges.SelectMany(e => new[] { e.From, e.To }), s => Assert.NotNull(Table.Find(s)));
    }

    /// <summary>
    /// The stages reachable from an area's arrival stages without a hub stage (19.30.2). stag0205 has two sides: its
    /// plateau (entered from the Fortress, staggf04's exit) reaches only staggf04 back; its train side (every other way in)
    /// everything but staggf04, as the Power Plant's walls (kind 0x802, campaign.cpp 313-316) keep them apart.
    /// </summary>
    private static SortedSet<string> Scope(string area, (List<Edge> Edges, List<(string Stage, int Point)> Gates) graph)
    {
        var next = graph.Edges.GroupBy(e => e.From).ToDictionary(g => g.Key, g => g.Select(e => e.To).Distinct().ToList());
        var seen = new HashSet<(string Stage, bool Plateau)>();
        var todo = new Stack<(string Stage, bool Plateau)>(Arrivals(graph)[area].Select(s => (s, false)));
        while (todo.Count > 0)
        {
            var (stage, plateau) = todo.Pop();
            if (Table.IsHub(stage) || !seen.Add((stage, plateau))) continue;
            foreach (var to in next.GetValueOrDefault(stage, []))
            {
                if (stage == "stag0205" && (to == "staggf04") != plateau) continue;
                todo.Push((to, stage == "staggf04" && to == "stag0205"));
            }
        }
        return new SortedSet<string>(seen.Select(s => s.Stage));
    }

    [Fact]
    public void Each_areas_scope_is_pinned_on_the_graph_of_all_stage_scripts()
    {
        var graph = Graph();
        Assert.Equal(new SortedSet<string>(["dng101b1", "dng101b2", "dng102b1", "dng102b2", "dng102b3", "dng103f1", "dng103f2", "stag0101", "stag0102",
            "stag0104", "stag0205", "staggf01", "staggf02", "staggf03", "staggf04"]), Scope("death_valley", graph));
        Assert.Equal(new SortedSet<string>(["dng204b1", "dng204b2", "dng205b1", "dng205b2", "dng205b3", "dng206f1", "dng206f2", "stag0201", "stag0202",
            "stag0203", "stag0204", "stag0205"]), Scope("dry_land", graph));
        Assert.Equal(new SortedSet<string>(["dng307b1", "dng307b2", "dng308b1", "dng308b2", "dng308b3", "dng309b1", "dng309b2", "dng309b3", "dng309b4",
            "dng310f1", "dng310f2", "stag0301", "stag0302", "stag0303", "stag0304"]), Scope("venom_jungle", graph));
        Assert.Equal(new SortedSet<string>(["dng411b1", "dng411b2", "dng411b3", "dng412b1", "dng412b2", "dng412b3", "dng412b4", "dngq1b1e", "dngq1b2e",
            "dngq2b1e", "dngq2b2e", "dngq2b2h", "dngq3b1e", "dngq3b2e", "dngq4b1e", "dngq4b1h", "dngq4b1v", "dngq5b1e", "dngq5b1h", "dngq5b1v",
            "dngq6b01", "dngq7b1e", "dngq8b1e", "stag0401", "stag0403", "stag0404", "stagbs04"]), Scope("machine_pit", graph));
        // Every scope stays in its area, but Death Valley's, which holds stag0205's plateau (Dry Land's by its card).
        foreach (var (area, _) in AreaTable.Areas)
            Assert.All(Scope(area, graph), s => Assert.True(Table.AreaOf(s) == area || (area == "death_valley" && s == "stag0205"), $"{area}: {s}"));
        // The ways campaign.cpp's kExits lacks are in the scripts' graph (19.24).
        Assert.Contains(graph.Edges, e => e.From == "stag0401" && e.To == "dngq1b1e");
        Assert.Contains(graph.Edges, e => e.From == "stag0401" && e.To == "stagml11" && e.Script == "mats00");
        Assert.Contains(graph.Edges, e => e.From == "dngq1b1e" && e.To == "stag0401");
        Assert.Contains(graph.Edges, e => e.From == "stag0101" && e.To == "staggf01");
        Assert.Contains(graph.Edges, e => e.From == "staggf04" && e.To == "stag0205");
    }
}
