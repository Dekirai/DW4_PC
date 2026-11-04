using Dapper;
using DW4Online.Domain.Abstractions;
using MySqlConnector;

namespace DW4Online.Data.Repositories;

/// <summary>
/// online_characters holds the current revision; online_character_history holds every written revision
/// (the current one included), pruned to the newest history-keep + 1. Revisions are contiguous per account and keep
/// counting across a reset.
/// </summary>
public sealed class OnlineCharacterRepository(Database db) : IOnlineCharacterRepository
{
    public async Task<OnlineCharacterRow?> GetAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.QuerySingleOrDefaultAsync<OnlineCharacterRow>(new CommandDefinition(
            "SELECT account_id, record, record_sha256, revision, play_seconds, flagged, client_version, created_at, updated_at " +
            "FROM online_characters WHERE account_id = @a", new { a = accountId }, cancellationToken: ct));
    }

    public async Task<WriteOutcome> SaveAsync(OnlineCharacterWrite w, int? expectedRevision, int historyKeep, CancellationToken ct = default)
    {
        for (var attempt = 1; ; ++attempt)
        {
            await using var c = await db.OpenAsync(ct);
            try
            {
                await using var tx = await c.BeginTransactionAsync(MySqlErrors.RevisionedWrite, ct);
                if (await CheckRevisionAsync(c, tx, w.AccountId, expectedRevision, ct) is { } refused) return refused;
                var revision = await WriteRevisionAsync(c, tx, w, expectedRevision, historyKeep, ct);
                await tx.CommitAsync(ct);
                return new WriteOutcome(WriteStatus.Saved, revision);
            }
            catch (MySqlException e) when (MySqlErrors.RetryDeadlock(e, attempt))
            {
                // Another account's write locked the same gap: this one runs again.
            }
            catch (MySqlException e) when (MySqlErrors.IsDuplicate(e) || MySqlErrors.IsDeadlockOrTimeout(e))
            {
                // A concurrent upload won the race.
                var now = await GetAsync(w.AccountId, ct);
                return new WriteOutcome(WriteStatus.Conflict, now?.Revision ?? 0);
            }
        }
    }

    public async Task<WriteOutcome> RestoreAsync(OnlineCharacterWrite w, int? expectedRevision, DateTime? endedAt, int historyKeep,
        CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        try
        {
            await using var tx = await c.BeginTransactionAsync(ct);
            if (await CheckRevisionAsync(c, tx, w.AccountId, expectedRevision, ct) is { } refused) return refused;
            if (endedAt is { } ended)
            {
                // The live character this restore replaces (if any) ends now, like a reset: its revisions stay
                // ResetHistoryDays. The restored character lives again: its revisions are no longer up for deletion.
                await c.ExecuteAsync(new CommandDefinition(
                    "UPDATE online_character_history SET ended_at = @now WHERE account_id = @a AND ended_at IS NULL",
                    new { a = w.AccountId, now = w.Now }, tx, cancellationToken: ct));
                await c.ExecuteAsync(new CommandDefinition(
                    "UPDATE online_character_history SET ended_at = NULL WHERE account_id = @a AND ended_at = @ended",
                    new { a = w.AccountId, ended }, tx, cancellationToken: ct));
                // The live character could have started at the reset before the one that ended it (or with the
                // account): the history's earlier ends, else the security log's earlier resets.
                await c.ExecuteAsync(new CommandDefinition(
                    "UPDATE accounts SET online_reset_at = (SELECT MAX(t) FROM (" +
                    "SELECT MAX(ended_at) AS t FROM online_character_history WHERE account_id = @a AND ended_at < @ended " +
                    "UNION ALL SELECT MAX(at) FROM audit_log WHERE account_id = @a AND event = 'character_reset' AND at < @ended) AS earlier) " +
                    "WHERE id = @a",
                    new { a = w.AccountId, ended }, tx, cancellationToken: ct));
            }
            var revision = await WriteRevisionAsync(c, tx, w, expectedRevision, historyKeep, ct);
            await tx.CommitAsync(ct);
            return new WriteOutcome(WriteStatus.Saved, revision);
        }
        catch (MySqlException e) when (MySqlErrors.IsDuplicate(e) || MySqlErrors.IsDeadlockOrTimeout(e))
        {
            var now = await GetAsync(w.AccountId, ct);
            return new WriteOutcome(WriteStatus.Conflict, now?.Revision ?? 0);
        }
    }

    /// <summary>Locks the live row; null when <paramref name="expectedRevision"/> matches it (null: no live character).</summary>
    private static async Task<WriteOutcome?> CheckRevisionAsync(MySqlConnection c, MySqlTransaction tx, long accountId, int? expectedRevision,
        CancellationToken ct)
    {
        var current = await c.QuerySingleOrDefaultAsync<int?>(new CommandDefinition(
            "SELECT revision FROM online_characters WHERE account_id = @a FOR UPDATE", new { a = accountId }, tx, cancellationToken: ct));
        if (expectedRevision is null) return current is null ? null : new WriteOutcome(WriteStatus.Conflict, current.Value);
        if (current is null) return new WriteOutcome(WriteStatus.NotFound, 0);
        return current.Value == expectedRevision.Value ? null : new WriteOutcome(WriteStatus.Conflict, current.Value);
    }

    /// <summary>Writes the live row and its history row (with the revision's play time and flag), prunes the history.</summary>
    private static async Task<int> WriteRevisionAsync(MySqlConnection c, MySqlTransaction tx, OnlineCharacterWrite w, int? expectedRevision,
        int historyKeep, CancellationToken ct)
    {
        int revision;
        var s = w.Summary;
        var values = new
        {
            a = w.AccountId,
            record = w.Record,
            sha = w.RecordSha256,
            unique = s.Unique,
            digimon = s.Digimon,
            row = s.SpeciesRow,
            name = s.PlayerName,
            level = s.Level,
            highest = s.HighestLevel,
            hp = s.Hp,
            maxHp = s.MaxHp,
            mp = s.Mp,
            maxMp = s.MaxMp,
            bits = s.Bits,
            bank = s.BankBits,
            forms = s.FormsReached,
            cards = s.CardsCollected,
            clears = s.Clears,
            play = w.PlaySeconds,
            flagged = w.Flagged,
            version = w.ClientVersion,
            now = w.Now,
        };
        if (expectedRevision is null)
        {
            revision = await c.ExecuteScalarAsync<int>(new CommandDefinition(
                "SELECT COALESCE(MAX(revision), 0) + 1 FROM online_character_history WHERE account_id = @a",
                new { a = w.AccountId }, tx, cancellationToken: ct));
            await c.ExecuteAsync(new CommandDefinition(
                "INSERT INTO online_characters (account_id, record, record_sha256, revision, unique_id, digimon, species_row, " +
                "player_name, level, highest_level, hp, max_hp, mp, max_mp, bits, bank_bits, forms_reached, cards, clears, " +
                "play_seconds, flagged, client_version, created_at, updated_at) VALUES (@a, @record, @sha, @rev, @unique, " +
                "@digimon, @row, @name, @level, @highest, @hp, @maxHp, @mp, @maxMp, @bits, @bank, @forms, @cards, @clears, " +
                "@play, @flagged, @version, @now, @now)",
                new DynamicParameters(values).With("rev", revision), tx, cancellationToken: ct));
        }
        else
        {
            revision = expectedRevision.Value + 1;
            await c.ExecuteAsync(new CommandDefinition(
                "UPDATE online_characters SET record = @record, record_sha256 = @sha, revision = @rev, unique_id = @unique, " +
                "digimon = @digimon, species_row = @row, player_name = @name, level = @level, highest_level = @highest, " +
                "hp = @hp, max_hp = @maxHp, mp = @mp, max_mp = @maxMp, bits = @bits, bank_bits = @bank, " +
                "forms_reached = @forms, cards = @cards, clears = @clears, play_seconds = @play, flagged = @flagged, " +
                "client_version = @version, updated_at = @now WHERE account_id = @a AND revision = @expected",
                new DynamicParameters(values).With("rev", revision).With("expected", expectedRevision.Value), tx, cancellationToken: ct));
        }
        await c.ExecuteAsync(new CommandDefinition(
            "INSERT INTO online_character_history (account_id, revision, record, record_sha256, level, play_seconds, flagged, reason, saved_at) " +
            "VALUES (@a, @rev, @record, @sha, @level, @play, @flagged, @reason, @now)",
            new
            {
                a = w.AccountId, rev = revision, record = w.Record, sha = w.RecordSha256, level = s.Level, play = w.PlaySeconds,
                flagged = w.Flagged, reason = w.Reason, now = w.Now,
            },
            tx, cancellationToken: ct));
        await c.ExecuteAsync(new CommandDefinition(
            "DELETE FROM online_character_history WHERE account_id = @a AND revision <= @cut",
            new { a = w.AccountId, cut = revision - historyKeep - 1 }, tx, cancellationToken: ct));
        return revision;
    }

    public async Task<IReadOnlyList<HistoryEntry>> ListHistoryAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return (await c.QueryAsync<HistoryEntry>(new CommandDefinition(
            "SELECT revision, level, play_seconds, reason, saved_at, flagged, ended_at FROM online_character_history WHERE account_id = @a ORDER BY revision DESC",
            new { a = accountId }, cancellationToken: ct))).AsList();
    }

    public async Task<KeptRevision?> GetHistoryRevisionAsync(long accountId, int revision, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.QuerySingleOrDefaultAsync<KeptRevision>(new CommandDefinition(
            "SELECT record, play_seconds, flagged, ended_at FROM online_character_history WHERE account_id = @a AND revision = @r",
            new { a = accountId, r = revision }, cancellationToken: ct));
    }

    public async Task<bool> ResetAsync(long accountId, DateTime now, int historyKeep, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        var done = await c.ExecuteAsync(new CommandDefinition(
            "DELETE FROM online_characters WHERE account_id = @a", new { a = accountId }, tx, cancellationToken: ct)) > 0;
        if (done)
        {
            await c.ExecuteAsync(new CommandDefinition(
                "UPDATE accounts SET online_reset_at = @now WHERE id = @a", new { a = accountId, now }, tx, cancellationToken: ct));
            // The ended character's revisions: the housekeeping deletes them ResetHistoryDays after this reset.
            await c.ExecuteAsync(new CommandDefinition(
                "UPDATE online_character_history SET ended_at = @now WHERE account_id = @a AND ended_at IS NULL",
                new { a = accountId, now }, tx, cancellationToken: ct));
        }
        await tx.CommitAsync(ct);
        return done;
    }

    public async Task<byte[]?> GetLastEndedRecordAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.QuerySingleOrDefaultAsync<byte[]?>(new CommandDefinition(
            "SELECT record FROM online_character_history WHERE account_id = @a AND ended_at IS NOT NULL ORDER BY ended_at DESC, revision DESC LIMIT 1",
            new { a = accountId }, cancellationToken: ct));
    }

    public async Task<DateTime?> GetStartAsync(long accountId, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        return await c.QuerySingleOrDefaultAsync<DateTime?>(new CommandDefinition(
            "SELECT GREATEST(created_at, COALESCE(online_reset_at, created_at)) FROM accounts WHERE id = @a",
            new { a = accountId }, cancellationToken: ct));
    }
}

internal static class DynamicParametersExtensions
{
    public static DynamicParameters With(this DynamicParameters p, string name, object? value)
    {
        p.Add(name, value);
        return p;
    }
}
