using System.Security.Cryptography;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Achievements;
using DW4Online.Domain.Save;
using DW4Online.Domain.Story;

namespace DW4Online.Domain.Services;

public sealed record CharacterView(int Revision, byte[] Record, CharacterSummary Summary, long PlaySeconds, bool Flagged, DateTime CreatedAt, DateTime UpdatedAt);

public sealed record CharacterSaveResult(int Revision, CharacterSummary Summary, IReadOnlyList<ValidationIssue> Warnings, IReadOnlyList<string> Unlocked);

/// <summary>
/// The online character: one per account, starts from zero, independent of local saves. Uploads are decoded
/// and validated here; the summary columns come from this decode, never from the client. The record's story bytes
/// (BASE_FLAG, BASE_FLAGFOLDER, BASE_COUNTER) are the account's own online story (SEAMLESS.md 8.4): GET composes them
/// from online_story, PUT replaces the uploaded ones with it (a guest uploads its character with the room owner's story;
/// state achievements never come from a borrowed story).
/// </summary>
public sealed class OnlineCharacterService(
    IOnlineCharacterRepository characters,
    AchievementService achievements,
    IAuditLog audit,
    VersionGate versions,
    OnlineCharacterOptions options,
    TimeProvider time,
    OnlineStoryService? stories = null)
{
    public async Task<CharacterView?> GetAsync(long accountId, CancellationToken ct = default)
    {
        var row = await characters.GetAsync(accountId, ct);
        if (row is null) return null;
        var record = stories is null ? row.Record : await stories.ComposeAsync(accountId, row.Record, ct);
        var summary = CharacterSummary.From(RecordCodec.Decode(record));
        return new CharacterView(row.Revision, record, summary, row.PlaySeconds, row.Flagged, row.CreatedAt, row.UpdatedAt);
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
        if (stories is not null)
        {
            // The uploaded story bytes are ignored: the stored record carries the account's own story.
            record = await stories.ComposeAsync(accountId, record, ct);
            decoded = RecordCodec.Decode(record);
        }

        var now = Clock.Now(time);
        var current = await characters.GetAsync(accountId, ct);
        // A save sent again after its answer was lost (the same record on the revision it was based on, the one right after
        // it): its own revision, not another PC's (the client said "saved on another PC" and stopped saving).
        if (current is not null && expectedRevision is { } based && current.Revision == based + 1 && SameOutsideStory(current.Record, record))
            return new CharacterSaveResult(current.Revision, CharacterSummary.From(RecordCodec.Decode(current.Record)), [], []);
        if (expectedRevision is null && current is not null)
            return DomainError.Conflict("character_exists", "An online character exists; update it with its revision.", Revision(current.Revision));
        if (expectedRevision is not null && current is null)
            return DomainError.NotFound("no_online_character", "There is no online character yet.");
        if (current is not null && current.Revision != expectedRevision)
            return DomainError.Conflict("revision_conflict", "The online character changed in the meantime.", Revision(current.Revision));
        // The character the last reset ended does not come back as a new one (another PC still had it in its card cache). A new
        // character's UNIQUE is the frame count of its creation and may repeat, so only a record beyond a new game's level with
        // the ended character's UNIQUE, name and partner is the old character.
        if (current is null && decoded.Level > 1 && await characters.GetLastEndedRecordAsync(accountId, ct) is { } ended &&
            RecordCodec.Decode(ended) is var old && old.Unique == decoded.Unique && old.Name == decoded.Name && old.Digimon == decoded.Digimon)
            return DomainError.Conflict("character_was_reset", "This online character was reset. Start a new game to make a new one.");

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

    /// <summary>Whether a reset has anything to do: an online character, or an online story (an owner whose guests played,
    /// or whose character is gone while its story is not).</summary>
    public async Task<bool> HasResetWorkAsync(long accountId, CancellationToken ct = default) =>
        await characters.GetAsync(accountId, ct) is not null || (stories is not null && !(await stories.LoadAsync(accountId, ct)).IsEmpty);

    /// <summary>Staff: whether the account's story history keeps that revision.</summary>
    public async Task<bool> HasStoryRevisionAsync(long accountId, long revision, CancellationToken ct = default) =>
        stories is not null && await stories.HasRevisionAsync(accountId, revision, ct);

    /// <summary>
    /// Ends the current online character; the next upload creates a new one. Its revisions stay in the history for
    /// <see cref="OnlineCharacterOptions.ResetHistoryDays"/> (staff can undo a reset made by mistake or look at a
    /// flagged character), then the housekeeping deletes them. The audit row keeps whether the character was flagged.
    /// The account's online story is emptied with it (a new character starts from a new game's story, its unlocked
    /// difficulties too); the old story stays in the story history. An account without a character whose story is not
    /// empty (an owner whose guests played) gets the story emptied alone, and a reset whose story part failed can be run
    /// again. The caller closes the account's own room first (its hub would save the old story again).
    /// </summary>
    public async Task<bool> ResetAsync(long accountId, long actorId, byte[]? ip, CancellationToken ct = default)
    {
        var now = Clock.Now(time);
        var current = await characters.GetAsync(accountId, ct);
        var character = current is not null && await characters.ResetAsync(accountId, now, options.HistoryRevisions, ct);
        var hadStory = stories is not null && !(await stories.LoadAsync(accountId, ct)).IsEmpty;
        var story = hadStory ? await stories!.ResetAsync(accountId, ct) : 0;
        if (!character && !hadStory) return false;
        await audit.WriteAsync("character_reset", accountId, actorId, ip,
            new { revision = current?.Revision, flagged = current?.Flagged, historyKeptDays = options.ResetHistoryDays, storyRevision = story }, now, ct);
        return true;
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

    /// <summary>Staff: the kept revisions of the account's online story, newest first.</summary>
    public async Task<IReadOnlyList<StoryHistoryEntry>> StoryHistoryAsync(long accountId, CancellationToken ct = default) =>
        stories is null ? [] : await stories.HistoryAsync(accountId, ct);

    /// <summary>
    /// Staff: makes a kept revision of the account's online story the current one again (the next revision, reason
    /// admin_restore), for damage a forged or faulty merge did. The caller closes the account's own room first.
    /// </summary>
    public async Task<Result<long>> RestoreStoryAsync(long accountId, long revision, long actorId, byte[]? ip, CancellationToken ct = default)
    {
        if (stories is null || await stories.RestoreAsync(accountId, revision, ct) is not { } restored)
            return DomainError.NotFound("revision_not_found", "No such kept revision of the online story.");
        var now = Clock.Now(time);
        await audit.WriteAsync("story_restore", accountId, actorId, ip, new { from = revision, revision = restored }, now, ct);
        return restored;
    }

    /// <summary>Two records alike outside the story bytes (the stored one carries the account's own story).</summary>
    private static bool SameOutsideStory(byte[] stored, byte[] upload)
    {
        if (stored.Length != upload.Length) return false;
        var empty = new RoomStoryData();
        return StoryRecord.Compose(stored, empty).AsSpan().SequenceEqual(StoryRecord.Compose(upload, empty));
    }

    private static DomainError Rejected(ValidationReport report) =>
        DomainError.Invalid("invalid_record", "The character record failed validation.", new Dictionary<string, object?>
        {
            ["issues"] = report.Errors,
        });

    private static Dictionary<string, object?> Revision(int revision) => new() { ["currentRevision"] = revision };

    private static string? Truncate(string? s, int max) => s is null ? null : s.Length <= max ? s : s[..max];
}
