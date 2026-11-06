using DW4Online.Domain.Rooms;

namespace DW4Online.Domain.World;

// STUB COPY in the relay lane's branch: the web lane owns this file (i1a-contract.md, "web 12:50: IWorldRelay").

/// <summary>An account's online preferences (online_prefs, 19.17.1); a missing row = these defaults.</summary>
public sealed record OnlinePrefs(string DefaultVisibility = "public", string Invites = "anyone", string Whereabouts = "friends",
    bool FriendToasts = true, int CameraMode = 0, int LightMode = 1, bool FastLoading = false, uint? RandomizerSeed = null,
    uint RandomizerModes = 0, int LastBox = 1)
{
    public static readonly OnlinePrefs Default = new();

    /// <summary>The host settings of 19.8.7 with the box's difficulty (randomizer_modes: drops bit 0, enemies bits 1-2, zones bits 3-4).</summary>
    public RoomSettings HostSettings(int difficulty) => new(difficulty, CameraMode, LightMode, RandomizerSeed ?? 0,
        (RandomizerModes & 1) != 0, (int)((RandomizerModes >> 1) & 3), (int)((RandomizerModes >> 3) & 3), FastLoading);
}
