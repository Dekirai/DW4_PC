using System.Text.Json;
using DW4Online.Domain;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Rooms;
using DW4Online.Domain.Services;
using DW4Online.Relay;

namespace DW4Online.Tests;

/// <summary>The live room registry with fake persistence and a fake relay (no database, no sockets).</summary>
public class RoomServiceTests
{
    private sealed class Clock : TimeProvider
    {
        public DateTimeOffset Now { get; set; } = new(2026, 11, 1, 12, 0, 0, TimeSpan.Zero);
        public override DateTimeOffset GetUtcNow() => Now;
    }

    private sealed class FakeRepository : IRoomRepository
    {
        private long _next = 100;
        public List<string> Calls { get; } = [];
        public int DuplicatesToThrow { get; set; }

        public Task<long> CreateAsync(NewRoom room, CancellationToken ct = default)
        {
            if (DuplicatesToThrow-- > 0) throw new DuplicateRoomCodeException();
            Calls.Add($"create {room.Code} ranked={room.Ranked} randomizer={room.Randomizer}");
            return Task.FromResult(++_next);
        }

        /// <summary>While set, adding a member waits for it (a database that hangs).</summary>
        public TaskCompletionSource? Hang { get; set; }

        public Task AddMemberAsync(long roomId, long accountId, int slot, DateTime now, CancellationToken ct = default)
        {
            Calls.Add($"add {roomId} {accountId} {slot}");
            return Hang?.Task ?? Task.CompletedTask;
        }

        public Task RemoveMemberAsync(long roomId, long accountId, string reason, DateTime now, CancellationToken ct = default)
        {
            Calls.Add($"remove {roomId} {accountId} {reason}");
            return Task.CompletedTask;
        }

        public Task SetStoryFrozenAsync(long roomId, DateTime? since, CancellationToken ct = default)
        {
            Calls.Add($"frozen {roomId} {(since is null ? "no" : "yes")}");
            return Task.CompletedTask;
        }

        public Task MarkPlayingAsync(long roomId, DateTime now, CancellationToken ct = default)
        {
            Calls.Add($"playing {roomId}");
            return Task.CompletedTask;
        }

        public Task CloseAsync(long roomId, string reason, DateTime now, CancellationToken ct = default)
        {
            Calls.Add($"close {roomId} {reason}");
            return Task.CompletedTask;
        }

        public Task<int> CloseAllOpenAsync(DateTime now, CancellationToken ct = default) => Task.FromResult(0);
    }

    private sealed class FakeRelay : IRelayGateway
    {
        public bool Available { get; set; } = true;
        public int? BoundPort => 52041;
        public bool FailTickets { get; set; }
        public RelayTickets? Real { get; set; }
        public List<RelayTicket> Issued { get; } = [];
        public List<string> Events { get; } = [];
        public Dictionary<string, List<InstanceGroupView>> Groups { get; } = [];
        public Func<RelayAdmission, ValueTask<RelayAdmitted?>>? Admission { get; private set; }
        public Func<string, IReadOnlyCollection<long>, ValueTask>? Beat { get; private set; }

        public RelayTicket IssueTicket(string roomCode, long accountId)
        {
            if (FailTickets) throw new ArgumentException("no ticket");
            var ticket = Real?.Create(roomCode, accountId, RelayRoles.Member) is { } real
                ? new RelayTicket(real.Ticket, real.Nonce)
                : new RelayTicket($"{roomCode}|{accountId}|member", "n" + (Issued.Count + 1));
            Issued.Add(ticket);
            return ticket;
        }

