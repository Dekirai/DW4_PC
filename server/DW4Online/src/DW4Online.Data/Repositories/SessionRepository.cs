using System.Text.Json;
using Dapper;
using DW4Online.Domain.Abstractions;

namespace DW4Online.Data.Repositories;

public sealed class SessionRepository(Database db) : ISessionRepository
{
    public async Task<long> CreateAsync(long accountId, byte[] tokenHash, string kind, string? deviceName, byte[]? ip, DateTime now, DateTime expiresAt, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteScalarAsync<long>(new CommandDefinition(
            "INSERT INTO sessions (account_id, token_hash, kind, device_name, created_ip, created_at, last_used_at, expires_at) " +
            "VALUES (@a, @t, @k, @d, @ip, @now, @now, @exp); SELECT LAST_INSERT_ID();",
            new { a = accountId, t = tokenHash, k = kind, d = deviceName, ip, now, exp = expiresAt }, cancellationToken: ct));
    }

    public async Task<AuthenticatedSession?> FindAsync(byte[] tokenHash, DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.QuerySingleOrDefaultAsync<AuthenticatedSession>(new CommandDefinition(
            "SELECT s.id AS session_id, s.account_id, a.username, a.role, s.kind, s.last_used_at, s.expires_at, a.must_change_password " +
            "FROM sessions s JOIN accounts a ON a.id = s.account_id " +
            "WHERE s.token_hash = @t AND s.revoked_at IS NULL AND s.expires_at > @now AND a.status = 'active'",
            new { t = tokenHash, now }, cancellationToken: ct));
    }

    public async Task TouchAsync(long sessionId, DateTime now, DateTime expiresAt, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE sessions SET last_used_at = @now, expires_at = @exp WHERE id = @id AND revoked_at IS NULL",
            new { id = sessionId, now, exp = expiresAt }, cancellationToken: ct));
    }

    public async Task<bool> RevokeAsync(long accountId, long sessionId, DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteAsync(new CommandDefinition(
            "UPDATE sessions SET revoked_at = @now WHERE id = @id AND account_id = @a AND revoked_at IS NULL",
            new { id = sessionId, a = accountId, now }, cancellationToken: ct)) > 0;
    }

    public async Task<int> RevokeAllAsync(long accountId, long? exceptSessionId, DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteAsync(new CommandDefinition(
            "UPDATE sessions SET revoked_at = @now WHERE account_id = @a AND revoked_at IS NULL AND expires_at > @now " +
            "AND (@except IS NULL OR id <> @except)",
            new { a = accountId, now, except = exceptSessionId }, cancellationToken: ct));
    }

    public async Task TrimAsync(long accountId, int keep, DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        var ids = (await c.QueryAsync<long>(new CommandDefinition(
            "SELECT id FROM sessions WHERE account_id = @a AND revoked_at IS NULL AND expires_at > @now ORDER BY created_at DESC, id DESC",
            new { a = accountId, now }, cancellationToken: ct))).Skip(keep).ToList();
        if (ids.Count > 0)
            await c.ExecuteAsync(new CommandDefinition(
                "UPDATE sessions SET revoked_at = @now WHERE id IN @ids", new { ids, now }, cancellationToken: ct));
    }

    public async Task<IReadOnlyList<SessionInfo>> ListAsync(long accountId, DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return (await c.QueryAsync<SessionInfo>(new CommandDefinition(
            "SELECT id, kind, device_name, created_at, last_used_at, expires_at FROM sessions " +
            "WHERE account_id = @a AND revoked_at IS NULL AND expires_at > @now ORDER BY COALESCE(last_used_at, created_at) DESC",
            new { a = accountId, now }, cancellationToken: ct))).AsList();
    }

    public async Task<bool> KnowsAddressAsync(long accountId, byte[] ip, DateTime since, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        var network = ip.Length == 16 ? ip[..8] : null;
        return await c.ExecuteScalarAsync<bool>(new CommandDefinition(
            "SELECT EXISTS (SELECT 1 FROM sessions WHERE account_id = @a AND (created_at >= @since OR last_used_at >= @since) " +
            "AND (created_ip = @ip OR (@net IS NOT NULL AND LENGTH(created_ip) = 16 AND LEFT(created_ip, 8) = @net)))",
            new { a = accountId, ip, net = network, since }, cancellationToken: ct));
    }
}

public sealed class AuditLog(Database db) : IAuditLog
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);

    public async Task WriteAsync(string evt, long? accountId, long? actorId, byte[]? ip, object? detail, DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await c.ExecuteAsync(new CommandDefinition(
            "INSERT INTO audit_log (at, account_id, actor_id, event, ip, detail) VALUES (@now, @a, @actor, @e, @ip, @d)",
            new { now, a = accountId, actor = actorId, e = evt, ip, d = detail is null ? null : JsonSerializer.Serialize(detail, Json) },
            cancellationToken: ct));
    }
}

/// <summary>Housekeeping: audit retention and old sessions.</summary>
public sealed class MaintenanceRepository(Database db)
{
    public async Task<(int Audit, int Sessions)> PruneAsync(DateTime now, int auditDays, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        var audit = await c.ExecuteAsync(new CommandDefinition(
            "DELETE FROM audit_log WHERE at < @cut LIMIT 10000", new { cut = now.AddDays(-auditDays) }, cancellationToken: ct));
        var sessions = await c.ExecuteAsync(new CommandDefinition(
            "DELETE FROM sessions WHERE expires_at < @cut OR revoked_at < @cut LIMIT 10000", new { cut = now.AddDays(-30) }, cancellationToken: ct));
        return (audit, sessions);
    }
}
