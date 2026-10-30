using Dapper;
using DW4Online.Domain.Abstractions;

namespace DW4Online.Data.Repositories;

/// <summary>online_story: one row per account, the account's own online story (SEAMLESS.md 8.4). Newer revisions only.</summary>
public sealed class OnlineStoryRepository(Database db) : IOnlineStoryRepository
{
    public async Task<OnlineStoryRow?> GetAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.QuerySingleOrDefaultAsync<OnlineStoryRow>(new CommandDefinition(
            "SELECT account_id, revision, flags, folder, counters, updated_at FROM online_story WHERE account_id = @a",
            new { a = accountId }, cancellationToken: ct));
    }

    public async Task<bool> SaveAsync(OnlineStoryRow row, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        // A revision the row already has (or passed) changes nothing: saves can arrive out of order.
        var n = await c.ExecuteAsync(new CommandDefinition(
            "INSERT INTO online_story (account_id, revision, flags, folder, counters, updated_at) VALUES (@AccountId, @Revision, @Flags, @Folder, @Counters, @UpdatedAt) " +
            "ON DUPLICATE KEY UPDATE " +
            "flags = IF(VALUES(revision) > revision, VALUES(flags), flags), " +
            "folder = IF(VALUES(revision) > revision, VALUES(folder), folder), " +
            "counters = IF(VALUES(revision) > revision, VALUES(counters), counters), " +
            "updated_at = IF(VALUES(revision) > revision, VALUES(updated_at), updated_at), " +
            "revision = GREATEST(revision, VALUES(revision))",
            row, cancellationToken: ct));
        // MariaDB: 1 = inserted, 2 = updated, 0 = unchanged.
        return n > 0;
    }
}
