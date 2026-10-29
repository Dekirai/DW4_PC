using DW4Online.Domain.Save;

namespace DW4Online.Tests;

public class ValidationTests
{
    private static ValidationReport Check(SaveRecord r) => RecordValidator.Validate(RecordCodec.Encode(r), out _);

    private static ValidationReport CheckBytes(byte[] bytes) => RecordValidator.Validate(bytes, out _);

    [Fact]
    public void Plausible_records_pass()
    {
        Assert.True(Check(Samples.Record()).Ok);
        var fresh = Check(Samples.Fresh());
        Assert.True(fresh.Ok);
        Assert.Empty(fresh.Warnings);
    }

    [Fact]
    public void Wrong_size_and_blank_are_errors()
    {
        Assert.Contains(CheckBytes(new byte[100]).Errors, e => e.Code == "record_size");
        Assert.Contains(CheckBytes(new byte[3317]).Errors, e => e.Code == "record_size");
        Assert.Contains(CheckBytes(new byte[3316]).Errors, e => e.Code == "record_blank");
    }

    [Theory]
    [InlineData("record_unused")]
    [InlineData("unknown_digimon")]
    [InlineData("name_empty")]
    [InlineData("name_character")]
    [InlineData("level_range")]
    [InlineData("hp_range")]
    [InlineData("max_mp_range")]
    [InlineData("bits_range")]
    [InlineData("bank_bits_range")]
    [InlineData("device_word")]
    [InlineData("equipment_index")]
    [InlineData("base_level_range")]
    [InlineData("base_exp_range")]
    public void Structural_errors(string code)
    {
        var r = Samples.Fresh();
        switch (code)
        {
            case "record_unused": r.Used = false; break;
            case "unknown_digimon": r.Digimon = "p_numemon"; break;
            case "name_empty": r.Name = ""; break;
            case "name_character": r.Name = "Kevé"; break;
            case "level_range": r.Level = 1000; break;
            case "hp_range": r.Hp = r.MaxHp + 1; break;
            case "max_mp_range": r.MaxMp = -1; r.Mp = -1; break;
            case "bits_range": r.Bits = 10_000_000; break;
            case "bank_bits_range": r.BankBits = -5; break;
            case "device_word": r.Bank[3] = -7; break;
            case "equipment_index": r.MainSlot = 36; break;
            case "base_level_range": r.BaseLevel[9] = 1000; break;
            case "base_exp_range": r.BaseExp[2] = -1; break;
        }
        var report = Check(r);
        Assert.False(report.Ok);
        Assert.Contains(report.Errors, e => e.Code == code);
    }

    [Fact]
    public void Name_marker_must_be_set()
    {
        var bytes = RecordCodec.Encode(Samples.Fresh());
        bytes[40] = 0;
        bytes[41] = 0;
        Assert.Contains(CheckBytes(bytes).Errors, e => e.Code == "name_marker");
    }

    [Fact]
    public void Soft_findings_are_warnings()
    {
        var r = Samples.Fresh();
        r.BaseIsUse[0] = 0;          // current form not marked as reached
        r.PowerSocket[1] = 5;        // points at an empty slot
        r.Disks[0] = 0x000A0000;     // 10 disks in one slot
        var report = Check(r);
        Assert.True(report.Ok);
        Assert.Contains(report.Warnings, w => w.Code == "species_not_reached");
        Assert.Contains(report.Warnings, w => w.Code == "equipment_empty");
        Assert.Contains(report.Warnings, w => w.Code == "disk_count");
    }

    [Fact]
    public void A_different_character_is_an_error()
    {
        var before = Samples.Fresh();
        var after = Samples.Fresh();
        after.Unique = 0x0BADF00D;
        var report = new ValidationReport();
        RecordValidator.CheckTransition(report, before, 600, after, 1200, 600, new CharacterRules());
        Assert.Contains(report.Errors, e => e.Code == "unique_changed");
    }

    [Fact]
    public void Play_time_must_not_run_backwards_or_too_fast()
    {
        var before = Samples.Fresh();
        var after = Samples.Fresh();
        var rules = new CharacterRules();
        var back = new ValidationReport();
        RecordValidator.CheckTransition(back, before, 5000, after, 4000, 600, rules);
        Assert.Contains(back.Warnings, w => w.Code == "play_time_decreased");
        var fast = new ValidationReport();
        RecordValidator.CheckTransition(fast, before, 1000, after, 1000 + 3600, 600, rules);
        Assert.Contains(fast.Warnings, w => w.Code == "play_time_too_fast");
        var fine = new ValidationReport();
        RecordValidator.CheckTransition(fine, before, 1000, after, 1500, 600, rules);
        Assert.Empty(fine.Warnings);
        Assert.Empty(fine.Errors);
    }

