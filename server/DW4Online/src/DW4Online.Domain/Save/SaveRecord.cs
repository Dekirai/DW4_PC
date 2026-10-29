namespace DW4Online.Domain.Save;

/// <summary>
/// One Digimon World 4 character record (GAMERAM 80364AEC), the C# mirror of
/// <c>dw4::save::Record</c> in include/dw4/save_data.hpp. 3316 bytes, big-endian.
/// </summary>
public sealed class SaveRecord
{
    /// <summary>True when every byte of the record is zero (a box the game never wrote).</summary>
    public bool Blank { get; set; } = true;
    public bool Used { get; set; }                 // ISUSE
    public uint Unique { get; set; }               // UNIQUE
    public string Digimon { get; set; } = "";      // DIGIMONNAME, e.g. p_agumon
    /// <summary>The 16-bit word before PLAYERNAME; the game always writes 0xFFFF.</summary>
    public ushort NameMarker { get; set; }
    /// <summary>PLAYERNAME as stored (full-width UTF-16 code units), at most 8, up to the first zero.</summary>
    public string Name { get; set; } = "";
    /// <summary>Non-zero code units after the name's terminator (the game never writes them).</summary>
    public bool NameHasTrailingData { get; set; }
    public int Level { get; set; }
    public int Hp { get; set; }
    public int MaxHp { get; set; }
    public int Mp { get; set; }
    public int MaxMp { get; set; }
    public int XData { get; set; }
    public int Bits { get; set; }
    public int[] Devices { get; } = new int[36];        // DEVICEFOLDER packed words, -1 empty
    public int[] Disks { get; } = new int[12];          // DISKFOLDER packed words, -1 empty
    public byte[] Cards { get; } = new byte[52];        // CARDLIST
    public int[] Bank { get; } = new int[96];           // BANKDEVICE packed words, -1 empty
    public int BankBits { get; set; }                   // BANKBIT
    public int[] PowerSocket { get; } = new int[3];     // DEVICEFOLDER indices, -1 none
    public int[] PowerPocket { get; } = new int[5];
    public int MainSlot { get; set; }
    public int[] MainPocket { get; } = new int[5];
    public int SubSlot { get; set; }
    public byte[] Flags { get; } = new byte[1024];      // BASE_FLAG
    public byte[] FlagFolder { get; } = new byte[12];   // BASE_FLAGFOLDER
    public int[] Counters { get; } = new int[16];       // BASE_COUNTER
    public byte[] BaseIsUse { get; } = new byte[16];    // species reached (shinka rows)
    public int[] BaseLevel { get; } = new int[16];
    public int[] BaseExp { get; } = new int[16];
    public int[][] BaseSkill { get; } = NewRows(16, 9);
    public int[][] BaseUpCount { get; } = NewRows(16, 11);

    private static int[][] NewRows(int rows, int columns)
    {
        var result = new int[rows][];
        for (var i = 0; i < rows; ++i) result[i] = new int[columns];
        return result;
    }
}