        public void MemberLeft(string roomCode, long accountId, string reason) => Events.Add($"left {roomCode} {accountId} {reason}");
        public void RoomClosed(string roomCode) => Events.Add($"closed {roomCode}");
        public void OwnerPresence(string roomCode, bool present) => Events.Add($"owner {roomCode} {(present ? "present" : "away")}");
        public IReadOnlyList<InstanceGroupView> Instances(string roomCode) => Groups.GetValueOrDefault(roomCode) ?? [];
        public void SetAdmission(Func<RelayAdmission, ValueTask<RelayAdmitted?>> check) => Admission = check;
        public void SetHeartbeat(Func<string, IReadOnlyCollection<long>, ValueTask> beat) => Beat = beat;

        public ValueTask<RelayAdmitted?> Admit(string room, long account, string nonce) => Admission!(new RelayAdmission(room, account, nonce));
    }

    private sealed class NoAudit : IAuditLog
    {
        public List<string> Events { get; } = [];

        public Task WriteAsync(string evt, long? accountId, long? actorId, byte[]? ip, object? detail, DateTime now, CancellationToken ct = default)
        {
            Events.Add(evt);
            return Task.CompletedTask;
        }
    }

    private readonly Clock _clock = new();
    private readonly FakeRepository _repo = new();
    private readonly FakeRelay _relay = new();
    private readonly NoAudit _audit = new();

    private RoomService Service(RoomOptions? options = null) =>
        new(_repo, _relay, _audit, new VersionGate(new PlayOptions()), options ?? new RoomOptions { PasswordIterations = 1000 }, _clock);

    private static RoomCreate Create(string name = "Night run", string? password = null, int max = 4, ulong hash = 77, string? settings = null) =>
        new(name, password, max, 41, hash, "0.48.0", settings is null ? null : JsonDocument.Parse(settings).RootElement.Clone());

    private static async Task<RoomState> Ok(Task<Result<RoomState>> call)
    {
        var result = await call;
        Assert.True(result.Ok, result.Error?.Code);
        return result.Value!;
    }

    [Fact]
    public async Task Create_validates_and_reads_the_settings()
    {
        var rooms = Service();
        Assert.Equal("invalid_room_name", (await rooms.CreateAsync(1, "Owner", Create(name: " "), false)).Error!.Code);
        Assert.Equal("invalid_room_password", (await rooms.CreateAsync(1, "Owner", Create(password: "abc"), false)).Error!.Code);
        Assert.Equal("invalid_max_players", (await rooms.CreateAsync(1, "Owner", Create(max: 11), false)).Error!.Code);
        Assert.Equal("invalid_max_players", (await rooms.CreateAsync(1, "Owner", Create(max: 1), false)).Error!.Code);
        Assert.Equal("unranked_settings", (await rooms.CreateAsync(1, "Owner", Create(settings: "{\"cheats\":true}"), false)).Error!.Code);
        Assert.Equal("invalid_settings", (await rooms.CreateAsync(1, "Owner", Create(settings: "{\"difficulty\":3}"), false)).Error!.Code);
        Assert.Equal("invalid_settings", (await rooms.CreateAsync(1, "Owner", Create(settings: "[1]"), false)).Error!.Code);
        Assert.Equal("maintenance", (await rooms.CreateAsync(1, "Owner", Create(), true)).Error!.Code);

        var state = await Ok(rooms.CreateAsync(1, "Owner", Create(settings: "{\"difficulty\":2,\"randomizer\":{\"enabled\":true,\"seed\":123,\"enemies\":1}}"), false));
        Assert.Matches("^[A-HJ-NP-Z2-9]{6}$", state.Room.Code);
        Assert.True(state.Room.Randomizer);
        Assert.Equal(2, state.Room.Difficulty);
        Assert.Equal(0, state.You.Slot);
        Assert.True(state.You.Owner);
        Assert.Equal(123, state.Settings!.Value.GetProperty("randomizer").GetProperty("seed").GetInt32());
        Assert.Equal(new RoomSettings(2, 0, 1, 123, false, 1, 0, false).Hash(41, 77).ToString("x16"), state.SettingsHash);
        Assert.Contains(_repo.Calls, c => c.StartsWith("create " + state.Room.Code + " ranked=True randomizer=True", StringComparison.Ordinal));

        var again = await rooms.CreateAsync(1, "Owner", Create(), false);
        Assert.Equal("room_owned", again.Error!.Code);
        Assert.Equal(state.Room.Code, again.Error.Extra!["roomCode"]);
    }

