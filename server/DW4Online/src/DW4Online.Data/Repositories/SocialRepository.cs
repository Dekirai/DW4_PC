using Dapper;
using DW4Online.Domain.Abstractions;
using MySqlConnector;

namespace DW4Online.Data.Repositories;

/// <summary>
/// friendships (one row per pair, account_lo &lt; account_hi), friend_requests and account_blocks (migration 0012,
/// SEAMLESS.md 19.17.1). The rules live in SocialService; this is the store. Writes that name an account write nothing
/// when that account is deleted (the account row stays as an anonymous placeholder, so the foreign keys alone would not
/// stop it).
/// </summary>
public sealed class SocialRepository(Database db) : ISocialRepository
{
    private const string RequestColumns = "from_account_id, to_account_id, state, delivered, created_at, decided_at, cancelled_at";

    /// <summary>Both accounts exist and are not deleted (the guard of every insert).</summary>
    private const string BothLive = "(SELECT COUNT(*) FROM accounts WHERE id IN (@a, @b) AND status <> 'deleted') = 2";

    private static (long Lo, long Hi) Pair(long a, long b) => a < b ? (a, b) : (b, a);

    public async Task<bool> AreFriendsAsync(long a, long b, CancellationToken ct = default)
    {
        var (lo, hi) = Pair(a, b);
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteScalarAsync<long>(new CommandDefinition(
            "SELECT COUNT(*) FROM friendships WHERE account_lo = @lo AND account_hi = @hi", new { lo, hi }, cancellationToken: ct)) > 0;
    }

