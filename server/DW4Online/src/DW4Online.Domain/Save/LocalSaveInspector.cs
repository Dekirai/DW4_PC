namespace DW4Online.Domain.Save;

/// <summary>One box of a local save as the cloud backup lists it (private: shown to the owner only).</summary>
public sealed record SaveBoxSummary(int Box, bool Used, string PlayerName, string Digimon, int Level);

/// <summary>The result of checking an uploaded local save file.</summary>
public sealed record LocalSaveInfo(string Format, IReadOnlyList<SaveBoxSummary> Boxes);

/// <summary>
/// Checks a cloud backup upload: the local save is the GCI bytes unchanged (saves/savedata.dat, 64-byte
/// directory entry + 90112 bytes) or the bare 90112-byte NDMWSaveData file. At least one copy must pass its checksum.
/// </summary>
public static class LocalSaveInspector
{
    public const int GciSize = GciEntry.Size + SaveFile.FileSize;

    public static LocalSaveInfo? Inspect(ReadOnlySpan<byte> data, out string? error)
    {
        error = null;
        ReadOnlySpan<byte> file;
        string format;
        if (data.Length == GciSize)
        {
            var entry = GciEntry.Decode(data);
            if (entry.Game != "GDJE" || entry.Maker != "B2" || entry.FileName != "NDMWSaveData")
            {
                error = "The GCI directory entry is not Digimon World 4's NDMWSaveData (GDJE / B2).";
                return null;
            }
            file = data[GciEntry.Size..];
            format = "gci";
        }
        else if (data.Length == SaveFile.FileSize)
        {
            file = data;
            format = "raw";
        }
        else
        {
            error = $"A Digimon World 4 save is {GciSize} bytes (GCI) or {SaveFile.FileSize} bytes (raw); got {data.Length}.";
            return null;
        }
        var records = SaveFile.Read(file);
        if (records is null)
        {
            error = "Neither copy of the save data passes its checksum.";
            return null;
        }
        var boxes = new List<SaveBoxSummary>(records.Count);
        for (var i = 0; i < records.Count; ++i)
        {
            var r = records[i];
            var used = !r.Blank && r.Used;
            boxes.Add(new SaveBoxSummary(i, used, used ? CardName.FromCard(r.Name) : "", used ? r.Digimon : "", used ? r.Level : 0));
        }
        return new LocalSaveInfo(format, boxes);
    }
}
