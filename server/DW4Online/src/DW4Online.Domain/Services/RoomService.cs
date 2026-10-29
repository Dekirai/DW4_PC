using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Rooms;
using DW4Online.Domain.Security;

namespace DW4Online.Domain.Services;

/// <summary>
/// The live room registry. A room has an owner (the host: the room's story is the owner's online save), a name, an
/// optional password and a limit of 2..10 players. Players who are in the same stage share a stage instance of at
/// most four players, whose host machine is the centre of the lockstep star; the relay splices the others to it.
/// When the owner leaves, the room keeps running with the story frozen until the owner returns; a room closes when its
/// last member leaves, when the owner closes it, or by staff. The durable record (rooms, room_members) is written
/// before the memory changes, so achievements and the website see the same membership.
/// </summary>
public sealed partial class RoomService
{
    private const string CodeAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no 0/O, 1/I
    private const int CodeLength = 6;
    /// <summary>Instance numbers stay within 9 digits (the relay's channel names); they wrap around after that.</summary>
    internal const int MaxInstanceId = 999_999_999;

    private readonly IRoomRepository _repository;
    private readonly IRelayGateway _relay;
    private readonly IAuditLog _audit;
    private readonly VersionGate _versions;
    private readonly RoomOptions _options;
    private readonly TimeProvider _time;
    private readonly PasswordHasher _passwords;
    private readonly HashSet<ulong> _rankedHashes;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly Dictionary<string, LiveRoom> _rooms = new(StringComparer.Ordinal);
    private readonly Dictionary<long, string> _memberOf = [];

    [GeneratedRegex("^[A-Za-z0-9_]{1,24}$", RegexOptions.CultureInvariant)]
    private static partial Regex StageName();

    public RoomService(IRoomRepository repository, IRelayGateway relay, IAuditLog audit, VersionGate versions, RoomOptions options, TimeProvider time)
    {
        _repository = repository;
        _relay = relay;
        _audit = audit;
        _versions = versions;
        _options = options;
        _time = time;
        _passwords = new PasswordHasher(Math.Max(1_000, options.PasswordIterations));
        _rankedHashes = options.RankedDataHashes.Select(ParseHash).ToHashSet();
        _relay.HostLost += channel => _ = OnHostLostAsync(channel);
        _relay.SetAdmission(AdmitAsync);
    }

    public int HeartbeatSeconds => _options.HeartbeatSeconds;

    // Queries ------------------------------------------------------------------------------------------------------

    /// <summary>Open rooms, newest first; with a protocol / data hash only the compatible ones (HELLO rejects others).</summary>
    public async Task<IReadOnlyList<RoomSummary>> ListAsync(int? protocol = null, ulong? dataHash = null, CancellationToken ct = default)
    {
        await _gate.WaitAsync(ct);
        try
        {
            return _rooms.Values
                .Where(r => (protocol is null || r.Protocol == protocol) && (dataHash is null || r.DataHash == dataHash))
                .OrderByDescending(r => r.CreatedAt).ThenBy(r => r.Code, StringComparer.Ordinal)
                .Select(Summary).ToList();
        }
        finally
        {
            _gate.Release();
        }
    }

    /// <summary>Open rooms and players in them (the website's home page).</summary>
    public async Task<(int Rooms, int Players)> CountsAsync(CancellationToken ct = default)
    {
        await _gate.WaitAsync(ct);
        try
        {
            return (_rooms.Count, _memberOf.Count);
        }
        finally
        {
            _gate.Release();
        }
    }

    /// <summary>The public summary of a room, and the full state when the caller is a member.</summary>
    public async Task<(RoomSummary? Summary, RoomState? State)> GetAsync(string code, long? accountId, CancellationToken ct = default)
    {
        await _gate.WaitAsync(ct);
        try
        {
            if (!_rooms.TryGetValue(NormalizeCode(code), out var room)) return (null, null);
            var state = accountId is { } id && room.Members.TryGetValue(id, out var member) ? State(room, member) : null;
            return (Summary(room), state);
        }
        finally
        {
            _gate.Release();
        }
    }

