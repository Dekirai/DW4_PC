using Dapper;
using DW4Online.Domain.Abstractions;
using MySqlConnector;

namespace DW4Online.Data.Repositories;

public sealed class AchievementRepository(Database db) : IAchievementRepository
{
    public async Task<IReadOnlyList<AchievementDefinition>> ListDefinitionsAsync(CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return (await c.QueryAsync<AchievementDefinition>(new CommandDefinition(
            "SELECT id, code, name, description, category, tier, points, kind, goal, stat_key, hidden, sort_order, active " +
            "FROM achievements ORDER BY sort_order, id", cancellationToken: ct))).AsList();
    }

    public async Task<IReadOnlyList<AchievementUnlock>> ListUnlocksAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return (await c.QueryAsync<AchievementUnlock>(new CommandDefinition(
            "SELECT achievement_id, ruleset, unlocked_at, players, flagged FROM achievement_unlocks WHERE account_id = @a",
            new { a = accountId }, cancellationToken: ct))).AsList();
    }

    public async Task<bool> TryUnlockAsync(UnlockWrite u, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        try
        {
            await c.ExecuteAsync(new CommandDefinition(
                "INSERT INTO achievement_unlocks (account_id, achievement_id, ruleset, unlocked_at, room_id, players, client_version, flagged, evidence) " +
                "VALUES (@a, @id, @ruleset, @now, @room, @players, @version, @flagged, @evidence)",
                new
                {
                    a = u.AccountId,
                    id = u.AchievementId,
                    ruleset = u.Ruleset,
                    now = u.Now,
                    room = u.RoomId,
                    players = u.Players is { } p ? (int?)Math.Clamp(p, 0, 255) : null,
                    version = u.ClientVersion,
                    flagged = u.Flagged,
                    evidence = u.EvidenceJson,
                }, cancellationToken: ct));
            return true;
        }
        catch (MySqlException e) when (MySqlErrors.IsDuplicate(e))
        {
            return false;
        }
    }

    public async Task<bool> SubmitTimeAsync(long accountId, string board, string ruleset, long igtMs, long? roomId, DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        var best = await c.QuerySingleOrDefaultAsync<long?>(new CommandDefinition(
            "SELECT igt_ms FROM speedrun_times WHERE account_id = @a AND board = @b AND ruleset = @r FOR UPDATE",
            new { a = accountId, b = board, r = ruleset }, tx, cancellationToken: ct));
        if (best is not null && best.Value <= igtMs)
        {
            await tx.RollbackAsync(ct);
            return false;
        }
        await c.ExecuteAsync(new CommandDefinition(
            "INSERT INTO speedrun_times (account_id, board, ruleset, igt_ms, room_id, achieved_at) VALUES (@a, @b, @r, @t, @room, @now) " +
            "ON DUPLICATE KEY UPDATE igt_ms = VALUES(igt_ms), room_id = VALUES(room_id), achieved_at = VALUES(achieved_at)",
            new { a = accountId, b = board, r = ruleset, t = igtMs, room = roomId, now }, tx, cancellationToken: ct));
        await tx.CommitAsync(ct);
        return true;
    }

    public async Task<IReadOnlyDictionary<string, long>> GetStatsAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        var rows = await c.QueryAsync<(string Key, long Value)>(new CommandDefinition(
            "SELECT stat_key, CAST(value AS SIGNED) AS value FROM player_stats WHERE account_id = @a", new { a = accountId }, cancellationToken: ct));
        return rows.ToDictionary(r => r.Key, r => r.Value, StringComparer.Ordinal);
    }

    public async Task<DateTime?> GetLastStatsAtAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteScalarAsync<DateTime?>(new CommandDefinition(
            "SELECT COALESCE((SELECT MAX(updated_at) FROM player_stats WHERE account_id = @a), (SELECT created_at FROM accounts WHERE id = @a))",
            new { a = accountId }, cancellationToken: ct));
    }

    public async Task<IReadOnlyDictionary<string, long>> AddStatsAsync(long accountId, IReadOnlyDictionary<string, long> deltas, DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        foreach (var (key, value) in deltas)
        {
            await c.ExecuteAsync(new CommandDefinition(
                "INSERT INTO player_stats (account_id, stat_key, value, updated_at) VALUES (@a, @k, @v, @now) " +
                "ON DUPLICATE KEY UPDATE value = value + VALUES(value), updated_at = VALUES(updated_at)",
                new { a = accountId, k = key, v = value, now }, tx, cancellationToken: ct));
        }
        var rows = await c.QueryAsync<(string Key, long Value)>(new CommandDefinition(
            "SELECT stat_key, CAST(value AS SIGNED) AS value FROM player_stats WHERE account_id = @a", new { a = accountId }, tx, cancellationToken: ct));
        await tx.CommitAsync(ct);
        return rows.ToDictionary(r => r.Key, r => r.Value, StringComparer.Ordinal);
    }

    public async Task<AchievementRarity> GetRarityAsync(CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        var holders = await c.QueryAsync<(int Id, long Count)>(new CommandDefinition(
            "SELECT u.achievement_id AS id, COUNT(DISTINCT u.account_id) AS count FROM achievement_unlocks u " +
            "JOIN accounts a ON a.id = u.account_id WHERE u.flagged = 0 AND a.status IN ('active', 'locked') GROUP BY u.achievement_id",
            cancellationToken: ct));
        var players = await c.ExecuteScalarAsync<long>(new CommandDefinition(
            "SELECT COUNT(*) FROM accounts a WHERE a.status IN ('active', 'locked') AND " +
            "(EXISTS (SELECT 1 FROM online_characters o WHERE o.account_id = a.id) OR " +
            "EXISTS (SELECT 1 FROM achievement_unlocks u WHERE u.account_id = a.id AND u.flagged = 0))", cancellationToken: ct));
        return new AchievementRarity(holders.ToDictionary(h => h.Id, h => h.Count), players);
    }

    public async Task<bool> SetUnlockFlaggedAsync(long accountId, int achievementId, string ruleset, bool flagged, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        var exists = await c.ExecuteScalarAsync<long>(new CommandDefinition(
            "SELECT COUNT(*) FROM achievement_unlocks WHERE account_id = @a AND achievement_id = @id AND ruleset = @ruleset",
            new { a = accountId, id = achievementId, ruleset }, cancellationToken: ct));
        if (exists == 0) return false;
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE achievement_unlocks SET flagged = @flagged WHERE account_id = @a AND achievement_id = @id AND ruleset = @ruleset",
            new { a = accountId, id = achievementId, ruleset, flagged }, cancellationToken: ct));
        return true;
    }

    public async Task<bool> RevokeUnlockAsync(long accountId, int achievementId, string ruleset, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteAsync(new CommandDefinition(
            "DELETE FROM achievement_unlocks WHERE account_id = @a AND achievement_id = @id AND ruleset = @ruleset",
            new { a = accountId, id = achievementId, ruleset }, cancellationToken: ct)) > 0;
    }
}

/// <summary>Room membership from the durable rooms / room_members tables (written by the room service).</summary>
public sealed class RoomDirectory(Database db) : IRoomDirectory
{
    public async Task<RoomMembership?> FindMembershipAsync(string roomCode, long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.QuerySingleOrDefaultAsync<RoomMembership>(new CommandDefinition(
            "SELECT r.id AS room_id, r.public_code AS code, r.ranked, r.randomizer, r.status, (r.host_account_id = @a) AS is_host, " +
            "(SELECT COUNT(DISTINCT m2.account_id) FROM room_members m2 WHERE m2.room_id = r.id AND m2.left_at IS NULL) AS members " +
            "FROM rooms r WHERE r.open_code = @code AND r.status IN ('open', 'playing') AND EXISTS " +
            "(SELECT 1 FROM room_members m WHERE m.room_id = r.id AND m.account_id = @a AND m.left_at IS NULL) LIMIT 1",
            new { code = roomCode.Trim().ToUpperInvariant(), a = accountId }, cancellationToken: ct));
    }
}
