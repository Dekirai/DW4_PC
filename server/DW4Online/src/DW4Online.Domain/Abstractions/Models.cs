namespace DW4Online.Domain.Abstractions;

public static class Roles
{
    public const string Player = "player";
    public const string Moderator = "moderator";
    public const string Admin = "admin";
    public static readonly string[] All = [Player, Moderator, Admin];
}

public static class AccountStatus
{
    public const string Active = "active";
    public const string Locked = "locked";   // locked by staff until unlocked
    public const string Banned = "banned";
    public const string Deleted = "deleted";
    public static readonly string[] All = [Active, Locked, Banned, Deleted];
}

public static class SessionKind
{
    public const string Game = "game";
    public const string Web = "web";
}

public sealed class AccountRecord
{
    public long Id { get; init; }
    public string Username { get; init; } = "";
    public string? Email { get; init; }
    public string PasswordHash { get; init; } = "";
    public string Role { get; init; } = Roles.Player;
    public string Status { get; init; } = AccountStatus.Active;
    public int FailedLogins { get; init; }
    public DateTime? LockedUntil { get; init; }
    public bool MustChangePassword { get; init; }
    public bool ProfilePublic { get; init; }
    public byte[]? RecoveryCodeHash { get; init; }
    public DateTime CreatedAt { get; init; }
    public DateTime? LastLoginAt { get; init; }
}

public sealed record NewAccount(string Username, string? Email, string PasswordHash, byte[] RecoveryCodeHash, string Role);

/// <summary>A valid session found from a token (the account is active).</summary>
public sealed class AuthenticatedSession
{
    public long SessionId { get; init; }
    public long AccountId { get; init; }
    public string Username { get; init; } = "";
    public string Role { get; init; } = Roles.Player;
    public string Kind { get; init; } = SessionKind.Game;
    public DateTime? LastUsedAt { get; init; }
    public DateTime ExpiresAt { get; init; }
    public bool MustChangePassword { get; init; }
}

public sealed class SessionInfo
{
    public long Id { get; init; }
    public string Kind { get; init; } = "";
    public string? DeviceName { get; init; }
    public DateTime CreatedAt { get; init; }
    public DateTime? LastUsedAt { get; init; }
    public DateTime ExpiresAt { get; init; }
}

public sealed class OnlineCharacterRow
{
    public long AccountId { get; init; }
    public byte[] Record { get; init; } = [];
    public byte[] RecordSha256 { get; init; } = [];
    public int Revision { get; init; }
    public long PlaySeconds { get; init; }
    public bool Flagged { get; init; }
    public string? ClientVersion { get; init; }
    public DateTime CreatedAt { get; init; }
    public DateTime UpdatedAt { get; init; }
}

/// <summary>A new online character revision with the server-side summary columns.</summary>
public sealed record OnlineCharacterWrite(
    long AccountId,
    byte[] Record,
    byte[] RecordSha256,
    Save.CharacterSummary Summary,
    long PlaySeconds,
    bool Flagged,
    string? ClientVersion,
    string Reason,
    DateTime Now);

public enum WriteStatus
{
    Saved,
    Conflict,
    NotFound,
}

public sealed record WriteOutcome(WriteStatus Status, int Revision);

public sealed class HistoryEntry
{
    public int Revision { get; init; }
    public int Level { get; init; }
    public long PlaySeconds { get; init; }
    public string Reason { get; init; } = "";
    public DateTime SavedAt { get; init; }
}

public sealed class CloudSaveMeta
{
    public int Revision { get; init; }
    public string Format { get; init; } = "";
    public int SizeBytes { get; init; }
    public byte[] Sha256 { get; init; } = [];
    public string? SlotSummary { get; init; }
    public string? DeviceName { get; init; }
    public string? ClientVersion { get; init; }
    public DateTime UploadedAt { get; init; }
}

public sealed class CloudSaveData
{
    public int Revision { get; init; }
    public string Format { get; init; } = "";
    public byte[] Data { get; init; } = [];
    public byte[] Sha256 { get; init; } = [];
    public DateTime UploadedAt { get; init; }
}

public sealed record CloudSaveWrite(
    long AccountId,
    string Format,
    byte[] Data,
    byte[] Sha256,
    string SlotSummaryJson,
    string? DeviceName,
    string? ClientVersion,
    DateTime Now);

public sealed class AchievementDefinition
{
    public int Id { get; init; }
    public string Code { get; init; } = "";
    public string Name { get; init; } = "";
    public string Description { get; init; } = "";
    public string Category { get; init; } = "";
    public string Tier { get; init; } = "";
    public int Points { get; init; }
    public string Kind { get; init; } = "";
    public long? Goal { get; init; }
    public string? StatKey { get; init; }
    public bool Hidden { get; init; }
    public int SortOrder { get; init; }
    public bool Active { get; init; }
}

public sealed class AchievementUnlock
{
    public int AchievementId { get; init; }
    public string Ruleset { get; init; } = "";
    public DateTime UnlockedAt { get; init; }
    public int? Players { get; init; }
    public bool Flagged { get; init; }
}

public sealed record UnlockWrite(
    long AccountId,
    int AchievementId,
    string Ruleset,
    DateTime Now,
    long? RoomId,
    int? Players,
    string? ClientVersion,
    bool Flagged,
    string? EvidenceJson);

/// <summary>A room membership as the durable rooms / room_members tables record it.</summary>
public sealed class RoomMembership
{
    public long RoomId { get; init; }
    public string Code { get; init; } = "";
    public bool Ranked { get; init; }
    public bool IsHost { get; init; }
    public int Members { get; init; }
    public string Status { get; init; } = "";
}