    // Membership ---------------------------------------------------------------------------------------------------

    public async Task<Result<RoomState>> CreateAsync(long accountId, string username, RoomCreate request, bool maintenance, CancellationToken ct = default)
    {
        if (maintenance) return new DomainError(ErrorKind.Unavailable, "maintenance", "The server is in maintenance: no new rooms open.");
        if (_versions.Check(request.Protocol, request.ClientVersion) is { } outdated) return outdated;
        var name = request.Name?.Trim() ?? "";
        if (name.Length is 0 or > RoomLimits.NameMax || name.Any(char.IsControl))
            return DomainError.Invalid("invalid_room_name", $"Room names have 1 to {RoomLimits.NameMax} characters.");
        var password = string.IsNullOrEmpty(request.Password) ? null : request.Password;
        if (password is not null && password.Length is < RoomLimits.PasswordMin or > RoomLimits.PasswordMax)
            return DomainError.Invalid("invalid_room_password", $"Room passwords have {RoomLimits.PasswordMin} to {RoomLimits.PasswordMax} characters.");
        if (request.MaxPlayers is < RoomLimits.MinPlayers or > RoomLimits.MaxPlayers)
            return DomainError.Invalid("invalid_max_players", $"A room holds {RoomLimits.MinPlayers} to {RoomLimits.MaxPlayers} players.");
        if (request.Protocol <= 0) return DomainError.Invalid("invalid_protocol", "The netplay protocol is missing.");
        var settings = ParseSettings(request.Settings, _options.SettingsMaxBytes);
        if (!settings.Ok) return settings.Error!;
        var (settingsJson, randomizer, difficulty) = settings.Value;
        var hash = password is null ? null : _passwords.Hash(password); // expensive: outside the gate
        var version = Truncate(request.ClientVersion?.Trim() ?? "", 32);

        await _gate.WaitAsync(ct);
        try
        {
            if (_rooms.Values.FirstOrDefault(r => r.OwnerId == accountId) is { } owned && CountOwned(accountId) >= _options.RoomsPerOwner)
                return DomainError.Conflict("room_owned", "You already own an open room. Rejoin or close it first.",
                    new Dictionary<string, object?> { ["roomCode"] = owned.Code });
            if (_rooms.Count >= _options.MaxRooms)
                return new DomainError(ErrorKind.Unavailable, "rooms_full", "The server has no room for more rooms. Try again later.");
            var now = Clock.Now(_time);
            if (_memberOf.TryGetValue(accountId, out var previous) && _rooms.TryGetValue(previous, out var old))
                await LeaveLockedAsync(old, accountId, LeaveReasons.Left, now, ct);

            var ranked = _rankedHashes.Count == 0 || _rankedHashes.Contains(request.DataHash);
            string? code = null;
            long id = 0;
            for (var attempt = 0; attempt < 10 && code is null; ++attempt)
            {
                var candidate = NewCode();
                if (_rooms.ContainsKey(candidate)) continue;
                try
                {
                    id = await _repository.CreateAsync(new NewRoom(candidate, name, accountId, hash is not null, request.MaxPlayers,
                        request.Protocol, request.DataHash, version, settingsJson, ranked, randomizer, now), ct);
                    code = candidate;
                }
                catch (DuplicateRoomCodeException)
                {
                }
            }
            if (code is null) return new DomainError(ErrorKind.Unavailable, "rooms_full", "No free room code was found. Try again.");

            var room = new LiveRoom
            {
                Id = id,
                Code = code,
                Name = name,
                OwnerId = accountId,
                OwnerName = username,
                PasswordHash = hash,
                MaxPlayers = request.MaxPlayers,
                Protocol = request.Protocol,
                DataHash = request.DataHash,
                ClientVersion = version,
                Settings = settingsJson is null ? null : JsonDocument.Parse(settingsJson).RootElement.Clone(),
                Ranked = ranked,
                Randomizer = randomizer,
                Difficulty = difficulty,
                CreatedAt = now,
            };
            var member = new LiveMember(accountId, username, 0, now) { LastSeen = now };
            room.Members[accountId] = member;
            _rooms[code] = room;
            _memberOf[accountId] = code;
            return State(room, member);
        }
        finally
        {
            _gate.Release();
        }
    }