    public async Task<int> CountFriendsAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return (int)await c.ExecuteScalarAsync<long>(new CommandDefinition(
            "SELECT (SELECT COUNT(*) FROM friendships WHERE account_lo = @id) + (SELECT COUNT(*) FROM friendships WHERE account_hi = @id)",
            new { id = accountId }, cancellationToken: ct));
    }

    public async Task<IReadOnlyList<SocialListRow>> ListFriendsAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        var rows = await c.QueryAsync<SocialListRow>(new CommandDefinition(
            "SELECT a.id AS account_id, a.username, f.at FROM (" +
            "  SELECT account_hi AS other, created_at AS at FROM friendships WHERE account_lo = @id" +
            "  UNION ALL SELECT account_lo, created_at FROM friendships WHERE account_hi = @id) f " +
            "JOIN accounts a ON a.id = f.other ORDER BY a.username_norm", new { id = accountId }, cancellationToken: ct));
        return rows.AsList();
    }

    public async Task<bool> AddFriendshipAsync(long a, long b, DateTime now, CancellationToken ct = default)
    {
        var (lo, hi) = Pair(a, b);
        await using var c = await db.OpenAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        var written = await c.ExecuteAsync(new CommandDefinition(
            $"INSERT IGNORE INTO friendships (account_lo, account_hi, created_at) SELECT @lo, @hi, @now FROM DUAL WHERE {BothLive}",
            new { lo, hi, a, b, now }, tx, cancellationToken: ct)) > 0;
        if (written)
            await c.ExecuteAsync(new CommandDefinition(
                "DELETE FROM friend_requests WHERE (from_account_id = @a AND to_account_id = @b) OR (from_account_id = @b AND to_account_id = @a)",
                new { a, b }, tx, cancellationToken: ct));
        await tx.CommitAsync(ct);
        return written;
    }

    public async Task<bool> RemoveFriendshipAsync(long a, long b, CancellationToken ct = default)
    {
        var (lo, hi) = Pair(a, b);
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteAsync(new CommandDefinition(
            "DELETE FROM friendships WHERE account_lo = @lo AND account_hi = @hi", new { lo, hi }, cancellationToken: ct)) > 0;
    }

    public async Task<FriendRequestRow?> GetRequestAsync(long from, long to, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.QuerySingleOrDefaultAsync<FriendRequestRow>(new CommandDefinition(
            $"SELECT {RequestColumns} FROM friend_requests WHERE from_account_id = @from AND to_account_id = @to",
            new { from, to }, cancellationToken: ct));
    }

    public async Task<bool> PutPendingRequestAsync(long from, long to, bool delivered, DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteAsync(new CommandDefinition(
            "INSERT INTO friend_requests (from_account_id, to_account_id, state, delivered, created_at, decided_at, cancelled_at) " +
            $"SELECT @a, @b, 'pending', @delivered, @now, NULL, NULL FROM DUAL WHERE {BothLive} " +
            "ON DUPLICATE KEY UPDATE state = 'pending', delivered = VALUES(delivered), created_at = VALUES(created_at), decided_at = NULL, cancelled_at = NULL",
            new { a = from, b = to, delivered, now }, cancellationToken: ct)) > 0;
    }

    public async Task<bool> ResendDeclinedRequestAsync(long from, long to, DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteAsync(new CommandDefinition(
            "UPDATE friend_requests SET cancelled_at = NULL, created_at = @now " +
            "WHERE from_account_id = @from AND to_account_id = @to AND state = 'declined'",
            new { from, to, now }, cancellationToken: ct)) > 0;
    }

    public async Task<bool> DeclineRequestAsync(long from, long to, DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteAsync(new CommandDefinition(
            "UPDATE friend_requests SET state = 'declined', decided_at = @now " +
            "WHERE from_account_id = @from AND to_account_id = @to AND state = 'pending'",
            new { from, to, now }, cancellationToken: ct)) > 0;
    }

    public async Task<bool> CancelDeclinedRequestAsync(long from, long to, DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteAsync(new CommandDefinition(
            "UPDATE friend_requests SET cancelled_at = @now " +
            "WHERE from_account_id = @from AND to_account_id = @to AND state = 'declined' AND cancelled_at IS NULL",
            new { from, to, now }, cancellationToken: ct)) > 0;
    }

    public async Task<bool> DeleteRequestAsync(long from, long to, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteAsync(new CommandDefinition(
            "DELETE FROM friend_requests WHERE from_account_id = @from AND to_account_id = @to",
            new { from, to }, cancellationToken: ct)) > 0;
    }

    public async Task<int> CountOutgoingAsync(long from, DateTime since, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return (int)await c.ExecuteScalarAsync<long>(new CommandDefinition(
            "SELECT COUNT(*) FROM friend_requests WHERE from_account_id = @from AND created_at >= @since AND cancelled_at IS NULL",
            new { from, since }, cancellationToken: ct));
    }

    public async Task<int> CountIncomingAsync(long to, DateTime since, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return (int)await c.ExecuteScalarAsync<long>(new CommandDefinition(
            "SELECT COUNT(*) FROM friend_requests WHERE to_account_id = @to AND state = 'pending' AND created_at >= @since AND delivered = 1",
            new { to, since }, cancellationToken: ct));
    }

    public async Task<IReadOnlyList<SocialListRow>> ListOutgoingAsync(long from, DateTime since, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        var rows = await c.QueryAsync<SocialListRow>(new CommandDefinition(
            "SELECT a.id AS account_id, a.username, r.created_at AS at FROM friend_requests r JOIN accounts a ON a.id = r.to_account_id " +
            "WHERE r.from_account_id = @from AND r.created_at >= @since AND r.cancelled_at IS NULL ORDER BY r.created_at DESC",
            new { from, since }, cancellationToken: ct));
        return rows.AsList();
    }

    public async Task<IReadOnlyList<SocialListRow>> ListIncomingAsync(long to, DateTime since, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        var rows = await c.QueryAsync<SocialListRow>(new CommandDefinition(
            "SELECT a.id AS account_id, a.username, r.created_at AS at FROM friend_requests r JOIN accounts a ON a.id = r.from_account_id " +
            "WHERE r.to_account_id = @to AND r.state = 'pending' AND r.created_at >= @since AND r.delivered = 1 ORDER BY r.created_at DESC",
            new { to, since }, cancellationToken: ct));
        return rows.AsList();
    }

    public async Task<int> PurgeRequestsAsync(DateTime cut, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteAsync(new CommandDefinition(
            "DELETE FROM friend_requests WHERE created_at < @cut LIMIT 10000", new { cut }, cancellationToken: ct));
    }

    public async Task<bool> IsBlockedAsync(long blocker, long blocked, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteScalarAsync<long>(new CommandDefinition(
            "SELECT COUNT(*) FROM account_blocks WHERE account_id = @blocker AND blocked_account_id = @blocked",
            new { blocker, blocked }, cancellationToken: ct)) > 0;
    }

    public async Task<int> CountBlocksAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return (int)await c.ExecuteScalarAsync<long>(new CommandDefinition(
            "SELECT COUNT(*) FROM account_blocks WHERE account_id = @id", new { id = accountId }, cancellationToken: ct));
    }

    public async Task<IReadOnlyList<SocialListRow>> ListBlocksAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        var rows = await c.QueryAsync<SocialListRow>(new CommandDefinition(
            "SELECT a.id AS account_id, a.username, b.created_at AS at FROM account_blocks b JOIN accounts a ON a.id = b.blocked_account_id " +
            "WHERE b.account_id = @id ORDER BY a.username_norm", new { id = accountId }, cancellationToken: ct));
        return rows.AsList();
    }

    public async Task<bool> AddBlockAsync(long blocker, long blocked, DateTime now, CancellationToken ct = default)
    {
        var (lo, hi) = Pair(blocker, blocked);
        await using var c = await db.OpenAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        var written = await c.ExecuteAsync(new CommandDefinition(
            "INSERT IGNORE INTO account_blocks (account_id, blocked_account_id, created_at) " +
            $"SELECT @a, @b, @now FROM DUAL WHERE {BothLive}", new { a = blocker, b = blocked, now }, tx, cancellationToken: ct)) > 0;
        if (written)
        {
            await c.ExecuteAsync(new CommandDefinition(
                "DELETE FROM friendships WHERE account_lo = @lo AND account_hi = @hi", new { lo, hi }, tx, cancellationToken: ct));
            // The blocker's own request goes; the blocked player's stays "sent" to him but no longer reaches the blocker.
            await c.ExecuteAsync(new CommandDefinition(
                "DELETE FROM friend_requests WHERE from_account_id = @a AND to_account_id = @b", new { a = blocker, b = blocked }, tx, cancellationToken: ct));
            await c.ExecuteAsync(new CommandDefinition(
                "UPDATE friend_requests SET delivered = 0 WHERE from_account_id = @b AND to_account_id = @a AND state = 'pending'",
                new { a = blocker, b = blocked }, tx, cancellationToken: ct));
        }
        await tx.CommitAsync(ct);
        return written;
    }

    public async Task<bool> RemoveBlockAsync(long blocker, long blocked, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        var removed = await c.ExecuteAsync(new CommandDefinition(
            "DELETE FROM account_blocks WHERE account_id = @a AND blocked_account_id = @b", new { a = blocker, b = blocked }, tx, cancellationToken: ct)) > 0;
        if (removed)
            await c.ExecuteAsync(new CommandDefinition(
                "UPDATE friend_requests SET delivered = 1 WHERE from_account_id = @b AND to_account_id = @a AND state = 'pending' AND delivered = 0",
                new { a = blocker, b = blocked }, tx, cancellationToken: ct));
        await tx.CommitAsync(ct);
        return removed;
    }
}
