using System.Buffers.Binary;
using System.Text;

namespace DW4Online.Domain.Save;

/// <summary>
/// The NDMWSaveData file (90112 bytes): an 8 KB header, then two redundant 40960-byte copies at
/// 0x2000 and 0xC000, each a checksum, the version word 4 and four records in 8 KB buffers.
/// Mirrors save::checksum / write_file / read_file in src/save_data.cpp.
/// </summary>
public static class SaveFile
{
    public const int RecordStride = 0x2000;
    public const int CopySize = 40960;
    public const int HeaderSize = 0x2000;
    public const int FileSize = 90112;
    public const int RecordsPerCopy = 4;
    public const uint CopyVersion = 4;
    public static readonly int[] CopyOffsets = [0x2000, 0xC000];

    /// <summary>Sum of the big-endian words after the first (8031FB98).</summary>
    public static uint Checksum(ReadOnlySpan<byte> copy)
    {
        uint sum = 0;
        for (var at = 4; at + 4 <= copy.Length; at += 4)
            sum = unchecked(sum + BinaryPrimitives.ReadUInt32BigEndian(copy[at..]));
        return sum;
    }

    /// <summary>The whole file from its 8 KB header and up to four records.</summary>
    public static byte[] Write(ReadOnlySpan<byte> header, IReadOnlyList<SaveRecord> records)
    {
        var file = new byte[FileSize];
        header[..Math.Min(header.Length, HeaderSize)].CopyTo(file);
        var copy = new byte[CopySize];
        BinaryPrimitives.WriteUInt32BigEndian(copy.AsSpan(4), CopyVersion);
        for (var i = 0; i < Math.Min(records.Count, RecordsPerCopy); ++i)
            RecordCodec.Encode(records[i]).CopyTo(copy, 8 + i * RecordStride);
        BinaryPrimitives.WriteUInt32BigEndian(copy, Checksum(copy));
        foreach (var offset in CopyOffsets) copy.CopyTo(file, offset);
        return file;
    }

    /// <summary>The records of the first copy whose checksum and version hold, or null.</summary>
    public static IReadOnlyList<SaveRecord>? Read(ReadOnlySpan<byte> file)
    {
        if (file.Length < FileSize) return null;
        foreach (var offset in CopyOffsets)
        {
            var copy = file.Slice(offset, CopySize);
            if (BinaryPrimitives.ReadUInt32BigEndian(copy[4..]) != CopyVersion ||
                BinaryPrimitives.ReadUInt32BigEndian(copy) != Checksum(copy))
                continue;
            var records = new List<SaveRecord>(RecordsPerCopy);
            for (var i = 0; i < RecordsPerCopy; ++i)
                records.Add(RecordCodec.Decode(copy.Slice(8 + i * RecordStride, RecordCodec.RecordSize)));
            return records;
        }
        return null;
    }
}

/// <summary>Dolphin's GCI folder format: a 64-byte directory entry before the file (save::GciEntry).</summary>
public sealed class GciEntry
{
    public const int Size = 64;

    public string Game { get; set; } = "GDJE";
    public string Maker { get; set; } = "B2";
    public byte BannerFormat { get; set; } = 1;
    public string FileName { get; set; } = "NDMWSaveData";
    public uint Modified { get; set; }           // seconds since 2000-01-01 on the console clock
    public uint ImageOffset { get; set; } = 0x40;
    public ushort IconFormat { get; set; } = 1;
    public ushort IconSpeed { get; set; } = 2;
    public byte Permissions { get; set; } = 4;
    public byte Copies { get; set; }
    public ushort FirstBlock { get; set; } = 5;
    public ushort Blocks { get; set; } = 11;
    public uint CommentOffset { get; set; }

    public byte[] Encode()
    {
        var o = new byte[Size];
        Encoding.ASCII.GetBytes(Game.PadRight(4, '\0')[..4]).CopyTo(o, 0);
        Encoding.ASCII.GetBytes(Maker.PadRight(2, '\0')[..2]).CopyTo(o, 4);
        o[6] = 0xFF;
        o[7] = BannerFormat;
        var name = Encoding.ASCII.GetBytes(FileName);
        name.AsSpan(0, Math.Min(name.Length, 31)).CopyTo(o.AsSpan(8));
        BinaryPrimitives.WriteUInt32BigEndian(o.AsSpan(40), Modified);
        BinaryPrimitives.WriteUInt32BigEndian(o.AsSpan(44), ImageOffset);
        BinaryPrimitives.WriteUInt16BigEndian(o.AsSpan(48), IconFormat);
        BinaryPrimitives.WriteUInt16BigEndian(o.AsSpan(50), IconSpeed);
        o[52] = Permissions;
        o[53] = Copies;
        BinaryPrimitives.WriteUInt16BigEndian(o.AsSpan(54), FirstBlock);
        BinaryPrimitives.WriteUInt16BigEndian(o.AsSpan(56), Blocks);
        BinaryPrimitives.WriteUInt16BigEndian(o.AsSpan(58), 0xFFFF);
        BinaryPrimitives.WriteUInt32BigEndian(o.AsSpan(60), CommentOffset);
        return o;
    }

    public static GciEntry Decode(ReadOnlySpan<byte> b)
    {
        if (b.Length < Size) throw new FormatException("GCI entry: short data");
        var name = b.Slice(8, 32);
        var end = name.IndexOf((byte)0);
        return new GciEntry
        {
            Game = Encoding.Latin1.GetString(b[..4]),
            Maker = Encoding.Latin1.GetString(b.Slice(4, 2)),
            BannerFormat = b[7],
            FileName = Encoding.Latin1.GetString(end < 0 ? name : name[..end]),
            Modified = BinaryPrimitives.ReadUInt32BigEndian(b[40..]),
            ImageOffset = BinaryPrimitives.ReadUInt32BigEndian(b[44..]),
            IconFormat = BinaryPrimitives.ReadUInt16BigEndian(b[48..]),
            IconSpeed = BinaryPrimitives.ReadUInt16BigEndian(b[50..]),
            Permissions = b[52],
            Copies = b[53],
            FirstBlock = BinaryPrimitives.ReadUInt16BigEndian(b[54..]),
            Blocks = BinaryPrimitives.ReadUInt16BigEndian(b[56..]),
            CommentOffset = BinaryPrimitives.ReadUInt32BigEndian(b[60..]),
        };
    }
}