    /// <summary>
    /// Joins a room (repeating it is harmless and counts as a heartbeat). The owner always gets back in (their place
    /// is kept) and unfreezes the story; guests need the password and a free place. A member of another room leaves it.
    /// </summary>
    public async Task<Result<RoomState>> JoinAsync(long accountId, string username, string code, string? password, int protocol, ulong dataHash,
        string? clientVersion, CancellationToken ct = default)
    {
        code = NormalizeCode(code);
        if (_versions.Check(protocol, clientVersion) is { } outdated) return outdated;
        string? hash;
        await _gate.WaitAsync(ct);
        try
        {
            if (!_rooms.TryGetValue(code, out var room)) return RoomNotFound();
            if (room.Protocol != protocol || room.DataHash != dataHash)
                return DomainError.Conflict("incompatible_room", "This room runs a different game version or game data.",
                    new Dictionary<string, object?> { ["protocol"] = room.Protocol, ["dataHash"] = room.DataHash });
            if (room.Members.TryGetValue(accountId, out var already))
            {
                already.LastSeen = Clock.Now(_time);
                return State(room, already);
            }
            if (room.Kicked.Contains(accountId)) return DomainError.Forbidden("kicked", "You were removed from this room.");
            hash = room.OwnerId == accountId ? null : room.PasswordHash;
        }
        finally
        {
            _gate.Release();
        }
        if (hash is not null && _passwords.Verify(hash, password ?? "") == PasswordCheck.Failed)
            return DomainError.Forbidden("wrong_room_password", "The room password is wrong.");

        await _gate.WaitAsync(ct);
        try
        {
            if (!_rooms.TryGetValue(code, out var room)) return RoomNotFound();
            var now = Clock.Now(_time);
            if (room.Members.TryGetValue(accountId, out var already))
            {
                already.LastSeen = now;
                return State(room, already);
            }
            var owner = room.OwnerId == accountId;
            if (!owner && room.Members.Keys.Count(k => k != room.OwnerId) >= room.MaxPlayers - 1)
                return DomainError.Conflict("room_full", "The room is full.");
            if (_memberOf.TryGetValue(accountId, out var previous) && _rooms.TryGetValue(previous, out var old))
                await LeaveLockedAsync(old, accountId, LeaveReasons.Left, now, ct);
            if (!_rooms.ContainsKey(code)) return RoomNotFound(); // cannot happen (leaving another room), kept for safety
            var slot = owner ? 0 : Enumerable.Range(1, RoomLimits.MaxPlayers - 1).First(s => room.Members.Values.All(m => m.Slot != s));
            await _repository.AddMemberAsync(room.Id, accountId, slot, now, ct);
            if (owner && room.StoryFrozenAt is not null)
            {
                await _repository.SetStoryFrozenAsync(room.Id, null, ct);
                room.StoryFrozenAt = null;
            }
            var member = new LiveMember(accountId, username, slot, now) { LastSeen = now };
            room.Members[accountId] = member;
            _memberOf[accountId] = code;
            return State(room, member);
        }
        finally
        {
            _gate.Release();
        }
    }

    /// <summary>Keeps the membership alive (every <see cref="HeartbeatSeconds"/>); answers with the room state.</summary>
    public async Task<Result<RoomState>> HeartbeatAsync(long accountId, string code, CancellationToken ct = default)
    {
        await _gate.WaitAsync(ct);
        try
        {
            if (!TryMember(code, accountId, out var room, out var member)) return NotInRoom();
            member.LastSeen = Clock.Now(_time);
            return State(room, member);
        }
        finally
        {
            _gate.Release();
        }
    }