    [Fact]
    public async Task Ranked_data_hashes_and_code_collisions()
    {
        var rooms = Service(new RoomOptions { PasswordIterations = 1000, RankedDataHashes = ["0x4D", "12"] });
        _repo.DuplicatesToThrow = 2; // two codes taken in the database: the service draws new ones
        Assert.True((await Ok(rooms.CreateAsync(1, "A", Create(hash: 77), false))).Room.Ranked);
        Assert.False((await Ok(rooms.CreateAsync(2, "B", Create(hash: 13), false))).Room.Ranked);
        Assert.Throws<InvalidOperationException>(() => Service(new RoomOptions { RankedDataHashes = ["nope"] }));
    }

    /// <summary>
    /// The room service holds its lock during database writes: while the database hangs, the relay's ticket check is
    /// refused after the admission timeout instead of keeping the handshake (and the address's connection) open.
    /// </summary>
    [Fact]
    public async Task A_hanging_database_refuses_relay_tickets_in_time()
    {
        var rooms = Service(new RoomOptions { PasswordIterations = 1000, AdmissionTimeoutMilliseconds = 100 });
        var code = (await Ok(rooms.CreateAsync(1, "Owner", Create(), false))).Room.Code;
        _repo.Hang = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var join = rooms.JoinAsync(2, "Two", code, null, 41, 77, null); // waits in the database, the lock held
        var watch = System.Diagnostics.Stopwatch.StartNew();
        Assert.Null(await _relay.Admit(code, 2, "nonce"));
        Assert.InRange(watch.ElapsedMilliseconds, 50, 4000);
        _repo.Hang.SetResult();
        Assert.True((await join).Ok);
    }

    [Fact]
    public async Task Join_checks_password_version_capacity_and_kicks()
    {
        var rooms = Service();
        var code = (await Ok(rooms.CreateAsync(1, "Owner", Create(password: "secret!", max: 3), false))).Room.Code;
        Assert.Equal("room_not_found", (await rooms.JoinAsync(2, "Two", "ZZZZZZ", "secret!", 41, 77, null)).Error!.Code);
        Assert.Equal("incompatible_room", (await rooms.JoinAsync(2, "Two", code, "secret!", 40, 77, null)).Error!.Code);
        Assert.Equal("incompatible_room", (await rooms.JoinAsync(2, "Two", code, "secret!", 41, 78, null)).Error!.Code);
        Assert.Equal("wrong_room_password", (await rooms.JoinAsync(2, "Two", code, "wrong", 41, 77, null)).Error!.Code);
        var two = await Ok(rooms.JoinAsync(2, "Two", code.ToLowerInvariant(), "secret!", 41, 77, null));
        Assert.Equal(1, two.You.Slot);
        Assert.Equal(2, two.Members.Count);
        Assert.Equal(1, (await Ok(rooms.JoinAsync(2, "Two", code, null, 41, 77, null))).You.Slot); // repeating is a heartbeat
        await Ok(rooms.JoinAsync(3, "Three", code, "secret!", 41, 77, null));
        Assert.Equal("room_full", (await rooms.JoinAsync(4, "Four", code, "secret!", 41, 77, null)).Error!.Code);

        Assert.Equal("not_room_owner", (await rooms.KickAsync(2, code, "Three", null)).Error!.Code);
        Assert.Equal("cannot_kick_owner", (await rooms.KickAsync(1, code, "owner", null)).Error!.Code);
        Assert.True((await rooms.KickAsync(1, code, "three", null)).Ok);
        Assert.Equal("kicked", (await rooms.JoinAsync(3, "Three", code, "secret!", 41, 77, null)).Error!.Code);
        Assert.Equal("not_in_room", (await rooms.HeartbeatAsync(3, code)).Error!.Code);
        Assert.Contains("room_kick", _audit.Events);
        Assert.Contains(_repo.Calls, c => c.EndsWith(" 3 kicked", StringComparison.Ordinal));
    }

