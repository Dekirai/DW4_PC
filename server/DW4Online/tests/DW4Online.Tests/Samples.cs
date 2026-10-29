using DW4Online.Domain.Save;

namespace DW4Online.Tests;

/// <summary>The sample record of tests/save_data_tests.cpp (sample()), field for field.</summary>
internal static class Samples
{
    public static SaveRecord Record()
    {
        var r = new SaveRecord
        {
            Blank = false,
            Used = true,
            Unique = 0x0C0D42D7,
            Digimon = "p_dorumon",
            Name = CardName.ToCard("Tamer"),
            NameMarker = 0xFFFF,
            Level = 88,
            Hp = 13532,
            MaxHp = 13532,
            Mp = 12270,
            MaxMp = 12270,
            XData = 530,
            Bits = 2059734,
            BankBits = 7,
            MainSlot = -1,
            SubSlot = -1,
        };
        Array.Fill(r.Devices, -1);
        r.Devices[0] = 0x10D;
        r.Devices[1] = 0x141140A;
        Array.Fill(r.Disks, -1);
        r.Disks[0] = 0x24000;
        r.Cards[5] = 1;
        Array.Fill(r.Bank, -1);
        int[] socket = [1, 0, -1];
        socket.CopyTo(r.PowerSocket, 0);
        Array.Fill(r.PowerPocket, -1);
        Array.Fill(r.MainPocket, -1);
        r.Flags[0] = r.Flags[1023] = 1;
        r.FlagFolder[0] = 1;
        r.Counters[1] = 6237962;
        r.BaseIsUse[3] = 1;
        Array.Fill(r.BaseLevel, 1);
        r.BaseLevel[3] = 88;
        r.BaseExp[3] = 1745778;
        foreach (var row in r.BaseSkill) Array.Fill(row, 1);
        r.BaseSkill[3][3] = 1815;
        r.BaseUpCount[3][2] = 2;
        return r;
    }

    /// <summary>A plausible fresh character: level 5 Agumon, starting forms at level 1.</summary>
    public static SaveRecord Fresh(string name = "Kevin")
    {
        var r = new SaveRecord
        {
            Blank = false,
            Used = true,
            Unique = 0x12345678,
            Digimon = "p_agumon",
            Name = CardName.ToCard(name),
            Level = 5,
            Hp = 150,
            MaxHp = 150,
            Mp = 60,
            MaxMp = 60,
            Bits = 1200,
            MainSlot = -1,
            SubSlot = -1,
        };
        Array.Fill(r.Devices, -1);
        r.Devices[0] = 0x3001;
        Array.Fill(r.Disks, -1);
        Array.Fill(r.Bank, -1);
        int[] socket = [0, -1, -1];
        socket.CopyTo(r.PowerSocket, 0);
        Array.Fill(r.PowerPocket, -1);
        Array.Fill(r.MainPocket, -1);
        r.BaseIsUse[0] = 1;
        Array.Fill(r.BaseLevel, 1);
        r.BaseLevel[0] = 5;
        r.Flags[960] = 1;
        foreach (var row in r.BaseSkill) Array.Fill(row, 1);
        return r;
    }
}