    public async Task<Result<bool>> LeaveAsync(long accountId, string code, CancellationToken ct = default)
    {
        await _gate.WaitAsync(ct);
        try
        {
            if (!TryMember(code, accountId, out var room, out _)) return NotInRoom();
            await LeaveLockedAsync(room, accountId, LeaveReasons.Left, Clock.Now(_time), ct);
            return true;
        }
        finally
        {
            _gate.Release();
        }
    }

    /// <summary>The owner (while present) removes a guest; the guest cannot rejoin this room.</summary>
    public async Task<Result<bool>> KickAsync(long actorId, string code, string? username, byte[]? ip, CancellationToken ct = default)
    {
        await _gate.WaitAsync(ct);
        try
        {
            if (!TryMember(code, actorId, out var room, out _)) return NotInRoom();
            if (room.OwnerId != actorId) return DomainError.Forbidden("not_room_owner", "Only the room's owner can do this.");
            var target = room.Members.Values.FirstOrDefault(m => string.Equals(m.Username, username, StringComparison.OrdinalIgnoreCase));
            if (target is null) return DomainError.NotFound("member_not_found", "No such player in the room.");
            if (target.AccountId == actorId) return DomainError.Invalid("cannot_kick_owner", "The owner cannot be kicked.");
            var now = Clock.Now(_time);
            room.Kicked.Add(target.AccountId);
            await LeaveLockedAsync(room, target.AccountId, LeaveReasons.Kicked, now, ct);
            await _audit.WriteAsync("room_kick", target.AccountId, actorId, ip, new { room = room.Code }, now, ct);
            return true;
        }
        finally
        {
            _gate.Release();
        }
    }

    /// <summary>Closes a room: by its owner (present or not) or by staff.</summary>
    public async Task<Result<bool>> CloseAsync(long actorId, bool staff, string code, byte[]? ip, CancellationToken ct = default)
    {
        await _gate.WaitAsync(ct);
        try
        {
            if (!_rooms.TryGetValue(NormalizeCode(code), out var room)) return RoomNotFound();
            var owner = room.OwnerId == actorId;
            if (!owner && !staff) return DomainError.Forbidden("not_room_owner", "Only the room's owner can do this.");
            var now = Clock.Now(_time);
            await _repository.CloseAsync(room.Id, owner ? CloseReasons.HostClosed : CloseReasons.Admin, now, ct);
            RemoveRoomLocked(room);
            if (!owner) await _audit.WriteAsync("room_close_admin", room.OwnerId, actorId, ip, new { room = room.Code }, now, ct);
            return true;
        }
        finally
        {
            _gate.Release();
        }
    }

    /// <summary>Takes an account out of its room (staff locked or banned it, or it was deleted).</summary>
    public async Task RemoveAccountAsync(long accountId, CancellationToken ct = default)
    {
        await _gate.WaitAsync(ct);
        try
        {
            if (_memberOf.TryGetValue(accountId, out var code) && _rooms.TryGetValue(code, out var room))
                await LeaveLockedAsync(room, accountId, LeaveReasons.Kicked, Clock.Now(_time), ct);
        }
        finally
        {
            _gate.Release();
        }
    }

    // Stage instances ----------------------------------------------------------------------------------------------