    [Fact]
    public async Task Owner_leaving_freezes_the_story_until_the_owner_returns()
    {
        var rooms = Service();
        var code = (await Ok(rooms.CreateAsync(1, "Owner", Create(max: 2), false))).Room.Code;
        await Ok(rooms.JoinAsync(2, "Guest", code, null, 41, 77, null));
        Assert.True((await rooms.LeaveAsync(1, code)).Ok);

        var guest = await Ok(rooms.HeartbeatAsync(2, code));
        Assert.True(guest.Room.StoryFrozen);
        Assert.False(guest.Room.OwnerPresent);
        Assert.Single(await rooms.ListAsync());
        Assert.Contains(_repo.Calls, c => c.StartsWith("frozen", StringComparison.Ordinal) && c.EndsWith("yes", StringComparison.Ordinal));

        // The owner's place is kept even in a full room, and no password is asked.
        var back = await Ok(rooms.JoinAsync(1, "Owner", code, null, 41, 77, null));
        Assert.False(back.Room.StoryFrozen);
        Assert.Equal(0, back.You.Slot);
        Assert.Contains(_repo.Calls, c => c.StartsWith("frozen", StringComparison.Ordinal) && c.EndsWith("no", StringComparison.Ordinal));

        await rooms.LeaveAsync(2, code);
        await rooms.LeaveAsync(1, code);
        Assert.Empty(await rooms.ListAsync());
        Assert.Contains(_repo.Calls, c => c.StartsWith("close", StringComparison.Ordinal) && c.EndsWith(CloseReasons.Empty, StringComparison.Ordinal));
        Assert.Equal((0, 0), await rooms.CountsAsync());
    }

    [Fact]
    public async Task Create_and_join_hand_out_room_tickets_and_the_settings_hash()
    {
        var rooms = Service();
        var created = await Ok(rooms.CreateAsync(1, "P1", Create(max: 10, settings: "{\"difficulty\":1,\"cameraMode\":1}"), false));
        var code = created.Room.Code;
        Assert.Equal($"{code}|1|member", created.RelayTicket);
        var expected = new RoomSettings(1, 1, 1, 0, false, 0, 0, false).Hash(41, 77).ToString("x16");
        Assert.Equal(expected, created.SettingsHash);
        var two = await Ok(rooms.JoinAsync(2, "P2", code, null, 41, 77, null));
        Assert.Equal($"{code}|2|member", two.RelayTicket);
        Assert.Equal(expected, two.SettingsHash);
        // A heartbeat carries no ticket; /ticket issues a new one (it replaces the older one).
        Assert.Null((await Ok(rooms.HeartbeatAsync(2, code))).RelayTicket);
        Assert.NotNull((await Ok(rooms.TicketAsync(2, code))).RelayTicket);
        Assert.Equal("not_in_room", (await rooms.TicketAsync(3, code)).Error!.Code);

        // Instance groups come from the relay and name each member's instance.
        _relay.Groups[code] = [new InstanceGroupView(7, "stag0101", 2, "sealed", [new InstanceGroupMember(1, "P1", 0, true, false),
            new InstanceGroupMember(2, "P2", 1, true, false)], false, false, true, 1234, null)];
        var state = await Ok(rooms.HeartbeatAsync(1, code));
        Assert.Equal(7u, state.You.Instance);
        Assert.Equal(7u, state.Members.Single(m => m.AccountId == 2).Instance);
        Assert.Equal(1, state.Room.Instances);

        _relay.Available = false;
        Assert.Equal("relay_unavailable", (await rooms.TicketAsync(1, code)).Error!.Code);
        Assert.Null((await Ok(rooms.JoinAsync(1, "P1", code, null, 41, 77, null))).RelayTicket);
    }

