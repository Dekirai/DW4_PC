namespace DW4Online.Domain.Achievements;

/// <summary>
/// What the game reports about the session an unlock, a stats batch or a character upload comes from.
/// Online play has no cheats, no rebalancing and no save states; a report that says otherwise is not ranked.
/// </summary>
public sealed class PlayContext
{
    /// <summary>The room era's public code (protocol 48 and older); ignored since the world (round I1).</summary>
    public string? RoomCode { get; set; }
    /// <summary>Players in the reporter's stage group (1-4), as the client sees it.</summary>
    public int? Players { get; set; }
    /// <summary>The room era's member count; ignored since the world (the relay counts the instance's players).</summary>
    public int? RoomPlayers { get; set; }
    /// <summary>
    /// The instance as the client knows it (the run-local wid, 0 at home), its players (1-4) and whether the reporter hosts
    /// it (SEAMLESS.md 19.8.9). Informational: the server takes all three from the relay's play context, never from here.
    /// </summary>
    public uint? InstanceId { get; set; }
    public int? InstancePlayers { get; set; }
    public bool? IsHost { get; set; }
    /// <summary>The host's randomizer setting: randomizer runs are their own ruleset.</summary>
    public bool Randomizer { get; set; }
    public bool Cheats { get; set; }
    /// <summary>EXP / bit / damage rates differ from 100 %.</summary>
    public bool Rebalanced { get; set; }
    /// <summary>A save state was loaded in the current stage.</summary>
    public bool SaveStateLoaded { get; set; }
    /// <summary>0 Normal, 1 Hard, 2 Very Hard.</summary>
    public int? Difficulty { get; set; }
    public int? Protocol { get; set; }
    public string? ClientVersion { get; set; }
}

public static class Rulesets
{
    public const string Standard = "standard";
    public const string Randomizer = "randomizer";

    public static string Of(PlayContext? context) => context?.Randomizer == true ? Randomizer : Standard;
}

/// <summary>The ranked rules every unlock, stats batch and online character upload is checked against.</summary>
public static class RankedRules
{
    /// <summary>Null when the context is ranked, otherwise the error code and message.</summary>
    public static (string Code, string Message)? Check(PlayContext? context)
    {
        if (context is null) return null;
        if (context.Cheats) return ("unranked_cheats", "Cheats were on: nothing counts for the online account.");
        if (context.Rebalanced) return ("unranked_rebalanced", "Rebalanced rates were on: nothing counts for the online account.");
        if (context.SaveStateLoaded) return ("unranked_save_state", "A save state was loaded in this stage: nothing counts for the online account.");
        if (context.Players is < 1 or > 10) return ("invalid_players", "The player count is outside 1..10.");
        if (context.RoomPlayers is < 1 or > 10) return ("invalid_players", "The room's player count is outside 1..10.");
        if (context.InstancePlayers is < 1 or > 4) return ("invalid_players", "The instance's player count is outside 1..4.");
        if (context.Difficulty is < 0 or > 2) return ("invalid_difficulty", "The difficulty is outside 0..2.");
        return null;
    }
}
