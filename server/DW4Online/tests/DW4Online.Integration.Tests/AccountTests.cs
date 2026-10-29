using System.Net;
using System.Net.Http.Json;
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

    [DbFact]
    public async Task Status_health_and_version_gate()
    {
        await using var host = await TestHost.StartAsync(new() { ["Play:MinimumProtocol"] = "41", ["Play:CurrentProtocol"] = "41", ["Service:Motd"] = "Hello" });
        var status = await (await host.Client().GetAsync("/api/v1/status")).Json();
        Assert.Equal("DW4Online", status.GetProperty("service").GetString());
        Assert.Equal(41, status.GetProperty("client").GetProperty("minimumProtocol").GetInt32());
        Assert.Equal("Hello", status.GetProperty("motd").GetString());
        var health = await (await host.Client().GetAsync("/health")).Json();
        Assert.Equal(2, health.GetProperty("schema").GetInt32());

        await host.RegisterAsync("Oldie");
        var old = await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "Oldie", password = "a decent password", protocol = 40 });
        Assert.Equal(HttpStatusCode.UpgradeRequired, old.StatusCode);
        Assert.Equal("client_outdated", await old.ProblemCode());
        var current = await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "Oldie", password = "a decent password", protocol = 41 });
        Assert.Equal(HttpStatusCode.OK, current.StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await host.Client().GetAsync("/")).StatusCode); // the website skeleton renders
    }
}