    /// <summary>The relay accepts only each member's newest ticket, once, and only while it is in the room.</summary>
    [Fact]
    public async Task Relay_admission_accepts_the_newest_ticket_once()
    {
        var rooms = Service();
        var code = (await Ok(rooms.CreateAsync(1, "Owner", Create(max: 4), false))).Room.Code;
        await Ok(rooms.JoinAsync(2, "Guest", code, null, 41, 77, null));
        var first = _relay.Issued[^1].Nonce;
        await Ok(rooms.TicketAsync(2, code));
        var second = _relay.Issued[^1].Nonce;
        Assert.Null(await _relay.Admit(code, 2, first)); // hoarded: an older ticket
        var admitted = await _relay.Admit(code, 2, second);
        Assert.NotNull(admitted);
        Assert.Equal((1, "Guest", false, 41, 77ul), (admitted.MemberIndex, admitted.Username, admitted.Owner, admitted.Protocol, admitted.DataHash));
        Assert.Equal(RoomSettings.Default.Hash(41, 77), admitted.SettingsHash);
        Assert.Null(await _relay.Admit(code, 2, second)); // accepted once
        Assert.Contains(_repo.Calls, c => c.StartsWith("playing", StringComparison.Ordinal));
        Assert.Null(await _relay.Admit("ZZZZZZ", 2, second));

        // Kicked: a ticket from before the kick is worthless, and the relay is told.
        await Ok(rooms.TicketAsync(2, code));
        var beforeKick = _relay.Issued[^1].Nonce;
        Assert.True((await rooms.KickAsync(1, code, "Guest", null)).Ok);
        Assert.Null(await _relay.Admit(code, 2, beforeKick));
        Assert.Contains($"left {code} 2 {LeaveReasons.Kicked}", _relay.Events);
        var owner = await _relay.Admit(code, 1, _relay.Issued[0].Nonce);
        Assert.True(owner!.Owner);
    }

    [Fact]
    public async Task The_relay_connection_is_the_heartbeat_and_hears_the_room_changes()
    {
        var rooms = Service();
        var code = (await Ok(rooms.CreateAsync(1, "Owner", Create(max: 4), false))).Room.Code;
        await Ok(rooms.JoinAsync(2, "Two", code, null, 41, 77, null));
        await Ok(rooms.JoinAsync(3, "Three", code, null, 41, 77, null));
        _clock.Now += TimeSpan.FromSeconds(30);
        await _relay.Beat!(code, [1L, 2L]); // members 1 and 2 hold relay connections; 3 is silent
        _clock.Now += TimeSpan.FromSeconds(10);
        Assert.Equal(1, await rooms.SweepAsync());
        Assert.Contains($"left {code} 3 {LeaveReasons.Timeout}", _relay.Events);

        Assert.True((await rooms.LeaveAsync(1, code)).Ok);
        Assert.Contains($"owner {code} away", _relay.Events);
        await Ok(rooms.JoinAsync(1, "Owner", code, null, 41, 77, null));
        Assert.Contains($"owner {code} present", _relay.Events);
        Assert.True((await rooms.CloseAsync(1, false, code, null)).Ok);
        Assert.Contains($"closed {code}", _relay.Events);
    }

    [Fact]
    public async Task A_failed_ticket_still_answers_the_join()
    {
        var rooms = Service();
        _relay.FailTickets = true;
        var created = await Ok(rooms.CreateAsync(1, "P1", Create(max: 4), false));
        Assert.Null(created.RelayTicket);
        Assert.Equal("relay_ticket_failed", (await rooms.TicketAsync(1, created.Room.Code)).Error!.Code);
    }

