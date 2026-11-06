using System.Text.Json;
using DW4Online.Domain.Rooms;

namespace DW4Online.Domain.World;

/// <summary>
/// An account's online preferences (online_prefs, SEAMLESS.md 19.17.1): the visibility of the next instance it hosts
/// (the last one chosen, Q9), who may invite it (I4), who sees its whereabouts, the friend toasts, and the host settings
/// its instances start with (19.8.7; the defaults are <see cref="RoomSettings.Default"/>'s: camera 0, light 1, fast
/// loading off, no randomizer). The difficulty is not a preference: it is the box's story's. A missing row = <see cref="Default"/>.
/// </summary>
/// <param name="RandomizerSeed">The randomizer's master seed (1..4294967295); null = the randomizer is off.</param>
/// <param name="RandomizerModes">Bit 0 drops shuffled, bits 1-2 enemies (0 off, 1 every enemy on its own, 2 one family per
/// wave), bits 3-4 the zone shuffle (0 off, 1, 2); the values of <see cref="RoomSettings"/>.</param>
public sealed record OnlinePrefs(
    string DefaultVisibility = VisibilityNames.Public,
    string Invites = OnlinePrefs.InvitesAnyone,
    string Whereabouts = OnlinePrefs.WhereaboutsFriends,
    bool FriendToasts = true,
    int CameraMode = 0,
    int LightMode = 1,
    bool FastLoading = false,
    uint? RandomizerSeed = null,
    uint RandomizerModes = 0,
    int LastBox = 1)
{
    public const string InvitesAnyone = "anyone";
    public const string InvitesFriends = "friends";
    public const string WhereaboutsFriends = "friends";
    public const string WhereaboutsNobody = "nobody";

    public static readonly OnlinePrefs Default = new();

    public bool RandomizerDrops => (RandomizerModes & 1) != 0;
    public int RandomizerEnemies => (int)((RandomizerModes >> 1) & 3);
    public int RandomizerZones => (int)((RandomizerModes >> 3) & 3);

    /// <summary>The host settings of an instance this account starts, with its box's difficulty (19.8.7).</summary>
    public RoomSettings HostSettings(int difficulty) => new(difficulty, CameraMode, LightMode, RandomizerSeed ?? 0, RandomizerDrops,
        RandomizerEnemies, RandomizerZones, FastLoading);

    /// <summary>Packs the randomizer's three modes into <see cref="RandomizerModes"/>.</summary>
    public static uint PackModes(bool drops, int enemies, int zones) =>
        (drops ? 1u : 0u) | ((uint)Math.Clamp(enemies, 0, 2) << 1) | ((uint)Math.Clamp(zones, 0, 2) << 3);

    /// <summary>Whether every value is in its range (the table's CHECK constraints).</summary>
    public bool IsValid =>
        VisibilityNames.IsValid(DefaultVisibility) && Invites is InvitesAnyone or InvitesFriends &&
        Whereabouts is WhereaboutsFriends or WhereaboutsNobody && CameraMode is 0 or 1 && LightMode is 0 or 1 &&
        RandomizerSeed is null or >= 1 && RandomizerModes <= 31 && RandomizerEnemies <= 2 && RandomizerZones <= 2 &&
        LastBox is >= 1 and <= 4;

    /// <summary>The REST shape (GET / PUT /me/online-prefs, 19.19).</summary>
    public object ToJson() => new
    {
        defaultVisibility = DefaultVisibility,
        invites = Invites,
        whereabouts = Whereabouts,
        friendToasts = FriendToasts,
        cameraMode = CameraMode,
        lightMode = LightMode,
        fastLoading = FastLoading,
        randomizer = new
        {
            enabled = RandomizerSeed is not null,
            seed = RandomizerSeed ?? 0,
            drops = RandomizerDrops,
            enemies = RandomizerEnemies,
            zones = RandomizerZones,
        },
        lastBox = LastBox,
    };

    /// <summary>
    /// A PUT /me/online-prefs body applied to these preferences: every field optional, unknown fields refused, each value
    /// checked (invalid_prefs). The randomizer object takes {enabled, seed, drops, enemies, zones} like a room's settings;
    /// enabled false (or seed 0) switches it off.
    /// </summary>
    public Result<OnlinePrefs> Apply(JsonElement body)
    {
        if (body.ValueKind != JsonValueKind.Object) return Bad("The preferences are a JSON object.");
        var p = this;
        foreach (var field in body.EnumerateObject())
        {
            var v = field.Value;
            switch (field.Name)
            {
                case "defaultVisibility":
                    if (v.ValueKind != JsonValueKind.String || !VisibilityNames.IsValid(v.GetString())) return Bad("defaultVisibility is public, friends or private.");
                    p = p with { DefaultVisibility = v.GetString()! };
                    break;
                case "invites":
                    if (v.GetStringOrNull() is not (InvitesAnyone or InvitesFriends)) return Bad("invites is anyone or friends.");
                    p = p with { Invites = v.GetString()! };
                    break;
                case "whereabouts":
                    if (v.GetStringOrNull() is not (WhereaboutsFriends or WhereaboutsNobody)) return Bad("whereabouts is friends or nobody.");
                    p = p with { Whereabouts = v.GetString()! };
                    break;
                case "friendToasts":
                    if (v.ValueKind is not (JsonValueKind.True or JsonValueKind.False)) return Bad("friendToasts is true or false.");
                    p = p with { FriendToasts = v.GetBoolean() };
                    break;
                case "cameraMode":
                    if (!(v.ValueKind == JsonValueKind.Number && v.TryGetInt32(out var camera) && camera is 0 or 1)) return Bad("cameraMode is 0 or 1.");
                    p = p with { CameraMode = camera };
                    break;
                case "lightMode":
                    if (!(v.ValueKind == JsonValueKind.Number && v.TryGetInt32(out var light) && light is 0 or 1)) return Bad("lightMode is 0 or 1.");
                    p = p with { LightMode = light };
                    break;
                case "fastLoading":
                    if (v.ValueKind is not (JsonValueKind.True or JsonValueKind.False)) return Bad("fastLoading is true or false.");
                    p = p with { FastLoading = v.GetBoolean() };
                    break;
                case "lastBox":
                    if (!(v.ValueKind == JsonValueKind.Number && v.TryGetInt32(out var box) && box is >= 1 and <= 4)) return Bad("lastBox is 1 to 4.");
                    p = p with { LastBox = box };
                    break;
                case "randomizer":
                    var settings = RoomSettings.FromJson(JsonDocument.Parse($"{{\"randomizer\":{v.GetRawText()}}}").RootElement);
                    if (!settings.Ok) return settings.Error!;
                    var s = settings.Value!;
                    p = s.RandomizerSeed == 0
                        ? p with { RandomizerSeed = null, RandomizerModes = 0 }
                        : p with { RandomizerSeed = s.RandomizerSeed, RandomizerModes = PackModes(s.RandomizerDrops, s.RandomizerEnemies, s.RandomizerZones) };
                    break;
                default:
                    return Bad($"Unknown preference '{(field.Name.Length > 32 ? field.Name[..32] : field.Name)}'.");
            }
        }
        return p;
    }

    private static DomainError Bad(string message) => DomainError.Invalid("invalid_prefs", message);
}

internal static class JsonElementExtensions
{
    public static string? GetStringOrNull(this JsonElement e) => e.ValueKind == JsonValueKind.String ? e.GetString() : null;
}
