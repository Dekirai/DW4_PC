using DW4Online.Domain.World;

namespace DW4Online.Web;

/// <summary>
/// Readable names of the game's stage folders ("stag0101" -> "Death Valley: Goblin Pass") and areas for the website: the
/// game client's table (src/game/presence_data.cpp, exported as data/online/areas.json, read by
/// <see cref="AreaTable"/>; the client's presence test compares the file with its table). Unknown folders keep their name.
/// </summary>
public static class StageNames
{
    public static string Of(string? stage) => AreaTable.Default.DisplayName(stage);

    /// <summary>Hub and UI stages (lobby, briefing room, Digi-Lab, menus): always home, on the player's own story (19.4).</summary>
    public static bool IsHub(string? stage) => AreaTable.Default.IsHub(stage);

    /// <summary>"Dry Land" for "dry_land".</summary>
    public static string Area(string? area) => AreaTable.AreaName(area);

    public static int Count => AreaTable.Default.All.Count;
}
