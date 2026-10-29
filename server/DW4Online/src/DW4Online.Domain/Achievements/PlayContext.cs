namespace DW4Online.Domain.Achievements;

/// <summary>
/// What the game reports about the session an unlock, a stats batch or a character upload comes from.
/// Online play has no cheats, no rebalancing and no save states; a report that says otherwise is not ranked.
/// </summary>
public sealed class PlayContext
{
    /// <summary>The room's public code; null when the client played without a room.</summary>
    public string? RoomCode { get; set; }
    /// <summary>Players in the stage instance (1-4) or the room (up to 10), as the client sees it.</summary>
    public int? Players { get; set; }
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
        if (context.Difficulty is < 0 or > 2) return ("invalid_difficulty", "The difficulty is outside 0..2.");
        return null;
    }
}
