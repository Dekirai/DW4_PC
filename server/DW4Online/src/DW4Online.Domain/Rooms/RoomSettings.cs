using System.Globalization;
using System.Text;
using System.Text.Json;

namespace DW4Online.Domain.Rooms;

/// <summary>
/// The simulation-relevant settings of a room (SEAMLESS.md 3.1, 6.2): difficulty, camera mode, light mode, the host's
/// randomizer and fast loading. Every member's engine runs with them; the relay refuses a member whose settings hash
/// differs from the room's.
/// </summary>
/// <param name="Difficulty">0 Normal, 1 Hard, 2 Very Hard.</param>
/// <param name="CameraMode">The title option camera_mode (0 or 1).</param>
/// <param name="LightMode">The title option light_mode (0 or 1; 1 is the original's new game default).</param>
/// <param name="RandomizerSeed">The randomizer's master seed; 0 = the original game (the randomizer is off).</param>
/// <param name="RandomizerDrops">Item drops shuffled.</param>
/// <param name="RandomizerEnemies">0 off, 1 every enemy drawn on its own, 2 one family per wave.</param>
/// <param name="RandomizerZones">0 off, else the loading zone shuffle rule (1 round 48, 2 current).</param>
/// <param name="FastLoading">The PC's fast loading.</param>
public sealed record RoomSettings(int Difficulty, int CameraMode, int LightMode, uint RandomizerSeed, bool RandomizerDrops, int RandomizerEnemies,
    int RandomizerZones, bool FastLoading)
{
    public static readonly RoomSettings Default = new(0, 0, 1, 0, false, 0, 0, false);

    /// <summary>The randomizer changes anything (RandomizerConfig::active in the client).</summary>
    public bool RandomizerActive => RandomizerSeed != 0 && (RandomizerDrops || RandomizerEnemies != 0 || RandomizerZones != 0);

    /// <summary>
    /// The canonical text the settings hash covers (the client builds the same text from the values its engine runs with):
    /// "DW4R2-settings/1;protocol=P;data=D;difficulty=N;camera=N;light=N;rand=SEED,DROPS,ENEMIES,ZONES;fast=N" with decimal
    /// numbers, DROPS and fast 0 or 1, and rand=0,0,0,0 while the randomizer is not active.
    /// </summary>
    public string Canonical(int protocol, ulong dataHash)
    {
        var active = RandomizerActive;
        return string.Create(CultureInfo.InvariantCulture,
            $"DW4R2-settings/1;protocol={protocol};data={dataHash};difficulty={Difficulty};camera={CameraMode};light={LightMode};" +
            $"rand={(active ? RandomizerSeed : 0)},{(active && RandomizerDrops ? 1 : 0)},{(active ? RandomizerEnemies : 0)},{(active ? RandomizerZones : 0)};" +
            $"fast={(FastLoading ? 1 : 0)}");
    }

    /// <summary>FNV-1a 64 of <see cref="Canonical"/>: the settingsHash of the DW4R2 handshake (sent as 16 hex digits).</summary>
    public ulong Hash(int protocol, ulong dataHash) => Fnv1a64(Encoding.ASCII.GetBytes(Canonical(protocol, dataHash)));

    public static ulong Fnv1a64(ReadOnlySpan<byte> data)
    {
        var h = 14695981039346656037UL;
        foreach (var b in data)
        {
            h ^= b;
            h *= 1099511628211UL;
        }
        return h;
    }

    /// <summary>
    /// Reads and validates the room settings object: difficulty 0..2, cameraMode / lightMode 0..1, fastLoading,
    /// randomizer ({enabled, seed, drops, enemies 0..2, zones 0..2}; false or null for none); cheats and rebalanced rates are refused.
    /// </summary>
    public static Result<RoomSettings> FromJson(JsonElement? settings)
    {
        if (settings is not { } s || s.ValueKind is JsonValueKind.Null or JsonValueKind.Undefined) return Default;
        if (s.ValueKind != JsonValueKind.Object) return DomainError.Invalid("invalid_settings", "Room settings must be a JSON object.");
        foreach (var forbidden in new[] { "cheats", "rebalanced" })
            if (s.TryGetProperty(forbidden, out var f) && f.ValueKind == JsonValueKind.True)
                return DomainError.Invalid("unranked_settings", "Online rooms allow no cheats and no rebalanced rates.");
        if (!Int(s, "difficulty", 0, 2, 0, out var difficulty)) return Bad("difficulty is 0 (Normal), 1 (Hard) or 2 (Very Hard).");
        if (!Int(s, "cameraMode", 0, 1, 0, out var camera)) return Bad("cameraMode is 0 or 1.");
        if (!Int(s, "lightMode", 0, 1, 1, out var light)) return Bad("lightMode is 0 or 1.");
        if (!Bool(s, "fastLoading", out var fast)) return Bad("fastLoading is true or false.");
        uint seed = 0;
        bool drops = false;
        int enemies = 0, zones = 0;
        if (s.TryGetProperty("randomizer", out var r))
        {
            if (r.ValueKind == JsonValueKind.Object)
            {
                if (!Bool(r, "enabled", out var enabled)) return Bad("randomizer.enabled is true or false.");
                if (r.TryGetProperty("seed", out var sd) && !(sd.ValueKind == JsonValueKind.Number && sd.TryGetUInt32(out seed)))
                    return Bad("randomizer.seed is a number from 0 to 4294967295.");
                if (!Bool(r, "drops", out drops)) return Bad("randomizer.drops is true or false.");
                if (!Int(r, "enemies", 0, 2, 0, out enemies)) return Bad("randomizer.enemies is 0, 1 or 2.");
                if (!Int(r, "zones", 0, 2, 0, out zones)) return Bad("randomizer.zones is 0, 1 or 2.");
                if (!enabled) (seed, drops, enemies, zones) = (0, false, 0, 0);
            }
            // (A bare true would mark the room as a randomizer room while the game ran without one: no seed.)
            else if (r.ValueKind is not (JsonValueKind.False or JsonValueKind.Null))
                return Bad("randomizer is an object {enabled, seed, drops, enemies, zones}.");
        }
        return new RoomSettings(difficulty, camera, light, seed, drops, enemies, zones, fast);
    }

    /// <summary>
    /// Whether the settings run a randomizer (the ranked ruleset of the room's unlocks): exactly when the settings every
    /// engine runs with have an active one, so the room list never shows a randomizer the simulation does not play.
    /// </summary>
    public static bool RandomizerRequested(JsonElement s) => FromJson(s) is { Ok: true } r && r.Value!.RandomizerActive;

    private static DomainError Bad(string message) => DomainError.Invalid("invalid_settings", message);

    private static bool Int(JsonElement o, string name, int min, int max, int fallback, out int value)
    {
        value = fallback;
        if (!o.TryGetProperty(name, out var v) || v.ValueKind == JsonValueKind.Null) return true;
        return v.ValueKind == JsonValueKind.Number && v.TryGetInt32(out value) && value >= min && value <= max;
    }

    private static bool Bool(JsonElement o, string name, out bool value)
    {
        value = false;
        if (!o.TryGetProperty(name, out var v) || v.ValueKind == JsonValueKind.Null) return true;
        if (v.ValueKind is not (JsonValueKind.True or JsonValueKind.False)) return false;
        value = v.ValueKind == JsonValueKind.True;
        return true;
    }
}