    /// <summary>
    /// The caller enters a stage: it joins an instance of that stage with a free place (the preferred one when given)
    /// or opens a new instance and hosts it. Entering again as a guest of the same stage reconnects with a fresh
    /// ticket; an instance host who enters again opens a new instance (the old one ends).
    /// </summary>
    public async Task<Result<StageAssignment>> EnterStageAsync(long accountId, string code, string? stage, int? preferredInstance, CancellationToken ct = default)
    {
        if (stage is null || !StageName().IsMatch(stage)) return DomainError.Invalid("invalid_stage", "Stage names are 1-24 of A-Z, a-z, 0-9 and '_'.");
        if (!_relay.Available) return new DomainError(ErrorKind.Unavailable, "relay_unavailable", "The relay is switched off on this server.");
        await _gate.WaitAsync(ct);
        try
        {
            if (!TryMember(code, accountId, out var room, out var member)) return NotInRoom();
            var now = Clock.Now(_time);
            member.LastSeen = now;
            if (!room.Playing)
            {
                await _repository.MarkPlayingAsync(room.Id, now, ct);
                room.Playing = true;
            }
            var current = member.Instance is { } id ? room.Instances.GetValueOrDefault(id) : null;
            if (current is not null && current.Stage == stage && current.HostId != accountId)
            {
                // Reconnecting as a guest: a fresh ticket first (it replaces any older one), then the old connection goes.
                if (Ticket(room, current.Id, StageRoles.Guest, accountId) is not { } again) return TicketFailed();
                current.Tickets[accountId] = again.Nonce;
                _relay.DropGuest(Channel(room, current), accountId);
                return new StageAssignment(InstanceView(room, current), StageRoles.Guest, again.Ticket, Channel(room, current.Id));
            }

            // The target: an instance of that stage with a free place (never the one the caller leaves now), else a
            // new one the caller hosts. The ticket is issued before anything changes, so a failure leaves the room
            // as it was.
            var capacity = Math.Clamp(_options.PlayersPerInstance, 1, RoomLimits.MaxPlayers);
            bool Open(LiveInstance i) => i != current && i.Stage == stage && i.Players.Count < capacity;
            var target = preferredInstance is { } wanted && room.Instances.TryGetValue(wanted, out var p) && Open(p)
                ? p
                : room.Instances.Values.FirstOrDefault(Open);
            var role = target is null ? StageRoles.Host : StageRoles.Guest;
            var instanceId = target?.Id ?? NewInstanceId(room);
            if (Ticket(room, instanceId, role, accountId) is not { } ticket) return TicketFailed();

            if (current is not null) LeaveInstanceLocked(room, member);
            if (target is null)
            {
                target = new LiveInstance(instanceId, stage, accountId, now);
                room.Instances[target.Id] = target;
                room.NextInstance = instanceId >= MaxInstanceId ? 1 : instanceId + 1;
            }
            target.Players.Add(accountId);
            target.Tickets[accountId] = ticket.Nonce; // the only ticket of this player the relay accepts for the instance
            member.Instance = target.Id;
            return new StageAssignment(InstanceView(room, target), role, ticket.Ticket, Channel(room, target.Id));
        }
        finally
        {
            _gate.Release();
        }
    }

    public async Task<Result<bool>> LeaveStageAsync(long accountId, string code, CancellationToken ct = default)
    {
        await _gate.WaitAsync(ct);
        try
        {
            if (!TryMember(code, accountId, out var room, out var member)) return NotInRoom();
            member.LastSeen = Clock.Now(_time);
            LeaveInstanceLocked(room, member);
            return true;
        }
        finally
        {
            _gate.Release();
        }
    }

    // Housekeeping -------------------------------------------------------------------------------------------------

    /// <summary>
    /// Members without a heartbeat for <see cref="RoomOptions.MemberTimeoutSeconds"/> leave (reason timeout), and relay
    /// channels that belong to no running stage instance are closed.
    /// </summary>
    public async Task<int> SweepAsync(CancellationToken ct = default)
    {
        await _gate.WaitAsync(ct);
        try
        {
            foreach (var channel in _relay.OpenChannels)
                if (FindInstance(channel) is null) _relay.CloseChannel(channel);

            var now = Clock.Now(_time);
            var limit = TimeSpan.FromSeconds(_options.MemberTimeoutSeconds);
            var removed = 0;
            foreach (var room in _rooms.Values.ToList())
            {
                foreach (var member in room.Members.Values.Where(m => now - m.LastSeen > limit).ToList())
                {
                    if (!_rooms.ContainsKey(room.Code)) break;
                    await LeaveLockedAsync(room, member.AccountId, LeaveReasons.Timeout, now, ct);
                    ++removed;
                }
            }
            return removed;
        }
        finally
        {
            _gate.Release();
        }
    }

