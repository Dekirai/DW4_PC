using System.Text.Json;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Achievements;
using DW4Online.Domain.Save;

namespace DW4Online.Domain.Services;

public sealed record AchievementView(
    int Id, string Code, string Name, string Description, string Category, string Tier, int Points, string Kind,
    long? Goal, bool Hidden, IReadOnlyList<UnlockView> Unlocks, long? Progress);

public sealed record UnlockView(string Ruleset, DateTime UnlockedAt, int? Players, bool Flagged);

/// <summary><paramref name="TimeRejected"/>: the clear's in-game time was below the board's floor (not kept; a new unlock is flagged).</summary>
public sealed record UnlockResult(string Code, string Ruleset, bool NewlyUnlocked, bool NewBestTime, bool TimeRejected = false);

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
    private readonly StatBudgets _budgets = new();

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

        var check = new AchievementCheck(definition.Goal ?? 0, evidence, players, room is not null, room?.IsHost == true, play.StoryFloorMs);
        if (AchievementRules.CheckEvent(definition.Code, check) is { } reason)
            return DomainError.Invalid("requirement_not_met", reason);

        var now = Clock.Now(time);
        var ruleset = RulesetOf(room, context);
        var evidenceJson = evidence is null ? null : JsonSerializer.Serialize(evidence, Json);
        // A story clear faster than the board's floor is impossible: no best time, and a new unlock waits for review.
        var hasBoard = AchievementRules.SpeedrunBoards.TryGetValue(definition.Code, out var board);
        var igt = evidence?.IgtMs ?? 0;
        var tooFast = hasBoard && igt > 0 && igt < play.SpeedrunFloorMs(board!);
        var added = await repository.TryUnlockAsync(new UnlockWrite(accountId, definition.Id, ruleset, now, room?.RoomId, players,
            Truncate(context?.ClientVersion, 32), tooFast, evidenceJson), ct);
        var best = false;
        if (tooFast)
            await audit.WriteAsync("speedrun_implausible", accountId, null, null,
                new { code = definition.Code, board, ruleset, igtMs = igt, floorMs = play.SpeedrunFloorMs(board!), flagged = added }, now, ct);
        else if (hasBoard && igt > 0)
            best = await repository.SubmitTimeAsync(accountId, board!, ruleset, igt, room?.RoomId, now, ct);
        return new UnlockResult(definition.Code, ruleset, added, best, tooFast);
    }

    /// <summary>
    /// Counter deltas from the game, bounded per counter by a token bucket: <see cref="AchievementRules.StatKeys"/> per
    /// minute of real time, at most <see cref="StatBudgets.WindowMinutes"/> minutes saved up. Batches of one account are
    /// checked one at a time. Stat achievements unlock here, in the room's ruleset.
    /// </summary>
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

        var account = _budgets.For(accountId);
        await account.Gate.WaitAsync(ct);
        try
        {
            var now = Clock.Now(time);
            if (!account.Ready)
            {
                // The first batch since the service started: the budget saved up since the last stored batch (or,
                // for the account's first batch ever, since the account was made).
                var last = await repository.GetLastStatsAtAsync(accountId, ct);
                account.Start(last is null ? 0 : Math.Clamp((now - last.Value).TotalMinutes, 0, StatBudgets.WindowMinutes), now);
            }
            account.Refill(now);
            foreach (var (key, value) in deltas)
            {
                if (value > account.Available(key))
                {
                    await audit.WriteAsync("stats_rejected", accountId, null, ip,
                        new { key, value, available = Math.Floor(account.Available(key)) }, now, ct);
                    return DomainError.Invalid("stats_implausible", $"The '{key}' counter grew faster than the game allows.");
                }
            }
            var totals = await repository.AddStatsAsync(accountId, deltas.Where(d => d.Value > 0).ToDictionary(), now, ct);
            foreach (var (key, value) in deltas) account.Spend(key, value);
            var unlocked = new List<string>();
            var room = session.Value.Room;
            var ruleset = RulesetOf(room, context);
            foreach (var d in (await DefinitionsAsync(ct)).Where(d => d.Kind == AchievementKinds.Stat && d.StatKey is not null))
            {
                if (totals.TryGetValue(d.StatKey!, out var total) && total >= (d.Goal ?? long.MaxValue) &&
                    await repository.TryUnlockAsync(new UnlockWrite(accountId, d.Id, ruleset, now, room?.RoomId, session.Value.Players,
                        Truncate(context?.ClientVersion, 32), false, null), ct))
                    unlocked.Add(d.Code);
            }
            return new StatsResult(totals, unlocked);
        }
        finally
        {
            account.Gate.Release();
        }
    }

    /// <summary>
    /// State achievements from the server's decode of an accepted online character upload. Like event unlocks and stats
    /// they need online play: a ranked room the account is a member of (unless Play:RequireRoom is off), and they go to
    /// that room's ruleset. An upload outside a room unlocks nothing (the character is still saved).
    /// </summary>
    public async Task<IReadOnlyList<string>> EvaluateStateAsync(long accountId, SaveRecord record, bool flagged, PlayContext? context, CancellationToken ct = default)
    {
        var session = await CheckSessionAsync(accountId, context, ct);
        if (!session.Ok) return [];
        var (room, players) = session.Value;
        var ruleset = RulesetOf(room, context);
        var now = Clock.Now(time);
        var unlocked = new List<string>();
        foreach (var d in (await DefinitionsAsync(ct)).Where(d => d.Kind == AchievementKinds.State))
        {
            if (AchievementRules.StateHolds(d.Code, record, d.Goal) &&
                await repository.TryUnlockAsync(new UnlockWrite(accountId, d.Id, ruleset, now, room?.RoomId, players,
                    Truncate(context?.ClientVersion, 32), flagged, null), ct))
                unlocked.Add(d.Code);
        }
        return unlocked;
    }

    /// <summary>Every active definition with the share of players holding it (unflagged unlocks, any ruleset).</summary>
    public async Task<(IReadOnlyList<(AchievementDefinition Definition, long Holders, double Percent)> Rows, long Players)> RarityAsync(CancellationToken ct = default)
    {
        var definitions = await DefinitionsAsync(ct);
        var rarity = await repository.GetRarityAsync(ct);
        var rows = definitions.Select(d =>
        {
            var holders = rarity.Holders.GetValueOrDefault(d.Id);
            return (d, holders, rarity.Players == 0 ? 0.0 : Math.Min(100.0, 100.0 * holders / rarity.Players));
        }).ToList();
        return (rows, rarity.Players);
    }

    /// <summary>Staff: flags an unlock (hidden from rankings and rarity until reviewed) or clears the flag.</summary>
    public async Task<Result<bool>> AdminSetFlagAsync(long actorId, long accountId, string? code, string? ruleset, bool flagged, byte[]? ip, CancellationToken ct = default)
    {
        if (await FindUnlockTargetAsync(code, ruleset, ct) is not { } target) return UnknownUnlock();
        if (!await repository.SetUnlockFlaggedAsync(accountId, target.Id, target.Ruleset, flagged, ct)) return UnknownUnlock();
        await audit.WriteAsync(flagged ? "admin_unlock_flag" : "admin_unlock_unflag", accountId, actorId, ip,
            new { code, ruleset = target.Ruleset }, Clock.Now(time), ct);
        return true;
    }

    /// <summary>Staff: removes an unlock (a cheated one, after review).</summary>
    public async Task<Result<bool>> AdminRevokeAsync(long actorId, long accountId, string? code, string? ruleset, byte[]? ip, CancellationToken ct = default)
    {
        if (await FindUnlockTargetAsync(code, ruleset, ct) is not { } target) return UnknownUnlock();
        if (!await repository.RevokeUnlockAsync(accountId, target.Id, target.Ruleset, ct)) return UnknownUnlock();
        await audit.WriteAsync("admin_unlock_revoke", accountId, actorId, ip, new { code, ruleset = target.Ruleset }, Clock.Now(time), ct);
        return true;
    }

    private async Task<(int Id, string Ruleset)?> FindUnlockTargetAsync(string? code, string? ruleset, CancellationToken ct)
    {
        ruleset ??= Rulesets.Standard;
        if (ruleset is not (Rulesets.Standard or Rulesets.Randomizer)) return null;
        var definition = (await DefinitionsAsync(ct)).FirstOrDefault(d => d.Code == code);
        return definition is null ? null : (definition.Id, ruleset);
    }

    private static DomainError UnknownUnlock() => DomainError.NotFound("unlock_not_found", "The account has no such unlock.");

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

    /// <summary>The room's randomizer setting decides (a client cannot report a randomizer room as standard).</summary>
    private static string RulesetOf(RoomMembership? room, PlayContext? context) =>
        room?.Randomizer == true ? Rulesets.Randomizer : Rulesets.Of(context);

    private static string? Truncate(string? s, int max) => s is null ? null : s.Length <= max ? s : s[..max];
}

