using DW4Online.Domain.Save;

namespace DW4Online.Domain.Achievements;

public static class AchievementKinds
{
    /// <summary>Reported by the game when it happens (story, bosses, challenges, events).</summary>
    public const string Event = "event";
    /// <summary>Unlocked by the server when a counter in player_stats reaches the goal.</summary>
    public const string Stat = "stat";
    /// <summary>Unlocked by the server from its own decode of an online character upload.</summary>
    public const string State = "state";
}

/// <summary>Evidence a client sends with an event unlock.</summary>
public sealed class UnlockEvidence
{
    /// <summary>In-game time without loading screens, in milliseconds (speedrun times).</summary>
    public long? IgtMs { get; set; }
    /// <summary>The player's level when it happened (low-level challenges).</summary>
    public int? Level { get; set; }
    /// <summary>Stage folder, e.g. stagBS06 (stored as evidence only).</summary>
    public string? Stage { get; set; }
}

/// <summary>
/// The server-side rules behind the achievement definitions (sql/0002_achievements.sql, aligned with the game client's
/// catalogue by sql/0009_achievement_catalogue.sql and data/online/achievements.json). Definitions carry the display data,
/// the kind, the goal and the stat key; this class knows how to check them.
/// </summary>
public static class AchievementRules
{
    /// <summary>Counters the game may report, with the most it can plausibly add per minute of play.</summary>
    public static readonly IReadOnlyDictionary<string, long> StatKeys = new Dictionary<string, long>(StringComparer.Ordinal)
    {
        ["enemies_defeated"] = 400,
        ["critical_hits"] = 600,
        ["numemon_defeated"] = 200,
        ["bosses_defeated"] = 10,
        ["defeats"] = 30,
        ["play_seconds"] = 61,
    };

    /// <summary>Story clear achievements that also submit a speedrun time when the evidence carries one.</summary>
    public static readonly IReadOnlyDictionary<string, string> SpeedrunBoards = new Dictionary<string, string>(StringComparer.Ordinal)
    {
        ["clear_normal"] = "story_normal",
        ["clear_hard"] = "story_hard",
        ["clear_veryhard"] = "story_veryhard",
    };

    /// <summary>
    /// The least in-game time (seconds, loading excluded) a story clear can take, per board; a faster claim is impossible
    /// and only comes from a broken or edited client. Derived from four runs of the whole Normal story by the campaign
    /// bot (work/campaign, checkpoint sets r25c and r25t: god mode, one-hit defeats, infinite MP, every text box skipped
    /// at the fastest rate): 6.93 to 7.52 hours of live in-game time over the 64 story stages, and the best time of each
    /// stage over those runs adds up to 6.61 hours; the six story boss stages alone take 9.6 to 13.3 minutes. 45 minutes
    /// is about a ninth of that best-of-every-stage sum and a quarter of Speed Demon's 3-hour goal, so no real run comes
    /// near it even with a much better route than the bot's. Hard and Very Hard play the same stages, so the same floor
    /// applies. Play:SpeedrunFloorSeconds overrides it per board (calibrate with real online runs).
    /// </summary>
    public const long DefaultSpeedrunFloorSeconds = 45 * 60;

    /// <summary>The default floor of every speedrun board (see <see cref="DefaultSpeedrunFloorSeconds"/>).</summary>
    public static Dictionary<string, long> DefaultSpeedrunFloors() =>
        SpeedrunBoards.Values.ToDictionary(board => board, _ => DefaultSpeedrunFloorSeconds, StringComparer.Ordinal);

    private static readonly Dictionary<string, Func<SaveRecord, long, bool>> StateRules = new(StringComparer.Ordinal)
    {
        ["lv_10"] = HighestLevel,
        ["lv_50"] = HighestLevel,
        ["lv_99"] = HighestLevel,
        ["lv_200"] = HighestLevel,
        ["lv_500"] = HighestLevel,
        ["lv_999"] = HighestLevel,
        ["form_rookies"] = (r, _) => Enumerable.Range(0, Species.RookieCount).All(s => r.BaseIsUse[s] != 0),
        ["form_eight"] = FormCount,
        ["form_all"] = FormCount,
        ["form_susanoomon"] = (r, _) => r.BaseIsUse[Species.RowOf("p_susanoo")] != 0,
        // goal = how many forms (all 16) stand at level 99.
        ["form_master"] = (r, goal) => Enumerable.Range(0, Species.Count).Count(s => r.BaseIsUse[s] != 0 && FormLevel(r, s) >= 99) >= goal,
        ["card_first"] = CardCount,
        ["card_25"] = CardCount,
        ["card_50"] = CardCount,
        // Devices the character holds: its folder (the client's rule; the bank does not count).
        ["item_rank5"] = (r, goal) => FolderDevices(r).Any(d => d.Rank >= goal),
        ["item_pink"] = (r, goal) => FolderDevices(r).Any(d => d.Value >= goal),
        ["bits_100k"] = (r, goal) => (long)r.Bits + r.BankBits >= goal,
        ["bits_1m"] = (r, goal) => (long)r.Bits + r.BankBits >= goal,
        ["bits_max"] = (r, goal) => r.Bits >= goal, // in the wallet
        ["hoarder"] = (r, goal) => r.Devices.Count(w => w != -1) >= goal,
    };

