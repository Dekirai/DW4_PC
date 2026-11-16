using Dapper;
using DW4Online.Domain.Abstractions;

namespace DW4Online.Data.Repositories;

/// <summary>
/// player_reports (round I1b, migration 0014_reports, SEAMLESS.md 19.17.1). The rules (limits, repeats, who may report whom)
/// live in ReportService; this is the store. An insert writes nothing when either account is deleted.
/// </summary>
public sealed class ReportRepository(Database db) : IReportRepository
{
    private const string Select =
        "SELECT r.id, r.reporter_account_id, rp.username AS reporter, r.reported_account_id, rd.username AS reported, r.reason, r.source, " +
        "r.instance_id, r.created_at, r.state, r.handled_by, hs.username AS handled_by_name, r.handled_at, " +
        "(SELECT COUNT(*) FROM player_reports o WHERE o.reported_account_id = r.reported_account_id AND o.state = 'open') AS open_about_reported, " +
        "ih.username AS instance_host, h.first_stage AS instance_stage, h.started_at AS instance_started " +
        "FROM player_reports r JOIN accounts rp ON rp.id = r.reporter_account_id JOIN accounts rd ON rd.id = r.reported_account_id " +
        "LEFT JOIN accounts hs ON hs.id = r.handled_by " +
        "LEFT JOIN instance_history h ON h.id = r.instance_id LEFT JOIN accounts ih ON ih.id = h.host_account_id";

    public async Task<long?> InsertAsync(ReportInsert report, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        var written = await c.ExecuteAsync(new CommandDefinition(
            "INSERT INTO player_reports (reporter_account_id, reported_account_id, reason, source, instance_id, created_at) " +
            "SELECT @Reporter, @Reported, @Reason, @Source, " +
            "(SELECT id FROM instance_history WHERE id = @InstanceId), @At FROM DUAL " +
            "WHERE (SELECT COUNT(*) FROM accounts WHERE id IN (@Reporter, @Reported) AND status <> 'deleted') = 2",
            report, cancellationToken: ct));
        if (written == 0) return null;
        return await c.ExecuteScalarAsync<long>(new CommandDefinition("SELECT LAST_INSERT_ID()", cancellationToken: ct));
    }

    public async Task<int> CountByReporterAsync(long reporter, DateTime since, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return (int)await c.ExecuteScalarAsync<long>(new CommandDefinition(
            "SELECT COUNT(*) FROM player_reports WHERE reporter_account_id = @reporter AND created_at >= @since",
            new { reporter, since }, cancellationToken: ct));
    }

    public async Task<DateTime?> ReportTimeAsync(long reporter, DateTime since, int skip, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteScalarAsync<DateTime?>(new CommandDefinition(
            "SELECT created_at FROM player_reports WHERE reporter_account_id = @reporter AND created_at >= @since " +
            "ORDER BY created_at, id LIMIT 1 OFFSET @skip",
            new { reporter, since, skip = Math.Max(0, skip) }, cancellationToken: ct));
    }

    public async Task<int> DeleteDoneAsync(DateTime handledBefore, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteAsync(new CommandDefinition(
            "DELETE FROM player_reports WHERE state = 'done' AND handled_at < @handledBefore LIMIT 10000",
            new { handledBefore }, cancellationToken: ct));
    }

    public async Task<DateTime?> LastAboutAsync(long reporter, long reported, DateTime since, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteScalarAsync<DateTime?>(new CommandDefinition(
            "SELECT MAX(created_at) FROM player_reports WHERE reporter_account_id = @reporter AND reported_account_id = @reported AND created_at >= @since",
            new { reporter, reported, since }, cancellationToken: ct));
    }

    public async Task<long?> SharedInstanceAsync(long a, long b, DateTime since, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteScalarAsync<long?>(new CommandDefinition(
            "SELECT h.id FROM instance_history h " +
            "JOIN instance_history_members ma ON ma.instance_id = h.id AND ma.account_id = @a AND ma.role <> 'spectator' " +
            "JOIN instance_history_members mb ON mb.instance_id = h.id AND mb.account_id = @b AND mb.role <> 'spectator' " +
            "WHERE COALESCE(h.ended_at, UTC_TIMESTAMP(3)) >= @since ORDER BY h.started_at DESC LIMIT 1",
            new { a, b, since }, cancellationToken: ct));
    }

    public async Task<ReportRow?> GetAsync(long id, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.QuerySingleOrDefaultAsync<ReportRow>(new CommandDefinition(Select + " WHERE r.id = @id", new { id }, cancellationToken: ct));
    }

    public async Task<IReadOnlyList<ReportRow>> ListAsync(string? state, int offset, int limit, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        var rows = await c.QueryAsync<ReportRow>(new CommandDefinition(
            Select + (state is null ? "" : " WHERE r.state = @state") + " ORDER BY r.created_at DESC, r.id DESC LIMIT @limit OFFSET @offset",
            new { state, limit = Math.Max(1, limit), offset = Math.Max(0, offset) }, cancellationToken: ct));
        return rows.AsList();
    }

    public async Task<int> CountAsync(string? state, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return (int)await c.ExecuteScalarAsync<long>(new CommandDefinition(
            "SELECT COUNT(*) FROM player_reports" + (state is null ? "" : " WHERE state = @state"), new { state }, cancellationToken: ct));
    }

    public async Task<IReadOnlyList<ReportRow>> AboutAsync(long reported, int limit, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        var rows = await c.QueryAsync<ReportRow>(new CommandDefinition(
            Select + " WHERE r.reported_account_id = @reported ORDER BY r.created_at DESC, r.id DESC LIMIT @limit",
            new { reported, limit = Math.Max(1, limit) }, cancellationToken: ct));
        return rows.AsList();
    }

    public async Task<bool> MarkDoneAsync(long id, long staff, DateTime at, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteAsync(new CommandDefinition(
            "UPDATE player_reports SET state = 'done', handled_by = @staff, handled_at = @at WHERE id = @id AND state = 'open'",
            new { id, staff, at }, cancellationToken: ct)) > 0;
    }

    public async Task<bool> ReopenAsync(long id, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.ExecuteAsync(new CommandDefinition(
            "UPDATE player_reports SET state = 'open', handled_by = NULL, handled_at = NULL WHERE id = @id AND state = 'done'",
            new { id }, cancellationToken: ct)) > 0;
    }
}
