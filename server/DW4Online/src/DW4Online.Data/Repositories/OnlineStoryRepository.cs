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
        // Only a newer revision replaces the row (saves can arrive out of order); a new account's first one inserts it.
        var updated = await c.ExecuteAsync(new CommandDefinition(
            "UPDATE online_story SET revision = @Revision, flags = @Flags, folder = @Folder, counters = @Counters, updated_at = @UpdatedAt " +
            "WHERE account_id = @AccountId AND revision < @Revision", row, cancellationToken: ct));
        if (updated > 0) return true;
        return await c.ExecuteAsync(new CommandDefinition(
            "INSERT IGNORE INTO online_story (account_id, revision, flags, folder, counters, updated_at) " +
            "VALUES (@AccountId, @Revision, @Flags, @Folder, @Counters, @UpdatedAt)", row, cancellationToken: ct)) > 0;
    }
}
