using Dapper;
using DW4Online.Domain.Abstractions;
using MySqlConnector;

namespace DW4Online.Data.Repositories;

/// <summary>cloud_saves holds the latest backup; cloud_save_history the older revisions (newest history-keep kept).</summary>
public sealed class CloudSaveRepository(Database db) : ICloudSaveRepository
{
    private const string MetaColumns = "revision, format, size_bytes, sha256, slot_summary, device_name, client_version, uploaded_at";

    public async Task<CloudSaveMeta?> GetMetaAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.QuerySingleOrDefaultAsync<CloudSaveMeta>(new CommandDefinition(
            $"SELECT {MetaColumns} FROM cloud_saves WHERE account_id = @a", new { a = accountId }, cancellationToken: ct));
    }

    public async Task<IReadOnlyList<CloudSaveMeta>> ListHistoryAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return (await c.QueryAsync<CloudSaveMeta>(new CommandDefinition(
            $"SELECT {MetaColumns} FROM cloud_save_history WHERE account_id = @a ORDER BY revision DESC",
            new { a = accountId }, cancellationToken: ct))).AsList();
    }

    public async Task<CloudSaveData?> GetDataAsync(long accountId, int? revision, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        var latest = await c.QuerySingleOrDefaultAsync<CloudSaveData>(new CommandDefinition(
            "SELECT revision, format, data, sha256, uploaded_at FROM cloud_saves WHERE account_id = @a AND (@r IS NULL OR revision = @r)",
            new { a = accountId, r = revision }, cancellationToken: ct));
        if (latest is not null || revision is null) return latest;
        return await c.QuerySingleOrDefaultAsync<CloudSaveData>(new CommandDefinition(
            "SELECT revision, format, data, sha256, uploaded_at FROM cloud_save_history WHERE account_id = @a AND revision = @r",
            new { a = accountId, r = revision }, cancellationToken: ct));
    }

    public async Task<WriteOutcome> SaveAsync(CloudSaveWrite w, int? expectedRevision, int historyKeep, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        try
        {
            await using var tx = await c.BeginTransactionAsync(ct);
            var current = await c.QuerySingleOrDefaultAsync<int?>(new CommandDefinition(
                "SELECT revision FROM cloud_saves WHERE account_id = @a FOR UPDATE", new { a = w.AccountId }, tx, cancellationToken: ct));
            var values = new
            {
                a = w.AccountId,
                format = w.Format,
                data = w.Data,
                size = w.Data.Length,
                sha = w.Sha256,
                summary = w.SlotSummaryJson,
                device = w.DeviceName,
                version = w.ClientVersion,
                now = w.Now,
            };
            int revision;
            if (expectedRevision is null)
            {
                if (current is not null) return new WriteOutcome(WriteStatus.Conflict, current.Value);
                revision = await c.ExecuteScalarAsync<int>(new CommandDefinition(
                    "SELECT COALESCE(MAX(revision), 0) + 1 FROM cloud_save_history WHERE account_id = @a",
                    new { a = w.AccountId }, tx, cancellationToken: ct));
                await c.ExecuteAsync(new CommandDefinition(
                    "INSERT INTO cloud_saves (account_id, revision, format, data, size_bytes, sha256, slot_summary, device_name, " +
                    "client_version, uploaded_at) VALUES (@a, @rev, @format, @data, @size, @sha, @summary, @device, @version, @now)",
                    new DynamicParameters(values).With("rev", revision), tx, cancellationToken: ct));
            }
            else
            {
                if (current is null) return new WriteOutcome(WriteStatus.NotFound, 0);
                if (current.Value != expectedRevision.Value) return new WriteOutcome(WriteStatus.Conflict, current.Value);
                revision = expectedRevision.Value + 1;
                await c.ExecuteAsync(new CommandDefinition(
                    "INSERT INTO cloud_save_history (account_id, revision, format, data, size_bytes, sha256, slot_summary, device_name, " +
                    "client_version, uploaded_at) SELECT account_id, revision, format, data, size_bytes, sha256, slot_summary, " +
                    "device_name, client_version, uploaded_at FROM cloud_saves WHERE account_id = @a",
                    new { a = w.AccountId }, tx, cancellationToken: ct));
                await c.ExecuteAsync(new CommandDefinition(
                    "UPDATE cloud_saves SET revision = @rev, format = @format, data = @data, size_bytes = @size, sha256 = @sha, " +
                    "slot_summary = @summary, device_name = @device, client_version = @version, uploaded_at = @now " +
                    "WHERE account_id = @a AND revision = @expected",
                    new DynamicParameters(values).With("rev", revision).With("expected", expectedRevision.Value), tx, cancellationToken: ct));
                await c.ExecuteAsync(new CommandDefinition(
                    "DELETE FROM cloud_save_history WHERE account_id = @a AND revision <= @cut",
                    new { a = w.AccountId, cut = revision - historyKeep - 1 }, tx, cancellationToken: ct));
            }
            await tx.CommitAsync(ct);
            return new WriteOutcome(WriteStatus.Saved, revision);
        }
        catch (MySqlException e) when (MySqlErrors.IsDuplicate(e) || MySqlErrors.IsDeadlockOrTimeout(e))
        {
            var now = await GetMetaAsync(w.AccountId, ct);
            return new WriteOutcome(WriteStatus.Conflict, now?.Revision ?? 0);
        }
    }

    public async Task<bool> DeleteAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        var deleted = await c.ExecuteAsync(new CommandDefinition(
            "DELETE FROM cloud_saves WHERE account_id = @a", new { a = accountId }, tx, cancellationToken: ct));
        var history = await c.ExecuteAsync(new CommandDefinition(
            "DELETE FROM cloud_save_history WHERE account_id = @a", new { a = accountId }, tx, cancellationToken: ct));
        await tx.CommitAsync(ct);
        return deleted + history > 0;
    }
}
