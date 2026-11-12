namespace DW4Online.Domain.Abstractions;

/// <summary>Thrown by <see cref="IAccountRepository.CreateAsync"/> when a unique key is taken.</summary>
public sealed class DuplicateAccountException(string field) : Exception($"{field} is taken")
{
    public string Field { get; } = field; // "username" or "email"
}

public interface IAccountRepository
{
    Task<long> CreateAsync(NewAccount account, DateTime now, CancellationToken ct = default);
    Task<AccountRecord?> GetAsync(long id, CancellationToken ct = default);
    Task<AccountRecord?> FindByUsernameAsync(string username, CancellationToken ct = default);
    Task RecordFailedLoginAsync(long id, int failedLogins, DateTime? lockedUntil, CancellationToken ct = default);
    Task RecordLoginAsync(long id, DateTime now, string? newPasswordHash, CancellationToken ct = default);
    Task SetPasswordAsync(long id, string passwordHash, bool mustChange, DateTime now, CancellationToken ct = default);
    Task SetRecoveryCodeAsync(long id, byte[] hash, CancellationToken ct = default);
    /// <summary>False when another account uses the address.</summary>
    Task<bool> SetEmailAsync(long id, string? email, CancellationToken ct = default);
    Task SetStatusAsync(long id, string status, CancellationToken ct = default);
    Task SetRoleAsync(long id, string role, CancellationToken ct = default);
    Task<IReadOnlyList<AccountRecord>> SearchAsync(string? query, int limit, CancellationToken ct = default);
    Task SetProfilePublicAsync(long id, bool value, CancellationToken ct = default);
    /// <summary>
    /// Deletes everything tied to the account (sessions, online character, cloud save, unlocks, stats, times, room
    /// memberships, bans, friendships, friend requests and blocks in both directions, its own player reports), removes its IP addresses from the audit log and anonymises the row (status deleted,
    /// user name <paramref name="anonymousName"/>, no e-mail, no usable password). Rooms it hosted keep their record.
    /// Failed logins with an unknown name whose tag is <paramref name="nameTag"/> (the account's name) lose the tag and
    /// their IP address too.
    /// </summary>
    Task ScrubAsync(long id, string anonymousName, string? nameTag, CancellationToken ct = default);
    /// <summary>Accounts that are not deleted or banned (the website's home page).</summary>
    Task<long> CountActiveAsync(CancellationToken ct = default);
}

public interface ISessionRepository
{
    Task<long> CreateAsync(long accountId, byte[] tokenHash, string kind, string? deviceName, byte[]? ip, DateTime now, DateTime expiresAt, CancellationToken ct = default);
    /// <summary>The live session of a token (not revoked, not expired, account active), or null.</summary>
    Task<AuthenticatedSession?> FindAsync(byte[] tokenHash, DateTime now, CancellationToken ct = default);
    /// <summary>Moves the session's sliding expiry; a non-null <paramref name="ip"/> becomes its last address.</summary>
    Task TouchAsync(long sessionId, DateTime now, DateTime expiresAt, byte[]? ip, CancellationToken ct = default);
    Task<bool> RevokeAsync(long accountId, long sessionId, DateTime now, CancellationToken ct = default);
    Task<int> RevokeAllAsync(long accountId, long? exceptSessionId, DateTime now, CancellationToken ct = default);
    /// <summary>Revokes the oldest live sessions beyond <paramref name="keep"/>.</summary>
    Task TrimAsync(long accountId, int keep, DateTime now, CancellationToken ct = default);
    Task<IReadOnlyList<SessionInfo>> ListAsync(long accountId, DateTime now, CancellationToken ct = default);
    /// <summary>
    /// True when the account created or used a session since <paramref name="since"/> that was created from this address
    /// or last used from it (IPv4 exactly, IPv6 by its /64 network).
    /// </summary>
    Task<bool> KnowsAddressAsync(long accountId, byte[] ip, DateTime since, CancellationToken ct = default);
}

public interface IAuditLog
{
    Task WriteAsync(string evt, long? accountId, long? actorId, byte[]? ip, object? detail, DateTime now, CancellationToken ct = default);
}

public interface IOnlineCharacterRepository
{
    Task<OnlineCharacterRow?> GetAsync(long accountId, CancellationToken ct = default);
    /// <summary>
    /// Creates (<paramref name="expectedRevision"/> null: fails with Conflict when a character exists) or updates
    /// (fails with Conflict on another revision, NotFound when none exists) and keeps <paramref name="historyKeep"/> old revisions.
    /// </summary>
    Task<WriteOutcome> SaveAsync(OnlineCharacterWrite write, int? expectedRevision, int historyKeep, CancellationToken ct = default);
    Task<IReadOnlyList<HistoryEntry>> ListHistoryAsync(long accountId, CancellationToken ct = default);
    /// <summary>A kept revision with its play time, its flag and when its character ended (null: the live character's).</summary>
    Task<KeptRevision?> GetHistoryRevisionAsync(long accountId, int revision, CancellationToken ct = default);
    /// <summary>
    /// Staff restore: writes <paramref name="write"/> (a kept revision's record, play time and flag) as the next revision
    /// like <see cref="SaveAsync"/> (<paramref name="expectedRevision"/>: the live revision, null when none). When the
    /// kept revision's character ended (<paramref name="endedAt"/>), that character lives again: its revisions lose
    /// ended_at, a live character it replaces ends now (its revisions stay ResetHistoryDays like a reset's), and
    /// accounts.online_reset_at goes back to the reset before it (or null).
    /// </summary>
    Task<WriteOutcome> RestoreAsync(OnlineCharacterWrite write, int? expectedRevision, DateTime? endedAt, int historyKeep, CancellationToken ct = default);
    /// <summary>Moves the current character into the history and removes it; the next upload starts a new character.</summary>
    Task<bool> ResetAsync(long accountId, DateTime now, int historyKeep, CancellationToken ct = default);
    /// <summary>When the account's next online character could have started: its last reset, else the account's creation (null: no account).</summary>
    Task<DateTime?> GetStartAsync(long accountId, CancellationToken ct = default);
    /// <summary>The last record of the character the account's newest reset ended (null: none kept).</summary>
    Task<byte[]?> GetLastEndedRecordAsync(long accountId, CancellationToken ct = default);
}

