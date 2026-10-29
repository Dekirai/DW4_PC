namespace DW4Online.Domain.Save;

/// <summary>One finding of a validation pass: a stable machine code plus an English message.</summary>
public sealed record ValidationIssue(string Code, string Message);

/// <summary>Errors reject an upload; warnings are stored as a flag for moderators (never auto-bans).</summary>
public sealed class ValidationReport
{
    public List<ValidationIssue> Errors { get; } = [];
    public List<ValidationIssue> Warnings { get; } = [];
    public bool Ok => Errors.Count == 0;

    internal void Error(string code, string message) => Errors.Add(new ValidationIssue(code, message));
    internal void Warn(string code, string message) => Warnings.Add(new ValidationIssue(code, message));
}

/// <summary>Plausibility limits for the online character. The defaults are first guesses that need calibration on real online play.</summary>
public sealed class CharacterRules
{
    public const int MaxBits = 9_999_999;   // player BIT and BANKBIT clamp (managers.hpp PlayerSlot)
    public const int MaxLevel = 999;
    public const int MaxStat = 999_999;
    public const int MaxDiskCount = 9;      // DISKFOLDER counts (managers.hpp PlayerSlot::Disk)

    /// <summary>More levels than this per hour of play time (summed over all forms) raises a warning.</summary>
    public double MaxLevelsPerPlayHour { get; set; } = 120;
    /// <summary>Play time may grow at most by the wall-clock time between two uploads plus this slack.</summary>
    public int PlayTimeSlackSeconds { get; set; } = 600;
}

/// <summary>Validates an online character record server side (structure, value ranges and the change against the previous revision).</summary>
public static class RecordValidator
{
    public static ValidationReport Validate(ReadOnlySpan<byte> bytes, out SaveRecord? record)
    {
        var report = new ValidationReport();
        record = null;
        if (bytes.Length != RecordCodec.RecordSize)
        {
            report.Error("record_size", $"A character record is exactly {RecordCodec.RecordSize} bytes (got {bytes.Length}).");
            return report;
        }
        var r = RecordCodec.Decode(bytes);
        record = r;
        if (r.Blank)
        {
            report.Error("record_blank", "The record is empty.");
            return report;
        }
        if (!r.Used) report.Error("record_unused", "The record's ISUSE byte is not set.");
        if (r.Unique == 0) report.Warn("unique_zero", "The character id (UNIQUE) is zero.");

        var row = Species.RowOf(r.Digimon);
        if (row < 0) report.Error("unknown_digimon", $"Unknown partner model '{Sanitize(r.Digimon)}'.");
        else if (r.BaseIsUse[row] == 0) report.Warn("species_not_reached", "The current form is not marked as reached.");

        if (r.NameMarker != 0xFFFF) report.Error("name_marker", "The word before the player name is not 0xFFFF.");
        if (r.Name.Length == 0) report.Error("name_empty", "The player name is empty.");
        foreach (var c in r.Name)
        {
            if (!CardName.IsCardCharacter(c))
            {
                report.Error("name_character", $"The player name holds a character the name entry cannot produce (U+{(int)c:X4}).");
                break;
            }
        }
        if (r.NameHasTrailingData) report.Error("name_trailing_data", "Data follows the player name's terminator.");

        if (r.Level < 1 || r.Level > CharacterRules.MaxLevel) report.Error("level_range", $"Level {r.Level} is outside 1..{CharacterRules.MaxLevel}.");
        if (r.MaxHp < 1 || r.MaxHp > CharacterRules.MaxStat) report.Error("max_hp_range", $"Max HP {r.MaxHp} is outside 1..{CharacterRules.MaxStat}.");
        if (r.Hp < 0 || r.Hp > r.MaxHp) report.Error("hp_range", $"HP {r.Hp} is outside 0..max HP.");
        if (r.MaxMp < 0 || r.MaxMp > CharacterRules.MaxStat) report.Error("max_mp_range", $"Max MP {r.MaxMp} is outside 0..{CharacterRules.MaxStat}.");
        if (r.Mp < 0 || r.Mp > r.MaxMp) report.Error("mp_range", $"MP {r.Mp} is outside 0..max MP.");
        if (r.Bits < 0 || r.Bits > CharacterRules.MaxBits) report.Error("bits_range", $"Bits {r.Bits} are outside 0..{CharacterRules.MaxBits}.");
        if (r.BankBits < 0 || r.BankBits > CharacterRules.MaxBits) report.Error("bank_bits_range", $"Bank bits {r.BankBits} are outside 0..{CharacterRules.MaxBits}.");

        CheckWords(report, r.Devices, "devices");
        CheckWords(report, r.Bank, "bank");
        CheckWords(report, r.Disks, "disks");
        foreach (var w in r.Disks)
        {
            var d = new PackedDevice(w);
            if (!d.Empty && d.WellFormed && d.Rank > CharacterRules.MaxDiskCount)
            {
                report.Warn("disk_count", $"A disk slot holds {d.Rank} disks (the game keeps at most {CharacterRules.MaxDiskCount}).");
                break;
            }
        }
        foreach (var w in r.Devices)
        {
            if (new PackedDevice(w) is { Empty: false, WellFormed: true, ReservedBit: true })
            {
                report.Warn("device_reserved_bit", "A device word sets the unused bit 11.");
                break;
            }
        }

        CheckEquipment(report, r, r.PowerSocket, "power socket");
        CheckEquipment(report, r, r.PowerPocket, "power pocket");
        CheckEquipment(report, r, [r.MainSlot], "main slot");
        CheckEquipment(report, r, r.MainPocket, "main pocket");
        CheckEquipment(report, r, [r.SubSlot], "sub slot");

        for (var s = 0; s < Species.Count; ++s)
        {
            if (r.BaseIsUse[s] > 1)
            {
                report.Warn("base_isuse_value", "A form's reached byte is neither 0 nor 1.");
                break;
            }
        }
        for (var s = 0; s < Species.Count; ++s)
        {
            if (r.BaseLevel[s] < 0 || r.BaseLevel[s] > CharacterRules.MaxLevel)
            {
                report.Error("base_level_range", $"A form's level {r.BaseLevel[s]} is outside 0..{CharacterRules.MaxLevel}.");
                break;
            }
        }
        for (var s = 0; s < Species.Count; ++s)
        {
            if (r.BaseExp[s] < 0)
            {
                report.Error("base_exp_range", "A form's EXP is negative.");
                break;
            }
        }
        return report;
    }

