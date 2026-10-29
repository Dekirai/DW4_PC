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
/// optional password, a limit of 2..10 players and its settings (difficulty, camera and light mode, randomizer, fast
/// loading) with their hash. Every member holds one relay connection (DW4R2, SEAMLESS.md 6); the relay groups the
/// members into instances of at most four (Rooms:PlayersPerInstance) and this service lists them. Create, join and
/// /ticket hand out the member's one-time relay ticket; the relay asks this service before it accepts one, and a live
/// relay connection counts as the room heartbeat. When the owner leaves, the room keeps running with the story frozen
/// until the owner returns; a room closes when its last member leaves, when the owner closes it, or by staff. The
/// durable record (rooms, room_members) is written before the memory changes, so achievements and the website see the
/// same membership.
/// </summary>
public sealed partial class RoomService
{
    private const string CodeAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no 0/O, 1/I
    private const int CodeLength = 6;

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
        _relay.SetAdmission(AdmitAsync);
        _relay.SetHeartbeat(RelayHeartbeatAsync);
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
                .Select(r => Summary(r)).ToList();
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
        var parsed = RoomSettings.FromJson(settingsJson is null ? null : JsonDocument.Parse(settingsJson).RootElement);
        if (!parsed.Ok) return parsed.Error!;
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
                RoomSettings = parsed.Value!,
                SettingsHash = parsed.Value!.Hash(request.Protocol, request.DataHash),
                CreatedAt = now,
            };
            var member = new LiveMember(accountId, username, 0, now) { LastSeen = now };
            room.Members[accountId] = member;
            _rooms[code] = room;
            _memberOf[accountId] = code;
            return WithTicket(room, member);
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
                return WithTicket(room, already);
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
                return WithTicket(room, already);
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
            if (owner) _relay.OwnerPresence(room.Code, true);
            return WithTicket(room, member);
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

    // Relay ------------------------------------------------------------------------------------------------------------

    /// <summary>A fresh one-time relay ticket for a member (a reconnect: SEAMLESS.md 6.2, 6.10).</summary>
    public async Task<Result<RoomState>> TicketAsync(long accountId, string code, CancellationToken ct = default)
    {
        if (!_relay.Available) return new DomainError(ErrorKind.Unavailable, "relay_unavailable", "The relay is switched off on this server.");
        await _gate.WaitAsync(ct);
        try
        {
            if (!TryMember(code, accountId, out var room, out var member)) return NotInRoom();
            member.LastSeen = Clock.Now(_time);
            var state = WithTicket(room, member);
            return state.RelayTicket is null ? TicketFailed() : state;
        }
        finally
        {
            _gate.Release();
        }
    }

    /// <summary>
    /// The relay asks before it accepts a valid ticket: the room is open, the account is one of its members and the ticket
    /// is the newest one issued to it (older tickets, tickets of members who left or were kicked are refused). A ticket is
    /// accepted once. The answer carries the member's room slot and the room's protocol, data hash and settings hash.
    /// </summary>
    internal async ValueTask<RelayAdmitted?> AdmitAsync(RelayAdmission ticket)
    {
        // The gate is held during database writes: a hanging database refuses the ticket instead of holding the
        // handshake (and the address's relay connection) open; the client asks for a new ticket and tries again.
        if (!await _gate.WaitAsync(TimeSpan.FromMilliseconds(Math.Max(1, _options.AdmissionTimeoutMilliseconds)))) return null;
        try
        {
            if (!_rooms.TryGetValue(ticket.Room, out var room) || !room.Members.TryGetValue(ticket.AccountId, out var member)) return null;
            if (member.TicketNonce is not { } nonce || !string.Equals(nonce, ticket.Nonce, StringComparison.Ordinal)) return null;
            member.TicketNonce = null;
            var now = Clock.Now(_time);
            member.LastSeen = now;
            if (!room.Playing)
            {
                await _repository.MarkPlayingAsync(room.Id, now);
                room.Playing = true;
            }
            return new RelayAdmitted(member.Slot, member.Username, member.AccountId == room.OwnerId, room.Protocol, room.DataHash, room.SettingsHash,
                room.MaxPlayers);
        }
        finally
        {
            _gate.Release();
        }
    }

    /// <summary>Members with a live relay connection count as heard from (the REST heartbeat is needed only while it is down).</summary>
    internal async ValueTask RelayHeartbeatAsync(string code, IReadOnlyCollection<long> accounts)
    {
        await _gate.WaitAsync();
        try
        {
            if (!_rooms.TryGetValue(code, out var room)) return;
            var now = Clock.Now(_time);
            foreach (var id in accounts)
                if (room.Members.TryGetValue(id, out var m)) m.LastSeen = now;
        }
        finally
        {
            _gate.Release();
        }
    }

    // Housekeeping -------------------------------------------------------------------------------------------------

    /// <summary>
    /// Members without a heartbeat (REST or a live relay connection) for <see cref="RoomOptions.MemberTimeoutSeconds"/>
    /// leave (reason timeout).
    /// </summary>
    public async Task<int> SweepAsync(CancellationToken ct = default)
    {
        await _gate.WaitAsync(ct);
        try
        {
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

        room.Members.Remove(accountId);
        _memberOf.Remove(accountId);
        _relay.MemberLeft(room.Code, accountId, reason);
        _ = member;
        if (last) RemoveRoomLocked(room);
        else if (accountId == room.OwnerId)
        {
            room.StoryFrozenAt = now;
            _relay.OwnerPresence(room.Code, false);
        }
    }

    private void RemoveRoomLocked(LiveRoom room)
    {
        _relay.RoomClosed(room.Code);
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

    /// <summary>The member's state with a fresh one-time relay ticket (it replaces any older one), when the relay runs.</summary>
    private RoomState WithTicket(LiveRoom room, LiveMember member)
    {
        string? ticket = null;
        if (_relay.Available)
        {
            try
            {
                var issued = _relay.IssueTicket(room.Code, member.AccountId);
                member.TicketNonce = issued.Nonce;
                ticket = issued.Ticket;
            }
            catch (ArgumentException)
            {
            }
        }
        return State(room, member) with { RelayTicket = ticket };
    }

    private static DomainError TicketFailed() =>
        new(ErrorKind.Unavailable, "relay_ticket_failed", "The relay could not issue a ticket for this room. Try again.");

    private RoomSummary Summary(LiveRoom r) => Summary(r, _relay.Instances(r.Code).Count);

    private static RoomSummary Summary(LiveRoom r, int instances) => new(r.Code, r.Name, r.OwnerName, r.Members.ContainsKey(r.OwnerId),
        r.StoryFrozenAt is not null, r.StoryFrozenAt, r.PasswordHash is not null, r.Members.Count, r.MaxPlayers, r.Protocol, r.DataHash,
        r.ClientVersion, r.Ranked, r.Randomizer, r.Difficulty, instances, r.CreatedAt);

    private RoomState State(LiveRoom room, LiveMember you)
    {
        var instances = _relay.Instances(room.Code);
        var where = new Dictionary<long, uint>();
        foreach (var i in instances)
            foreach (var m in i.Members) where[m.AccountId] = i.Id;
        RoomMemberView View(LiveMember m) => new(m.AccountId, m.Username, m.Slot, m.AccountId == room.OwnerId,
            where.TryGetValue(m.AccountId, out var id) ? id : null, m.JoinedAt);
        return new RoomState(Summary(room, instances.Count), room.Settings, room.Members.Values.OrderBy(m => m.Slot).Select(View).ToList(),
            instances, View(you), _options.HeartbeatSeconds)
        {
            SettingsHash = room.SettingsHash.ToString("x16", CultureInfo.InvariantCulture),
        };
    }

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
        var parsed = RoomSettings.FromJson(s);
        if (!parsed.Ok) return parsed.Error!;
        int? difficulty = s.TryGetProperty("difficulty", out var d) && d.ValueKind == JsonValueKind.Number ? parsed.Value!.Difficulty : null;
        return (json, RoomSettings.RandomizerRequested(s), difficulty);
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
        public RoomSettings RoomSettings { get; init; } = RoomSettings.Default;
        public ulong SettingsHash { get; init; }
        public Dictionary<long, LiveMember> Members { get; } = [];
        public HashSet<long> Kicked { get; } = [];
    }

    private sealed class LiveMember(long accountId, string username, int slot, DateTime joinedAt)
    {
        public long AccountId { get; } = accountId;
        public string Username { get; } = username;
        public int Slot { get; } = slot;
        public DateTime JoinedAt { get; } = joinedAt;
        public DateTime LastSeen { get; set; }
        /// <summary>The nonce of the member's newest relay ticket that the relay has not accepted yet.</summary>
        public string? TicketNonce { get; set; }
    }
}