    /// <summary>
    /// The story challenges (SEAMLESS.md 11): online the story is the room owner's, so only the owner's machine reports
    /// them, computed from the owner's online character; a guest gets the plain clear.
    /// </summary>
    public static readonly IReadOnlySet<string> OwnerOnlyCodes = new HashSet<string>(StringComparer.Ordinal)
    {
        "deathless_clear", "solo_veryhard", "speed_normal", "speed_fast", "rookie_clear",
    };

    /// <summary>Codes whose event reports need a check against the definition's goal or the context.</summary>
    private static readonly Dictionary<string, Func<AchievementCheck, string?>> EventRequirements = new(StringComparer.Ordinal)
    {
        ["speed_normal"] = c => Owner(c) ?? SpeedLimit(c),
        ["speed_fast"] = c => Owner(c) ?? SpeedLimit(c),
        ["deathless_clear"] = Owner,
        ["rookie_clear"] = Owner,
        ["solo_veryhard"] = c => Owner(c) ?? ((c.RoomPlayers ?? c.Players) == 1 ? null : "Lone Tamer needs a run with one player."),
        ["lowlevel_apocalymon"] = c => c.Evidence?.Level is { } l && l >= 1 && l <= c.Goal ? null : "The level is missing or above the limit.",
        ["mp_couch"] = c => c.Players >= 2 ? null : "Needs at least two players.",
        ["mp_fullhouse"] = c => c.Players >= 4 ? null : "Needs four players.",
        ["online_join"] = c => c.InRoom ? null : "Needs an online room.",
        ["online_host"] = c => c.InRoom && c.IsHost && (c.RoomPlayers ?? c.Players) >= 3 ? null : "Needs a hosted room with at least three players.",
        ["online_raid"] = c => c.InRoom && (c.RoomPlayers ?? c.Players) >= 2 ? null : "Needs at least two online players in the room.",
    };

    public static IReadOnlyCollection<string> StateCodes => StateRules.Keys;
    public static IReadOnlyCollection<string> CheckedEventCodes => EventRequirements.Keys;

    /// <summary>True when the state achievement holds for the record. Unknown codes never unlock.</summary>
    public static bool StateHolds(string code, SaveRecord record, long? goal) =>
        StateRules.TryGetValue(code, out var rule) && rule(record, goal ?? 0);

    /// <summary>Null when the event report satisfies the code's requirement, otherwise the reason.</summary>
    public static string? CheckEvent(string code, AchievementCheck check) =>
        EventRequirements.TryGetValue(code, out var rule) ? rule(check) : null;

    /// <summary>A story challenge in a room: the room owner's only.</summary>
    private static string? Owner(AchievementCheck c) =>
        c.InRoom && !c.IsHost ? "Story challenges count only for the room owner (the story is the owner's)." : null;

    /// <summary>Speed goals: the in-game time is given, at most the goal and not below the story's floor.</summary>
    private static string? SpeedLimit(AchievementCheck c) => c.Evidence?.IgtMs switch
    {
        null or <= 0 => "The in-game time is missing.",
        long ms when ms > c.Goal * 1000 => "The in-game time is above the limit.",
        long ms when ms < c.StoryFloorMs => "The in-game time is below the fastest possible story clear.",
        _ => null,
    };

    private static bool HighestLevel(SaveRecord r, long goal)
    {
        if (r.Level >= goal) return true;
        for (var s = 0; s < Species.Count; ++s)
            if (r.BaseIsUse[s] != 0 && r.BaseLevel[s] >= goal) return true;
        return false;
    }

    private static int FormLevel(SaveRecord r, int row) =>
        Species.RowOf(r.Digimon) == row ? Math.Max(r.Level, r.BaseLevel[row]) : r.BaseLevel[row];

    private static bool CardCount(SaveRecord r, long goal) => r.Cards.Count(c => c != 0) >= Math.Max(goal, 1);

    private static bool FormCount(SaveRecord r, long goal) => r.BaseIsUse.Count(b => b != 0) >= goal;

    private static IEnumerable<PackedDevice> FolderDevices(SaveRecord r) =>
        r.Devices.Select(w => new PackedDevice(w)).Where(d => !d.Empty && d.WellFormed);
}

/// <summary>
/// The facts an event requirement is checked against. <paramref name="Players"/>: the players of the reporter's stage
/// instance; <paramref name="RoomPlayers"/>: the room's members (both capped at the room's real member count).
/// <paramref name="StoryFloorMs"/>: the least in-game time of a story clear on any board (speed goals below it are impossible).
/// </summary>
public sealed record AchievementCheck(long Goal, UnlockEvidence? Evidence, int? Players, bool InRoom, bool IsHost, long StoryFloorMs = 0,
    int? RoomPlayers = null);