    [Fact]
    public async Task Silent_members_time_out_and_joining_elsewhere_leaves()
    {
        var rooms = Service();
        var first = (await Ok(rooms.CreateAsync(1, "Owner", Create(max: 4), false))).Room.Code;
        await Ok(rooms.JoinAsync(2, "Two", first, null, 41, 77, null));
        await Ok(rooms.JoinAsync(3, "Three", first, null, 41, 77, null));
        _clock.Now += TimeSpan.FromSeconds(20);
        await Ok(rooms.HeartbeatAsync(1, first));
        await Ok(rooms.HeartbeatAsync(2, first));
        _clock.Now += TimeSpan.FromSeconds(20); // Three last seen 40 s ago, the others 20 s ago
        Assert.Equal(1, await rooms.SweepAsync());
        Assert.Contains(_repo.Calls, c => c.EndsWith(" 3 timeout", StringComparison.Ordinal));

        var second = (await Ok(rooms.CreateAsync(5, "Other", Create(max: 4), false))).Room.Code;
        var moved = await Ok(rooms.JoinAsync(2, "Two", second, null, 41, 77, null));
        Assert.Equal(second, moved.Room.Code);
        Assert.Equal("not_in_room", (await rooms.HeartbeatAsync(2, first)).Error!.Code);
        Assert.Equal(1, (await rooms.GetAsync(first, 1)).Summary!.Players);
    }

    [Fact]
    public async Task Close_by_owner_or_staff_and_removing_an_account()
    {
        var rooms = Service();
        var code = (await Ok(rooms.CreateAsync(1, "Owner", Create(), false))).Room.Code;
        await Ok(rooms.JoinAsync(2, "Two", code, null, 41, 77, null));
        Assert.Equal("not_room_owner", (await rooms.CloseAsync(2, false, code, null)).Error!.Code);

        await rooms.RemoveAccountAsync(2);
        Assert.Equal(1, (await rooms.GetAsync(code, null)).Summary!.Players);

        Assert.True((await rooms.CloseAsync(9, true, code, null)).Ok);
        Assert.Contains(_repo.Calls, c => c.StartsWith("close", StringComparison.Ordinal) && c.EndsWith(CloseReasons.Admin, StringComparison.Ordinal));
        Assert.Contains("room_close_admin", _audit.Events);
        Assert.Contains($"left {code} 2 {LeaveReasons.Kicked}", _relay.Events);
        Assert.Contains($"closed {code}", _relay.Events);
        Assert.Empty(await rooms.ListAsync());
        // The owner may open a new room now.
        Assert.True((await rooms.CreateAsync(1, "Owner", Create(), false)).Ok);
    }

    [Fact]
    public void Settings_parser()
    {
        Assert.Equal((null, false, null), RoomService.ParseSettings(null, 100).Value);
        Assert.True(RoomService.ParseSettings(JsonDocument.Parse("{\"randomizer\":{\"enabled\":true,\"seed\":3,\"enemies\":1}}").RootElement, 100).Value.Randomizer);
        Assert.False(RoomService.ParseSettings(JsonDocument.Parse("{\"randomizer\":{\"enabled\":true}}").RootElement, 100).Value.Randomizer); // seed 0: the original game
        Assert.Equal("invalid_settings", RoomService.ParseSettings(JsonDocument.Parse("{\"randomizer\":true}").RootElement, 100).Error!.Code); // no seed: no randomizer room
        Assert.False(RoomService.ParseSettings(JsonDocument.Parse("{\"randomizer\":{\"enabled\":false}}").RootElement, 100).Value.Randomizer);
        Assert.Equal("invalid_settings", RoomService.ParseSettings(JsonDocument.Parse("{\"x\":\"" + new string('a', 200) + "\"}").RootElement, 100).Error!.Code);
        Assert.Equal("unranked_settings", RoomService.ParseSettings(JsonDocument.Parse("{\"rebalanced\":true}").RootElement, 100).Error!.Code);
        Assert.Equal("invalid_settings", RoomService.ParseSettings(JsonDocument.Parse("{\"cameraMode\":2}").RootElement, 100).Error!.Code);
        Assert.Equal("invalid_settings", RoomService.ParseSettings(JsonDocument.Parse("{\"randomizer\":{\"enabled\":true,\"seed\":\"x\"}}").RootElement, 100).Error!.Code);
    }