    /// <summary>At startup: rooms a previous process left open are closed (reason server_restart).</summary>
    public Task<int> CloseLeftoversAsync(CancellationToken ct = default) => _repository.CloseAllOpenAsync(Clock.Now(_time), ct);

    private async Task OnHostLostAsync(string channel)
    {
        await _gate.WaitAsync();
        try
        {
            if (FindInstance(channel) is (var room, var instance)) EndInstanceLocked(room, instance);
        }
        finally
        {
            _gate.Release();
        }
    }

    /// <summary>
    /// The relay asks before it accepts a valid ticket: the instance still runs, the account is its host (host tickets)
    /// or one of its guests and still in the room, and the ticket is the newest one issued to that player for the
    /// instance. Older tickets (hoarded by repeated /stage calls), tickets of ended instances and tickets of players who
    /// left or were kicked are refused. A ticket is accepted once.
    /// </summary>
    internal async ValueTask<bool> AdmitAsync(RelayAdmission ticket)
    {
        await _gate.WaitAsync();
        try
        {
            if (FindInstance(ticket.Channel) is not (var room, var instance)) return false;
            var role = instance.HostId == ticket.AccountId;
            if (role != ticket.Host || !instance.Players.Contains(ticket.AccountId)) return false;
            if (!room.Members.TryGetValue(ticket.AccountId, out var member) || member.Instance != instance.Id) return false;
            if (!instance.Tickets.TryGetValue(ticket.AccountId, out var nonce) || !string.Equals(nonce, ticket.Nonce, StringComparison.Ordinal))
                return false;
            instance.Tickets.Remove(ticket.AccountId);
            return true;
        }
        finally
        {
            _gate.Release();
        }
    }

    /// <summary>The room and running instance of a relay channel ("CODE-n"), or null.</summary>
    private (LiveRoom Room, LiveInstance Instance)? FindInstance(string channel)
    {
        var dash = channel.LastIndexOf('-');
        if (dash <= 0 || !int.TryParse(channel.AsSpan(dash + 1), NumberStyles.None, CultureInfo.InvariantCulture, out var id)) return null;
        return _rooms.TryGetValue(channel[..dash], out var room) && room.Instances.TryGetValue(id, out var instance) ? (room, instance) : null;
    }

    // Internals (callers hold the gate) ----------------------------------------------------------------------------

    private async Task LeaveLockedAsync(LiveRoom room, long accountId, string reason, DateTime now, CancellationToken ct)
    {
        var member = room.Members[accountId];
        var last = room.Members.Count == 1;
        await _repository.RemoveMemberAsync(room.Id, accountId, reason, now, ct);
        if (last)
            await _repository.CloseAsync(room.Id, CloseReasons.Empty, now, ct);
        else if (accountId == room.OwnerId)
            await _repository.SetStoryFrozenAsync(room.Id, now, ct);

        LeaveInstanceLocked(room, member);
        room.Members.Remove(accountId);
        _memberOf.Remove(accountId);
        if (last) RemoveRoomLocked(room);
        else if (accountId == room.OwnerId) room.StoryFrozenAt = now;
    }

    private void LeaveInstanceLocked(LiveRoom room, LiveMember member)
    {
        if (member.Instance is not { } id) return;
        member.Instance = null;
        if (!room.Instances.TryGetValue(id, out var instance)) return;
        if (instance.HostId == member.AccountId)
        {
            EndInstanceLocked(room, instance);
            return;
        }
        instance.Players.Remove(member.AccountId);
        instance.Tickets.Remove(member.AccountId);
        _relay.DropGuest(Channel(room, instance), member.AccountId);
    }

    /// <summary>The instance host left: everyone in it leaves the instance (they enter the stage again).</summary>
    private void EndInstanceLocked(LiveRoom room, LiveInstance instance)
    {
        room.Instances.Remove(instance.Id);
        foreach (var player in instance.Players)
            if (room.Members.TryGetValue(player, out var m) && m.Instance == instance.Id) m.Instance = null;
        _relay.CloseChannel(Channel(room, instance));
    }

