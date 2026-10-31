using System.Net;
using System.Net.Http.Json;
using Dapper;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Security;
using DW4Online.Domain.Services;
using Microsoft.Extensions.DependencyInjection;

namespace DW4Online.Integration.Tests;

public class AccountTests
{
    [DbFact]
    public async Task Register_login_me_and_logout()
    {
        await using var host = await TestHost.StartAsync();
        var (token, recovery, id) = await host.RegisterAsync("Kevin");
        Assert.Matches("^[0-9A-Z]{4}(-[0-9A-Z]{4}){4}$", recovery);

        var me = await (await host.Client(token).GetAsync("/api/v1/me")).Json();
        Assert.Equal("Kevin", me.GetProperty("username").GetString());
        Assert.Equal("player", me.GetProperty("role").GetString());
        Assert.Equal(id, me.GetProperty("id").GetInt64());

        var dup = await host.Client().PostAsJsonAsync("/api/v1/auth/register", new { username = "kevin", password = "another password" });
        Assert.Equal(HttpStatusCode.Conflict, dup.StatusCode);
        Assert.Equal("username_taken", await dup.ProblemCode());
        var weak = await host.Client().PostAsJsonAsync("/api/v1/auth/register", new { username = "Other", password = "short" });
        Assert.Equal(HttpStatusCode.UnprocessableEntity, weak.StatusCode);
        Assert.Equal("weak_password", await weak.ProblemCode());
        var badName = await host.Client().PostAsJsonAsync("/api/v1/auth/register", new { username = "x", password = "a decent password" });
        Assert.Equal("invalid_username", await badName.ProblemCode());
        var bot = await host.Client().PostAsJsonAsync("/api/v1/auth/register", new { username = "Botty", password = "a decent password", website = "http://spam" });
        Assert.Equal(HttpStatusCode.BadRequest, bot.StatusCode);

        var login = await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "KEVIN", password = "a decent password", deviceName = "PC" });
        Assert.Equal(HttpStatusCode.OK, login.StatusCode);
        var second = (await login.Json()).GetProperty("token").GetString()!;
        var sessions = await (await host.Client(second).GetAsync("/api/v1/me/sessions")).Json();
        Assert.Equal(2, sessions.GetArrayLength());

        Assert.Equal(HttpStatusCode.NoContent, (await host.Client(second).PostAsync("/api/v1/auth/logout", null)).StatusCode);
        var after = await host.Client(second).GetAsync("/api/v1/me");
        Assert.Equal(HttpStatusCode.Unauthorized, after.StatusCode);
        Assert.Equal("unauthorized", await after.ProblemCode());
        Assert.Equal(HttpStatusCode.OK, (await host.Client(token).GetAsync("/api/v1/me")).StatusCode);

        var wrong = await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "Kevin", password = "not the password" });
        Assert.Equal(HttpStatusCode.Unauthorized, wrong.StatusCode);
        Assert.Equal("bad_credentials", await wrong.ProblemCode());
        var unknown = await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "nobody", password = "not the password" });
        Assert.Equal("bad_credentials", await unknown.ProblemCode());
    }

    [DbFact]
    public async Task Unknown_user_names_are_logged_only_as_a_keyed_hash()
    {
        await using var host = await TestHost.StartAsync();
        var names = host.App.Services.GetRequiredService<AuditNameHasher>();
        // Someone types the password into the name field; someone tries a name that is registered later.
        await host.Client(ip: "203.0.113.7").PostAsJsonAsync("/api/v1/auth/login", new { username = "MySecretPassw0rd", password = "x" });
        await host.Client(ip: "203.0.113.8").PostAsJsonAsync("/api/v1/auth/login", new { username = " LaterUser ", password = "x" });

        await using var sql = await host.SqlAsync();
        var rows = (await sql.QueryAsync<(string Detail, byte[] Ip)>("SELECT detail, ip FROM audit_log WHERE event = 'login_unknown' ORDER BY id")).ToList();
        Assert.Equal(2, rows.Count);
        Assert.DoesNotContain("MySecretPassw0rd", rows[0].Detail, StringComparison.OrdinalIgnoreCase);
        Assert.Contains(names.Tag("MySecretPassw0rd")!, rows[0].Detail);
        Assert.Contains(names.Tag("laterUSER")!, rows[1].Detail); // case and spaces do not matter
        Assert.Matches("^[0-9a-f]{32}$", names.Tag("x")!);
        Assert.NotNull(rows[1].Ip);

        // Deleting the account clears the attempts on its name (tag and IP); other rows stay.
        var (token, _, _) = await host.RegisterAsync("LaterUser");
        Assert.Equal(HttpStatusCode.NoContent,
            (await host.Client(token).PostAsJsonAsync("/api/v1/me/delete", new { currentPassword = "a decent password" })).StatusCode);
        rows = (await sql.QueryAsync<(string Detail, byte[] Ip)>("SELECT detail, ip FROM audit_log WHERE event = 'login_unknown' ORDER BY id")).ToList();
        Assert.Contains(names.Tag("MySecretPassw0rd")!, rows[0].Detail);
        Assert.NotNull(rows[0].Ip);
        Assert.Null(rows[1].Detail);
        Assert.Null(rows[1].Ip);
    }

    [DbFact]
    public async Task Lockout_after_failed_logins_expires()
    {
        await using var host = await TestHost.StartAsync(new() { ["Accounts:MaxFailedLogins"] = "3", ["Accounts:LockoutMinutes"] = "15" });
        await host.RegisterAsync("Locky");
        for (var i = 0; i < 3; ++i)
            await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "Locky", password = "wrong password!" });
        var locked = await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "Locky", password = "a decent password" });
        Assert.Equal(HttpStatusCode.Locked, locked.StatusCode);
        var problem = await locked.Json();
        Assert.Equal("account_locked_temporarily", problem.GetProperty("code").GetString());
        Assert.InRange(problem.GetProperty("retryAfterSeconds").GetInt32(), 899, 900);
        host.Clock.Advance(TimeSpan.FromMinutes(16));
        var ok = await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "Locky", password = "a decent password" });
        Assert.Equal(HttpStatusCode.OK, ok.StatusCode);
    }

    private static Task<HttpResponseMessage> Login(TestHost host, string ip, string username, string password) =>
        host.Client(ip: ip).PostAsJsonAsync("/api/v1/auth/login", new { username, password });

    /// <summary>A stranger who knows a name (room list, profiles) locks out only their own address, never the owner.</summary>
    [DbFact]
    public async Task Strangers_lock_out_only_their_own_address()
    {
        await using var host = await TestHost.StartAsync(new() { ["Accounts:MaxFailedLogins"] = "3" });
        await host.RegisterAsync("Famous");
        for (var i = 0; i < 3; ++i)
            Assert.Equal(HttpStatusCode.Unauthorized, (await Login(host, "198.51.100.66", "Famous", "guess " + i)).StatusCode);
        // The stranger's address is refused, even with the right password; no password check happens.
        Assert.Equal(HttpStatusCode.Locked, (await Login(host, "198.51.100.66", "Famous", "a decent password")).StatusCode);
        // The owner, from anywhere else, logs in as usual.
        Assert.Equal(HttpStatusCode.OK, (await Login(host, "203.0.113.20", "Famous", "a decent password")).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await Login(host, "2001:db8:5::1", "Famous", "a decent password")).StatusCode);

        // A name without an account behaves the same (the answer does not reveal whether it exists).
        for (var i = 0; i < 3; ++i)
            Assert.Equal(HttpStatusCode.Unauthorized, (await Login(host, "198.51.100.66", "Nobody", "guess " + i)).StatusCode);
        Assert.Equal(HttpStatusCode.Locked, (await Login(host, "198.51.100.66", "Nobody", "guess 4")).StatusCode);

        // The recovery form counts on the same pair.
        Assert.Equal("account_locked_temporarily", await (await host.Client(ip: "198.51.100.66").PostAsJsonAsync("/api/v1/auth/recover",
            new { username = "Famous", recoveryCode = "AAAA-AAAA-AAAA-AAAA-AAAA", newPassword = "another decent password" })).ProblemCode());
    }

    /// <summary>Guessing from many addresses locks the account, but its owner still gets in from an address used before.</summary>
    [DbFact]
    public async Task Guessing_from_many_addresses_locks_the_account_except_for_known_addresses()
    {
        await using var host = await TestHost.StartAsync(new() { ["Accounts:MaxFailedLogins"] = "3", ["Accounts:MaxFailedLoginsPerAccount"] = "6" });
        await host.RegisterAsync("Target"); // from the default test address 192.0.2.1: a known address now
        Assert.Equal(HttpStatusCode.OK, (await Login(host, "2001:db8:77:1::5", "Target", "a decent password")).StatusCode);
        foreach (var ip in new[] { "198.51.100.1", "198.51.100.2", "198.51.100.3" })
            for (var i = 0; i < 2; ++i)
                Assert.Equal(HttpStatusCode.Unauthorized, (await Login(host, ip, "Target", "guess " + i)).StatusCode);

        // Six failures from three addresses: the account is locked for new addresses, even with the right password ...
        Assert.Equal(HttpStatusCode.Locked, (await Login(host, "198.51.100.200", "Target", "a decent password")).StatusCode);
        // ... while the owner's known addresses (IPv6: the same /64 network) still work.
        Assert.Equal(HttpStatusCode.OK, (await Login(host, "2001:db8:77:1::abcd", "Target", "a decent password")).StatusCode);
        // That login ended the account-wide lock.
        Assert.Equal(HttpStatusCode.OK, (await Login(host, "198.51.100.200", "Target", "a decent password")).StatusCode);

        foreach (var ip in new[] { "198.51.100.4", "198.51.100.5", "198.51.100.6" })
            for (var i = 0; i < 2; ++i)
                await Login(host, ip, "Target", "guess " + i);
        Assert.Equal(HttpStatusCode.Locked, (await Login(host, "198.51.100.201", "Target", "a decent password")).StatusCode);
        host.Clock.Advance(TimeSpan.FromMinutes(16));
        Assert.Equal(HttpStatusCode.OK, (await Login(host, "198.51.100.201", "Target", "a decent password")).StatusCode);
        await using var sql = await host.SqlAsync();
        Assert.Equal(2, await Dapper.SqlMapper.ExecuteScalarAsync<int>(sql, "SELECT COUNT(*) FROM audit_log WHERE event = 'login_fail_locked'"));
    }

    /// <summary>
    /// One stranger guessing slowly from one address (each time until that address is refused, then again after the
    /// window) never adds up to the account-wide lock: the owner logs in from a new address as usual.
    /// </summary>
    [DbFact]
    public async Task Slow_guessing_from_one_address_never_locks_the_account()
    {
        await using var host = await TestHost.StartAsync(new() { ["Accounts:MaxFailedLogins"] = "3", ["Accounts:MaxFailedLoginsPerAccount"] = "6" });
        await host.RegisterAsync("Patient");
        for (var round = 0; round < 4; ++round) // 12 failures in an hour, never 6 within 15 minutes
        {
            for (var i = 0; i < 3; ++i)
                Assert.Equal(HttpStatusCode.Unauthorized, (await Login(host, "198.51.100.77", "Patient", "guess " + i)).StatusCode);
            Assert.Equal(HttpStatusCode.Locked, (await Login(host, "198.51.100.77", "Patient", "guess 3")).StatusCode);
            host.Clock.Advance(TimeSpan.FromMinutes(16));
        }
        Assert.Equal(HttpStatusCode.OK, (await Login(host, "203.0.113.99", "Patient", "a decent password")).StatusCode);
        await using var sql = await host.SqlAsync();
        Assert.Equal(0, await Dapper.SqlMapper.ExecuteScalarAsync<int>(sql, "SELECT COUNT(*) FROM audit_log WHERE event = 'login_fail_locked'"));
        Assert.Equal(12, await Dapper.SqlMapper.ExecuteScalarAsync<int>(sql, "SELECT COUNT(*) FROM audit_log WHERE event = 'login_fail'"));
    }

    /// <summary>During an account-wide lock a wrong password from the owner's known address does not end the lock.</summary>
    [DbFact]
    public async Task A_known_address_typo_does_not_end_the_account_lock()
    {
        await using var host = await TestHost.StartAsync(new() { ["Accounts:MaxFailedLogins"] = "3", ["Accounts:MaxFailedLoginsPerAccount"] = "6" });
        await host.RegisterAsync("Typo"); // from the default test address 192.0.2.1: known
        foreach (var ip in new[] { "198.51.100.11", "198.51.100.12", "198.51.100.13" })
            for (var i = 0; i < 2; ++i)
                await Login(host, ip, "Typo", "guess " + i);
        Assert.Equal(HttpStatusCode.Locked, (await Login(host, "198.51.100.200", "Typo", "a decent password")).StatusCode);
        Assert.Equal(HttpStatusCode.Unauthorized, (await Login(host, "192.0.2.1", "Typo", "a decent passwort")).StatusCode);
        Assert.Equal(HttpStatusCode.Locked, (await Login(host, "198.51.100.200", "Typo", "a decent password")).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await Login(host, "192.0.2.1", "Typo", "a decent password")).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await Login(host, "198.51.100.200", "Typo", "a decent password")).StatusCode);
    }

    /// <summary>
    /// The review's probe (8 spellings x 9 wrong passwords from one address locked the account): spellings that the
    /// database collation folds into the owner's name ("álice", "ａlice") are not looked up at all, and the ones that are
    /// the name share its one count. One address never adds up to the account-wide lock.
    /// </summary>
    [DbFact]
    public async Task Spellings_the_collation_folds_do_not_multiply_the_attempts()
    {
        await using var host = await TestHost.StartAsync(new() { ["Accounts:MaxFailedLogins"] = "3", ["Accounts:MaxFailedLoginsPerAccount"] = "6" });
        await host.RegisterAsync("alice");
        // The right password under a folded spelling is no login.
        var probe = 40;
        foreach (var folded in new[] { "álice", "ａlice", "ＡＬＩＣＥ", "alicé", "alice​" })
            Assert.Equal(HttpStatusCode.Unauthorized, (await Login(host, "203.0.113." + probe++, folded, "a decent password")).StatusCode);

        string[] spellings = ["álice", "ａlice", "ÁLICE", "alicé", "ａｌｉｃｅ", "ALICE", " alice ", "Alice"];
        foreach (var spelling in spellings)
            for (var i = 0; i < 9; ++i)
                Assert.Contains((await Login(host, "203.0.113.5", spelling, "guess " + i)).StatusCode, new[] { HttpStatusCode.Unauthorized, HttpStatusCode.Locked });
        // The owner logs in from a new address as usual; only the real name's 3 failures reached the account.
        Assert.Equal(HttpStatusCode.OK, (await Login(host, "198.51.100.9", "alice", "a decent password")).StatusCode);
        await using var sql = await host.SqlAsync();
        Assert.Equal(3, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM audit_log WHERE event = 'login_fail'"));
        Assert.Equal(0, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM audit_log WHERE event = 'login_fail_locked'"));
    }

    /// <summary>
    /// One IPv6 connection has many /64 networks (a home /56 has 256): each counts on its own for the name, but together
    /// they add at most MaxFailedLogins to the account (one /48), so they cannot lock the owner out. Guessing from
    /// several connections still locks the account.
    /// </summary>
    [DbFact]
    public async Task The_networks_of_one_ipv6_connection_do_not_lock_the_account()
    {
        await using var host = await TestHost.StartAsync(new() { ["Accounts:MaxFailedLogins"] = "3", ["Accounts:MaxFailedLoginsPerAccount"] = "6" });
        await host.RegisterAsync("Homer");
        for (var net = 0x20; net <= 0x24; ++net) // the review's 5 x 10 from 2001:db8:bb:20..24::1, here 5 x 3
            for (var i = 0; i < 3; ++i)
                Assert.Equal(HttpStatusCode.Unauthorized, (await Login(host, $"2001:db8:bb:{net:x}::1", "Homer", "guess " + i)).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await Login(host, "2001:db8:cc:1::1", "Homer", "a decent password")).StatusCode);

        // Another connection adds its own share; more /64 networks of it add nothing more within the window.
        for (var i = 0; i < 3; ++i)
            Assert.Equal(HttpStatusCode.Unauthorized, (await Login(host, "2001:db8:dd:1::1", "Homer", "guess " + i)).StatusCode);
        for (var i = 0; i < 3; ++i)
            Assert.Equal(HttpStatusCode.Unauthorized, (await Login(host, "2001:db8:dd:2::1", "Homer", "guess " + i)).StatusCode);
        // Had the second /64 counted, 3 + 3 would have locked the account now. Separate addresses still add up.
        Assert.Equal(HttpStatusCode.OK, (await Login(host, "198.51.100.30", "Homer", "a decent password")).StatusCode);
        for (var i = 0; i < 3; ++i)
            await Login(host, "198.51.100.31", "Homer", "guess " + i);
        for (var i = 0; i < 3; ++i)
            await Login(host, "198.51.100.32", "Homer", "guess " + i);
        Assert.Equal(HttpStatusCode.Locked, (await Login(host, "198.51.100.33", "Homer", "a decent password")).StatusCode);
    }

    /// <summary>
    /// A game token used from today's address makes that address known: during an account-wide lock its owner logs in
    /// from there (the review: only the address a session was created from counted).
    /// </summary>
    [DbFact]
    public async Task An_address_a_session_was_last_used_from_counts_as_known()
    {
        await using var host = await TestHost.StartAsync(new() { ["Accounts:MaxFailedLogins"] = "3", ["Accounts:MaxFailedLoginsPerAccount"] = "6" });
        var (token, _, id) = await host.RegisterAsync("Roamer"); // created from 192.0.2.1
        Assert.Equal(HttpStatusCode.OK, (await host.Client(token, ip: "203.0.113.77").GetAsync("/api/v1/me")).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await host.Client(token, ip: "2001:db8:aa:5::77").GetAsync("/api/v1/me")).StatusCode);
        await using var sql = await host.SqlAsync();
        Assert.Equal(System.Net.IPAddress.Parse("2001:db8:aa:5::77").GetAddressBytes(),
            await sql.ExecuteScalarAsync<byte[]>("SELECT last_used_ip FROM sessions WHERE account_id = @id", new { id }));

        foreach (var ip in new[] { "198.51.100.21", "198.51.100.22", "198.51.100.23" })
            for (var i = 0; i < 2; ++i)
                await Login(host, ip, "Roamer", "guess " + i);
        Assert.Equal(HttpStatusCode.Locked, (await Login(host, "198.51.100.200", "Roamer", "a decent password")).StatusCode);
        Assert.Equal(HttpStatusCode.Locked, (await Login(host, "203.0.113.77", "Roamer", "a decent password")).StatusCode); // no longer its last
        Assert.Equal(HttpStatusCode.OK, (await Login(host, "2001:db8:aa:5::1234", "Roamer", "a decent password")).StatusCode); // same /64
    }

    /// <summary>
    /// The review's memory probe sent 60,000-character names: no typed name is kept (they share the address's "?"
    /// count), and the per-address rate limits treat an IPv6 /64 as one client.
    /// </summary>
    [DbFact]
    public async Task Long_typed_names_are_not_kept_and_ipv6_rate_limits_count_by_network()
    {
        await using (var host = await TestHost.StartAsync(new() { ["Accounts:MaxFailedLogins"] = "3" }))
        {
            var throttle = host.App.Services.GetRequiredService<DW4Online.Domain.Accounts.LoginThrottle>();
            var before = throttle.EntryCount;
            var answers = new List<HttpStatusCode>();
            for (var i = 0; i < 5; ++i)
                answers.Add((await Login(host, "203.0.113.60", new string('x', 60_000) + i, "whatever pass")).StatusCode);
            Assert.Equal([HttpStatusCode.Unauthorized, HttpStatusCode.Unauthorized, HttpStatusCode.Unauthorized, HttpStatusCode.Locked, HttpStatusCode.Locked], answers);
            Assert.Equal(before + 1, throttle.EntryCount);
            await using var sql = await host.SqlAsync();
            Assert.True(await sql.ExecuteScalarAsync<int>("SELECT MAX(LENGTH(detail)) FROM audit_log WHERE event = 'login_unknown'") < 100);
        }

        await using var limited = await TestHost.StartAsync(new() { ["RateLimits:LoginPerMinute"] = "2" });
        Task<HttpResponseMessage> Try(string ip) => Login(limited, ip, "nobody", "whatever pass");
        Assert.Equal(HttpStatusCode.Unauthorized, (await Try("2001:db8:ee:1::1")).StatusCode);
        Assert.Equal(HttpStatusCode.Unauthorized, (await Try("2001:db8:ee:1::2")).StatusCode);
        Assert.Equal(HttpStatusCode.TooManyRequests, (await Try("2001:db8:ee:1:ffff::3")).StatusCode); // the same /64
        Assert.Equal(HttpStatusCode.Unauthorized, (await Try("2001:db8:ee:2::1")).StatusCode);         // another /64
    }

    [DbFact]
    public async Task Login_rate_limit_answers_429()
    {
        await using var host = await TestHost.StartAsync(new() { ["RateLimits:LoginPerMinute"] = "2" });
        HttpResponseMessage last = null!;
        for (var i = 0; i < 3; ++i)
            last = await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "nobody", password = "whatever pass" });
        Assert.Equal(HttpStatusCode.TooManyRequests, last.StatusCode);
        Assert.Equal("rate_limited", await last.ProblemCode());
        Assert.True(last.Headers.RetryAfter is not null);
    }

    [DbFact]
    public async Task Password_change_and_logout_everywhere()
    {
        await using var host = await TestHost.StartAsync();
        var (first, _, _) = await host.RegisterAsync("Changer");
        var second = (await (await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "Changer", password = "a decent password" })).Json())
            .GetProperty("token").GetString()!;

        var wrong = await host.Client(first).PostAsJsonAsync("/api/v1/auth/password", new { currentPassword = "nope nope nope", newPassword = "brand new password" });
        Assert.Equal("wrong_password", await wrong.ProblemCode());
        var change = await host.Client(first).PostAsJsonAsync("/api/v1/auth/password", new { currentPassword = "a decent password", newPassword = "brand new password" });
        Assert.Equal(HttpStatusCode.NoContent, change.StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await host.Client(first).GetAsync("/api/v1/me")).StatusCode);          // this session stays
        Assert.Equal(HttpStatusCode.Unauthorized, (await host.Client(second).GetAsync("/api/v1/me")).StatusCode); // others end
        Assert.Equal(HttpStatusCode.Unauthorized,
            (await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "Changer", password = "a decent password" })).StatusCode);

        var third = (await (await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "Changer", password = "brand new password" })).Json())
            .GetProperty("token").GetString()!;
        var all = await (await host.Client(third).PostAsync("/api/v1/auth/logout-all", null)).Json();
        Assert.Equal(2, all.GetProperty("revoked").GetInt32());
        Assert.Equal(HttpStatusCode.Unauthorized, (await host.Client(first).GetAsync("/api/v1/me")).StatusCode);
        Assert.Equal(HttpStatusCode.Unauthorized, (await host.Client(third).GetAsync("/api/v1/me")).StatusCode);
    }

    [DbFact]
    public async Task Recovery_code_is_one_time()
    {
        await using var host = await TestHost.StartAsync();
        var (token, code, _) = await host.RegisterAsync("Forgetful");
        var bad = await host.Client().PostAsJsonAsync("/api/v1/auth/recover", new { username = "Forgetful", recoveryCode = "0000-0000-0000-0000-0000", newPassword = "fresh password 1" });
        Assert.Equal("bad_recovery_code", await bad.ProblemCode());
        var ok = await host.Client().PostAsJsonAsync("/api/v1/auth/recover", new { username = "forgetful", recoveryCode = code.ToLowerInvariant(), newPassword = "fresh password 1" });
        Assert.Equal(HttpStatusCode.OK, ok.StatusCode);
        var next = (await ok.Json()).GetProperty("recoveryCode").GetString()!;
        Assert.NotEqual(code, next);
        Assert.Equal(HttpStatusCode.Unauthorized, (await host.Client(token).GetAsync("/api/v1/me")).StatusCode); // sessions ended
        var reuse = await host.Client().PostAsJsonAsync("/api/v1/auth/recover", new { username = "Forgetful", recoveryCode = code, newPassword = "fresh password 2" });
        Assert.Equal("bad_recovery_code", await reuse.ProblemCode());
        Assert.Equal(HttpStatusCode.OK,
            (await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "Forgetful", password = "fresh password 1" })).StatusCode);
        Assert.Equal(HttpStatusCode.OK,
            (await host.Client().PostAsJsonAsync("/api/v1/auth/recover", new { username = "Forgetful", recoveryCode = next, newPassword = "fresh password 3" })).StatusCode);
    }

    [DbFact]
    public async Task Sessions_slide_and_expire()
    {
        await using var host = await TestHost.StartAsync(new() { ["Accounts:GameSessionDays"] = "30" });
        var (token, _, _) = await host.RegisterAsync("Slider");
        host.Clock.Advance(TimeSpan.FromDays(29));
        Assert.Equal(HttpStatusCode.OK, (await host.Client(token).GetAsync("/api/v1/me")).StatusCode); // extends to day 59
        host.Clock.Advance(TimeSpan.FromDays(29));
        Assert.Equal(HttpStatusCode.OK, (await host.Client(token).GetAsync("/api/v1/me")).StatusCode);
        host.Clock.Advance(TimeSpan.FromDays(31));
        Assert.Equal(HttpStatusCode.Unauthorized, (await host.Client(token).GetAsync("/api/v1/me")).StatusCode);
    }

    [DbFact]
    public async Task Staff_reset_ban_and_roles()
    {
        await using var host = await TestHost.StartAsync();
        var (adminToken, _, _) = await host.RegisterAsync("Boss");
        var (playerToken, _, _) = await host.RegisterAsync("Player1");
        var accounts = host.App.Services.GetRequiredService<AccountService>();
        Assert.True((await accounts.AdminSetRoleAsync(null, "Boss", "admin", new CallerInfo(null, "tests"))).Ok);

        Assert.Equal(HttpStatusCode.Forbidden, (await host.Client(playerToken).GetAsync("/api/v1/admin/accounts")).StatusCode);
        var list = await (await host.Client(adminToken).GetAsync("/api/v1/admin/accounts?query=play")).Json();
        Assert.Equal(1, list.GetArrayLength());

        var reset = await (await host.Client(adminToken).PostAsync("/api/v1/admin/accounts/Player1/reset-password", null)).Json();
        var temporary = reset.GetProperty("temporaryPassword").GetString()!;
        Assert.Equal(HttpStatusCode.Unauthorized, (await host.Client(playerToken).GetAsync("/api/v1/me")).StatusCode);
        var login = await (await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "Player1", password = temporary })).Json();
        Assert.True(login.GetProperty("account").GetProperty("mustChangePassword").GetBoolean());
        var fresh = login.GetProperty("token").GetString()!;
        var blocked = await host.Client(fresh).GetAsync("/api/v1/online-character");
        Assert.Equal("password_change_required", await blocked.ProblemCode());
        Assert.Equal(HttpStatusCode.NoContent,
            (await host.Client(fresh).PostAsJsonAsync("/api/v1/auth/password", new { currentPassword = temporary, newPassword = "my own password" })).StatusCode);
        Assert.Equal(HttpStatusCode.NotFound, (await host.Client(fresh).GetAsync("/api/v1/online-character")).StatusCode);

        var ban = await host.Client(adminToken).PostAsJsonAsync("/api/v1/admin/accounts/Player1/status", new { status = "banned", reason = "test" });
        Assert.Equal(HttpStatusCode.OK, ban.StatusCode);
        Assert.Equal(HttpStatusCode.Unauthorized, (await host.Client(fresh).GetAsync("/api/v1/me")).StatusCode);
        var bannedLogin = await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "Player1", password = "my own password" });
        Assert.Equal("account_banned", await bannedLogin.ProblemCode());
        var stranger = await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "Player1", password = "a wrong guess" });
        Assert.Equal("bad_credentials", await stranger.ProblemCode()); // the ban is not revealed without the password
    }

    /// <summary>The review's cases: moderators can neither set 'deleted' nor reset passwords (account takeover).</summary>
    [DbFact]
    public async Task Moderators_cannot_delete_by_status_or_reset_passwords()
    {
        await using var host = await TestHost.StartAsync();
        var (modToken, _, modId) = await host.RegisterAsync("ModA");
        await host.RegisterAsync("ModB");
        var (_, _, victimId) = await host.RegisterAsync("Victim");
        var (adminToken, _, _) = await host.RegisterAsync("Chief");
        var accounts = host.App.Services.GetRequiredService<AccountService>();
        Assert.True((await accounts.AdminSetRoleAsync(null, "ModA", "moderator", new CallerInfo(null, "tests"))).Ok);
        Assert.True((await accounts.AdminSetRoleAsync(null, "ModB", "moderator", new CallerInfo(null, "tests"))).Ok);
        Assert.True((await accounts.AdminSetRoleAsync(null, "Chief", "admin", new CallerInfo(null, "tests"))).Ok);
        var mod = host.Client(modToken);

        var deleted = await mod.PostAsJsonAsync("/api/v1/admin/accounts/Victim/status", new { status = "deleted" });
        Assert.Equal("invalid_status", await deleted.ProblemCode());
        Assert.Equal(HttpStatusCode.Forbidden, (await mod.PostAsync("/api/v1/admin/accounts/Victim/reset-password", null)).StatusCode);
        Assert.Equal(HttpStatusCode.Forbidden, (await mod.PostAsync("/api/v1/admin/accounts/ModB/reset-password", null)).StatusCode);
        // The service refuses too (the website's path).
        var modSession = new AuthenticatedSession { AccountId = modId, Role = "moderator", Username = "ModA" };
        Assert.Equal("forbidden", (await accounts.AdminResetPasswordAsync(modSession, "Victim", new CallerInfo(null, "tests"))).Error!.Code);
        // Locking still works for moderators, and an admin can still reset and delete.
        Assert.Equal(HttpStatusCode.OK, (await mod.PostAsJsonAsync("/api/v1/admin/accounts/Victim/status", new { status = "locked" })).StatusCode);
        var admin = host.Client(adminToken);
        Assert.Equal(HttpStatusCode.OK, (await admin.PostAsync("/api/v1/admin/accounts/Victim/reset-password", null)).StatusCode);
        Assert.Equal("invalid_status", await (await admin.PostAsJsonAsync("/api/v1/admin/accounts/Victim/status", new { status = "deleted" })).ProblemCode());
        Assert.Equal(HttpStatusCode.NoContent, (await admin.DeleteAsync("/api/v1/admin/accounts/Victim")).StatusCode);
        Assert.Equal("account_not_found", await (await admin.PostAsJsonAsync($"/api/v1/admin/accounts/deleted_{victimId}/status", new { status = "active" })).ProblemCode());
    }

    /// <summary>Wrong current passwords in confirmations count like failed logins: the lockout stops guessing.</summary>
    [DbFact]
    public async Task Password_confirmations_count_towards_the_lockout()
    {
        await using var host = await TestHost.StartAsync(new() { ["Accounts:MaxFailedLogins"] = "5" });
        var (token, _, _) = await host.RegisterAsync("Guarded");
        var client = host.Client(token);
        for (var i = 0; i < 4; ++i)
            Assert.Equal("wrong_password", await (await client.PutAsJsonAsync("/api/v1/me/email", new { currentPassword = "guess " + i, email = "x@example.com" })).ProblemCode());
        Assert.Equal("wrong_password", await (await client.PostAsJsonAsync("/api/v1/online-character/reset", new { currentPassword = "guess 4" })).ProblemCode());
        // Locked now: even the right password confirms nothing, and logins wait too.
        Assert.Equal("account_locked_temporarily",
            await (await client.PostAsJsonAsync("/api/v1/me/delete", new { currentPassword = "a decent password" })).ProblemCode());
        Assert.Equal("account_locked_temporarily",
            await (await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "Guarded", password = "a decent password" })).ProblemCode());
        host.Clock.Advance(TimeSpan.FromMinutes(16));
        Assert.Equal(HttpStatusCode.OK, (await client.PutAsJsonAsync("/api/v1/me/email", new { currentPassword = "a decent password", email = "x@example.com" })).StatusCode);
    }

    [DbFact]
    public async Task Status_health_and_version_gate()
    {
        await using var host = await TestHost.StartAsync(new() { ["Play:MinimumProtocol"] = "41", ["Play:CurrentProtocol"] = "41", ["Service:Motd"] = "Hello" });
        var status = await (await host.Client().GetAsync("/api/v1/status")).Json();
        Assert.Equal("DW4Online", status.GetProperty("service").GetString());
        Assert.Equal(PropsVersion(), status.GetProperty("version").GetString()); // what the operator checks after an update
        Assert.Equal(41, status.GetProperty("client").GetProperty("minimumProtocol").GetInt32());
        Assert.Equal("Hello", status.GetProperty("motd").GetString());
        var health = await (await host.Client().GetAsync("/health")).Json();
        Assert.Equal(DW4Online.Data.Migrations.Migrator.LoadEmbedded()[^1].Version, health.GetProperty("schema").GetInt32());

        await host.RegisterAsync("Oldie");
        var old = await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "Oldie", password = "a decent password", protocol = 40 });
        Assert.Equal(HttpStatusCode.UpgradeRequired, old.StatusCode);
        Assert.Equal("client_outdated", await old.ProblemCode());
        var current = await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "Oldie", password = "a decent password", protocol = 41 });
        Assert.Equal(HttpStatusCode.OK, current.StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await host.Client().GetAsync("/")).StatusCode); // the website renders
    }

    /// <summary>The release version in server/DW4Online/Directory.Build.props (it also names the release zip).</summary>
    private static string PropsVersion()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir is not null && !File.Exists(Path.Combine(dir.FullName, "Directory.Build.props"))) dir = dir.Parent;
        Assert.NotNull(dir);
        var props = System.Xml.Linq.XDocument.Load(Path.Combine(dir.FullName, "Directory.Build.props"));
        return Assert.Single(props.Descendants("Version")).Value;
    }
}
