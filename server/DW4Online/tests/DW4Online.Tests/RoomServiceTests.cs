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

        public Task AddMemberAsync(long roomId, long accountId, int slot, DateTime now, CancellationToken ct = default)
        {
            Calls.Add($"add {roomId} {accountId} {slot}");
            return Task.CompletedTask;
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
        public List<string> Closed { get; } = [];
        public List<string> Dropped { get; } = [];
        public event Action<string>? HostLost;

        public bool FailTickets { get; set; }
        public RelayTickets? Real { get; set; }

        public string IssueTicket(string channel, long accountId, bool host)
        {
            if (FailTickets) throw new ArgumentException("no ticket");
            if (Real is not null) return Real.Issue(channel, accountId, host ? RelayRoles.Host : RelayRoles.Guest);
            return $"{channel}|{accountId}|{(host ? "host" : "guest")}";
        }
        public void CloseChannel(string channel) => Closed.Add(channel);
        public void DropGuest(string channel, long accountId) => Dropped.Add($"{channel} {accountId}");
        public void LoseHost(string channel) => HostLost?.Invoke(channel);
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

        var state = await Ok(rooms.CreateAsync(1, "Owner", Create(settings: "{\"difficulty\":2,\"randomizer\":{\"enabled\":true,\"seed\":\"ABC\"}}"), false));
        Assert.Matches("^[A-HJ-NP-Z2-9]{6}$", state.Room.Code);
        Assert.True(state.Room.Randomizer);
        Assert.Equal(2, state.Room.Difficulty);
        Assert.Equal(0, state.You.Slot);
        Assert.True(state.You.Owner);
        Assert.Equal("ABC", state.Settings!.Value.GetProperty("randomizer").GetProperty("seed").GetString());
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
    public async Task Stage_instances_hold_four_players_each()
    {
        var rooms = Service();
        var code = (await Ok(rooms.CreateAsync(1, "P1", Create(max: 10), false))).Room.Code;
        for (var id = 2; id <= 6; ++id) await Ok(rooms.JoinAsync(id, "P" + id, code, null, 41, 77, null));

        var a = new Dictionary<long, StageAssignment>();
        for (var id = 1; id <= 6; ++id)
        {
            var result = await rooms.EnterStageAsync(id, code, "stagD101", null);
            Assert.True(result.Ok, result.Error?.Code);
            a[id] = result.Value!;
        }
        Assert.Equal(StageRoles.Host, a[1].Role);
        Assert.All(new long[] { 2, 3, 4 }, id => Assert.Equal((StageRoles.Guest, 1), (a[id].Role, a[id].Instance.Id)));
        Assert.Equal((StageRoles.Host, 2), (a[5].Role, a[5].Instance.Id)); // instance 1 is full
        Assert.Equal((StageRoles.Guest, 2), (a[6].Role, a[6].Instance.Id));
        Assert.Equal(code + "-1", a[1].Channel);
        Assert.Equal($"{code}-1|1|host", a[1].Ticket);
        Assert.Equal($"{code}-2|6|guest", a[6].Ticket);
        Assert.Contains(_repo.Calls, c => c.StartsWith("playing", StringComparison.Ordinal));

        // A guest entering the same stage again reconnects to its instance; another stage moves it.
        var again = (await rooms.EnterStageAsync(3, code, "stagD101", null)).Value!;
        Assert.Equal((StageRoles.Guest, 1), (again.Role, again.Instance.Id));
        Assert.Contains($"{code}-1 3", _relay.Dropped);
        var moved = (await rooms.EnterStageAsync(4, code, "stagD102", null)).Value!;
        Assert.Equal((StageRoles.Host, 3), (moved.Role, moved.Instance.Id));
        // The preferred instance is honoured when it has room.
        var follow = (await rooms.EnterStageAsync(2, code, "stagD102", 3)).Value!;
        Assert.Equal((StageRoles.Guest, 3), (follow.Role, follow.Instance.Id));

        // The instance host leaves: the instance ends, its players are out of every instance.
        Assert.True((await rooms.LeaveStageAsync(1, code)).Ok);
        Assert.Contains(code + "-1", _relay.Closed);
        var three = await Ok(rooms.HeartbeatAsync(3, code));
        Assert.Null(three.You.Instance);
        Assert.DoesNotContain(three.Instances, i => i.Id == 1);

        // The relay reports a lost host control socket: same effect.
        _relay.LoseHost(code + "-2");
        for (var i = 0; i < 50 && (await Ok(rooms.HeartbeatAsync(6, code))).You.Instance is not null; ++i) await Task.Delay(10);
        Assert.Null((await Ok(rooms.HeartbeatAsync(6, code))).You.Instance);

        Assert.Equal("invalid_stage", (await rooms.EnterStageAsync(1, code, "../x", null)).Error!.Code);
        _relay.Available = false;
        Assert.Equal("relay_unavailable", (await rooms.EnterStageAsync(1, code, "stagD101", null)).Error!.Code);
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
        await rooms.EnterStageAsync(1, code, "stagA", null);
        Assert.Equal("not_room_owner", (await rooms.CloseAsync(2, false, code, null)).Error!.Code);

        await rooms.RemoveAccountAsync(2);
        Assert.Equal(1, (await rooms.GetAsync(code, null)).Summary!.Players);

        Assert.True((await rooms.CloseAsync(9, true, code, null)).Ok);
        Assert.Contains(_repo.Calls, c => c.StartsWith("close", StringComparison.Ordinal) && c.EndsWith(CloseReasons.Admin, StringComparison.Ordinal));
        Assert.Contains("room_close_admin", _audit.Events);
        Assert.Contains(code + "-1", _relay.Closed);
        Assert.Empty(await rooms.ListAsync());
        // The owner may open a new room now.
        Assert.True((await rooms.CreateAsync(1, "Owner", Create(), false)).Ok);
    }

    /// <summary>The review's blocker: a room keeps working past instance 9999 (real relay tickets).</summary>
    [Fact]
    public async Task Instance_numbers_keep_working_past_9999()
    {
        _relay.Real = new RelayTickets(new byte[32], TimeSpan.FromSeconds(60), _clock);
        var rooms = Service();
        var code = (await Ok(rooms.CreateAsync(1, "P1", Create(max: 4), false))).Room.Code;
        await Ok(rooms.JoinAsync(2, "P2", code, null, 41, 77, null));
        StageAssignment? last = null;
        for (var i = 0; i < 10_050; ++i)
        {
            var result = await rooms.EnterStageAsync(1, code, i % 2 == 0 ? "stag0101" : "stag0102", null);
            Assert.True(result.Ok, $"enter {i}: {result.Error?.Code}");
            last = result.Value!;
        }
        Assert.Equal(10_050, last!.Instance.Id);
        Assert.Equal($"{code}-10050", last.Channel);
        var guest = await rooms.EnterStageAsync(2, code, "stag0102", null);
        Assert.True(guest.Ok, guest.Error?.Code);
        Assert.Equal((StageRoles.Guest, 10_050), (guest.Value!.Role, guest.Value.Instance.Id));
        var state = await Ok(rooms.HeartbeatAsync(1, code));
        Assert.Single(state.Instances); // every earlier instance ended when its host moved on
    }

    /// <summary>A ticket that cannot be issued changes nothing (no half-entered instance).</summary>
    [Fact]
    public async Task A_failed_ticket_leaves_the_room_as_it_was()
    {
        var rooms = Service();
        var code = (await Ok(rooms.CreateAsync(1, "P1", Create(max: 4), false))).Room.Code;
        await Ok(rooms.JoinAsync(2, "P2", code, null, 41, 77, null));
        var hosted = (await rooms.EnterStageAsync(1, code, "stag0101", null)).Value!;
        await rooms.EnterStageAsync(2, code, "stag0101", null);
        _relay.FailTickets = true;
        Assert.Equal("relay_ticket_failed", (await rooms.EnterStageAsync(2, code, "stag0102", null)).Error!.Code);
        Assert.Equal("relay_ticket_failed", (await rooms.EnterStageAsync(1, code, "stag0102", null)).Error!.Code);
        Assert.Equal("relay_ticket_failed", (await rooms.EnterStageAsync(2, code, "stag0101", null)).Error!.Code);
        var state = await Ok(rooms.HeartbeatAsync(2, code));
        Assert.Equal(hosted.Instance.Id, state.You.Instance); // still in its instance
        Assert.Single(state.Instances);
        Assert.Equal(2, state.Instances[0].Players.Count);
        Assert.Empty(_relay.Closed);
        _relay.FailTickets = false;
        var moved = (await rooms.EnterStageAsync(1, code, "stag0102", null)).Value!;
        Assert.Equal((StageRoles.Host, hosted.Instance.Id + 1), (moved.Role, moved.Instance.Id));
    }

    [Fact]
    public void Settings_parser()
    {
        Assert.Equal((null, false, null), RoomService.ParseSettings(null, 100).Value);
        Assert.True(RoomService.ParseSettings(JsonDocument.Parse("{\"randomizer\":true}").RootElement, 100).Value.Randomizer);
        Assert.False(RoomService.ParseSettings(JsonDocument.Parse("{\"randomizer\":{\"enabled\":false}}").RootElement, 100).Value.Randomizer);
        Assert.Equal("invalid_settings", RoomService.ParseSettings(JsonDocument.Parse("{\"x\":\"" + new string('a', 200) + "\"}").RootElement, 100).Error!.Code);
        Assert.Equal("unranked_settings", RoomService.ParseSettings(JsonDocument.Parse("{\"rebalanced\":true}").RootElement, 100).Error!.Code);
    }
}