    private void RemoveRoomLocked(LiveRoom room)
    {
        foreach (var instance in room.Instances.Values.ToList()) EndInstanceLocked(room, instance);
        foreach (var id in room.Members.Keys) _memberOf.Remove(id);
        room.Members.Clear();
        _rooms.Remove(room.Code);
    }

    private bool TryMember(string code, long accountId, out LiveRoom room, out LiveMember member)
    {
        member = null!;
        return _rooms.TryGetValue(NormalizeCode(code), out room!) && room.Members.TryGetValue(accountId, out member!);
    }

    private int CountOwned(long accountId) => _rooms.Values.Count(r => r.OwnerId == accountId);

    /// <summary>A relay ticket for the instance's channel, or null when the relay refuses to issue one.</summary>
    private RelayTicket? Ticket(LiveRoom room, int instanceId, string role, long accountId)
    {
        try
        {
            return _relay.IssueTicket(Channel(room, instanceId), accountId, role == StageRoles.Host);
        }
        catch (ArgumentException)
        {
            return null;
        }
    }

    /// <summary>The next free instance number (1..<see cref="MaxInstanceId"/>, wrapping around past instances that still run).</summary>
    private static int NewInstanceId(LiveRoom room)
    {
        var id = Math.Clamp(room.NextInstance, 1, MaxInstanceId);
        while (room.Instances.ContainsKey(id)) id = id >= MaxInstanceId ? 1 : id + 1;
        return id;
    }

    private static DomainError TicketFailed() =>
        new(ErrorKind.Unavailable, "relay_ticket_failed", "The relay could not issue a ticket for this stage. Try again.");

    private static string Channel(LiveRoom room, LiveInstance instance) => Channel(room, instance.Id);

    private static string Channel(LiveRoom room, int instanceId) => room.Code + "-" + instanceId.ToString(CultureInfo.InvariantCulture);

    private RoomSummary Summary(LiveRoom r) => new(r.Code, r.Name, r.OwnerName, r.Members.ContainsKey(r.OwnerId), r.StoryFrozenAt is not null,
        r.StoryFrozenAt, r.PasswordHash is not null, r.Members.Count, r.MaxPlayers, r.Protocol, r.DataHash, r.ClientVersion, r.Ranked,
        r.Randomizer, r.Difficulty, r.Instances.Count, r.CreatedAt);

    private RoomState State(LiveRoom room, LiveMember you) => new(
        Summary(room),
        room.Settings,
        room.Members.Values.OrderBy(m => m.Slot).Select(m => MemberView(room, m)).ToList(),
        room.Instances.Values.Select(i => InstanceView(room, i)).ToList(),
        MemberView(room, you),
        _options.HeartbeatSeconds);

    private static RoomMemberView MemberView(LiveRoom room, LiveMember m) =>
        new(m.AccountId, m.Username, m.Slot, m.AccountId == room.OwnerId, m.Instance, m.JoinedAt);

    private StageInstanceView InstanceView(LiveRoom room, LiveInstance i) => new(i.Id, i.Stage, Channel(room, i), i.HostId,
        room.Members.TryGetValue(i.HostId, out var host) ? host.Username : "", i.Players.ToList(),
        Math.Clamp(_options.PlayersPerInstance, 1, RoomLimits.MaxPlayers), i.CreatedAt);

    private static string NewCode()
    {
        Span<char> code = stackalloc char[CodeLength];
        for (var i = 0; i < CodeLength; ++i) code[i] = CodeAlphabet[RandomNumberGenerator.GetInt32(CodeAlphabet.Length)];
        return new string(code);
    }

    public static string NormalizeCode(string code) => code.Trim().ToUpperInvariant();

