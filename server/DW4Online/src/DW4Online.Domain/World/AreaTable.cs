namespace DW4Online.Domain.World;

// STUB COPY in the relay lane's branch: the web lane owns this file and reads data/online/areas.json format 2
// (i1a-contract.md, "web 12:50: areas.json format 2"). This stub holds the same columns inline (SEAMLESS.md 19.30.2), derived
// from the stage names of format 1, so the relay's tests run before the integration; the integration takes the web lane's.

/// <summary>A stage's row of areas.json format 2.</summary>
public sealed record AreaStage(string Stage, string Name, string? Area, string Kind, string? Dungeon, bool Gate);

/// <summary>The stages, their areas, kinds, dungeon keys and gate flags (SEAMLESS.md 19.30.2). Stage names compare case-insensitively.</summary>
public sealed class AreaTable
{
    public const string DeathValley = "death_valley";
    public const string DryLand = "dry_land";
    public const string VenomJungle = "venom_jungle";
    public const string MachinePit = "machine_pit";

    private static readonly string[] StageNames =
    [
        "stagis07", "stagis08", "stagis09", "stagis10", "stagis11", "stagis12", "stagis13", "stagis20", "stagml02", "stagml11", "stagml12",
        "stagml13", "stagml14", "stag0000", "stag0001", "stag0101", "stag0102", "stag0103", "stag0104", "dng101b1", "dng101b2", "dng102b1",
        "dng102b2", "dng102b3", "dng103f1", "dng103f2", "stag0201", "stag0202", "stag0203", "stag0204", "stag0205", "dng204b1", "dng204b2",
        "dng205b1", "dng205b2", "dng205b3", "dng206f1", "dng206f2", "stag0301", "stag0302", "stag0303", "stag0304", "dng307b1", "dng307b2",
        "dng308b1", "dng308b2", "dng308b3", "dng309b1", "dng309b2", "dng309b3", "dng309b4", "dng310f1", "dng310f2", "dng310f3", "stag0401",
        "stag0403", "stag0404", "dng411b1", "dng411b2", "dng411b3", "dng412b1", "dng412b2", "dng412b3", "dng412b4", "stagbs01", "stagbs02",
        "stagbs03", "stagbs04", "stagbs05", "stagbs06", "staggf01", "staggf02", "staggf03", "staggf04", "dngq1b1e", "dngq1b2e", "dngq2b1e",
        "dngq2b2e", "dngq2b2h", "dngq3b1e", "dngq3b2e", "dngq4b1e", "dngq4b1h", "dngq4b1v", "dngq4b2e", "dngq4b2h", "dngq4b2v", "dngq5b1e",
        "dngq5b1h", "dngq5b1v", "dngq6b01", "dngq7b1e", "dngq7b2e", "dngq8b1e", "dngq8b2e", "dngq8b3e", "dngq8b3h", "dngq8b3v",
    ];

    private readonly Dictionary<string, AreaStage> _stages;

    public AreaTable(IEnumerable<AreaStage> stages) =>
        _stages = stages.ToDictionary(s => s.Stage, StringComparer.OrdinalIgnoreCase);

    public static AreaTable Default { get; } = new(StageNames.Select(Row));

    public AreaStage? Find(string? stage) => stage is null ? null : _stages.GetValueOrDefault(stage);
    public string? AreaOf(string? stage) => Find(stage)?.Area;
    /// <summary>A hub or UI stage, or a stage the table does not know (empty: the title).</summary>
    public bool IsHub(string? stage) => Find(stage) is not { } s || s.Kind == "hub";
    public bool IsDungeon(string? stage) => Find(stage)?.Kind is "dungeon" or "boss";
    public bool IsGate(string? stage) => Find(stage)?.Gate == true;
    public IReadOnlyList<AreaStage> Stages(string area) => _stages.Values.Where(s => s.Area == area).OrderBy(s => s.Stage, StringComparer.Ordinal).ToList();
    public string DisplayName(string? stage) => Find(stage)?.Name ?? stage ?? "";

    public static string AreaName(string? area) => area switch
    {
        DeathValley => "Death Valley",
        DryLand => "Dry Land",
        VenomJungle => "Venom Jungle",
        MachinePit => "Machine Pit",
        _ => "",
    };

    private static AreaStage Row(string s)
    {
        if (s.StartsWith("stagis", StringComparison.Ordinal) || s.StartsWith("stagml", StringComparison.Ordinal))
            return new AreaStage(s, s, null, "hub", null, false);
        if (s.StartsWith("stagbs", StringComparison.Ordinal))
        {
            var (area, dungeon) = s switch
            {
                "stagbs01" => (DeathValley, "dng103"),
                "stagbs02" => (DryLand, "dng206"),
                "stagbs03" => (VenomJungle, "dng310"),
                _ => (MachinePit, "zone_x"),
            };
            return new AreaStage(s, s, area, "boss", dungeon, false);
        }
        if (s.StartsWith("dngq", StringComparison.Ordinal)) return new AreaStage(s, s, MachinePit, "dungeon", s[..5], false);
        if (s.StartsWith("dng", StringComparison.Ordinal)) return new AreaStage(s, s, AreaByDigit(s[3]), "dungeon", s[..6], false);
        if (s.StartsWith("staggf", StringComparison.Ordinal)) return new AreaStage(s, s, DeathValley, "field", null, false);
        // stag0000 / stag0001 (the training) are Death Valley's fields; stag0205 is Dry Land's (19.30.2).
        var digit = s == "stag0000" || s == "stag0001" ? '1' : s[5];
        return new AreaStage(s, s, AreaByDigit(digit), "field", null, s is "stag0101" or "stag0201" or "stag0301" or "stag0401");
    }

    private static string AreaByDigit(char c) => c switch
    {
        '1' => DeathValley,
        '2' => DryLand,
        '3' => VenomJungle,
        _ => MachinePit,
    };
}
