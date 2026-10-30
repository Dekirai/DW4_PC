using System.Text.Json;

namespace DW4Online.Web;

/// <summary>
/// Readable names of the game's stage folders ("stag0101" -> "Death Valley: Goblin Pass") for the room page. The table is
/// the game client's (src/game/presence_data.cpp, exported as data/online/areas.json and embedded here; the client's
/// presence test compares the file with its table). Unknown folders keep their folder name.
/// </summary>
public static class StageNames
{
    private static readonly Lazy<IReadOnlyDictionary<string, string>> Names = new(Load);

    public static string Of(string? stage)
    {
        if (string.IsNullOrEmpty(stage)) return "Title screen";
        return Names.Value.TryGetValue(stage.ToLowerInvariant(), out var name) ? name : stage;
    }

    /// <summary>Hub and UI stages (lobby, briefing room, Digi-Lab, menus) are solo in a room (SEAMLESS.md 3.3).</summary>
    public static bool IsHub(string? stage) =>
        string.IsNullOrEmpty(stage) || stage.StartsWith("stagml", StringComparison.OrdinalIgnoreCase) ||
        stage.StartsWith("stagis", StringComparison.OrdinalIgnoreCase);

    public static int Count => Names.Value.Count;

    private static IReadOnlyDictionary<string, string> Load()
    {
        using var stream = typeof(StageNames).Assembly.GetManifestResourceStream("DW4Online.Web.areas.json");
        if (stream is null) return new Dictionary<string, string>();
        using var doc = JsonDocument.Parse(stream);
        var names = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var area in doc.RootElement.GetProperty("areas").EnumerateArray())
            if (area.GetProperty("stage").GetString() is { } stage && area.GetProperty("name").GetString() is { } name)
                names[stage.ToLowerInvariant()] = name;
        return names;
    }
}