    /// <summary>The settings hash of the DW4R2 handshake: the canonical text (the client builds the same) and its FNV-1a 64.</summary>
    [Fact]
    public void Settings_hash_covers_every_simulation_setting()
    {
        var d = RoomSettings.Default;
        Assert.Equal("DW4R2-settings/1;protocol=43;data=1234;difficulty=0;camera=0;light=1;rand=0,0,0,0;fast=0", d.Canonical(43, 1234));
        Assert.Equal(RoomSettings.Fnv1a64(System.Text.Encoding.ASCII.GetBytes(d.Canonical(43, 1234))), d.Hash(43, 1234));
        Assert.Equal(0xcbf29ce484222325UL, RoomSettings.Fnv1a64([])); // the FNV-1a 64 offset basis
        var variants = new[]
        {
            d with { Difficulty = 1 }, d with { CameraMode = 1 }, d with { LightMode = 0 }, d with { FastLoading = true },
            d with { RandomizerSeed = 5, RandomizerEnemies = 1 }, d with { RandomizerSeed = 5, RandomizerZones = 2 }, d with { RandomizerSeed = 5, RandomizerDrops = true },
        };
        var hashes = variants.Select(v => v.Hash(43, 1234)).Append(d.Hash(43, 1234)).Append(d.Hash(44, 1234)).Append(d.Hash(43, 1235)).ToList();
        Assert.Equal(hashes.Count, hashes.Distinct().Count());
        // An inactive randomizer (seed 0, or no mode) is the original game.
        Assert.Equal(d.Hash(43, 1), (d with { RandomizerSeed = 9 }).Hash(43, 1));
        Assert.Equal(d.Hash(43, 1), (d with { RandomizerEnemies = 2 }).Hash(43, 1));
        Assert.Equal("DW4R2-settings/1;protocol=43;data=1;difficulty=2;camera=1;light=0;rand=4294967295,1,2,1;fast=1",
            new RoomSettings(2, 1, 0, uint.MaxValue, true, 2, 1, true).Canonical(43, 1));
        var parsed = RoomSettings.FromJson(JsonDocument.Parse("{\"difficulty\":2,\"cameraMode\":1,\"lightMode\":0,\"fastLoading\":true," +
            "\"randomizer\":{\"enabled\":true,\"seed\":4294967295,\"drops\":true,\"enemies\":2,\"zones\":1}}").RootElement);
        Assert.Equal(new RoomSettings(2, 1, 0, uint.MaxValue, true, 2, 1, true), parsed.Value);
        Assert.Equal(d, RoomSettings.FromJson(JsonDocument.Parse("{\"randomizer\":{\"enabled\":false,\"seed\":7,\"enemies\":1}}").RootElement).Value);
        Assert.Equal(d, RoomSettings.FromJson(null).Value);
        // A bare "randomizer": true is no randomizer (no seed): refused, never a randomizer room that plays without one.
        Assert.False(RoomSettings.FromJson(JsonDocument.Parse("{\"randomizer\":true}").RootElement).Ok);
        Assert.False(RoomSettings.RandomizerRequested(JsonDocument.Parse("{\"randomizer\":true}").RootElement));
        Assert.True(RoomSettings.RandomizerRequested(JsonDocument.Parse("{\"randomizer\":{\"enabled\":true,\"seed\":5,\"drops\":true}}").RootElement));
        Assert.False(RoomSettings.RandomizerRequested(JsonDocument.Parse("{\"randomizer\":{\"enabled\":true,\"seed\":5}}").RootElement)); // no mode
    }
}
