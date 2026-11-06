using System.Text.Json;

namespace DW4Online.Domain.World;

/// <summary>
/// One stage of the area table (data/online/areas.json, format dw4-areas/2, SEAMLESS.md 19.30.2): its folder (lower
/// case), the loading card's name, the world's area (null for hub and UI stages), its kind (hub, field, dungeon, boss),
/// the dungeon a dungeon or boss stage belongs to, and whether it is an arrival stage of the lobby gate.
/// </summary>
public sealed record AreaStage(string Stage, string Name, string? Area, string Kind, string? Dungeon, bool Gate)
{
    public bool IsHub => Kind == AreaKinds.Hub;
    /// <summary>A "dungeon stage" in 19.30: kind dungeon or boss.</summary>
    public bool IsDungeon => Kind is AreaKinds.Dungeon or AreaKinds.Boss;
}

public static class AreaKinds
{
    public const string Hub = "hub";
    public const string Field = "field";
    public const string Dungeon = "dungeon";
    public const string Boss = "boss";
}

/// <summary>
/// The game client's stage table (src/game/presence_data.cpp, exported by dw4_presence_tests --export-areas as
/// data/online/areas.json and embedded in this assembly): the website's stage and area names, the relay's areas, kinds,
/// dungeons and gates (SEAMLESS.md 19.30.2). Every column is explicit in the file, none is derived here. Stage names are
/// compared case-insensitively (the scripts and the engine also write "StagML12"). Unknown stages are none of the
/// table's: <see cref="Find"/> answers null and <see cref="IsHub"/> treats them, and an empty stage (the title), as hub.
/// </summary>
public sealed class AreaTable
{
    /// <summary>The area keys of the file's area column (the relay's matching and its tests name them).</summary>
    public const string DeathValley = "death_valley";
    public const string DryLand = "dry_land";
    public const string VenomJungle = "venom_jungle";
    public const string MachinePit = "machine_pit";

    /// <summary>The four areas in the game's order with their names (the loading cards' regions).</summary>
    public static readonly IReadOnlyList<(string Key, string Name)> Areas =
    [
        (DeathValley, "Death Valley"), (DryLand, "Dry Land"), (VenomJungle, "Venom Jungle"), (MachinePit, "Machine Pit"),
    ];

    private static readonly Lazy<AreaTable> Embedded = new(() =>
    {
        using var stream = typeof(AreaTable).Assembly.GetManifestResourceStream("DW4Online.Domain.areas.json")
                           ?? throw new InvalidOperationException("The area table (areas.json) is not embedded.");
        return Parse(stream);
    });

    private readonly Dictionary<string, AreaStage> _stages;

    /// <summary>A table of these rows (tests; the relay's own tables). <see cref="Default"/> is the embedded file's.</summary>
    public AreaTable(IEnumerable<AreaStage> stages)
    {
        All = stages.ToList();
        _stages = All.ToDictionary(s => s.Stage, StringComparer.OrdinalIgnoreCase);
    }

    /// <summary>The table embedded from data/online/areas.json.</summary>
    public static AreaTable Default => Embedded.Value;

    /// <summary>Every stage in the file's order.</summary>
    public IReadOnlyList<AreaStage> All { get; }

    public AreaStage? Find(string? stage) => stage is null ? null : _stages.GetValueOrDefault(stage.Trim());

    public string? AreaOf(string? stage) => Find(stage)?.Area;

    /// <summary>Hub and UI stages (lobby, briefing room, Digi-Lab, menus), the title (empty) and unknown stages.</summary>
    public bool IsHub(string? stage) => Find(stage) is not { } s || s.IsHub;

    public bool IsDungeon(string? stage) => Find(stage)?.IsDungeon == true;

    public bool IsGate(string? stage) => Find(stage)?.Gate == true;

    /// <summary>The stages of an area in the file's order.</summary>
    public IReadOnlyList<AreaStage> Stages(string area) => All.Where(s => s.Area == area).ToList();

    /// <summary>"Death Valley: Goblin Pass"; "Title screen" for the empty stage; an unknown folder as it is.</summary>
    public string DisplayName(string? stage)
    {
        if (string.IsNullOrEmpty(stage)) return "Title screen";
        return Find(stage)?.Name ?? stage;
    }

    /// <summary>"Dry Land" for "dry_land"; an unknown key as it is.</summary>
    public static string AreaName(string? area) =>
        Areas.FirstOrDefault(a => a.Key == area).Name ?? (string.IsNullOrEmpty(area) ? "Home" : area);

    /// <summary>Reads format dw4-areas/2; throws <see cref="FormatException"/> for anything else.</summary>
    public static AreaTable Parse(Stream json)
    {
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        if (!root.TryGetProperty("format", out var format) || format.GetString() != "dw4-areas/2")
            throw new FormatException("areas.json is not format dw4-areas/2 (regenerate it: dw4_presence_tests --export-areas FILE).");
        var stages = new List<AreaStage>();
        foreach (var row in root.GetProperty("areas").EnumerateArray())
        {
            var stage = row.GetProperty("stage").GetString() ?? throw new FormatException("A stage without a folder.");
            var kind = row.GetProperty("kind").GetString();
            if (kind is not (AreaKinds.Hub or AreaKinds.Field or AreaKinds.Dungeon or AreaKinds.Boss))
                throw new FormatException($"Stage {stage} has the unknown kind '{kind}'.");
            var area = row.GetProperty("area").ValueKind == JsonValueKind.Null ? null : row.GetProperty("area").GetString();
            if (area is not null && Areas.All(a => a.Key != area)) throw new FormatException($"Stage {stage} has the unknown area '{area}'.");
            var dungeon = row.GetProperty("dungeon").ValueKind == JsonValueKind.Null ? null : row.GetProperty("dungeon").GetString();
            stages.Add(new AreaStage(stage.ToLowerInvariant(), row.GetProperty("name").GetString() ?? stage, area, kind, dungeon,
                row.GetProperty("gate").GetBoolean()));
        }
        return new AreaTable(stages);
    }
}
