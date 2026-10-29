namespace DW4Online.Domain.Save;

/// <summary>
/// The 16 partner forms (shinka rows) in table order, as src/engine/menu_text.cpp kPartnerRows lists
/// their model names. Rows 0-3 are the rookies; DIGIMONNAME holds one of these names.
/// </summary>
public static class Species
{
    public const int Count = 16;
    public const int RookieCount = 4;

    public static readonly IReadOnlyList<string> Models =
    [
        "p_agumon", "p_vmon", "p_girumon", "p_dorumon",
        "p_weregaru", "p_hekabut", "p_wargrey", "p_angelr",
        "p_beelzeb", "p_alpha", "p_bwargrey", "p_impdrafm",
        "p_impdrapm", "p_metalgaru", "p_dukecrim", "p_susanoo",
    ];

    /// <summary>The shinka row of a model name, or -1.</summary>
    public static int RowOf(string model)
    {
        for (var i = 0; i < Count; ++i)
            if (string.Equals(Models[i], model, StringComparison.Ordinal)) return i;
        return -1;
    }
}

/// <summary>
/// A packed device word (ItemDevice::pack, docs/SAVES.md): bits 0-9 record, bit 10 equipped,
/// bits 12-15 kind, bits 16-19 rank (a disk's count), bits 20-30 value. -1 is an empty slot.
/// </summary>
public readonly record struct PackedDevice(int Word)
{
    public bool Empty => Word == -1;
    /// <summary>A word the game can write: -1 or a non-negative value.</summary>
    public bool WellFormed => Word >= -1;
    public int Record => Word & 0x3FF;
    public bool Equipped => (Word & 0x400) != 0;
    public bool ReservedBit => (Word & 0x800) != 0;
    public int Kind => (Word >> 12) & 0xF;
    public int Rank => (Word >> 16) & 0xF;
    public int Value => (Word >> 20) & 0x7FF;
}