    /// <summary>A new online character starts from zero: its play time must fit into the account's age (or the time since a reset).</summary>
    [Fact]
    public void A_new_character_cannot_bring_more_play_time_than_the_account_has()
    {
        var rules = new CharacterRules();
        var fresh = Samples.Fresh();
        var old = new ValidationReport();
        RecordValidator.CheckTransition(old, null, 0, fresh, 400L * 3600, 30, rules);
        Assert.Contains(old.Warnings, w => w.Code == "play_time_too_fast");
        var fits = new ValidationReport();
        RecordValidator.CheckTransition(fits, null, 0, fresh, 3600, 7200, rules);
        Assert.Empty(fits.Warnings);
        // The level rate counts claimed play time only as far as real time allows.
        var levelled = Samples.Fresh();
        levelled.Level = 60;
        levelled.BaseLevel[0] = 60;
        var claimed = new ValidationReport();
        RecordValidator.CheckTransition(claimed, null, 0, levelled, 100L * 3600, 60, rules);
        Assert.Contains(claimed.Warnings, w => w.Code == "level_rate");
        var real = new ValidationReport();
        RecordValidator.CheckTransition(real, null, 0, levelled, 3600, 3600, rules);
        Assert.DoesNotContain(real.Warnings, w => w.Code == "level_rate");
    }

    [Fact]
    public void Level_rate_and_dropped_forms_are_flagged()
    {
        var rules = new CharacterRules { MaxLevelsPerPlayHour = 100 };
        var maxed = Samples.Fresh();
        maxed.Level = 500;
        maxed.BaseLevel[0] = 500;
        var created = new ValidationReport();
        RecordValidator.CheckTransition(created, null, 0, maxed, 1800, 0, rules);
        Assert.Contains(created.Warnings, w => w.Code == "level_rate");

        var before = Samples.Fresh();
        before.BaseIsUse[4] = 1;
        before.BaseLevel[4] = 30;
        var after = Samples.Fresh();
        var report = new ValidationReport();
        RecordValidator.CheckTransition(report, before, 3600, after, 7200, 3600, rules);
        Assert.Contains(report.Warnings, w => w.Code == "form_level_dropped");
        Assert.Contains(report.Warnings, w => w.Code == "form_lost");
    }

    [Fact]
    public void Summary_comes_from_the_decode()
    {
        var r = Samples.Record();
        r.Flags[406] = 1;
        r.Flags[408] = 1;
        var s = CharacterSummary.From(RecordCodec.Decode(RecordCodec.Encode(r)));
        Assert.Equal("Tamer", s.PlayerName);
        Assert.Equal(3, s.SpeciesRow);
        Assert.Equal(88, s.HighestLevel);
        Assert.Equal(1, s.FormsReached);
        Assert.Equal(1, s.CardsCollected);
        Assert.Equal(2, s.DevicesHeld);
        Assert.Equal(0b101, s.Clears);
    }

    [Fact]
    public void Local_save_inspector_accepts_gci_and_raw()
    {
        var header = new byte[SaveFile.HeaderSize];
        var file = SaveFile.Write(header, [Samples.Record(), new SaveRecord(), Samples.Fresh(), new SaveRecord()]);
        var gci = new GciEntry().Encode().Concat(file).ToArray();

        var info = LocalSaveInspector.Inspect(gci, out var error);
        Assert.Null(error);
        Assert.NotNull(info);
        Assert.Equal("gci", info.Format);
        Assert.Equal("Tamer", info.Boxes[0].PlayerName);
        Assert.False(info.Boxes[1].Used);
        Assert.Equal("p_agumon", info.Boxes[2].Digimon);

        Assert.Equal("raw", LocalSaveInspector.Inspect(file, out _)!.Format);
        Assert.Null(LocalSaveInspector.Inspect(new byte[500], out error));
        Assert.Contains("bytes", error);

        var wrongGame = (byte[])gci.Clone();
        wrongGame[0] = (byte)'X';
        Assert.Null(LocalSaveInspector.Inspect(wrongGame, out error));
        Assert.Contains("NDMWSaveData", error);

        var broken = (byte[])file.Clone();
        broken[0x2000 + 50] ^= 1;
        broken[0xC000 + 50] ^= 1;
        Assert.Null(LocalSaveInspector.Inspect(broken, out error));
        Assert.Contains("checksum", error);
    }
}