/// <summary>
/// Per-account token buckets for the stats counters (in memory: a service start begins from the time of the last stored
/// batch). Each counter refills at its <see cref="AchievementRules.StatKeys"/> rate per minute up to
/// <see cref="WindowMinutes"/> minutes' worth; a batch spends what it adds.
/// </summary>
internal sealed class StatBudgets
{
    public const double WindowMinutes = 15;
    private readonly System.Collections.Concurrent.ConcurrentDictionary<long, Account> _accounts = new();
    private long _lastPurgeTicks;

    public Account For(long accountId)
    {
        var now = Environment.TickCount64;
        var last = Interlocked.Read(ref _lastPurgeTicks);
        if (now - last > 600_000 && Interlocked.CompareExchange(ref _lastPurgeTicks, now, last) == last)
        {
            // Accounts idle for a full window have a full bucket again: forget them.
            foreach (var (id, account) in _accounts)
                if (now - account.TouchedTicks > (long)(WindowMinutes * 60_000) && account.Gate.CurrentCount == 1)
                    _accounts.TryRemove(id, out _);
        }
        var entry = _accounts.GetOrAdd(accountId, _ => new Account());
        entry.TouchedTicks = now;
        return entry;
    }

    internal sealed class Account
    {
        private readonly Dictionary<string, double> _tokens = new(StringComparer.Ordinal);
        private DateTime _refilled;
        public SemaphoreSlim Gate { get; } = new(1, 1);
        public bool Ready { get; private set; }
        public long TouchedTicks { get; set; }

        public void Start(double minutesSaved, DateTime now)
        {
            foreach (var (key, rate) in AchievementRules.StatKeys) _tokens[key] = rate * minutesSaved;
            _refilled = now;
            Ready = true;
        }

        public void Refill(DateTime now)
        {
            var minutes = Math.Max(0, (now - _refilled).TotalMinutes);
            if (now > _refilled) _refilled = now;
            foreach (var (key, rate) in AchievementRules.StatKeys)
                _tokens[key] = Math.Min(rate * WindowMinutes, _tokens.GetValueOrDefault(key) + rate * minutes);
        }

        public double Available(string key) => _tokens.GetValueOrDefault(key);

        public void Spend(string key, long value) => _tokens[key] = Math.Max(0, _tokens.GetValueOrDefault(key) - value);
    }
}