    /// <summary>Validates the room settings object and reads the randomizer switch and the difficulty from it.</summary>
    internal static Result<(string? Json, bool Randomizer, int? Difficulty)> ParseSettings(JsonElement? settings, int maxBytes)
    {
        if (settings is not { } s || s.ValueKind is JsonValueKind.Null or JsonValueKind.Undefined) return (null, false, null);
        if (s.ValueKind != JsonValueKind.Object) return DomainError.Invalid("invalid_settings", "Room settings must be a JSON object.");
        var json = s.GetRawText();
        if (Encoding.UTF8.GetByteCount(json) > maxBytes)
            return DomainError.Invalid("invalid_settings", $"Room settings hold at most {maxBytes} bytes.");
        foreach (var forbidden in new[] { "cheats", "rebalanced" })
            if (s.TryGetProperty(forbidden, out var f) && f.ValueKind == JsonValueKind.True)
                return DomainError.Invalid("unranked_settings", "Online rooms allow no cheats and no rebalanced rates.");
        int? difficulty = null;
        if (s.TryGetProperty("difficulty", out var d))
        {
            if (d.ValueKind != JsonValueKind.Number || !d.TryGetInt32(out var value) || value is < 0 or > 2)
                return DomainError.Invalid("invalid_settings", "difficulty is 0 (Normal), 1 (Hard) or 2 (Very Hard).");
            difficulty = value;
        }
        var randomizer = s.TryGetProperty("randomizer", out var r) &&
            (r.ValueKind == JsonValueKind.True ||
             (r.ValueKind == JsonValueKind.Object && r.TryGetProperty("enabled", out var e) && e.ValueKind == JsonValueKind.True));
        return (json, randomizer, difficulty);
    }

    private static ulong ParseHash(string text)
    {
        text = text.Trim();
        var ok = text.StartsWith("0x", StringComparison.OrdinalIgnoreCase)
            ? ulong.TryParse(text.AsSpan(2), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var value)
            : ulong.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out value);
        return ok ? value : throw new InvalidOperationException($"Rooms:RankedDataHashes holds '{text}', which is not a number.");
    }

    private static DomainError RoomNotFound() => DomainError.NotFound("room_not_found", "No such open room.");

    private static DomainError NotInRoom() => DomainError.NotFound("not_in_room", "You are not in this room (join it again).");

    private static string Truncate(string s, int max) => s.Length <= max ? s : s[..max];

    private sealed class LiveRoom
    {
        public long Id { get; init; }
        public string Code { get; init; } = "";
        public string Name { get; init; } = "";
        public long OwnerId { get; init; }
        public string OwnerName { get; init; } = "";
        public string? PasswordHash { get; init; }
        public int MaxPlayers { get; init; }
        public int Protocol { get; init; }
        public ulong DataHash { get; init; }
        public string ClientVersion { get; init; } = "";
        public JsonElement? Settings { get; init; }
        public bool Ranked { get; init; }
        public bool Randomizer { get; init; }
        public int? Difficulty { get; init; }
        public DateTime CreatedAt { get; init; }
        public DateTime? StoryFrozenAt { get; set; }
        public bool Playing { get; set; }
        public int NextInstance { get; set; } = 1;
        public Dictionary<long, LiveMember> Members { get; } = [];
        public SortedDictionary<int, LiveInstance> Instances { get; } = [];
        public HashSet<long> Kicked { get; } = [];
    }

    private sealed class LiveMember(long accountId, string username, int slot, DateTime joinedAt)
    {
        public long AccountId { get; } = accountId;
        public string Username { get; } = username;
        public int Slot { get; } = slot;
        public DateTime JoinedAt { get; } = joinedAt;
        public DateTime LastSeen { get; set; }
        public int? Instance { get; set; }
    }

    private sealed class LiveInstance(int id, string stage, long hostId, DateTime createdAt)
    {
        public int Id { get; } = id;
        public string Stage { get; } = stage;
        public long HostId { get; } = hostId;
        public DateTime CreatedAt { get; } = createdAt;
        public List<long> Players { get; } = [];
        /// <summary>The nonce of each player's newest relay ticket that the relay has not accepted yet.</summary>
        public Dictionary<long, string> Tickets { get; } = [];
    }
}
