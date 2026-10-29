using System.Security.Cryptography;
using DW4Online.Domain.Save;

namespace DW4Online.Tests;

/// <summary>Mirrors tests/save_data_tests.cpp (records, files, gci) and checks the bytes against the C++ encoder.</summary>
public class RecordCodecTests
{
    // SHA-256 of the C++ encoder's output for the sample (save::encode, save::write_file with header[0] = 'D',
    // save::encode_gci_entry with modified = 0x324C22A2), dumped from src/save_data.cpp at b53421c.
    private const string CppRecordSha256 = "DA2380920CAF7906C525E1E31BA38691DAC723122D2753AC621D3EC7F79B75AA";
    private const string CppFileSha256 = "BFB7EFBA52294FE55FE4611FD8A8A480696C4718C6D521B094BB2C62B28D8503";
    private const string CppGciEntrySha256 = "3910929607EE62027C8A2CCF45C475E664F51D42CF243D24EEAC54F7DC7C8C14";

    [Fact]
    public void Record_layout_matches_the_cpp_tests()
    {
        var bytes = RecordCodec.Encode(Samples.Record());
        Assert.Equal(3316, bytes.Length);
        Assert.True(bytes[0] == 1 && bytes[4] == 0x0C && bytes[7] == 0xD7, "ISUSE and UNIQUE at 0 / 4");
        Assert.True(bytes[40] == 0xFF && bytes[41] == 0xFF && bytes[42] == 0xFF && bytes[43] == 0x34, "Name marker then full-width 'T'");
        Assert.True(bytes[75] == 88 && bytes[102] == 0x01 && bytes[103] == 0x0D, "LEVEL at 72, first device at 100");
        Assert.True(bytes[792] == 1 && bytes[1815] == 1 && bytes[1816] == 1, "BASE_FLAG spans 792..1815, folder at 1816");
    }

    [Fact]
    public void Record_bytes_equal_the_cpp_encoder()
    {
        Assert.Equal(CppRecordSha256, Convert.ToHexString(SHA256.HashData(RecordCodec.Encode(Samples.Record()))));
    }

    [Fact]
    public void Record_round_trips()
    {
        var bytes = RecordCodec.Encode(Samples.Record());
        var back = RecordCodec.Decode(bytes);
        Assert.Equal(bytes, RecordCodec.Encode(back));
        Assert.Equal("p_dorumon", back.Digimon);
        Assert.Equal("Tamer", CardName.FromCard(back.Name));
        Assert.Equal(0xFFFF, back.NameMarker);
        Assert.False(back.Blank);
        Assert.Equal(0x141140A, back.Devices[1]);
        Assert.Equal(1815, back.BaseSkill[3][3]);
        Assert.Equal(2, back.BaseUpCount[3][2]);
    }

    [Fact]
    public void Blank_record_stays_all_zero()
    {
        var zero = RecordCodec.Encode(new SaveRecord());
        Assert.All(zero, b => Assert.Equal(0, b));
        Assert.True(RecordCodec.Decode(zero).Blank);
    }

    [Fact]
    public void Short_record_is_rejected()
    {
        var bytes = RecordCodec.Encode(Samples.Record());
        Assert.Throws<FormatException>(() => RecordCodec.Decode(bytes.AsSpan(0, 100)));
    }

    [Fact]
    public void Name_garbage_after_the_terminator_is_detected()
    {
        var bytes = RecordCodec.Encode(Samples.Record());
        bytes[40 + 2 + 7 * 2 + 1] = 0x41; // last name slot after "Tamer" + terminator
        Assert.True(RecordCodec.Decode(bytes).NameHasTrailingData);
        Assert.Equal("Tamer", CardName.FromCard(RecordCodec.Decode(bytes).Name));
    }

    [Fact]
    public void File_round_trip_and_fallback_copy()
    {
        var header = new byte[SaveFile.HeaderSize];
        header[0] = (byte)'D';
        var records = new SaveRecord[SaveFile.RecordsPerCopy];
        records[0] = Samples.Record();
        for (var i = 1; i < records.Length; ++i) records[i] = new SaveRecord();
        var file = SaveFile.Write(header, records);
        Assert.Equal(SaveFile.FileSize, file.Length);
        Assert.Equal((byte)'D', file[0]);
        Assert.True(file[0x2004 + 3] == 4 && file[0xC004 + 3] == 4, "Both copies carry the version word");
        Assert.Equal(CppFileSha256, Convert.ToHexString(SHA256.HashData(file)));

        var read = SaveFile.Read(file);
        Assert.NotNull(read);
        Assert.Equal(4, read.Count);
        Assert.Equal(RecordCodec.Encode(records[0]), RecordCodec.Encode(read[0]));
        Assert.False(read[1].Used);

        file[0x2000 + 100] ^= 0x55; // spoil the first copy: the second one is read
        var fallback = SaveFile.Read(file);
        Assert.NotNull(fallback);
        Assert.Equal(RecordCodec.Encode(records[0]), RecordCodec.Encode(fallback[0]));
        file[0xC000 + 100] ^= 0x55;
        Assert.Null(SaveFile.Read(file));
        Assert.Null(SaveFile.Read(file.AsSpan(0, 1000)));
    }

    [Fact]
    public void Gci_entry_layout_and_round_trip()
    {
        var entry = new GciEntry { Modified = 0x324C22A2 };
        var bytes = entry.Encode();
        Assert.True(bytes[0] == 'G' && bytes[6] == 0xFF && bytes[45] == 0 && bytes[47] == 0x40 && bytes[57] == 11, "GCI entry layout");
        Assert.Equal(CppGciEntrySha256, Convert.ToHexString(SHA256.HashData(bytes)));
        var back = GciEntry.Decode(bytes);
        Assert.Equal("NDMWSaveData", back.FileName);
        Assert.Equal(0x324C22A2u, back.Modified);
        Assert.Equal(11, back.Blocks);
        Assert.Throws<FormatException>(() => GciEntry.Decode(bytes.AsSpan(0, 10)));
    }

    [Theory]
    [InlineData("Tamer", "Tamer")]
    [InlineData("A B", "A B")]
    [InlineData("TooLongName", "TooLongN")]
    public void Card_names_round_trip(string ascii, string expected)
    {
        var card = CardName.ToCard(ascii);
        Assert.All(card, c => Assert.True(CardName.IsCardCharacter(c)));
        Assert.Equal(expected, CardName.FromCard(card));
    }

    [Fact]
    public void Card_names_mark_unknown_characters()
    {
        Assert.Equal("A?", CardName.FromCard("Ａア"));
        Assert.Equal('Ｔ', CardName.ToCard("T")[0]);
    }

    [Fact]
    public void Packed_device_fields()
    {
        var d = new PackedDevice(0x141140A); // value 0x14, rank 1, kind 1, equipped, record 10
        Assert.Equal(10, d.Record);
        Assert.True(d.Equipped);
        Assert.Equal(1, d.Kind);
        Assert.Equal(1, d.Rank);
        Assert.Equal(0x14, d.Value);
        Assert.True(new PackedDevice(-1).Empty);
        Assert.False(new PackedDevice(-2).WellFormed);
    }

    [Fact]
    public void Species_rows()
    {
        Assert.Equal(16, Species.Models.Count);
        Assert.Equal(3, Species.RowOf("p_dorumon"));
        Assert.Equal(15, Species.RowOf("p_susanoo"));
        Assert.Equal(-1, Species.RowOf("p_numemon"));
    }
}
