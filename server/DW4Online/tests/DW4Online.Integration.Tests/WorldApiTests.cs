using System.Net;
using System.Net.Http.Json;
using Dapper;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Services;
using DW4Online.Tests;
using Microsoft.Extensions.DependencyInjection;

namespace DW4Online.Integration.Tests;

/// <summary>
/// The world's REST API and website (SEAMLESS.md 19.19, 19.20, round I1a) against the stand-in relay: /world/enter and
/// /world/ticket with their tickets and the admission the relay asks, /world/leave, the public counts, the instances a
/// player may see (Visible(), 19.7.2), the online preferences, the staff's /admin/instances, 410 for /rooms*, the protocol
/// floor in code (19.17.3), friends' whereabouts and "Recently played with", and the notifier calls.
/// </summary>
public class WorldApiTests
{
    private const int P = TestHost.Protocol;

    private static object Enter(int protocol = P) => new { box = 1, protocol, dataHash = 1UL, clientVersion = "0.60.0" };

    [DbFact]
    public async Task Enter_hands_out_a_ticket_the_admission_accepts_once_newest()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, id) = await host.RegisterAsync("Walker");
        var (friendToken, _, friend) = await host.RegisterAsync("Pal");
        var (_, _, blocker) = await host.RegisterAsync("Grumpy");
        var client = host.Client(token);
        Assert.Equal(HttpStatusCode.OK, (await client.PostAsJsonAsync("/api/v1/friends/requests", new { username = "Pal" })).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await host.Client(friendToken).PostAsync("/api/v1/friends/requests/Walker/accept", null)).StatusCode);
        Assert.Equal(HttpStatusCode.NoContent, (await host.Client((await host.LoginAsync("Grumpy")).Token).PostAsJsonAsync("/api/v1/blocks", new { username = "Walker" })).StatusCode);

        var entered = await client.PostAsJsonAsync("/api/v1/world/enter", Enter());
        Assert.Equal(HttpStatusCode.OK, entered.StatusCode);
        var body = await entered.Json();
        var ticket = body.GetProperty("relayTicket").GetString()!;
        Assert.Equal(ticket, body.GetProperty("ticket").GetString());
        Assert.Equal(50, body.GetProperty("protocol").GetInt32());
        Assert.Equal("player", body.GetProperty("role").GetString());
        Assert.Equal("member", body.GetProperty("relayRole").GetString());
        Assert.Equal(1, body.GetProperty("box").GetInt32());
        Assert.Equal(System.Text.Json.JsonValueKind.Null, body.GetProperty("heldSeat").ValueKind);
        Assert.Equal("DW4R2", body.GetProperty("relay").GetProperty("protocol").GetString());
        Assert.Equal(52041, body.GetProperty("relay").GetProperty("port").GetInt32());
        Assert.Equal("public", body.GetProperty("prefs").GetProperty("defaultVisibility").GetString());
        Assert.Equal(1, body.GetProperty("settings").GetProperty("lightMode").GetInt32()); // RoomSettings.Default's
        Assert.False(body.GetProperty("settings").GetProperty("randomizer").GetProperty("enabled").GetBoolean());

        // The relay asks: the newest ticket, with name, role, box, friends and blocks; again for its live re-check.
        var admitted = await host.World!.AdmitAsync(ticket);
        Assert.NotNull(admitted);
        Assert.Equal(("Walker", "member", 1, true), (admitted!.Username, admitted.Role, admitted.Box, admitted.NewSession));
        Assert.Equal([friend], admitted.Friends);
        Assert.Empty(admitted.Blocks);
        Assert.Equal([blocker], admitted.BlockedBy);
        Assert.NotNull(await host.World.AdmitAsync(ticket));

        // A reconnect's ticket replaces it: the older one counts no more; NewSession tells the two apart.
        var again = await (await client.PostAsJsonAsync("/api/v1/world/ticket", Enter())).Json();
        var second = again.GetProperty("relayTicket").GetString()!;
        Assert.Null(await host.World.AdmitAsync(ticket));
        Assert.False((await host.World.AdmitAsync(second))!.NewSession);

        // A lock: the live re-check refuses (the relay closes the member); the relay heard of it.
        var admin = host.App.Services.GetRequiredService<AccountService>();
        var (bossToken, _, _) = await host.RegisterAsync("Boss");
        await using (var sql = await host.SqlAsync()) await sql.ExecuteAsync("UPDATE accounts SET role = 'admin' WHERE username = 'Boss'");
        var boss = host.Client((await host.LoginAsync("Boss")).Token);
        Assert.Equal(HttpStatusCode.OK, (await boss.PostAsJsonAsync("/api/v1/admin/accounts/Walker/status", new { status = "locked", reason = "test" })).StatusCode);
        Assert.Contains($"account {id}", host.World.Notes);
        Assert.Null(await host.World.AdmitAsync(second));
        _ = admin;
        _ = bossToken;

        // Staff get the staff role in the ticket.
        var staff = await (await boss.PostAsJsonAsync("/api/v1/world/enter", Enter())).Json();
        Assert.Equal("admin", staff.GetProperty("role").GetString());
        Assert.Equal("staff", staff.GetProperty("relayRole").GetString());
        Assert.Equal("staff", (await host.World.AdmitAsync(staff.GetProperty("relayTicket").GetString()!))!.Role);

        // Leave: the relay closes the connection, and the ticket counts no more.
        var (leaverToken, _, leaver) = await host.RegisterAsync("Leaver");
        var leaverClient = host.Client(leaverToken);
        var leaving = (await (await leaverClient.PostAsJsonAsync("/api/v1/world/enter", Enter())).Json()).GetProperty("relayTicket").GetString()!;
        Assert.Equal(HttpStatusCode.NoContent, (await leaverClient.PostAsync("/api/v1/world/leave", null)).StatusCode);
        Assert.Contains(leaver, host.World.Left);
        Assert.Null(await host.World.AdmitAsync(leaving));

        // Entering kept the story the session starts from (none yet: nothing to keep) and wrote last seen (apart from the
        // admission: wait for it).
        await using var c = await host.SqlAsync();
        Assert.Equal(0, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM online_story_history WHERE account_id = @id", new { id }));
        Assert.NotNull(await Eventually(() => c.ExecuteScalarAsync<DateTime?>("SELECT last_seen_at FROM accounts WHERE id = @id", new { id })));
    }

    /// <summary>
    /// The admission never throws into the relay (review of I1a): a database error refuses the ticket (null, the relay's
    /// ERR ticket) and is reported; a lock wait on the account's row (last seen) never refuses a valid ticket, last seen
    /// is written once the lock is gone.
    /// </summary>
    [DbFact]
    public async Task Admission_refuses_on_database_errors_and_never_waits_for_last_seen()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, id) = await host.RegisterAsync("Fragile");
        var service = host.App.Services.GetRequiredService<WorldService>();
        var failures = new System.Collections.Concurrent.ConcurrentQueue<string>();
        var previous = service.WriteFailed;
        service.WriteFailed = (what, e) => { failures.Enqueue($"{what}: {e.GetType().Name}"); previous?.Invoke(what, e); };
        try
        {
            var ticket = (await (await host.Client(token).PostAsJsonAsync("/api/v1/world/enter", Enter())).Json()).GetProperty("relayTicket").GetString()!;

            // Another transaction holds the account's row: the admission answers at once, last seen follows the commit.
            await using (var locker = await host.SqlAsync())
            {
                await using var tx = await locker.BeginTransactionAsync();
                await locker.ExecuteAsync("SELECT id FROM accounts WHERE id = @id FOR UPDATE", new { id }, tx);
                var watch = System.Diagnostics.Stopwatch.StartNew();
                var admitted = await host.World!.AdmitAsync(ticket);
                Assert.NotNull(admitted);
                Assert.True(watch.Elapsed < service.AdmissionTimeout, $"the admission took {watch.Elapsed}");
                await using (var peek = await host.SqlAsync())
                    Assert.Null(await peek.ExecuteScalarAsync<DateTime?>("SELECT last_seen_at FROM accounts WHERE id = @id", new { id }));
                await tx.CommitAsync();
            }
            await using var c = await host.SqlAsync();
            Assert.NotNull(await Eventually(() => c.ExecuteScalarAsync<DateTime?>("SELECT last_seen_at FROM accounts WHERE id = @id", new { id })));

            // A database error (a table gone): refused and reported, never thrown.
            await c.ExecuteAsync("RENAME TABLE online_prefs TO online_prefs_gone");
            Assert.Null(await host.World!.AdmitAsync(ticket));
            Assert.Contains(failures, f => f.StartsWith($"the world admission of account {id}: ", StringComparison.Ordinal));
            await c.ExecuteAsync("RENAME TABLE online_prefs_gone TO online_prefs");
            Assert.NotNull(await host.World.AdmitAsync(ticket));
        }
        finally
        {
            service.WriteFailed = previous;
        }
    }

    /// <summary>Polls a value until it is not null (background writes), at most 5 s.</summary>
    private static async Task<T?> Eventually<T>(Func<Task<T?>> read) where T : struct
    {
        for (var i = 0; ; i++)
        {
            var value = await read();
            if (value is not null || i >= 50) return value;
            await Task.Delay(100);
        }
    }

    [DbFact]
    public async Task Enter_refuses_old_clients_maintenance_a_missing_relay_and_foreign_game_data()
    {
        await using (var host = await TestHost.StartAsync(new() { ["Play:MinimumProtocol"] = "46", ["World:DataHashes:0"] = "0x10" }))
        {
            var (token, _, _) = await host.RegisterAsync("Oldie");
            var client = host.Client(token);
            // The floor in code: 46 to 49 are refused although Play:MinimumProtocol says 46 (19.17.3, ProtocolFloor; 50 since round I1b).
            foreach (var old in new[] { 46, 47, 48, 49 })
            {
                var refused = await client.PostAsJsonAsync("/api/v1/world/enter", Enter(old));
                Assert.Equal(HttpStatusCode.UpgradeRequired, refused.StatusCode);
                Assert.Equal(50, (await refused.Json()).GetProperty("minimumProtocol").GetInt32());
                Assert.Equal(HttpStatusCode.UpgradeRequired, (await host.Client().PostAsJsonAsync("/api/v1/auth/login",
                    new { username = "Oldie", password = "a decent password", protocol = old })).StatusCode);
            }
            var status = await (await host.Client().GetAsync("/api/v1/status")).Json();
            Assert.Equal(50, status.GetProperty("client").GetProperty("minimumProtocol").GetInt32());
            Assert.Equal("unsupported_data", await (await client.PostAsJsonAsync("/api/v1/world/enter", Enter())).ProblemCode()); // data hash 1, not 0x10
            Assert.Equal(HttpStatusCode.OK, (await client.PostAsJsonAsync("/api/v1/world/enter",
                new { protocol = P, dataHash = 16UL, clientVersion = "0.60.0" })).StatusCode);
            // (A body without protocol is the relay's to refuse; the client always sends it.)
            Assert.Equal(HttpStatusCode.OK, (await client.PostAsJsonAsync("/api/v1/world/enter", new { box = 1 })).StatusCode);
        }
        await using (var host = await TestHost.StartAsync(new() { ["Service:Maintenance"] = "true" }))
        {
            var (token, _, _) = await host.RegisterAsync("Waiter");
            Assert.Equal("maintenance", await (await host.Client(token).PostAsJsonAsync("/api/v1/world/enter", Enter())).ProblemCode());
        }
        await using (var host = await TestHost.StartAsync())
        {
            host.World!.Available = false;
            var (token, _, _) = await host.RegisterAsync("Lonely");
            var response = await host.Client(token).PostAsJsonAsync("/api/v1/world/enter", Enter());
            Assert.Equal(HttpStatusCode.ServiceUnavailable, response.StatusCode);
            Assert.Equal("relay_unavailable", await response.ProblemCode());
            Assert.Equal(System.Text.Json.JsonValueKind.Null,
                (await (await host.Client().GetAsync("/api/v1/status")).Json()).GetProperty("relay").ValueKind);
        }
    }

    /// <summary>
    /// The production example configuration says Play:MinimumProtocol 49 (19.22 step 7); a server updated without that
    /// edit (48 from release 0.1.6) still refuses a 48 client by the floor in code: W22's server half.
    /// </summary>
    [DbFact]
    public async Task The_production_example_refuses_a_room_client()
    {
        var example = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "..", "deploy", "appsettings.Production.example.json"));
        Assert.True(File.Exists(example), example);
        using var json = System.Text.Json.JsonDocument.Parse(await File.ReadAllTextAsync(example));
        var configured = json.RootElement.GetProperty("Play").GetProperty("MinimumProtocol").GetInt32();
        Assert.Equal(DW4Online.Domain.World.WorldProtocol.Version, configured);
        Assert.Equal(50, json.RootElement.GetProperty("Play").GetProperty("CurrentProtocol").GetInt32());
        Assert.True(json.RootElement.GetProperty("Play").GetProperty("RequireWorld").GetBoolean());
        foreach (var minimum in new[] { configured, 48 })
        {
            await using var host = await TestHost.StartAsync(new() { ["Play:MinimumProtocol"] = minimum.ToString() });
            var (token, _, _) = await host.RegisterAsync("Room48");
            Assert.Equal(HttpStatusCode.UpgradeRequired, (await host.Client(token).PostAsJsonAsync("/api/v1/world/enter", Enter(48))).StatusCode);
            Assert.Equal("rooms_retired", await (await host.Client(token).GetAsync("/api/v1/rooms")).ProblemCode());
        }
    }

    [DbFact]
    public async Task Rooms_answer_410_everywhere()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, _) = await host.RegisterAsync("Roomer");
        foreach (var client in new[] { host.Client(), host.Client(token) })
        {
            foreach (var request in new[]
                     {
                         new HttpRequestMessage(HttpMethod.Get, "/api/v1/rooms"),
                         new HttpRequestMessage(HttpMethod.Post, "/api/v1/rooms") { Content = JsonContent.Create(new { name = "x" }) },
                         new HttpRequestMessage(HttpMethod.Post, "/api/v1/rooms/ABCDEF/join") { Content = JsonContent.Create(new { }) },
                         new HttpRequestMessage(HttpMethod.Delete, "/api/v1/rooms/ABCDEF"),
                         new HttpRequestMessage(HttpMethod.Get, "/api/v1/rooms/ABCDEF/story"),
                     })
            {
                var response = await client.SendAsync(request);
                Assert.Equal(HttpStatusCode.Gone, response.StatusCode);
                Assert.Equal("rooms_retired", await response.ProblemCode());
            }
        }
    }

    /// <summary>
    /// Visible() for the lists (19.7.2): Public for everyone signed in, Friends for the host's friends (not a member's),
    /// Private for nobody but its members and staff; a block either way with any member hides it; signed-out visitors get
    /// counts only. GET /world is public and nameless.
    /// </summary>
    [DbFact]
    public async Task Instances_follow_visibility_friends_blocks_and_staff()
    {
        await using var host = await TestHost.StartAsync();
        var ids = new Dictionary<string, long>();
        var tokens = new Dictionary<string, string>();
        foreach (var name in new[] { "Kim", "Lee", "Max", "Ann", "Bob", "Cat", "Moddy" })
        {
            var (token, _, id) = await host.RegisterAsync(name);
            (ids[name], tokens[name]) = (id, token);
        }
        await using (var sql = await host.SqlAsync()) await sql.ExecuteAsync("UPDATE accounts SET role = 'moderator' WHERE username = 'Moddy'");
        tokens["Moddy"] = (await host.LoginAsync("Moddy")).Token;
        // Ann is Kim's friend; Bob is Lee's friend (Lee a guest of Kim's Friends instance); Cat blocks Max.
        async Task Befriend(string a, string b)
        {
            Assert.Equal(HttpStatusCode.OK, (await host.Client(tokens[a]).PostAsJsonAsync("/api/v1/friends/requests", new { username = b })).StatusCode);
            Assert.Equal(HttpStatusCode.OK, (await host.Client(tokens[b]).PostAsync($"/api/v1/friends/requests/{a}/accept", null)).StatusCode);
        }
        await Befriend("Kim", "Ann");
        await Befriend("Lee", "Bob");
        Assert.Equal(HttpStatusCode.NoContent, (await host.Client(tokens["Cat"]).PostAsJsonAsync("/api/v1/blocks", new { username = "Max" })).StatusCode);
        host.World!.Set([
            FakeWorldRelay.Instance(1, ids["Kim"], "Kim", "dry_land", "dng205b1", "friends", [(ids["Lee"], "Lee", "stag0201")]),
            FakeWorldRelay.Instance(2, ids["Max"], "Max", "death_valley", "stag0101"),
            FakeWorldRelay.Instance(3, ids["Lee"] + 1000, "Ghost", "venom_jungle", "stag0301", "private"),
        ]);

        async Task<List<uint>> Seen(string who, string? area = null)
        {
            var response = await host.Client(tokens[who]).GetAsync("/api/v1/instances" + (area is null ? "" : "?area=" + area));
            Assert.Equal(HttpStatusCode.OK, response.StatusCode);
            return (await response.Json()).GetProperty("instances").EnumerateArray().Select(i => i.GetProperty("wid").GetUInt32()).ToList();
        }
        Assert.Equal([2u, 1u], (await Seen("Kim")).Order().Reverse().ToList()); // his own (Friends) and the public one
        Assert.Equal([1u, 2u], (await Seen("Ann")).Order().ToList());             // a friend of the host
        Assert.Equal([2u], await Seen("Bob"));                                     // a friend of a member only: not enough
        Assert.Equal([], await Seen("Cat"));                                       // she blocks Max, and is no friend of Kim
        Assert.Equal([1u, 2u, 3u], (await Seen("Moddy")).Order().ToList());          // staff see every instance
        Assert.Equal([1u], await Seen("Ann", "dry_land"));
        Assert.Equal("unknown_area", await (await host.Client(tokens["Ann"]).GetAsync("/api/v1/instances?area=moon")).ProblemCode());
        Assert.Equal(HttpStatusCode.Unauthorized, (await host.Client().GetAsync("/api/v1/instances")).StatusCode);
        var staffRow = (await (await host.Client(tokens["Moddy"]).GetAsync("/api/v1/instances?area=venom_jungle")).Json()).GetProperty("instances")[0];
        Assert.Equal("private", staffRow.GetProperty("visibility").GetString());
        var playerRow = (await (await host.Client(tokens["Ann"]).GetAsync("/api/v1/instances?area=dry_land")).Json()).GetProperty("instances")[0];
        Assert.Equal(System.Text.Json.JsonValueKind.Null, playerRow.GetProperty("visibility").ValueKind);
        Assert.Equal("Dry Land: Ancient Ruins", playerRow.GetProperty("stageName").GetString());
        Assert.Equal(2, playerRow.GetProperty("players").GetInt32());

        var world = await (await host.Client().GetAsync("/api/v1/world")).Json();
        Assert.Equal(4, world.GetProperty("players").GetInt32());
        Assert.Equal(3, world.GetProperty("instances").GetInt32());
        Assert.Equal(1, world.GetProperty("areas").GetProperty("venom_jungle").GetInt32());
        Assert.DoesNotContain("Kim", world.GetRawText());

        // The website: signed out counts only; signed in the visible rows; staff with the visibility badge.
        using var anonymous = new WebBrowser(host);
        var counts = await anonymous.GetAsync("/instances");
        Assert.Contains("4 players online, 3 instances", counts.Html);
        Assert.DoesNotContain("Kim", counts.Html);
        using var ann = new WebBrowser(host);
        await ann.LoginAsync("Ann");
        var list = await ann.GetAsync("/instances");
        Assert.Contains("/u/Kim", list.Html);
        Assert.DoesNotContain("Ghost", list.Html);
        using var mod = new WebBrowser(host);
        await mod.LoginAsync("Moddy");
        Assert.Contains("Ghost", (await mod.GetAsync("/instances")).Html);
    }

    [DbFact]
    public async Task Online_prefs_round_trip_and_reach_the_relay_and_the_next_enter()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, id) = await host.RegisterAsync("Prefer");
        var client = host.Client(token);
        var defaults = await (await client.GetAsync("/api/v1/me/online-prefs")).Json();
        Assert.Equal("public", defaults.GetProperty("defaultVisibility").GetString());
        Assert.Equal("friends", defaults.GetProperty("whereabouts").GetString());
        Assert.Equal(1, defaults.GetProperty("lightMode").GetInt32());
        Assert.Equal(1, defaults.GetProperty("lastBox").GetInt32());

        var put = await client.PutJson("/api/v1/me/online-prefs", new
        {
            defaultVisibility = "private",
            cameraMode = 1,
            randomizer = new { enabled = true, seed = 4242, drops = true, enemies = 2, zones = 1 },
        });
        Assert.Equal(HttpStatusCode.OK, put.StatusCode);
        var stored = await put.Json();
        Assert.Equal("private", stored.GetProperty("defaultVisibility").GetString());
        Assert.Equal("anyone", stored.GetProperty("invites").GetString()); // untouched
        Assert.Equal(4242, stored.GetProperty("randomizer").GetProperty("seed").GetInt64());
        Assert.Contains($"prefs {id}", host.World!.Notes);
        await using (var sql = await host.SqlAsync())
            Assert.Equal((4242L, 1 | 2 << 1 | 1 << 3), await sql.QuerySingleAsync<(long, int)>(
                "SELECT CAST(randomizer_seed AS SIGNED), randomizer_modes FROM online_prefs WHERE account_id = @id", new { id }));

        var enter = await (await client.PostAsJsonAsync("/api/v1/world/enter", Enter())).Json();
        Assert.Equal("private", enter.GetProperty("prefs").GetProperty("defaultVisibility").GetString());
        Assert.Equal(1, enter.GetProperty("settings").GetProperty("cameraMode").GetInt32());
        Assert.Equal(2, enter.GetProperty("settings").GetProperty("randomizer").GetProperty("enemies").GetInt32());

        foreach (var bad in new object[] { new { cameraMode = 2 }, new { defaultVisibility = "secret" }, new { colour = "red" }, new { lastBox = 5 },
                     new { randomizer = new { enabled = true, enemies = 3 } } })
            Assert.Equal("invalid_prefs_or_settings", await Code(await client.PutJson("/api/v1/me/online-prefs", bad)));
        var off = await (await client.PutJson("/api/v1/me/online-prefs", new { randomizer = new { enabled = false } })).Json();
        Assert.False(off.GetProperty("randomizer").GetProperty("enabled").GetBoolean());

        // The relay's visibility sink (the host's last choice) is the service's.
        var world = host.App.Services.GetRequiredService<WorldService>();
        await world.RememberVisibilityAsync(id, "friends");
        Assert.Equal("friends", (await (await client.GetAsync("/api/v1/me/online-prefs")).Json()).GetProperty("defaultVisibility").GetString());
    }

    /// <summary>invalid_prefs and the room settings' invalid_settings (the randomizer object) count as one kind here.</summary>
    private static async Task<string> Code(HttpResponseMessage response)
    {
        Assert.Equal(HttpStatusCode.UnprocessableEntity, response.StatusCode);
        var code = await response.ProblemCode();
        return code is "invalid_prefs" or "invalid_settings" ? "invalid_prefs_or_settings" : code;
    }

    [DbFact]
    public async Task Admin_instances_end_and_remove_with_audit_rows()
    {
        await using var host = await TestHost.StartAsync();
        var (_, _, kim) = await host.RegisterAsync("Kim");
        var (_, _, lee) = await host.RegisterAsync("Lee");
        await host.RegisterAsync("Moddy");
        await using (var sql = await host.SqlAsync()) await sql.ExecuteAsync("UPDATE accounts SET role = 'moderator' WHERE username = 'Moddy'");
        var mod = host.Client((await host.LoginAsync("Moddy")).Token);
        var player = host.Client((await host.LoginAsync("Lee")).Token);
        host.World!.Set([FakeWorldRelay.Instance(5, kim, "Kim", "dry_land", "stag0202", "private", [(lee, "Lee", "stag0202")], historyId: 55)]);

        Assert.Equal(HttpStatusCode.Forbidden, (await player.GetAsync("/api/v1/admin/instances")).StatusCode);
        var body = await (await mod.GetAsync("/api/v1/admin/instances")).Json();
        var list = body.GetProperty("instances");
        Assert.Equal(5u, list[0].GetProperty("wid").GetUInt32());
        Assert.Equal(55, list[0].GetProperty("historyId").GetInt64());
        Assert.Equal("private", list[0].GetProperty("visibility").GetString());
        Assert.Equal(2, list[0].GetProperty("members").GetArrayLength());
        // fix-019: the instance is the relay run's wid (wids count from 1 again after a restart): the list names the run, End and
        // Remove need it, and a list from before a restart is refused for the same wid.
        var run = body.GetProperty("run").GetString()!;
        Assert.Equal(host.World.Run, run);
        Assert.Equal(run, list[0].GetProperty("run").GetString());
        var oldRun = "?run=00000000deadbeef";
        var response = await mod.PostAsync("/api/v1/admin/instances/5/end", null);
        Assert.Equal((HttpStatusCode.UnprocessableEntity, "run_required"), (response.StatusCode, await response.ProblemCode()));
        response = await mod.PostAsync("/api/v1/admin/instances/5/end" + oldRun, null);
        Assert.Equal((HttpStatusCode.Conflict, "instance_list_outdated"), (response.StatusCode, await response.ProblemCode()));
        response = await mod.PostAsJsonAsync("/api/v1/admin/instances/5/remove" + oldRun, new { username = "lee" });
        Assert.Equal((HttpStatusCode.Conflict, "instance_list_outdated"), (response.StatusCode, await response.ProblemCode()));
        Assert.Empty(host.World.Ended);
        Assert.Empty(host.World.Removed);

        var q = "?run=" + run;
        Assert.Equal("member_not_found", await (await mod.PostAsJsonAsync("/api/v1/admin/instances/5/remove" + q, new { username = "Nobody" })).ProblemCode());
        Assert.Equal(HttpStatusCode.NoContent, (await mod.PostAsJsonAsync("/api/v1/admin/instances/5/remove" + q, new { username = "lee" })).StatusCode);
        Assert.Equal("instance_not_found", await (await mod.PostAsync("/api/v1/admin/instances/6/end" + q, null)).ProblemCode());
        Assert.Equal(HttpStatusCode.NoContent, (await mod.PostAsync("/api/v1/admin/instances/5/end" + q, null)).StatusCode);
        Assert.Equal((5u, lee), (host.World.Removed.Single().Wid, host.World.Removed.Single().Account));
        Assert.Equal(5u, host.World.Ended.Single().Wid);
        await using var c = await host.SqlAsync();
        var rows = (await c.QueryAsync<(string Event, long Account, string Detail)>(
            "SELECT event, CAST(account_id AS SIGNED), detail FROM audit_log WHERE event LIKE 'staff%' ORDER BY id")).ToList();
        Assert.Equal(["staff_remove", "staff_end_instance"], rows.Select(r => r.Event));
        Assert.Equal([lee, kim], rows.Select(r => r.Account));
        Assert.Contains("\"historyId\":55", rows[1].Detail);
        // Account ids, never names, in the details (a deleted account's name must not stay in other accounts' rows).
        Assert.Contains($"\"hostId\":{kim}", rows[0].Detail);
        Assert.Contains($"\"memberIds\":[{kim},{lee}]", rows[1].Detail);
        Assert.All(rows, r => Assert.DoesNotContain("Kim", r.Detail));
        Assert.All(rows, r => Assert.DoesNotContain("Lee", r.Detail));
        Assert.Equal(2, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM v_staff_audit WHERE staff = 'Moddy'"));
        var listed = (await (await mod.GetAsync("/api/v1/admin/audit?event=staff_end_instance")).Json()).GetProperty("rows");
        Assert.Equal("host Kim; members Kim, Lee", listed[0].GetProperty("accounts").GetString());

        // The audit list (19.19): a moderator his own rows, an admin everybody's; one event.
        await host.RegisterAsync("Chief");
        await c.ExecuteAsync("UPDATE accounts SET role = 'admin' WHERE username = 'Chief'");
        var chief = host.Client((await host.LoginAsync("Chief")).Token);
        await c.ExecuteAsync("INSERT INTO audit_log (at, account_id, actor_id, event) SELECT UTC_TIMESTAMP(3), @kim, id, 'staff_join' FROM accounts WHERE username = 'Chief'", new { kim });
        var mine = (await (await mod.GetAsync("/api/v1/admin/audit")).Json()).GetProperty("rows");
        Assert.Equal(2, mine.GetArrayLength());
        Assert.All(mine.EnumerateArray(), r => Assert.Equal("Moddy", r.GetProperty("staff").GetString()));
        Assert.Equal(3, (await (await chief.GetAsync("/api/v1/admin/audit")).Json()).GetProperty("rows").GetArrayLength());
        var joins = (await (await chief.GetAsync("/api/v1/admin/audit?event=staff_join")).Json()).GetProperty("rows");
        Assert.Equal(("staff_join", "Chief", "Kim"), (joins[0].GetProperty("event").GetString(), joins[0].GetProperty("staff").GetString(), joins[0].GetProperty("player").GetString()));
        Assert.Equal(HttpStatusCode.Forbidden, (await player.GetAsync("/api/v1/admin/audit")).StatusCode);
        using var web = new WebBrowser(host);
        await web.LoginAsync("Chief");
        var page = await web.GetAsync("/admin/audit?Event=staff_end_instance");
        Assert.Contains("staff_end_instance", page.Html);
        Assert.Contains("Moddy", page.Html);
    }

    /// <summary>
    /// Friends' whereabouts (19.20): online in a public instance with its area, "Playing" in a Private one, "In the lobby"
    /// at home, the last time online when offline; only "Online" / "Offline" (no place, no last time) for a friend whose
    /// whereabouts preference is "nobody" (19.11); strangers never see it on profiles.
    /// </summary>
    [DbFact]
    public async Task Friends_see_whereabouts_by_preference_and_played_with()
    {
        await using var host = await TestHost.StartAsync();
        var (meToken, _, me) = await host.RegisterAsync("Viewer");
        var names = new[] { "Roamer", "Hider", "Secret", "Homey", "Sleeper", "Shy" };
        var ids = new Dictionary<string, long>();
        foreach (var name in names)
        {
            var (token, _, id) = await host.RegisterAsync(name);
            ids[name] = id;
            Assert.Equal(HttpStatusCode.OK, (await host.Client(meToken).PostAsJsonAsync("/api/v1/friends/requests", new { username = name })).StatusCode);
            Assert.Equal(HttpStatusCode.OK, (await host.Client(token).PostAsync("/api/v1/friends/requests/Viewer/accept", null)).StatusCode);
        }
        var (_, _, stranger) = await host.RegisterAsync("Stranger");
        foreach (var hidden in new[] { "Hider", "Shy" })
            Assert.Equal(HttpStatusCode.OK, (await host.Client((await host.LoginAsync(hidden)).Token).PutJson("/api/v1/me/online-prefs", new { whereabouts = "nobody" })).StatusCode);
        var store = host.App.Services.GetRequiredService<IWorldRepository>();
        await store.TouchLastSeenAsync([ids["Sleeper"], ids["Shy"]], host.Clock.GetUtcNow().UtcDateTime.AddHours(-3));
        host.World!.Set([
                FakeWorldRelay.Instance(1, ids["Roamer"], "Roamer", "dry_land", "dng205b2"),
                FakeWorldRelay.Instance(2, ids["Hider"], "Hider", "dry_land", "stag0201"),
                FakeWorldRelay.Instance(3, ids["Secret"], "Secret", "machine_pit", "stag0401", "private"),
            ],
            [(ids["Homey"], "Homey", "stagml11")]);

        var friends = (await (await host.Client(meToken).GetAsync("/api/v1/friends")).Json()).GetProperty("friends").EnumerateArray()
            .ToDictionary(f => f.GetProperty("username").GetString()!, f => f.GetProperty("presence"));
        Assert.Equal("Playing in Dry Land", friends["Roamer"].GetProperty("status").GetString());
        Assert.Equal("Dry Land: Ancient Ruins", friends["Roamer"].GetProperty("stage").GetString());
        // "nobody": online only "Online" (Hider plays in a public Dry Land instance), offline "Offline" without the last time.
        Assert.True(friends["Hider"].GetProperty("online").GetBoolean());
        Assert.Equal("Online", friends["Hider"].GetProperty("status").GetString());
        Assert.Equal(System.Text.Json.JsonValueKind.Null, friends["Hider"].GetProperty("area").ValueKind);
        Assert.Equal(System.Text.Json.JsonValueKind.Null, friends["Hider"].GetProperty("stage").ValueKind);
        Assert.False(friends["Shy"].GetProperty("online").GetBoolean());
        Assert.Equal("Offline", friends["Shy"].GetProperty("status").GetString());
        Assert.Equal(System.Text.Json.JsonValueKind.Null, friends["Shy"].GetProperty("lastSeenAt").ValueKind);
        Assert.Equal("Playing", friends["Secret"].GetProperty("status").GetString());
        Assert.Equal(System.Text.Json.JsonValueKind.Null, friends["Secret"].GetProperty("area").ValueKind);
        Assert.Equal("In the lobby", friends["Homey"].GetProperty("status").GetString());
        Assert.False(friends["Sleeper"].GetProperty("online").GetBoolean());
        Assert.NotEqual(System.Text.Json.JsonValueKind.Null, friends["Sleeper"].GetProperty("lastSeenAt").ValueKind);

        using var web = new WebBrowser(host);
        await web.LoginAsync("Viewer");
        var page = await web.GetAsync("/account/friends");
        Assert.Contains("Playing in Dry Land", page.Html);
        Assert.Contains("last online 3 h ago", page.Html);
        Assert.Single(System.Text.RegularExpressions.Regex.Matches(page.Html, "last online 3 h ago"));   // Sleeper's, never Shy's
        Assert.Contains("Online preferences", page.Html);
        Assert.Contains("Playing in Dry Land", (await web.GetAsync("/u/Roamer")).Html);
        using var outsider = new WebBrowser(host);
        await outsider.RegisterAsync("Nosy");
        Assert.DoesNotContain("Playing in Dry Land", (await outsider.GetAsync("/u/Roamer")).Html);

        // "Recently played with": players who shared an instance (spectators never count), deleted accounts left out.
        var history = host.App.Services.GetRequiredService<IInstanceHistoryRepository>();
        var now = host.Clock.GetUtcNow().UtcDateTime;
        var wid = await history.StartAsync(new InstanceHistoryStart(ids["Roamer"], 1, "stag0201", "public", 0, 2, "s", now.AddHours(-1)));
        await history.MemberJoinedAsync(wid, ids["Roamer"], 1, "host", now.AddHours(-1));
        await history.MemberJoinedAsync(wid, me, 1, "guest", now.AddMinutes(-50));
        await history.MemberJoinedAsync(wid, stranger, 1, "spectator", now.AddMinutes(-40));
        await history.MemberLeftAsync(wid, me, now.AddMinutes(-50), "home", now.AddMinutes(-20), sharedStory: false);
        await history.EndAsync(wid, "host_left", 2, now.AddMinutes(-10));
        var played = await host.App.Services.GetRequiredService<WorldService>().PlayedWithAsync(me, 10);
        Assert.Equal(["Roamer"], played.Select(p => p.Username));
        Assert.Contains("Recently played with", (await web.GetAsync("/account/friends")).Html);

        // The housekeeping's start: rows a previous process left open end with server_stop.
        var open = await history.StartAsync(new InstanceHistoryStart(me, 1, "stag0101", "friends", 1, 3, "s", now));
        await history.MemberJoinedAsync(open, me, 1, "host", now);
        Assert.Equal(1, await host.App.Services.GetRequiredService<WorldService>().CloseLeftoversAsync());
        await using var c = await host.SqlAsync();
        Assert.Equal(("server_stop", "ended"), await c.QuerySingleAsync<(string, string)>(
            "SELECT h.end_reason, m.leave_reason FROM instance_history h JOIN instance_history_members m ON m.instance_id = h.id WHERE h.id = @open", new { open }));
        Assert.Equal(1, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM v_instance_history WHERE host = 'Roamer' AND players = 'Roamer, Viewer'"));
        host.Clock.Advance(TimeSpan.FromDays(91));
        Assert.Equal(2, await host.App.Services.GetRequiredService<WorldService>().PruneHistoryAsync());
        Assert.Equal(0, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM instance_history_members"));
    }

    /// <summary>
    /// The relay's history keys (review of I1a): its seat time and the join event's time are two DateTime.UtcNow reads with
    /// sub-millisecond ticks; the leave names the seat time. Every leave closes its own row (reason and time), a member that
    /// comes back gets a second row, and a repeated leave of the older seat never closes the newer one.
    /// </summary>
    [DbFact]
    public async Task Member_left_closes_the_row_with_the_relays_clock_readings()
    {
        await using var host = await TestHost.StartAsync();
        var (_, _, hostId) = await host.RegisterAsync("Keeper");
        var (_, _, guest) = await host.RegisterAsync("Visitor");
        var history = host.App.Services.GetRequiredService<IInstanceHistoryRepository>();
        static DateTime At(int min, int sec, int ms) => new(2026, 11, 6, 12, min, sec, ms, DateTimeKind.Utc);
        var t0 = At(0, 0, 123).AddTicks(4_567);                                                // 12:00:00.1234567
        var wid = await history.StartAsync(new InstanceHistoryStart(hostId, 1, "stag0101", "public", 0, 1, "s", t0));
        await history.MemberJoinedAsync(wid, hostId, 1, "host", t0.AddTicks(3_141));          // stored 12:00:00.123

        var seat1 = t0.AddTicks(52_000);                                                       // Seat.SinceUtc 12:00:00.1286567
        await history.MemberJoinedAsync(wid, guest, 1, "guest", seat1.AddTicks(9_876));        // HistoryJoined.At: stored .129
        await history.MemberLeftAsync(wid, guest, seat1, "home", At(5, 0, 123).AddTicks(4_321), sharedStory: false);

        var seat2 = At(10, 0, 580).AddTicks(1_568);
        await history.MemberJoinedAsync(wid, guest, 1, "guest", seat2.AddTicks(890));          // the member comes back: a new row
        await history.MemberLeftAsync(wid, guest, seat1, "lost", At(11, 0, 0), sharedStory: false);   // the older seat once more
        await history.MemberLeftAsync(wid, guest, seat2, "removed", At(12, 0, 123).AddTicks(7), sharedStory: false);
        await history.MemberLeftAsync(wid, hostId, t0.AddTicks(3_000), "left", At(13, 0, 0), sharedStory: false);

        await using var c = await host.SqlAsync();
        var rows = (await c.QueryAsync<(long Account, DateTime Joined, DateTime? Left, string? Reason)>(
            "SELECT CAST(account_id AS SIGNED), joined_at, left_at, leave_reason FROM instance_history_members WHERE instance_id = @wid " +
            "ORDER BY joined_at", new { wid })).ToList();
        static DateTime? U(DateTime? t) => t is { } v ? DateTime.SpecifyKind(v, DateTimeKind.Utc) : null;
        Assert.Equal(
            [
                (hostId, At(0, 0, 123), (DateTime?)At(13, 0, 0), "left"),
                (guest, At(0, 0, 129), (DateTime?)At(5, 0, 123), "home"),
                (guest, At(10, 0, 580), (DateTime?)At(12, 0, 123), "removed"),
            ],
            rows.Select(r => (r.Account, U(r.Joined)!.Value, U(r.Left), r.Reason)).ToList());

        // Nothing stays open for the instance's end to call 'ended'.
        await history.EndAsync(wid, "host_left", 2, At(14, 0, 0));
        Assert.Equal(0, await c.ExecuteScalarAsync<int>(
            "SELECT COUNT(*) FROM instance_history_members WHERE instance_id = @wid AND leave_reason = 'ended'", new { wid }));
    }

    /// <summary>A story reset or a staff restore tells the relay (StoryReplaced) after it is committed.</summary>
    [DbFact]
    public async Task Story_reset_and_restore_tell_the_relay()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, id) = await host.RegisterAsync("Resetter");
        var client = host.Client(token);
        var record = Convert.ToBase64String(DW4Online.Domain.Save.RecordCodec.Encode(Samples.Fresh()));
        Assert.Equal(HttpStatusCode.Created, (await client.PutJson("/api/v1/online-character",
            new { record, playSeconds = 60, context = new { protocol = P } }, create: true)).StatusCode);
        Assert.Equal(HttpStatusCode.NoContent, (await client.PostAsJsonAsync("/api/v1/online-character/reset", new { currentPassword = "a decent password" })).StatusCode);
        Assert.Contains($"story {id}/1", host.World!.Notes);
    }
}