    /// <summary>
    /// Checks the change from the previous revision. <paramref name="wallSeconds"/> is the real time between the uploads;
    /// for a new character (<paramref name="previous"/> null) the real time since the account was made or the online
    /// character was last reset: a new online character starts from zero, so its play time must fit into it. The level
    /// rate uses the play time only as far as real time allows. A different character (UNIQUE) is an error: a new
    /// character needs a reset of the online character first.
    /// </summary>
    public static void CheckTransition(ValidationReport report, SaveRecord? previous, long previousPlaySeconds,
        SaveRecord next, long nextPlaySeconds, double wallSeconds, CharacterRules rules)
    {
        if (nextPlaySeconds < 0) report.Error("play_time_range", "Play time is negative.");
        var playDelta = nextPlaySeconds - previousPlaySeconds;
        var realLimit = (long)Math.Min(long.MaxValue / 2, Math.Max(0, wallSeconds) + rules.PlayTimeSlackSeconds);
        if (previous is null)
        {
            if (nextPlaySeconds > realLimit)
                report.Warn("play_time_too_fast", "A new online character starts from zero, but its play time is longer than the account (or the time since its reset).");
            CheckLevelRate(report, LevelSum(next) - Species.Count, Math.Min(Math.Max(0, nextPlaySeconds), realLimit), rules);
            return;
        }
        if (previous.Unique != next.Unique)
            report.Error("unique_changed", "This is a different character; reset the online character before starting a new one.");
        if (!string.Equals(previous.Name, next.Name, StringComparison.Ordinal))
            report.Warn("name_changed", "The player name changed.");
        for (var s = 0; s < Species.Count; ++s)
        {
            if (next.BaseLevel[s] < previous.BaseLevel[s])
            {
                report.Warn("form_level_dropped", "A form's level went down.");
                break;
            }
        }
        for (var s = 0; s < Species.Count; ++s)
        {
            if (previous.BaseIsUse[s] != 0 && next.BaseIsUse[s] == 0)
            {
                report.Warn("form_lost", "A reached form is no longer marked as reached.");
                break;
            }
        }
        if (playDelta < 0) report.Warn("play_time_decreased", "Play time went down.");
        else if (playDelta > wallSeconds + rules.PlayTimeSlackSeconds)
            report.Warn("play_time_too_fast", "Play time grew faster than real time.");
        CheckLevelRate(report, LevelSum(next) - LevelSum(previous), Math.Min(Math.Max(0, playDelta), realLimit), rules);
    }

    /// <summary>The sum of the levels of all forms (the current form's level counts through its row when the row is behind).</summary>
    public static int LevelSum(SaveRecord r)
    {
        var sum = 0;
        var row = Species.RowOf(r.Digimon);
        for (var s = 0; s < Species.Count; ++s)
            sum += s == row ? Math.Max(r.BaseLevel[s], r.Level) : Math.Max(r.BaseLevel[s], 1);
        return sum;
    }

    private static void CheckLevelRate(ValidationReport report, int levelsGained, long playSeconds, CharacterRules rules)
    {
        if (levelsGained <= 0) return;
        var hours = Math.Max(playSeconds, 900) / 3600.0; // at least a quarter of an hour
        if (levelsGained / hours > rules.MaxLevelsPerPlayHour)
            report.Warn("level_rate", $"{levelsGained} levels in {playSeconds / 60} minutes of play is unusually fast.");
    }

    private static void CheckWords(ValidationReport report, int[] words, string what)
    {
        foreach (var w in words)
        {
            if (!new PackedDevice(w).WellFormed)
            {
                report.Error("device_word", $"A {what} word is negative but not -1.");
                return;
            }
        }
    }

    private static void CheckEquipment(ValidationReport report, SaveRecord r, int[] indices, string what)
    {
        foreach (var index in indices)
        {
            if (index < -1 || index >= r.Devices.Length)
            {
                report.Error("equipment_index", $"The {what} points outside the device folder ({index}).");
                return;
            }
            if (index >= 0 && r.Devices[index] == -1)
            {
                report.Warn("equipment_empty", $"The {what} points at an empty device slot.");
                return;
            }
        }
    }

    private static string Sanitize(string s)
    {
        var chars = s.Where(c => c is >= ' ' and < (char)0x7F).Take(32).ToArray();
        return new string(chars);
    }
}
