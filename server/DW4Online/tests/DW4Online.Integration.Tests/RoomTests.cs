using System.Net;
using System.Net.Http.Json;
using System.Security.Cryptography;
using System.Text.Json;
using Dapper;
using DW4Online.Domain.Services;
using DW4Online.Relay;
using Microsoft.Extensions.DependencyInjection;

namespace DW4Online.Integration.Tests;

/// <summary>The room API and the TCP relay together: HTTP on the test server, the relay on a real loopback port.</summary>
public class RoomTests
{
    private static object NewRoom(string name = "Night run", string? password = null, int maxPlayers = 4, object? settings = null) =>
        new { name, password, maxPlayers, protocol = 41, dataHash = 1234567890123UL, clientVersion = "0.48.0", settings };

    private static object Join(string? password = null) => new { password, protocol = 41, dataHash = 1234567890123UL, clientVersion = "0.48.0" };

    private static async Task<JsonElement> OkJson(HttpResponseMessage response, HttpStatusCode expected = HttpStatusCode.OK)
    {
        Assert.True(expected == response.StatusCode, $"{response.StatusCode}: {await response.Content.ReadAsStringAsync()}");
        return await response.Json();
    }

    [DbFact]
    public async Task Create_join_stage_and_relay_splice()
    {
        await using var host = await TestHost.StartAsync(RelaySettings.On());
        var relayPort = await host.App.Services.GetRequiredService<RelayServer>().Started.WaitAsync(TimeSpan.FromSeconds(10));
        var (hostToken, _, hostId) = await host.RegisterAsync("Hoster");
        var (guestToken, _, guestId) = await host.RegisterAsync("Guesty");
        var owner = host.Client(hostToken);
        var guest = host.Client(guestToken);

        Assert.Equal(HttpStatusCode.Unauthorized, (await host.Client().PostAsJsonAsync("/api/v1/rooms", NewRoom())).StatusCode);
        Assert.Equal("invalid_max_players", await (await owner.PostAsJsonAsync("/api/v1/rooms", NewRoom(maxPlayers: 11))).ProblemCode());
        var created = await OkJson(await owner.PostAsJsonAsync("/api/v1/rooms", NewRoom(password: "dw4dw4", settings: new { difficulty = 1 })), HttpStatusCode.Created);
        var code = created.GetProperty("room").GetProperty("code").GetString()!;
        Assert.Equal(1, created.GetProperty("room").GetProperty("difficulty").GetInt32());
        Assert.Equal(10, created.GetProperty("heartbeatSeconds").GetInt32());
        Assert.Equal("127.0.0.1", created.GetProperty("relay").GetProperty("host").GetString());
        Assert.Equal(relayPort, created.GetProperty("relay").GetProperty("port").GetInt32());
        Assert.Equal(relayPort, (await (await host.Client().GetAsync("/api/v1/status")).Json()).GetProperty("relay").GetProperty("port").GetInt32());

        // Public list, filtered like the HELLO check.
        var list = await OkJson(await host.Client().GetAsync("/api/v1/rooms?protocol=41&dataHash=1234567890123"));
        Assert.Equal(code, list[0].GetProperty("code").GetString());
        Assert.True(list[0].GetProperty("hasPassword").GetBoolean());
        Assert.Equal(0, (await OkJson(await host.Client().GetAsync("/api/v1/rooms?protocol=40"))).GetArrayLength());

        Assert.Equal("wrong_room_password", await (await guest.PostAsJsonAsync($"/api/v1/rooms/{code}/join", Join("nope"))).ProblemCode());
        var joined = await OkJson(await guest.PostAsJsonAsync($"/api/v1/rooms/{code}/join", Join("dw4dw4")));
        Assert.Equal(1, joined.GetProperty("you").GetProperty("slot").GetInt32());
        Assert.Equal(2, joined.GetProperty("members").GetArrayLength());
        var asMember = await OkJson(await guest.GetAsync($"/api/v1/rooms/{code}"));
        Assert.Equal(2, asMember.GetProperty("members").GetArrayLength());
        var (otherToken, _, _) = await host.RegisterAsync("Watcher");
        var outsider = await OkJson(await host.Client(otherToken).GetAsync($"/api/v1/rooms/{code}"));
        Assert.False(outsider.TryGetProperty("members", out _));
        Assert.Equal(2, outsider.GetProperty("room").GetProperty("players").GetInt32());

        // The durable record matches.
        await using (var sql = await host.SqlAsync())
        {
            var row = await sql.QuerySingleAsync<(string Status, int Max, bool HasPassword)>(
                "SELECT status, max_players, has_password FROM rooms WHERE public_code = @code", new { code });
            Assert.Equal(("open", 4, true), row);
            Assert.Equal(2, await sql.ExecuteScalarAsync<int>(
                "SELECT COUNT(*) FROM room_members m JOIN rooms r ON r.id = m.room_id WHERE r.public_code = @code AND m.left_at IS NULL", new { code }));
        }

        // Both enter the same stage: the owner hosts instance 1, the guest joins it.
        var hostStage = await OkJson(await owner.PostAsJsonAsync($"/api/v1/rooms/{code}/stage", new { stage = "stagD101" }));
        var guestStage = await OkJson(await guest.PostAsJsonAsync($"/api/v1/rooms/{code}/stage", new { stage = "stagD101" }));
        Assert.Equal("host", hostStage.GetProperty("role").GetString());
        Assert.Equal("guest", guestStage.GetProperty("role").GetString());
        var channel = hostStage.GetProperty("channel").GetString()!;
        Assert.Equal(code + "-1", channel);
        Assert.Equal(channel, guestStage.GetProperty("channel").GetString());

        // The relay: host control, guest, host data, frames both ways and a START-sized burst.
        await using var control = await RelayConnection.OpenAsync(relayPort, "DW4R1 HOST " + hostStage.GetProperty("ticket").GetString());
        var ok = await control.ReadLineAsync();
        Assert.StartsWith("OK ", ok, StringComparison.Ordinal);
        await using var guestSocket = await RelayConnection.OpenAsync(relayPort, "DW4R1 GUEST " + guestStage.GetProperty("ticket").GetString());
        var notice = (await control.ReadLineAsync()).Split(' ');
        Assert.Equal(["GUEST", notice[1], guestId.ToString(System.Globalization.CultureInfo.InvariantCulture)], notice);
        await using var data = await RelayConnection.OpenAsync(relayPort, $"DW4R1 DATA {channel} {notice[1]} {ok[3..]}");
        Assert.Equal("OK", await guestSocket.ReadLineAsync());
        var hello = new byte[] { 1, 5, 0, 0, 0 }.Concat("HELLO"u8.ToArray()).ToArray();
        await guestSocket.Stream.WriteAsync(hello);
        Assert.Equal(hello, await data.ReadExactlyAsync(hello.Length));
        var start = RandomNumberGenerator.GetBytes(90_176);
        await data.Stream.WriteAsync(start);
        Assert.Equal(start, await guestSocket.ReadExactlyAsync(start.Length));

        // A used ticket does not work twice.
        await using (var replay = await RelayConnection.OpenAsync(relayPort, "DW4R1 GUEST " + guestStage.GetProperty("ticket").GetString()))
            Assert.Equal("ERR ticket", await replay.ReadLineAsync());

        // Membership counts for achievements: the room is ranked and both are members.
        var unlock = await OkJson(await guest.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "online_join", context = new { roomCode = code, players = 2 } }));
        Assert.True(unlock.GetProperty("newlyUnlocked").GetBoolean());

        // The instance host's control socket closes: the instance ends, the guest's pair is dropped, the room stays.
        await control.DisposeAsync();
        Assert.True(await guestSocket.ClosedAsync());
        JsonElement beat = default;
        for (var i = 0; i < 100; ++i)
        {
            beat = await OkJson(await guest.PostAsync($"/api/v1/rooms/{code}/heartbeat", null));
            if (beat.GetProperty("instances").GetArrayLength() == 0) break;
            await Task.Delay(20);
        }
        Assert.Equal(0, beat.GetProperty("instances").GetArrayLength());
        Assert.Equal(JsonValueKind.Null, beat.GetProperty("you").GetProperty("instance").ValueKind);
        Assert.Equal(2, beat.GetProperty("members").GetArrayLength());
        _ = hostId;
    }

    [DbFact]
    public async Task Owner_away_freezes_the_story_kick_and_close()
    {
        await using var host = await TestHost.StartAsync(RelaySettings.On());
        var (ownerToken, _, _) = await host.RegisterAsync("Captain");
        var (aToken, _, _) = await host.RegisterAsync("Alpha");
        var (bToken, _, _) = await host.RegisterAsync("Bravo");
        var owner = host.Client(ownerToken);
        var a = host.Client(aToken);
        var b = host.Client(bToken);
        var code = (await OkJson(await owner.PostAsJsonAsync("/api/v1/rooms", NewRoom(maxPlayers: 3)), HttpStatusCode.Created))
            .GetProperty("room").GetProperty("code").GetString()!;
        await OkJson(await a.PostAsJsonAsync($"/api/v1/rooms/{code}/join", Join()));
        await OkJson(await b.PostAsJsonAsync($"/api/v1/rooms/{code}/join", Join()));
        Assert.Equal("room_full", await (await host.Client((await host.RegisterAsync("Charlie")).Token)
            .PostAsJsonAsync($"/api/v1/rooms/{code}/join", Join())).ProblemCode());

        // The owner leaves: the room keeps running with the story frozen.
        Assert.Equal(HttpStatusCode.NoContent, (await owner.PostAsync($"/api/v1/rooms/{code}/leave", null)).StatusCode);
        var state = await OkJson(await a.PostAsync($"/api/v1/rooms/{code}/heartbeat", null));
        Assert.True(state.GetProperty("room").GetProperty("storyFrozen").GetBoolean());
        Assert.False(state.GetProperty("room").GetProperty("ownerPresent").GetBoolean());
        await using (var sql = await host.SqlAsync())
            Assert.NotNull(await sql.ExecuteScalarAsync<DateTime?>("SELECT story_frozen_at FROM rooms WHERE public_code = @code", new { code }));
        var listed = await OkJson(await host.Client().GetAsync("/api/v1/rooms"));
        Assert.True(listed[0].GetProperty("storyFrozen").GetBoolean());

        // Guests cannot kick; the owner comes back (the full room kept the owner's place) and can.
        Assert.Equal("not_room_owner", await (await a.PostAsJsonAsync($"/api/v1/rooms/{code}/kick", new { username = "Bravo" })).ProblemCode());
        host.Clock.Advance(TimeSpan.FromSeconds(1)); // (a re-join in the same millisecond would reopen the old row)
        var back = await OkJson(await owner.PostAsJsonAsync($"/api/v1/rooms/{code}/join", Join()));
        Assert.False(back.GetProperty("room").GetProperty("storyFrozen").GetBoolean());
        Assert.Equal(HttpStatusCode.NoContent, (await owner.PostAsJsonAsync($"/api/v1/rooms/{code}/kick", new { username = "bravo" })).StatusCode);
        Assert.Equal("not_in_room", await (await b.PostAsync($"/api/v1/rooms/{code}/heartbeat", null)).ProblemCode());
        Assert.Equal("kicked", await (await b.PostAsJsonAsync($"/api/v1/rooms/{code}/join", Join())).ProblemCode());

        // Silent members time out.
        host.Clock.Advance(TimeSpan.FromSeconds(20));
        await OkJson(await owner.PostAsync($"/api/v1/rooms/{code}/heartbeat", null));
        host.Clock.Advance(TimeSpan.FromSeconds(20));
        Assert.Equal(1, await host.App.Services.GetRequiredService<RoomService>().SweepAsync());
        Assert.Equal("not_in_room", await (await a.PostAsync($"/api/v1/rooms/{code}/heartbeat", null)).ProblemCode());

        // Only the owner closes; closing ends every membership.
        Assert.Equal("not_room_owner", await (await a.DeleteAsync($"/api/v1/rooms/{code}")).ProblemCode());
        Assert.Equal(HttpStatusCode.NoContent, (await owner.DeleteAsync($"/api/v1/rooms/{code}")).StatusCode);
        Assert.Equal(0, (await OkJson(await host.Client().GetAsync("/api/v1/rooms"))).GetArrayLength());
        await using (var sql = await host.SqlAsync())
        {
            var row = await sql.QuerySingleAsync<(string Status, string Reason)>("SELECT status, close_reason FROM rooms WHERE public_code = @code", new { code });
            Assert.Equal(("closed", "host_closed"), row);
            var reasons = (await sql.QueryAsync<string>(
                "SELECT m.leave_reason FROM room_members m JOIN rooms r ON r.id = m.room_id WHERE r.public_code = @code ORDER BY m.joined_at, m.account_id",
                new { code })).ToList();
            Assert.Contains("left", reasons);      // the owner's first stay
            Assert.Contains("kicked", reasons);
            Assert.Contains("timeout", reasons);
            Assert.Contains("room_closed", reasons);
            Assert.Equal(0, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM room_members WHERE left_at IS NULL"));
        }
    }

    [DbFact]
    public async Task Randomizer_rooms_staff_and_leftovers()
    {
        await using var host = await TestHost.StartAsync(RelaySettings.On());
        var (ownerToken, _, ownerId) = await host.RegisterAsync("Shuffler");
        var (modToken, _, _) = await host.RegisterAsync("Moddy");
        await using (var sql = await host.SqlAsync())
            await sql.ExecuteAsync("UPDATE accounts SET role = 'moderator' WHERE username = 'Moddy'");
        var owner = host.Client(ownerToken);
        var code = (await OkJson(await owner.PostAsJsonAsync("/api/v1/rooms",
            NewRoom(settings: new { randomizer = new { enabled = true, seed = "DIGI-1234" } })), HttpStatusCode.Created))
            .GetProperty("room").GetProperty("code").GetString()!;

        // The room setting decides the ruleset, whatever the client reports.
        var unlock = await OkJson(await owner.PostAsJsonAsync("/api/v1/achievements/unlock",
            new { code = "online_join", context = new { roomCode = code, randomizer = false } }));
        Assert.Equal("randomizer", unlock.GetProperty("ruleset").GetString());

        // Owning a room blocks a second one; online rooms refuse cheats.
        Assert.Equal("room_owned", await (await owner.PostAsJsonAsync("/api/v1/rooms", NewRoom())).ProblemCode());
        var (otherToken, _, _) = await host.RegisterAsync("Cheater");
        Assert.Equal("unranked_settings", await (await host.Client(otherToken).PostAsJsonAsync("/api/v1/rooms",
            NewRoom(settings: new { cheats = true }))).ProblemCode());

        // Staff close a room; a banned owner leaves it at once.
        var mod = host.Client(modToken);
        Assert.Equal(HttpStatusCode.Forbidden, (await owner.PostAsync($"/api/v1/admin/rooms/{code}/close", null)).StatusCode);
        Assert.Equal(HttpStatusCode.NoContent, (await mod.PostAsync($"/api/v1/admin/rooms/{code}/close", null)).StatusCode);
        var second = (await OkJson(await owner.PostAsJsonAsync("/api/v1/rooms", NewRoom()), HttpStatusCode.Created))
            .GetProperty("room").GetProperty("code").GetString()!;
        var (memberToken, _, _) = await host.RegisterAsync("Member");
        await OkJson(await host.Client(memberToken).PostAsJsonAsync($"/api/v1/rooms/{second}/join", Join()));
        Assert.Equal(HttpStatusCode.OK, (await mod.PostAsJsonAsync("/api/v1/admin/accounts/Shuffler/status", new { status = "banned", reason = "test" })).StatusCode);
        var after = await OkJson(await host.Client(memberToken).PostAsync($"/api/v1/rooms/{second}/heartbeat", null));
        Assert.True(after.GetProperty("room").GetProperty("storyFrozen").GetBoolean());
        Assert.Equal(1, after.GetProperty("members").GetArrayLength());

        // Rows a previous process left open are closed at start (reason server_restart).
        await using (var sql = await host.SqlAsync())
        {
            await sql.ExecuteAsync(
                "INSERT INTO rooms (public_code, name, host_account_id, max_players, protocol, data_hash, client_version, status, created_at) " +
                "VALUES ('LEFT01', 'Old', @ownerId, 4, 41, 1, 'x', 'playing', UTC_TIMESTAMP(3))", new { ownerId });
            Assert.True(await host.App.Services.GetRequiredService<RoomService>().CloseLeftoversAsync() >= 1);
            Assert.Equal("server_restart", await sql.ExecuteScalarAsync<string>("SELECT close_reason FROM rooms WHERE public_code = 'LEFT01'"));
            Assert.Equal("admin", await sql.ExecuteScalarAsync<string>("SELECT close_reason FROM rooms WHERE public_code = @code", new { code }));
        }
    }

    [DbFact]
    public async Task Stage_needs_the_relay()
    {
        await using var host = await TestHost.StartAsync(); // relay off
        var (token, _, _) = await host.RegisterAsync("Lonely");
        var client = host.Client(token);
        var created = await OkJson(await client.PostAsJsonAsync("/api/v1/rooms", NewRoom()), HttpStatusCode.Created);
        Assert.Equal(JsonValueKind.Null, created.GetProperty("relay").ValueKind);
        var code = created.GetProperty("room").GetProperty("code").GetString()!;
        var stage = await client.PostAsJsonAsync($"/api/v1/rooms/{code}/stage", new { stage = "stagD101" });
        Assert.Equal(HttpStatusCode.ServiceUnavailable, stage.StatusCode);
        Assert.Equal("relay_unavailable", await stage.ProblemCode());
    }

    [DbFact]
    public async Task Announced_relay_port_follows_the_bound_port_unless_set()
    {
        // Default (Service:RelayPort 0): whatever Relay:Port the relay listens on is announced.
        await using (var host = await TestHost.StartAsync(RelaySettings.On()))
        {
            var bound = await host.App.Services.GetRequiredService<RelayServer>().Started.WaitAsync(TimeSpan.FromSeconds(10));
            var status = await OkJson(await host.Client().GetAsync("/api/v1/status"));
            Assert.Equal(bound, status.GetProperty("relay").GetProperty("port").GetInt32());
            Assert.NotEqual(52041, bound);
        }
        // A port forward: clients use another public port.
        await using (var host = await TestHost.StartAsync(RelaySettings.On(new() { ["Service:RelayPort"] = "45123" })))
        {
            await host.App.Services.GetRequiredService<RelayServer>().Started.WaitAsync(TimeSpan.FromSeconds(10));
            var status = await OkJson(await host.Client().GetAsync("/api/v1/status"));
            Assert.Equal(45123, status.GetProperty("relay").GetProperty("port").GetInt32());
        }
    }
}