/// <summary>
/// online_story and its history: the relay saves each new revision of a room's story; a reset and a staff restore write
/// the next revision whatever came before.
/// </summary>
public interface IOnlineStoryRepository
{
    Task<OnlineStoryRow?> GetAsync(long accountId, CancellationToken ct = default);
    /// <summary>
    /// Writes the row when it is new or its revision is higher than the stored one, and keeps it in the history (by
    /// <paramref name="history"/>); false when nothing changed (an older revision, or a deleted account).
    /// </summary>
    Task<bool> SaveAsync(OnlineStoryRow row, StoryHistoryPolicy history, CancellationToken ct = default);
    /// <summary>
    /// Writes <paramref name="row"/>'s story as the next revision whatever the stored one is (a reset, a staff restore)
    /// and keeps it in the history; returns that revision (0: nothing written, the account is deleted).
    /// </summary>
    Task<long> ReplaceAsync(OnlineStoryRow row, string reason, StoryHistoryPolicy history, CancellationToken ct = default);
    /// <summary>Keeps <paramref name="row"/> (an existing revision) in the history with <paramref name="reason"/> (a room
    /// session's start), without changing the story; not again when the history keeps that revision as a session start,
    /// reset or restore already.</summary>
    Task KeepAsync(OnlineStoryRow row, string reason, StoryHistoryPolicy history, CancellationToken ct = default);
    /// <summary>The kept revisions, newest first.</summary>
    Task<IReadOnlyList<OnlineStoryHistoryRow>> ListHistoryAsync(long accountId, CancellationToken ct = default);
}

public interface ICloudSaveRepository
{
    Task<CloudSaveMeta?> GetMetaAsync(long accountId, CancellationToken ct = default);
    Task<IReadOnlyList<CloudSaveMeta>> ListHistoryAsync(long accountId, CancellationToken ct = default);
    /// <summary>The latest data (<paramref name="revision"/> null) or a kept older revision.</summary>
    Task<CloudSaveData?> GetDataAsync(long accountId, int? revision, CancellationToken ct = default);
    Task<WriteOutcome> SaveAsync(CloudSaveWrite write, int? expectedRevision, int historyKeep, CancellationToken ct = default);
    /// <summary>Deletes the backup and its history.</summary>
    Task<bool> DeleteAsync(long accountId, CancellationToken ct = default);
}

public interface IAchievementRepository
{
    Task<IReadOnlyList<AchievementDefinition>> ListDefinitionsAsync(CancellationToken ct = default);
    Task<IReadOnlyList<AchievementUnlock>> ListUnlocksAsync(long accountId, CancellationToken ct = default);
    /// <summary>True when the unlock is new (repeated reports are harmless).</summary>
    Task<bool> TryUnlockAsync(UnlockWrite unlock, CancellationToken ct = default);
    /// <summary>Keeps the best (lowest) in-game time per board and ruleset; true when this one is the new best.</summary>
    Task<bool> SubmitTimeAsync(long accountId, string board, string ruleset, long igtMs, long? roomId, DateTime now, CancellationToken ct = default,
        long? instanceId = null);
    Task<IReadOnlyDictionary<string, long>> GetStatsAsync(long accountId, CancellationToken ct = default);
    /// <summary>The time of the account's last stats batch, else the account's creation (null: no such account).</summary>
    Task<DateTime?> GetLastStatsAtAsync(long accountId, CancellationToken ct = default);
    /// <summary>Adds the deltas and returns the new totals of the touched keys.</summary>
    Task<IReadOnlyDictionary<string, long>> AddStatsAsync(long accountId, IReadOnlyDictionary<string, long> deltas, DateTime now, CancellationToken ct = default);
    /// <summary>Accounts holding each achievement (unflagged unlocks of active accounts, any ruleset) and the number of players.</summary>
    Task<AchievementRarity> GetRarityAsync(CancellationToken ct = default);
    /// <summary>False when the account has no such unlock.</summary>
    Task<bool> SetUnlockFlaggedAsync(long accountId, int achievementId, string ruleset, bool flagged, CancellationToken ct = default);
    Task<bool> RevokeUnlockAsync(long accountId, int achievementId, string ruleset, CancellationToken ct = default);
}

