using System.Security.Cryptography;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Achievements;
using DW4Online.Domain.Save;

namespace DW4Online.Domain.Services;

public sealed record CharacterView(int Revision, byte[] Record, CharacterSummary Summary, long PlaySeconds, bool Flagged, DateTime CreatedAt, DateTime UpdatedAt);

public sealed record CharacterSaveResult(int Revision, CharacterSummary Summary, IReadOnlyList<ValidationIssue> Warnings, IReadOnlyList<string> Unlocked);

/// <summary>
/// The online character: one per account, starts from zero, independent of local saves. Uploads are decoded
/// and validated here; the summary columns come from this decode, never from the client.
/// </summary>
public sealed class OnlineCharacterService(
    IOnlineCharacterRepository characters,
    AchievementService achievements,
    IAuditLog audit,
    VersionGate versions,
    OnlineCharacterOptions options,
    TimeProvider time)
{
    public async Task<CharacterView?> GetAsync(long accountId, CancellationToken ct = default)
    {
        var row = await characters.GetAsync(accountId, ct);
        if (row is null) return null;
        var summary = CharacterSummary.From(RecordCodec.Decode(row.Record));
        return new CharacterView(row.Revision, row.Record, summary, row.PlaySeconds, row.Flagged, row.CreatedAt, row.UpdatedAt);
    }

    /// <summary>
    /// Saves a revision. <paramref name="expectedRevision"/> null creates the character (it must not exist yet);
    /// otherwise it must match the stored revision (optimistic concurrency).
    /// </summary>
    public async Task<Result<CharacterSaveResult>> SaveAsync(long accountId, byte[] record, long playSeconds, int? expectedRevision,
        PlayContext? context, byte[]? ip, CancellationToken ct = default)
    {
        if (RankedRules.Check(context) is { } unranked) return DomainError.Invalid(unranked.Code, unranked.Message);
        if (versions.Check(context) is { } outdated) return outdated;

        var report = RecordValidator.Validate(record, out var decoded);
        if (!report.Ok || decoded is null) return Rejected(report);

        var now = Clock.Now(time);
        var current = await characters.GetAsync(accountId, ct);
        if (expectedRevision is null && current is not null)
            return DomainError.Conflict("character_exists", "An online character exists; update it with its revision.", Revision(current.Revision));
        if (expectedRevision is not null && current is null)
            return DomainError.NotFound("no_online_character", "There is no online character yet.");
        if (current is not null && current.Revision != expectedRevision)
            return DomainError.Conflict("revision_conflict", "The online character changed in the meantime.", Revision(current.Revision));

        var previous = current is null ? null : RecordCodec.Decode(current.Record);
        // A new character: the real time since the account was made or its online character was last reset.
        var since = current?.UpdatedAt ?? await characters.GetStartAsync(accountId, ct) ?? now;
        var wallSeconds = Math.Max(0, (now - since).TotalSeconds);
        RecordValidator.CheckTransition(report, previous, current?.PlaySeconds ?? 0, decoded, playSeconds, wallSeconds, options.Rules);
        if (!report.Ok) return Rejected(report);

        var summary = CharacterSummary.From(decoded);
        var flagged = report.Warnings.Count > 0 || current?.Flagged == true;
        var write = new OnlineCharacterWrite(accountId, record, SHA256.HashData(record), summary, playSeconds, flagged,
            Truncate(context?.ClientVersion, 32), current is null ? "create" : "upload", now);
        var outcome = await characters.SaveAsync(write, expectedRevision, options.HistoryRevisions, ct);
        if (outcome.Status == WriteStatus.Conflict)
            return DomainError.Conflict("revision_conflict", "The online character changed in the meantime.", Revision(outcome.Revision));
        if (outcome.Status == WriteStatus.NotFound)
            return DomainError.NotFound("no_online_character", "There is no online character yet.");

        if (report.Warnings.Count > 0)
            await audit.WriteAsync("character_flagged", accountId, null, ip,
                new { revision = outcome.Revision, warnings = report.Warnings.Select(w => w.Code).ToArray() }, now, ct);
        var unlocked = await achievements.EvaluateStateAsync(accountId, decoded, flagged, context, ct);
        return new CharacterSaveResult(outcome.Revision, summary, report.Warnings, unlocked);
    }

    public Task<IReadOnlyList<HistoryEntry>> HistoryAsync(long accountId, CancellationToken ct = default) =>
        characters.ListHistoryAsync(accountId, ct);

    /// <summary>
    /// Ends the current online character; the next upload creates a new one. Its revisions stay in the history for
    /// <see cref="OnlineCharacterOptions.ResetHistoryDays"/> (staff can undo a reset made by mistake or look at a
    /// flagged character), then the housekeeping deletes them. The audit row keeps whether the character was flagged.
    /// </summary>
    public async Task<bool> ResetAsync(long accountId, long actorId, byte[]? ip, CancellationToken ct = default)
    {
        var now = Clock.Now(time);
        var current = await characters.GetAsync(accountId, ct);
        var done = current is not null && await characters.ResetAsync(accountId, now, options.HistoryRevisions, ct);
        if (done)
            await audit.WriteAsync("character_reset", accountId, actorId, ip,
                new { revision = current!.Revision, flagged = current.Flagged, historyKeptDays = options.ResetHistoryDays }, now, ct);
        return done;
    }

    /// <summary>
    /// Staff: makes a kept revision the current one again (a new revision with reason admin_restore) with that
    /// revision's own play time and flag, so the next upload is checked against what the record really holds (an
    /// honest player's next upload is not "too fast", a flagged character stays flagged). Restoring a revision of a
    /// reset character undoes the reset: that character's revisions stay (no longer deleted ResetHistoryDays after the
    /// reset), and a live character it replaces ends like a reset.
    /// </summary>
    public async Task<Result<int>> RestoreAsync(long accountId, int revision, long actorId, byte[]? ip, CancellationToken ct = default)
    {
        var kept = await characters.GetHistoryRevisionAsync(accountId, revision, ct);
        if (kept is null) return DomainError.NotFound("revision_not_found", "No such kept revision.");
        var current = await characters.GetAsync(accountId, ct);
        var decoded = RecordCodec.Decode(kept.Record);
        var now = Clock.Now(time);
        var write = new OnlineCharacterWrite(accountId, kept.Record, SHA256.HashData(kept.Record), CharacterSummary.From(decoded),
            kept.PlaySeconds, kept.Flagged, current?.ClientVersion, "admin_restore", now);
        var outcome = await characters.RestoreAsync(write, current?.Revision, kept.EndedAt, options.HistoryRevisions, ct);
        if (outcome.Status != WriteStatus.Saved)
            return DomainError.Conflict("revision_conflict", "The online character changed in the meantime.", Revision(outcome.Revision));
        await audit.WriteAsync("character_restore", accountId, actorId, ip, new
        {
            from = revision,
            revision = outcome.Revision,
            flagged = kept.Flagged,
            undidReset = kept.EndedAt is not null,
            // A live character replaced by undoing a reset ended (its revisions stay ResetHistoryDays).
            replaced = kept.EndedAt is not null && current is not null ? current.Revision : (int?)null,
        }, now, ct);
        return outcome.Revision;
    }

    private static DomainError Rejected(ValidationReport report) =>
        DomainError.Invalid("invalid_record", "The character record failed validation.", new Dictionary<string, object?>
        {
            ["issues"] = report.Errors,
        });

    private static Dictionary<string, object?> Revision(int revision) => new() { ["currentRevision"] = revision };

    private static string? Truncate(string? s, int max) => s is null ? null : s.Length <= max ? s : s[..max];
}
