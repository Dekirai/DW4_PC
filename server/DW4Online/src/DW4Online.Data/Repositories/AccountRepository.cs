using Dapper;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Accounts;
using MySqlConnector;

namespace DW4Online.Data.Repositories;

public sealed class AccountRepository(Database db) : IAccountRepository
{
    private const string Columns =
        "id, username, email, password_hash, role, status, failed_logins, locked_until, must_change_password, " +
        "profile_public, recovery_code_hash, created_at, last_login_at";

    public async Task<long> CreateAsync(NewAccount account, DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        try
        {
            return await c.ExecuteScalarAsync<long>(new CommandDefinition(
                "INSERT INTO accounts (username, username_norm, email, email_norm, password_hash, recovery_code_hash, role, " +
                "created_at, password_changed_at) VALUES (@u, @n, @e, @en, @h, @r, @role, @now, @now); SELECT LAST_INSERT_ID();",
                new
                {
                    u = account.Username,
                    n = UsernamePolicy.Normalize(account.Username),
                    e = account.Email,
                    en = account.Email is null ? null : EmailPolicy.Normalize(account.Email),
                    h = account.PasswordHash,
                    r = account.RecoveryCodeHash,
                    role = account.Role,
                    now,
                }, cancellationToken: ct));
        }
        catch (MySqlException e) when (MySqlErrors.IsDuplicate(e))
        {
            throw new DuplicateAccountException(e.Message.Contains("ux_accounts_email", StringComparison.Ordinal) ? "email" : "username");
        }
    }

    public async Task<AccountRecord?> GetAsync(long id, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.QuerySingleOrDefaultAsync<AccountRecord>(new CommandDefinition(
            $"SELECT {Columns} FROM accounts WHERE id = @id", new { id }, cancellationToken: ct));
    }

    public async Task<AccountRecord?> FindByUsernameAsync(string username, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.QuerySingleOrDefaultAsync<AccountRecord>(new CommandDefinition(
            $"SELECT {Columns} FROM accounts WHERE username_norm = @n", new { n = UsernamePolicy.Normalize(username) }, cancellationToken: ct));
    }

    public async Task RecordFailedLoginAsync(long id, int failedLogins, DateTime? lockedUntil, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE accounts SET failed_logins = @f, locked_until = @l WHERE id = @id",
            new { id, f = failedLogins, l = lockedUntil }, cancellationToken: ct));
    }

    public async Task RecordLoginAsync(long id, DateTime now, string? newPasswordHash, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE accounts SET failed_logins = 0, locked_until = NULL, last_login_at = @now, " +
            "password_hash = COALESCE(@h, password_hash) WHERE id = @id",
            new { id, now, h = newPasswordHash }, cancellationToken: ct));
    }

    public async Task SetPasswordAsync(long id, string passwordHash, bool mustChange, DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE accounts SET password_hash = @h, must_change_password = @m, password_changed_at = @now WHERE id = @id",
            new { id, h = passwordHash, m = mustChange, now }, cancellationToken: ct));
    }

    public async Task SetRecoveryCodeAsync(long id, byte[] hash, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE accounts SET recovery_code_hash = @h WHERE id = @id", new { id, h = hash }, cancellationToken: ct));
    }

    public async Task<bool> SetEmailAsync(long id, string? email, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        try
        {
            await c.ExecuteAsync(new CommandDefinition(
                "UPDATE accounts SET email = @e, email_norm = @n, " +
                "email_verified_at = IF(email_norm <=> @n, email_verified_at, NULL) WHERE id = @id",
                new { id, e = email, n = email is null ? null : EmailPolicy.Normalize(email) }, cancellationToken: ct));
            return true;
        }
        catch (MySqlException e) when (MySqlErrors.IsDuplicate(e))
        {
            return false;
        }
    }

    public async Task SetStatusAsync(long id, string status, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE accounts SET status = @status WHERE id = @id", new { id, status }, cancellationToken: ct));
    }

    public async Task SetRoleAsync(long id, string role, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE accounts SET role = @role WHERE id = @id", new { id, role }, cancellationToken: ct));
    }

    public async Task<IReadOnlyList<AccountRecord>> SearchAsync(string? query, int limit, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        var pattern = string.IsNullOrWhiteSpace(query)
            ? "%"
            : query.Trim().ToLowerInvariant().Replace("\\", "\\\\").Replace("%", "\\%").Replace("_", "\\_") + "%";
        var rows = await c.QueryAsync<AccountRecord>(new CommandDefinition(
            $"SELECT {Columns} FROM accounts WHERE username_norm LIKE @p OR email_norm LIKE @p ORDER BY username_norm LIMIT @limit",
            new { p = pattern, limit }, cancellationToken: ct));
        return rows.AsList();
    }

    public async Task SetProfilePublicAsync(long id, bool value, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE accounts SET profile_public = @value WHERE id = @id", new { id, value }, cancellationToken: ct));
    }

    public async Task ScrubAsync(long id, string anonymousName, string? nameTag, CancellationToken ct = default)
    {
        string[] owned =
        [
            "sessions", "email_tokens", "online_character_history", "online_characters", "online_story", "online_story_history", "cloud_save_history", "cloud_saves",
            "achievement_unlocks", "player_stats", "speedrun_times", "room_members", "account_bans",
        ];
        await using var c = await db.OpenAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        foreach (var table in owned)
            await c.ExecuteAsync(new CommandDefinition($"DELETE FROM {table} WHERE account_id = @id", new { id }, tx, cancellationToken: ct));
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE audit_log SET ip = NULL WHERE account_id = @id OR actor_id = @id", new { id }, tx, cancellationToken: ct));
        if (nameTag is not null)
            await c.ExecuteAsync(new CommandDefinition(
                "UPDATE audit_log SET ip = NULL, detail = NULL WHERE event = 'login_unknown' AND JSON_VALUE(detail, '$.name') = @tag",
                new { tag = nameTag }, tx, cancellationToken: ct));
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE accounts SET username = @name, username_norm = @name, email = NULL, email_norm = NULL, email_verified_at = NULL, " +
            "password_hash = '!', must_change_password = 0, recovery_code_hash = NULL, status = 'deleted', failed_logins = 0, " +
            "locked_until = NULL, profile_public = 0, last_login_at = NULL WHERE id = @id",
            new { id, name = anonymousName }, tx, cancellationToken: ct));
        await tx.CommitAsync(ct);
    }

    public async Task<long> CountActiveAsync(CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteScalarAsync<long>(new CommandDefinition(
            "SELECT COUNT(*) FROM accounts WHERE status IN ('active', 'locked')", cancellationToken: ct));
    }
}
