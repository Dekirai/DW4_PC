using System.Buffers.Binary;
using System.Text;

namespace DW4Online.Domain.Save;

/// <summary>
/// Encodes and decodes the 3316-byte character record. The offsets mirror src/save_data.cpp
/// (record layout 80364AEC, docs/SAVES.md "Character record").
/// </summary>
public static class RecordCodec
{
    public const int RecordSize = 3316;
    public const int NameChars = 8;

    internal const int UsedAt = 0, UniqueAt = 4, DigimonAt = 8, DigimonSize = 32, NameAt = 40;
    internal const int LevelAt = 72, HpAt = 76, MaxHpAt = 80, MpAt = 84, MaxMpAt = 88, XDataAt = 92, BitsAt = 96;
    internal const int DevicesAt = 100, DisksAt = 244, CardsAt = 292, BankAt = 344, BankBitsAt = 728;
    internal const int PowerSocketAt = 732, PowerPocketAt = 744, MainSlotAt = 764, MainPocketAt = 768, SubSlotAt = 788;
    internal const int FlagsAt = 792, FlagFolderAt = 1816, CountersAt = 1828, BaseIsUseAt = 1892;
    internal const int BaseLevelAt = 1908, BaseExpAt = 1972, BaseSkillAt = 2036, BaseUpCountAt = 2612;

    /// <summary>The record's bytes; a blank record encodes as all zero.</summary>
    public static byte[] Encode(SaveRecord r)
    {
        var o = new byte[RecordSize];
        if (r.Blank) return o;
        o[UsedAt] = r.Used ? (byte)1 : (byte)0;
        BinaryPrimitives.WriteUInt32BigEndian(o.AsSpan(UniqueAt), r.Unique);
        var digimon = Encoding.Latin1.GetBytes(r.Digimon);
        digimon.AsSpan(0, Math.Min(digimon.Length, DigimonSize - 1)).CopyTo(o.AsSpan(DigimonAt));
        BinaryPrimitives.WriteUInt16BigEndian(o.AsSpan(NameAt), 0xFFFF);
        for (var i = 0; i < Math.Min(r.Name.Length, NameChars); ++i)
            BinaryPrimitives.WriteUInt16BigEndian(o.AsSpan(NameAt + 2 + i * 2), r.Name[i]);
        I32(o, LevelAt, r.Level);
        I32(o, HpAt, r.Hp);
        I32(o, MaxHpAt, r.MaxHp);
        I32(o, MpAt, r.Mp);
        I32(o, MaxMpAt, r.MaxMp);
        I32(o, XDataAt, r.XData);
        I32(o, BitsAt, r.Bits);
        Ints(o, DevicesAt, r.Devices);
        Ints(o, DisksAt, r.Disks);
        r.Cards.CopyTo(o, CardsAt);
        Ints(o, BankAt, r.Bank);
        I32(o, BankBitsAt, r.BankBits);
        Ints(o, PowerSocketAt, r.PowerSocket);
        Ints(o, PowerPocketAt, r.PowerPocket);
        I32(o, MainSlotAt, r.MainSlot);
        Ints(o, MainPocketAt, r.MainPocket);
        I32(o, SubSlotAt, r.SubSlot);
        r.Flags.CopyTo(o, FlagsAt);
        r.FlagFolder.CopyTo(o, FlagFolderAt);
        Ints(o, CountersAt, r.Counters);
        r.BaseIsUse.CopyTo(o, BaseIsUseAt);
        Ints(o, BaseLevelAt, r.BaseLevel);
        Ints(o, BaseExpAt, r.BaseExp);
        for (var s = 0; s < 16; ++s) Ints(o, BaseSkillAt + s * 9 * 4, r.BaseSkill[s]);
        for (var s = 0; s < 16; ++s) Ints(o, BaseUpCountAt + s * 11 * 4, r.BaseUpCount[s]);
        return o;
    }

    /// <summary>Decodes the first <see cref="RecordSize"/> bytes. Throws <see cref="FormatException"/> on short data.</summary>
    public static SaveRecord Decode(ReadOnlySpan<byte> b)
    {
        if (b.Length < RecordSize) throw new FormatException("save record: short data");
        b = b[..RecordSize];
        var r = new SaveRecord
        {
            Blank = !b.ContainsAnyExcept((byte)0),
            Used = b[UsedAt] != 0,
            Unique = BinaryPrimitives.ReadUInt32BigEndian(b[UniqueAt..]),
        };
        var digimon = b.Slice(DigimonAt, DigimonSize);
        var end = digimon.IndexOf((byte)0);
        r.Digimon = Encoding.Latin1.GetString(end < 0 ? digimon : digimon[..end]);
        r.NameMarker = BinaryPrimitives.ReadUInt16BigEndian(b[NameAt..]);
        var name = new StringBuilder(NameChars);
        var terminated = false;
        for (var i = 0; i < NameChars; ++i)
        {
            var c = BinaryPrimitives.ReadUInt16BigEndian(b[(NameAt + 2 + i * 2)..]);
            if (terminated)
            {
                if (c != 0) r.NameHasTrailingData = true;
                continue;
            }
            if (c == 0)
            {
                terminated = true;
                continue;
            }
            name.Append((char)c);
        }
        r.Name = name.ToString();
        r.Level = I32(b, LevelAt);
        r.Hp = I32(b, HpAt);
        r.MaxHp = I32(b, MaxHpAt);
        r.Mp = I32(b, MpAt);
        r.MaxMp = I32(b, MaxMpAt);
        r.XData = I32(b, XDataAt);
        r.Bits = I32(b, BitsAt);
        Ints(b, DevicesAt, r.Devices);
        Ints(b, DisksAt, r.Disks);
        b.Slice(CardsAt, r.Cards.Length).CopyTo(r.Cards);
        Ints(b, BankAt, r.Bank);
        r.BankBits = I32(b, BankBitsAt);
        Ints(b, PowerSocketAt, r.PowerSocket);
        Ints(b, PowerPocketAt, r.PowerPocket);
        r.MainSlot = I32(b, MainSlotAt);
        Ints(b, MainPocketAt, r.MainPocket);
        r.SubSlot = I32(b, SubSlotAt);
        b.Slice(FlagsAt, r.Flags.Length).CopyTo(r.Flags);
        b.Slice(FlagFolderAt, r.FlagFolder.Length).CopyTo(r.FlagFolder);
        Ints(b, CountersAt, r.Counters);
        b.Slice(BaseIsUseAt, r.BaseIsUse.Length).CopyTo(r.BaseIsUse);
        Ints(b, BaseLevelAt, r.BaseLevel);
        Ints(b, BaseExpAt, r.BaseExp);
        for (var s = 0; s < 16; ++s) Ints(b, BaseSkillAt + s * 9 * 4, r.BaseSkill[s]);
        for (var s = 0; s < 16; ++s) Ints(b, BaseUpCountAt + s * 11 * 4, r.BaseUpCount[s]);
        return r;
    }

    private static void I32(byte[] o, int at, int v) => BinaryPrimitives.WriteInt32BigEndian(o.AsSpan(at), v);
    private static int I32(ReadOnlySpan<byte> b, int at) => BinaryPrimitives.ReadInt32BigEndian(b[at..]);

    private static void Ints(byte[] o, int at, int[] values)
    {
        for (var i = 0; i < values.Length; ++i) I32(o, at + i * 4, values[i]);
    }

    private static void Ints(ReadOnlySpan<byte> b, int at, int[] values)
    {
        for (var i = 0; i < values.Length; ++i) values[i] = I32(b, at + i * 4);
    }
}
