using System.Text.Json;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Achievements;
using DW4Online.Domain.Save;

namespace DW4Online.Domain.Services;

public sealed record AchievementView(
    int Id, string Code, string Name, string Description, string Category, string Tier, int Points, string Kind,
    long? Goal, bool Hidden, IReadOnlyList<UnlockView> Unlocks, long? Progress);

public sealed record UnlockView(string Ruleset, DateTime UnlockedAt, int? Players, bool Flagged);

public sealed record UnlockResult(string Code, string Ruleset, bool NewlyUnlocked, bool NewBestTime);

public sealed record StatsResult(IReadOnlyDictionary<string, long> Totals, IReadOnlyList<string> Unlocked);

/// <summary>
/// Achievements count only online, only in ranked sessions (no cheats, no rebalancing, no save-state load in the
/// stage); randomizer runs are their own ruleset. Event unlocks come from the game, stat and state unlocks are
/// decided here from counters and from the server's decode of the online character.
/// </summary>
public sealed class AchievementService(
    IAchievementRepository repository,
    IRoomDirectory rooms,
    IAuditLog audit,
    VersionGate versions,
    PlayOptions play,
    TimeProvider time)
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);
    private IReadOnlyList<AchievementDefinition>? _definitions;

    public async Task<IReadOnlyList<AchievementDefinition>> DefinitionsAsync(CancellationToken ct = default) =>
        _definitions ??= (await repository.ListDefinitionsAsync(ct)).Where(d => d.Active).OrderBy(d => d.SortOrder).ThenBy(d => d.Id).ToList();

    /// <summary>All definitions; with an account, its unlocks and progress. Hidden ones stay masked until unlocked.</summary>
    public async Task<IReadOnlyList<AchievementView>> ListAsync(long? accountId, CancellationToken ct = default)
    {
        var definitions = await DefinitionsAsync(ct);
        var unlocks = accountId is { } id ? await repository.ListUnlocksAsync(id, ct) : [];
        var stats = accountId is { } sid ? await repository.GetStatsAsync(sid, ct) : new Dictionary<string, long>();
        var byId = unlocks.GroupBy(u => u.AchievementId).ToDictionary(g => g.Key, g => g.ToList());
        var result = new List<AchievementView>(definitions.Count);
        foreach (var d in definitions)
        {
            var mine = byId.TryGetValue(d.Id, out var list)
                ? list.Select(u => new UnlockView(u.Ruleset, u.UnlockedAt, u.Players, u.Flagged)).ToList()
                : [];
            var masked = d.Hidden && mine.Count == 0;
            long? progress = d.Kind == AchievementKinds.Stat && d.StatKey is { } key && accountId is not null
                ? Math.Min(stats.GetValueOrDefault(key), d.Goal ?? long.MaxValue)
                : null;
            result.Add(new AchievementView(d.Id, d.Code, masked ? "Hidden achievement" : d.Name,
                masked ? "Keep playing to discover it." : d.Description, d.Category, d.Tier, d.Points, d.Kind, masked ? null : d.Goal,
                d.Hidden, mine, progress));
        }
        return result;
    }

    /// <summary>An event achievement reported by the game. Repeated reports are harmless (and may improve a speedrun time).</summary>
    public async Task<Result<UnlockResult>> UnlockAsync(long accountId, string? code, PlayContext? context, UnlockEvidence? evidence, CancellationToken ct = default)
    {
        var definition = (await DefinitionsAsync(ct)).FirstOrDefault(d => d.Code == code);
        if (definition is null) return DomainError.NotFound("unknown_achievement", "No such achievement.");
        if (definition.Kind != AchievementKinds.Event)
            return DomainError.Invalid("server_tracked", "The server unlocks this achievement itself.");
        var session = await CheckSessionAsync(accountId, context, ct);
        if (!session.Ok) return session.Error!;
        var (room, players) = session.Value;

        var check = new AchievementCheck(definition.Goal ?? 0, evidence, players, room is not null, room?.IsHost == true);
        if (AchievementRules.CheckEvent(definition.Code, check) is { } reason)
            return DomainError.Invalid("requirement_not_met", reason);

        var now = Clock.Now(time);
        var ruleset = Rulesets.Of(context);
        var evidenceJson = evidence is null ? null : JsonSerializer.Serialize(evidence, Json);
        var added = await repository.TryUnlockAsync(new UnlockWrite(accountId, definition.Id, ruleset, now, room?.RoomId, players,
            Truncate(context?.ClientVersion, 32), false, evidenceJson), ct);
        var best = false;
        if (AchievementRules.SpeedrunBoards.TryGetValue(definition.Code, out var board) && evidence?.IgtMs is > 0 and var igt)
            best = await repository.SubmitTimeAsync(accountId, board, ruleset, igt, room?.RoomId, now, ct);
        return new UnlockResult(definition.Code, ruleset, added, best);
    }

    /// <summary>Counter deltas from the game; bounded by the time since the last batch. Stat achievements unlock here.</summary>
    public async Task<Result<StatsResult>> AddStatsAsync(long accountId, IReadOnlyDictionary<string, long>? deltas, PlayContext? context, byte[]? ip, CancellationToken ct = default)
    {
        if (deltas is null || deltas.Count == 0) return DomainError.Invalid("no_stats", "No counters were sent.");
        foreach (var (key, value) in deltas)
        {
            if (!AchievementRules.StatKeys.ContainsKey(key)) return DomainError.Invalid("unknown_stat", $"Unknown counter '{Truncate(key, 48)}'.");
            if (value < 0) return DomainError.Invalid("negative_stat", "Counters only grow.");
        }
        var session = await CheckSessionAsync(accountId, context, ct);
        if (!session.Ok) return session.Error!;

        var now = Clock.Now(time);
        var last = await repository.GetLastStatsAtAsync(accountId, ct);
        var minutes = last is null ? 15 : Math.Clamp((now - last.Value).TotalMinutes, 1, 15);
        foreach (var (key, value) in deltas)
        {
            if (value > AchievementRules.StatKeys[key] * minutes)
            {
                await audit.WriteAsync("stats_rejected", accountId, null, ip, new { key, value, minutes = Math.Round(minutes, 2) }, now, ct);
                return DomainError.Invalid("stats_implausible", $"The '{key}' counter grew faster than the game allows.");
            }
        }
        var totals = await repository.AddStatsAsync(accountId, deltas.Where(d => d.Value > 0).ToDictionary(), now, ct);
        var unlocked = new List<string>();
        var room = session.Value.Room;
        foreach (var d in (await DefinitionsAsync(ct)).Where(d => d.Kind == AchievementKinds.Stat && d.StatKey is not null))
        {
            if (totals.TryGetValue(d.StatKey!, out var total) && total >= (d.Goal ?? long.MaxValue) &&
                await repository.TryUnlockAsync(new UnlockWrite(accountId, d.Id, Rulesets.Standard, now, room?.RoomId, session.Value.Players,
                    Truncate(context?.ClientVersion, 32), false, null), ct))
                unlocked.Add(d.Code);
        }
        return new StatsResult(totals, unlocked);
    }

    /// <summary>State achievements from the server's decode of an accepted online character upload.</summary>
    public async Task<IReadOnlyList<string>> EvaluateStateAsync(long accountId, SaveRecord record, bool flagged, PlayContext? context, CancellationToken ct = default)
    {
        var now = Clock.Now(time);
        var unlocked = new List<string>();
        foreach (var d in (await DefinitionsAsync(ct)).Where(d => d.Kind == AchievementKinds.State))
        {
            if (AchievementRules.StateHolds(d.Code, record, d.Goal) &&
                await repository.TryUnlockAsync(new UnlockWrite(accountId, d.Id, Rulesets.Standard, now, null, context?.Players,
                    Truncate(context?.ClientVersion, 32), flagged, null), ct))
                unlocked.Add(d.Code);
        }
        return unlocked;
    }

    /// <summary>Ranked rules, version gate and the room check shared by unlocks and stats.</summary>
    private async Task<Result<(RoomMembership? Room, int? Players)>> CheckSessionAsync(long accountId, PlayContext? context, CancellationToken ct)
    {
        if (RankedRules.Check(context) is { } unranked) return DomainError.Invalid(unranked.Code, unranked.Message);
        if (versions.Check(context) is { } outdated) return outdated;
        RoomMembership? room = null;
        if (!string.IsNullOrEmpty(context?.RoomCode))
        {
            room = await rooms.FindMembershipAsync(context.RoomCode, accountId, ct);
            if (room is null) return DomainError.Forbidden("not_in_room", "You are not in this room.");
            if (!room.Ranked) return DomainError.Invalid("unranked_room", "This room is not ranked.");
        }
        else if (play.RequireRoom)
        {
            return DomainError.Forbidden("room_required", "Achievements count only in online rooms.");
        }
        int? players = context?.Players;
        if (room is not null && players is not null) players = Math.Min(players.Value, room.Members);
        return (room, players);
    }

    private static string? Truncate(string? s, int max) => s is null ? null : s.Length <= max ? s : s[..max];
}
