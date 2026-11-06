using System.Text.Json;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Achievements;
using DW4Online.Domain.Save;
using DW4Online.Domain.World;

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
/// decided here from counters and from the server's decode of the online character. Online play is proven by the world
/// relay's play context (SEAMLESS.md 19.8.9: in the world within the last minute, the instance, its players, whether the
/// account hosts it, its settings and data hash), never by the client's report.
/// </summary>
public sealed class AchievementService(
    IAchievementRepository repository,
    IAuditLog audit,
    VersionGate versions,
    PlayOptions play,
    TimeProvider time,
    IWorldRelay? world = null,
    WorldWebOptions? worldOptions = null,
    DW4Online.Domain.Story.OnlineStoryService? stories = null)
{
    private readonly HashSet<ulong> _rankedHashes =
        (worldOptions?.RankedDataHashes ?? []).Select(h => WorldWebOptions.ParseHash(h, "World:RankedDataHashes")).ToHashSet();

    /// <summary>The story clears and the difficulty whose clear flag (406 + difficulty) they need in the reporter's own story.</summary>
    private static readonly Dictionary<string, int> ClearDifficulty = new(StringComparer.Ordinal)
    {
        ["clear_normal"] = 0,
        ["clear_hard"] = 1,
        ["clear_veryhard"] = 2,
    };

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
        var (place, players, instancePlayers) = session.Value;

        var check = new AchievementCheck(definition.Goal ?? 0, evidence, players, place is { Wid: > 0 }, place?.IsHost ?? true, play.StoryFloorMs,
            instancePlayers);
        if (AchievementRules.CheckEvent(definition.Code, check) is { } reason)
            return DomainError.Invalid("requirement_not_met", reason);
        // A story clear needs the reporter's own story to hold it (the server keeps it): one request claimed a clear and
        // the speedrun board's first place. A guest's clear counts when it was mirrored into his own story (19.8.6).
        if (place is not null && ClearDifficulty.TryGetValue(definition.Code, out var cleared) &&
            !await OwnStoryHoldsClearAsync(accountId, cleared, ct))
            return DomainError.Invalid("requirement_not_met", "Your online story has no clear on this difficulty.");

        var now = Clock.Now(time);
        var ruleset = RulesetOf(place, context);
        var evidenceJson = evidence is null ? null : JsonSerializer.Serialize(evidence, Json);
        // A story clear faster than the board's floor is impossible: no best time, and a new unlock waits for review.
        var hasBoard = AchievementRules.SpeedrunBoards.TryGetValue(definition.Code, out var board);
        var igt = evidence?.IgtMs ?? 0;
        var tooFast = hasBoard && igt > 0 && igt < play.SpeedrunFloorMs(board!);
        var added = await repository.TryUnlockAsync(new UnlockWrite(accountId, definition.Id, ruleset, now, null, players,
            Truncate(context?.ClientVersion, 32), tooFast, evidenceJson, place?.HistoryId), ct);
        var best = false;
        if (tooFast)
            await audit.WriteAsync("speedrun_implausible", accountId, null, null,
                new { code = definition.Code, board, ruleset, igtMs = igt, floorMs = play.SpeedrunFloorMs(board!), flagged = added }, now, ct);
        else if (hasBoard && igt > 0)
            best = await repository.SubmitTimeAsync(accountId, board!, ruleset, igt, null, now, ct, place?.HistoryId);
        return new UnlockResult(definition.Code, ruleset, added, best, tooFast);
    }

    /// <summary>
    /// Counter deltas from the game, bounded per counter by a token bucket: <see cref="AchievementRules.StatKeys"/> per
    /// minute of real time, at most <see cref="StatBudgets.WindowMinutes"/> minutes saved up. Batches of one account are
    /// checked one at a time. Stat achievements unlock here, in the instance's ruleset.
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
            var place = session.Value.Place;
            var ruleset = RulesetOf(place, context);
            foreach (var d in (await DefinitionsAsync(ct)).Where(d => d.Kind == AchievementKinds.Stat && d.StatKey is not null))
            {
                if (totals.TryGetValue(d.StatKey!, out var total) && total >= (d.Goal ?? long.MaxValue) &&
                    await repository.TryUnlockAsync(new UnlockWrite(accountId, d.Id, ruleset, now, null, session.Value.Players,
                        Truncate(context?.ClientVersion, 32), false, null, place?.HistoryId), ct))
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
    /// they need online play: the account in the world within the last minute with ranked game data (unless
    /// Play:RequireWorld is off), and they go to its instance's ruleset. An upload from outside the world unlocks nothing
    /// (the character is still saved).
    /// </summary>
    public async Task<IReadOnlyList<string>> EvaluateStateAsync(long accountId, SaveRecord record, bool flagged, PlayContext? context, CancellationToken ct = default)
    {
        var session = await CheckSessionAsync(accountId, context, ct);
        if (!session.Ok) return [];
        var (place, players, _) = session.Value;
        var ruleset = RulesetOf(place, context);
        var now = Clock.Now(time);
        var unlocked = new List<string>();
        foreach (var d in (await DefinitionsAsync(ct)).Where(d => d.Kind == AchievementKinds.State))
        {
            if (AchievementRules.StateHolds(d.Code, record, d.Goal) &&
                await repository.TryUnlockAsync(new UnlockWrite(accountId, d.Id, ruleset, now, null, players,
                    Truncate(context?.ClientVersion, 32), flagged, null, place?.HistoryId), ct))
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

    /// <summary>
    /// Ranked rules, version gate and the world check shared by unlocks, stats and uploads (19.8.9): the relay's play
    /// context (in the world within the last minute) with ranked game data, else, with Play:RequireWorld on, a refusal.
    /// The player counts: the reporter's stage group as the client reported it, at most the instance's players of the last
    /// minute as the relay saw them; the instance's players as the relay counts them (a client reporting one player in an
    /// instance of two got Lone Tamer).
    /// </summary>
    private Result<(WorldPlayContext? Place, int? Players, int? InstancePlayers)> CheckSession(long accountId, PlayContext? context)
    {
        if (RankedRules.Check(context) is { } unranked) return DomainError.Invalid(unranked.Code, unranked.Message);
        if (versions.Check(context) is { } outdated) return outdated;
        var place = world?.PlayContext(accountId);
        if (place is null)
        {
            if (play.RequireWorld) return DomainError.Forbidden("world_required", "Achievements count only while you play online.");
            return (null, context?.Players, context?.InstancePlayers ?? context?.RoomPlayers);
        }
        if (_rankedHashes.Count > 0 && !_rankedHashes.Contains(place.DataHash))
            return DomainError.Invalid("unranked_data", "This game data is not ranked on this server: nothing counts for the online account.");
        int? players = context?.Players is { } reported ? Math.Min(reported, Math.Max(1, place.Players)) : null;
        return (place, players, Math.Max(1, place.Players));
    }

    private Task<Result<(WorldPlayContext? Place, int? Players, int? InstancePlayers)>> CheckSessionAsync(long accountId, PlayContext? context,
        CancellationToken ct) => Task.FromResult(CheckSession(accountId, context));

    /// <summary>
    /// Whether the account's own online story holds the clear flag of <paramref name="difficulty"/> (406 + difficulty),
    /// waiting up to PlayOptions.ClearStoryWaitSeconds for it (the clear's STORY may come after the report; the relay
    /// queues it into the story service, whose load sees the queued revision). Without the story service: true.
    /// </summary>
    private async Task<bool> OwnStoryHoldsClearAsync(long accountId, int difficulty, CancellationToken ct)
    {
        if (stories is null || world is not { Available: true }) return true;
        // (Real time, not the service's clock: the relay's STORY comes in real time.)
        var waited = System.Diagnostics.Stopwatch.StartNew();
        var flag = DW4Online.Domain.Story.RoomStoryKeeper.FirstClearFlag + difficulty;
        while (true)
        {
            var story = await stories.LoadAsync(accountId, ct);
            if (flag < story.Flags.Length && story.Flags[flag] != 0) return true;
            if (waited.Elapsed.TotalSeconds >= Math.Max(0, play.ClearStoryWaitSeconds)) return false;
            await Task.Delay(250, ct);
        }
    }

    /// <summary>The instance's randomizer setting decides (a client cannot report a randomizer instance as standard).</summary>
    private static string RulesetOf(WorldPlayContext? place, PlayContext? context) =>
        place is not null ? (place.Randomizer ? Rulesets.Randomizer : Rulesets.Standard) : Rulesets.Of(context);

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
