using System.Text.Json;

namespace DW4Online.Domain.Rooms;

/// <summary>The room service (section "Rooms").</summary>
public sealed class RoomOptions
{
    /// <summary>Open rooms on the whole server.</summary>
    public int MaxRooms { get; set; } = 200;
    /// <summary>Players in one stage instance (the engine has four player slots; netplay.cpp clamps to 1..4).</summary>
    public int PlayersPerInstance { get; set; } = 4;
    /// <summary>How often clients send a heartbeat (announced in every room state).</summary>
    public int HeartbeatSeconds { get; set; } = 10;
    /// <summary>A member without a heartbeat for this long leaves the room (reason "timeout").</summary>
    public int MemberTimeoutSeconds { get; set; } = 35;
    /// <summary>Open rooms one account may own at a time (a room keeps running while its owner is away).</summary>
    public int RoomsPerOwner { get; set; } = 1;
    /// <summary>PBKDF2 iterations for room passwords (kept in memory only, never stored).</summary>
    public int PasswordIterations { get; set; } = 20_000;
    /// <summary>Largest JSON settings object of a room (difficulty, randomizer seed and options).</summary>
    public int SettingsMaxBytes { get; set; } = 4096;
    /// <summary>
    /// Game data hashes (decimal or 0x-hex) that count as unmodified, so rooms with them are ranked. Empty: every data
    /// hash is ranked (until the release ships its official hash).
    /// </summary>
    public List<string> RankedDataHashes { get; set; } = [];
}

public static class RoomLimits
{
    public const int MinPlayers = 2;
    public const int MaxPlayers = 10;
    public const int NameMax = 32;
    public const int PasswordMin = 4;
    public const int PasswordMax = 64;
}

public static class CloseReasons
{
    public const string Empty = "empty";
    public const string HostClosed = "host_closed";
    public const string Admin = "admin";
    public const string ServerRestart = "server_restart";
}

public static class LeaveReasons
{
    public const string Left = "left";
    public const string Kicked = "kicked";
    public const string Timeout = "timeout";
    public const string RoomClosed = "room_closed";
}

public static class StageRoles
{
    public const string Host = "host";
    public const string Guest = "guest";
}

/// <summary>A room as the public room list and the website show it.</summary>
public sealed record RoomSummary(
    string Code,
    string Name,
    string Owner,
    bool OwnerPresent,
    bool StoryFrozen,
    DateTime? StoryFrozenSince,
    bool HasPassword,
    int Players,
    int MaxPlayers,
    int Protocol,
    ulong DataHash,
    string ClientVersion,
    bool Ranked,
    bool Randomizer,
    int? Difficulty,
    int Instances,
    DateTime CreatedAt);

public sealed record RoomMemberView(long AccountId, string Username, int Slot, bool Owner, int? Instance, DateTime JoinedAt);

/// <summary>
/// A stage instance: the players of one room who are in the same stage share enemies and drops (at most four). The
/// instance host's machine is the centre of the lockstep star; the relay channel is "&lt;room&gt;-&lt;instance&gt;".
/// </summary>
public sealed record StageInstanceView(int Id, string Stage, string Channel, long HostAccountId, string HostName,
    IReadOnlyList<long> Players, int Capacity, DateTime CreatedAt);

/// <summary>What a member sees: the room, its settings, members and stage instances, and the member itself.</summary>
public sealed record RoomState(
    RoomSummary Room,
    JsonElement? Settings,
    IReadOnlyList<RoomMemberView> Members,
    IReadOnlyList<StageInstanceView> Instances,
    RoomMemberView You,
    int HeartbeatSeconds);

/// <summary>The answer to "I enter this stage": the instance, the caller's role in it and a one-time relay ticket.</summary>
public sealed record StageAssignment(StageInstanceView Instance, string Role, string Ticket, string Channel);

/// <summary>The parameters of a new room.</summary>
public sealed record RoomCreate(string? Name, string? Password, int MaxPlayers, int Protocol, ulong DataHash, string? ClientVersion, JsonElement? Settings);

/// <summary>The durable row the room service writes when a room opens (rooms + the owner's room_members row).</summary>
public sealed record NewRoom(
    string Code,
    string Name,
    long HostAccountId,
    bool HasPassword,
    int MaxPlayers,
    int Protocol,
    ulong DataHash,
    string ClientVersion,
    string? Settings,
    bool Ranked,
    bool Randomizer,
    DateTime Now);

/// <summary>Thrown by <see cref="IRoomRepository.CreateAsync"/> when an open room already uses the code.</summary>
public sealed class DuplicateRoomCodeException() : Exception("room code taken");

/// <summary>The durable room record (rooms, room_members). The live state lives in <see cref="Services.RoomService"/>.</summary>
public interface IRoomRepository
{
    /// <summary>Inserts the room and the owner's membership (slot 0); returns the room id.</summary>
    Task<long> CreateAsync(NewRoom room, CancellationToken ct = default);
    Task AddMemberAsync(long roomId, long accountId, int slot, DateTime now, CancellationToken ct = default);
    Task RemoveMemberAsync(long roomId, long accountId, string reason, DateTime now, CancellationToken ct = default);
    Task SetStoryFrozenAsync(long roomId, DateTime? since, CancellationToken ct = default);
    Task MarkPlayingAsync(long roomId, DateTime now, CancellationToken ct = default);
    /// <summary>Closes the room; every member still inside leaves with reason room_closed.</summary>
    Task CloseAsync(long roomId, string reason, DateTime now, CancellationToken ct = default);
    /// <summary>Closes rooms left open by a previous process (reason server_restart); returns how many.</summary>
    Task<int> CloseAllOpenAsync(DateTime now, CancellationToken ct = default);
}

/// <summary>A one-time relay ticket and its nonce (the room service accepts only the newest ticket per player).</summary>
public sealed record RelayTicket(string Ticket, string Nonce);

/// <summary>A ticket the relay is about to accept: whose, for which channel and role, and its nonce.</summary>
public sealed record RelayAdmission(string Channel, long AccountId, bool Host, string Nonce);

/// <summary>The relay as the room service uses it: tickets, closing a channel, dropping one guest.</summary>
public interface IRelayGateway
{
    /// <summary>False when the relay is switched off (Relay:Enabled); stage instances then cannot start.</summary>
    bool Available { get; }
    /// <summary>The port the relay listens on once it started (null before; tests bind port 0).</summary>
    int? BoundPort { get; }
    /// <summary>A one-time ticket for <paramref name="channel"/> ("ABC234-1").</summary>
    RelayTicket IssueTicket(string channel, long accountId, bool host);
    /// <summary>Drops the channel's host control socket and every spliced guest.</summary>
    void CloseChannel(string channel);
    /// <summary>Drops the spliced connections of one guest of the channel.</summary>
    void DropGuest(string channel, long accountId);
    /// <summary>The channels whose host is connected.</summary>
    IReadOnlyCollection<string> OpenChannels { get; }
    /// <summary>
    /// The check the relay makes before it accepts a valid ticket (signature, expiry and single use are its own): the
    /// room service answers whether the ticket still belongs to a running instance and is the player's newest.
    /// </summary>
    void SetAdmission(Func<RelayAdmission, ValueTask<bool>> check);
    /// <summary>Raised when a channel's host control socket ends (the instance host left or lost the connection).</summary>
    event Action<string>? HostLost;
}
