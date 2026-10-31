using Dapper;
using DW4Online.Domain.Abstractions;
using MySqlConnector;

namespace DW4Online.Data.Repositories;

/// <summary>
/// online_story: one row per account, the account's own online story (SEAMLESS.md 8.4), newer revisions only; every
/// written revision is kept in online_story_history (the last few per account). A deleted account gets no row again (a
/// room's save that was still on its way when the account was deleted writes nothing).
/// </summary>
public sealed class OnlineStoryRepository(Database db) : IOnlineStoryRepository
{
    private const string Live = "status <> 'deleted'";

    public async Task<OnlineStoryRow?> GetAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.QuerySingleOrDefaultAsync<OnlineStoryRow>(new CommandDefinition(
            "SELECT account_id, revision, flags, folder, counters, updated_at FROM online_story WHERE account_id = @a",
            new { a = accountId }, cancellationToken: ct));
    }

    public async Task<bool> SaveAsync(OnlineStoryRow row, int historyKeep, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await using var tx = await c.BeginTransactionAsync(MySqlErrors.RevisionedWrite, ct);
        // Only a newer revision replaces the row (saves can arrive out of order); a new account's first one inserts it.
        var written = await c.ExecuteAsync(new CommandDefinition(
            "UPDATE online_story SET revision = @Revision, flags = @Flags, folder = @Folder, counters = @Counters, updated_at = @UpdatedAt " +
            "WHERE account_id = @AccountId AND revision < @Revision", row, tx, cancellationToken: ct)) > 0;
        if (!written)
            written = await c.ExecuteAsync(new CommandDefinition(
                "INSERT IGNORE INTO online_story (account_id, revision, flags, folder, counters, updated_at) " +
                $"SELECT @AccountId, @Revision, @Flags, @Folder, @Counters, @UpdatedAt FROM accounts WHERE id = @AccountId AND {Live}",
                row, tx, cancellationToken: ct)) > 0;
        if (written) await KeepAsync(c, tx, row, row.Revision, StoryWriteReasons.Room, historyKeep, ct);
        await tx.CommitAsync(ct);
        return written;
    }

    public async Task<long> ReplaceAsync(OnlineStoryRow row, string reason, int historyKeep, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await using var tx = await c.BeginTransactionAsync(MySqlErrors.RevisionedWrite, ct);
        // The next revision in one statement: a room save of the old story that is still on its way is older and writes
        // nothing after this (only newer revisions replace the row).
        var updated = await c.ExecuteAsync(new CommandDefinition(
            "UPDATE online_story SET revision = revision + 1, flags = @Flags, folder = @Folder, counters = @Counters, updated_at = @UpdatedAt " +
            "WHERE account_id = @AccountId", row, tx, cancellationToken: ct));
        if (updated == 0)
            await c.ExecuteAsync(new CommandDefinition(
                "INSERT IGNORE INTO online_story (account_id, revision, flags, folder, counters, updated_at) " +
                $"SELECT @AccountId, 1, @Flags, @Folder, @Counters, @UpdatedAt FROM accounts WHERE id = @AccountId AND {Live}",
                row, tx, cancellationToken: ct));
        var revision = await c.ExecuteScalarAsync<long?>(new CommandDefinition(
            "SELECT revision FROM online_story WHERE account_id = @AccountId", row, tx, cancellationToken: ct)) ?? 0;
        if (revision > 0) await KeepAsync(c, tx, row, revision, reason, historyKeep, ct);
        await tx.CommitAsync(ct);
        return revision;
    }

    public async Task<IReadOnlyList<OnlineStoryHistoryRow>> ListHistoryAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        var rows = await c.QueryAsync<OnlineStoryHistoryRow>(new CommandDefinition(
            "SELECT id, revision, reason, saved_at, flags, folder, counters FROM online_story_history WHERE account_id = @a ORDER BY id DESC",
            new { a = accountId }, cancellationToken: ct));
        return rows.AsList();
    }

    private static async Task KeepAsync(MySqlConnection c, MySqlTransaction tx, OnlineStoryRow row, long revision, string reason, int keep,
        CancellationToken ct)
    {
        await c.ExecuteAsync(new CommandDefinition(
            "INSERT INTO online_story_history (account_id, revision, flags, folder, counters, reason, saved_at) " +
            "VALUES (@AccountId, @revision, @Flags, @Folder, @Counters, @reason, @UpdatedAt)",
            new { row.AccountId, revision, row.Flags, row.Folder, row.Counters, reason, row.UpdatedAt }, tx, cancellationToken: ct));
        // The last `keep` revisions stay (MariaDB 10.4 allows no LIMIT in an IN subquery, hence the derived table).
        await c.ExecuteAsync(new CommandDefinition(
            "DELETE h FROM online_story_history h JOIN (SELECT id FROM online_story_history WHERE account_id = @a ORDER BY id DESC " +
            "LIMIT 18446744073709551615 OFFSET @keep) old ON old.id = h.id",
            new { a = row.AccountId, keep = Math.Max(1, keep) }, tx, cancellationToken: ct));
    }
}
