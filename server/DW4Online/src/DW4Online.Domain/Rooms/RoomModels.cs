using System.Text.Json;

namespace DW4Online.Domain.Rooms;

/// <summary>The room service (section "Rooms").</summary>
public sealed class RoomOptions
{
    /// <summary>Open rooms on the whole server.</summary>
    public int MaxRooms { get; set; } = 200;
    /// <summary>Players in one stage instance (the engine has four player slots); the relay groups at most this many.</summary>
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

/// <summary>A room member; <paramref name="Instance"/> = the relay instance it plays in (null while not connected to the relay).</summary>
public sealed record RoomMemberView(long AccountId, string Username, int Slot, bool Owner, uint? Instance, DateTime JoinedAt);

/// <summary>A member of an instance group: its account, engine slot, and whether its relay connection is up or lagging.</summary>
public sealed record InstanceGroupMember(long AccountId, string Username, int Slot, bool Connected, bool Lagging);

/// <summary>
/// An instance as the relay runs it (SEAMLESS.md 3.2): a lockstep group of 1-4 members in one stage, mode "hub" (hub and UI
/// stages: solo, not logged), "solo" (one member leads on its own clock) or "sealed" (the server seals every tick);
/// departing = a forked group walking to its exit; ticks = the visit's current tick.
/// </summary>
public sealed record InstanceGroupView(uint Id, string Stage, uint Visit, string Mode, IReadOnlyList<InstanceGroupMember> Members, bool Departing,
    bool BossLock, bool Joinable, uint Ticks, uint? Parent);

/// <summary>
/// What a member sees: the room, its settings and their hash (DW4R2 handshake, 16 hex digits), the members, the instance
/// groups and the member itself; after create, join and /ticket also a one-time relay ticket for this member.
/// </summary>
public sealed record RoomState(
    RoomSummary Room,
    JsonElement? Settings,
    IReadOnlyList<RoomMemberView> Members,
    IReadOnlyList<InstanceGroupView> Instances,
    RoomMemberView You,
    int HeartbeatSeconds)
{
    public string SettingsHash { get; init; } = "";
    public string? RelayTicket { get; init; }
}

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

/// <summary>A one-time relay ticket and its nonce (the room service accepts only the newest ticket per member).</summary>
public sealed record RelayTicket(string Ticket, string Nonce);

/// <summary>A ticket the relay is about to accept: the room, the account and the ticket's nonce.</summary>
public sealed record RelayAdmission(string Room, long AccountId, string Nonce);

/// <summary>The room service's answer: the member's room slot (0-9), name, owner flag and the room's compatibility data.</summary>
public sealed record RelayAdmitted(int MemberIndex, string Username, bool Owner, int Protocol, ulong DataHash, ulong SettingsHash, int MaxPlayers);

/// <summary>The DW4R2 relay as the room service uses it (SEAMLESS.md 6.2, 14).</summary>
public interface IRelayGateway
{
    /// <summary>False when the relay is switched off (Relay:Enabled): rooms work, nobody plays together.</summary>
    bool Available { get; }
    /// <summary>The port the relay listens on once it started (null before; tests bind port 0).</summary>
    int? BoundPort { get; }
    /// <summary>A one-time room ticket (60 s) for the member's relay connection.</summary>
    RelayTicket IssueTicket(string roomCode, long accountId);
    /// <summary>The member left the room (leave, kick, timeout, staff): its relay connection closes, its slot leaves at once.</summary>
    void MemberLeft(string roomCode, long accountId, string reason);
    /// <summary>The room closed: every connection of it closes.</summary>
    void RoomClosed(string roomCode);
    /// <summary>The owner came back or went away (the members' ROOM notice).</summary>
    void OwnerPresence(string roomCode, bool present);
    /// <summary>The room's instance groups as the relay runs them.</summary>
    IReadOnlyList<InstanceGroupView> Instances(string roomCode);
    /// <summary>The check the relay makes after a ticket's signature, expiry and single use checked out.</summary>
    void SetAdmission(Func<RelayAdmission, ValueTask<RelayAdmitted?>> check);
    /// <summary>The relay connection is the room heartbeat: the accounts connected per room, every few seconds.</summary>
    void SetHeartbeat(Func<string, IReadOnlyCollection<long>, ValueTask> beat);
}
