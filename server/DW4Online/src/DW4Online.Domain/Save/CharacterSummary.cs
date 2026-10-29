namespace DW4Online.Domain.Save;

/// <summary>
/// What the server derives from a record for the website and achievements. Always computed from the
/// server's own decode, never taken from a client.
/// </summary>
public sealed record CharacterSummary(
    uint Unique,
    string Digimon,
    int SpeciesRow,
    string PlayerName,
    int Level,
    int HighestLevel,
    int Hp,
    int MaxHp,
    int Mp,
    int MaxMp,
    int Bits,
    int BankBits,
    int FormsReached,
    int CardsCollected,
    int DevicesHeld,
    int Clears)
{
    /// <summary>BASE_FLAG indices of the three story clears (Normal, Hard, Very Hard).</summary>
    public static readonly int[] ClearFlags = [406, 407, 408];

    public static CharacterSummary From(SaveRecord r)
    {
        var highest = r.Level;
        var forms = 0;
        for (var s = 0; s < Species.Count; ++s)
        {
            if (r.BaseIsUse[s] != 0)
            {
                ++forms;
                highest = Math.Max(highest, r.BaseLevel[s]);
            }
        }
        var clears = 0;
        for (var i = 0; i < ClearFlags.Length; ++i)
            if (r.Flags[ClearFlags[i]] != 0) clears |= 1 << i;
        return new CharacterSummary(
            r.Unique,
            r.Digimon,
            Species.RowOf(r.Digimon),
            CardName.FromCard(r.Name),
            r.Level,
            highest,
            r.Hp,
            r.MaxHp,
            r.Mp,
            r.MaxMp,
            r.Bits,
            r.BankBits,
            forms,
            r.Cards.Count(c => c != 0),
            r.Devices.Count(d => d != -1),
            clears);
    }
}
