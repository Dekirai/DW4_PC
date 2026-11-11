using System.Net;
using System.Net.Http.Json;
using Dapper;
using DW4Online.Domain.Save;
using DW4Online.Tests;

namespace DW4Online.Integration.Tests;

/// <summary>The website: forms with anti-forgery tokens, cookie sessions, account settings, profiles, admin pages.</summary>
public class WebTests
{
    /// <summary>An online character and the unlock "online_join", reported as a guest of someone's instance (the stand-in relay's context).</summary>
    private static async Task CharacterAndUnlockAsync(TestHost host, string token)
    {
        var client = host.Client(token);
        var id = (await (await client.GetAsync("/api/v1/me")).Json()).GetProperty("id").GetInt64();
        var context = new { protocol = TestHost.Protocol, players = 1 };
        var record = Convert.ToBase64String(RecordCodec.Encode(Samples.Fresh()));
        Assert.Equal(HttpStatusCode.Created, (await client.PutJson("/api/v1/online-character", new { record, playSeconds = 3725, context }, create: true)).StatusCode);
        host.InWorld(id, wid: 9, host: false, players: 2);
        var unlock = await client.PostAsJsonAsync("/api/v1/achievements/unlock", new { code = "online_join", context });
        Assert.Equal(HttpStatusCode.OK, unlock.StatusCode);
    }

    [DbFact]
    public async Task Register_login_logout_and_forgery_protection()
    {
        await using var host = await TestHost.StartAsync();
        using var web = new WebBrowser(host);
        Assert.Contains("Create an account", (await web.GetAsync("/register")).Html);
        var code = await web.RegisterAsync("WebUser");
        Assert.Matches("[A-Z0-9-]{10,}", code);
        Assert.NotNull(web.Cookie("dw4_session"));
        Assert.Contains("Account: WebUser", (await web.GetAsync("/account")).Html);
        await using (var sql = await host.SqlAsync())
            Assert.Equal("web", await sql.ExecuteScalarAsync<string>("SELECT s.kind FROM sessions s JOIN accounts a ON a.id = s.account_id WHERE a.username = 'WebUser'"));

        // Posts without the anti-forgery token are refused with the HTML error page.
        var forged = await web.PostAsync("/account?handler=Password", new() { ["CurrentPassword"] = "a decent password", ["NewPassword"] = "another fine password",
            ["ConfirmPassword"] = "another fine password" }, withToken: false);
        Assert.Equal(HttpStatusCode.BadRequest, forged.Status);
        Assert.Contains("Bad request", forged.Html);
        Assert.Equal(HttpStatusCode.BadRequest, (await web.PostAsync("/logout", [], withToken: false)).Status);

        var logout = await web.PostAsync("/logout", [], formPage: "/account");
        Assert.Equal(HttpStatusCode.Redirect, logout.Status);
        Assert.Equal("/", logout.Location);
        Assert.Null(web.Cookie("dw4_session"));
        var protectedPage = await web.GetAsync("/account");
        Assert.Equal(HttpStatusCode.Redirect, protectedPage.Status);
        Assert.Equal("/login?returnUrl=%2Faccount", protectedPage.Location);

        Assert.Contains("User name or password is wrong.", (await web.LoginAsync("WebUser", "not the password")).Html);
        var login = await web.LoginAsync("WebUser", returnUrl: "/u/WebUser");
        Assert.Equal(HttpStatusCode.Redirect, login.Status);
        Assert.Equal("/u/WebUser", login.Location);
        await web.PostAsync("/logout", [], formPage: "/account");
        Assert.Equal("/account", (await web.LoginAsync("WebUser", returnUrl: "https://evil.example/")).Location); // no open redirect

        // The honeypot field and mismatching passwords create nothing.
        using var bot = new WebBrowser(host);
        var trapped = await bot.PostAsync("/register", new() { ["Username"] = "BotUser", ["Password"] = "a decent password",
            ["ConfirmPassword"] = "a decent password", ["Website"] = "http://spam.example" });
        Assert.Contains("could not be processed", trapped.Html);
        Assert.Contains("do not match", (await bot.PostAsync("/register", new() { ["Username"] = "BotUser", ["Password"] = "a decent password",
            ["ConfirmPassword"] = "another password!" })).Html);
        await using (var sql = await host.SqlAsync())
            Assert.Equal(0, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM accounts WHERE username = 'BotUser'"));
    }

    [DbFact]
    public async Task Account_settings_sessions_recovery_and_deletion()
    {
        await using var host = await TestHost.StartAsync();
        using var web = new WebBrowser(host);
        var recovery = await web.RegisterAsync("Settler");
        var game = await host.Client().PostAsJsonAsync("/api/v1/auth/login", new { username = "Settler", password = "a decent password", deviceName = "DW4_PC test" });
        var gameToken = (await game.Json()).GetProperty("token").GetString()!;
        Assert.Contains("DW4_PC test", (await web.GetAsync("/account")).Html);

        Assert.Contains("The current password is wrong.", (await web.PostAsync("/account?handler=Email", new() { ["Email"] = "me@example.org",
            ["CurrentPassword"] = "wrong" }, "/account")).Html);
        Assert.Contains("Your e-mail address was saved.", (await web.PostAsync("/account?handler=Email", new() { ["Email"] = "me@example.org",
            ["CurrentPassword"] = "a decent password" }, "/account")).Html);
        await using (var sql = await host.SqlAsync())
            Assert.Equal("me@example.org", await sql.ExecuteScalarAsync<string>("SELECT email FROM accounts WHERE username = 'Settler'"));

        // Profile visibility.
        Assert.Contains("Your profile is private.", (await web.PostAsync("/account?handler=Visibility", new() { ["Public"] = "false" }, "/account")).Html);
        using (var stranger = new WebBrowser(host))
            Assert.Contains("This profile is private.", (await stranger.GetAsync("/u/Settler")).Html);
        Assert.DoesNotContain("This profile is private.", (await web.GetAsync("/u/Settler")).Html);
        await web.PostAsync("/account?handler=Visibility", new() { ["Public"] = "true" }, "/account");

        // A new recovery code replaces the old one.
        var renewed = await web.PostAsync("/account?handler=Recovery", new() { ["CurrentPassword"] = "a decent password" }, "/account");
        var newCode = WebBrowser.Secret(renewed.Html);
        Assert.NotNull(newCode);
        Assert.NotEqual(recovery, newCode);

        // Log out the game session from the website, then change the password.
        var sessionId = await (await host.SqlAsync()).ExecuteScalarAsync<long>(
            "SELECT s.id FROM sessions s JOIN accounts a ON a.id = s.account_id WHERE a.username = 'Settler' AND s.kind = 'game'");
        Assert.Contains("The session was logged out.", (await web.PostAsync("/account?handler=Revoke", new() { ["SessionId"] = sessionId.ToString(System.Globalization.CultureInfo.InvariantCulture) }, "/account")).Html);
        Assert.Equal(HttpStatusCode.Unauthorized, (await host.Client(gameToken).GetAsync("/api/v1/me")).StatusCode);
        Assert.Contains("do not match", (await web.PostAsync("/account?handler=Password", new() { ["CurrentPassword"] = "a decent password",
            ["NewPassword"] = "a brand new secret", ["ConfirmPassword"] = "something else!" }, "/account")).Html);
        Assert.Contains("Your password was changed.", (await web.PostAsync("/account?handler=Password", new() { ["CurrentPassword"] = "a decent password",
            ["NewPassword"] = "a brand new secret", ["ConfirmPassword"] = "a brand new secret" }, "/account")).Html);

        // Recovery with the old code fails, with the new one it works and shows the next code.
        using var other = new WebBrowser(host);
        Assert.Contains("recovery code is wrong", (await other.PostAsync("/recover", new() { ["Username"] = "Settler", ["RecoveryCode"] = recovery,
            ["NewPassword"] = "recovered password", ["ConfirmPassword"] = "recovered password" })).Html);
        var recovered = await other.PostAsync("/recover", new() { ["Username"] = "Settler", ["RecoveryCode"] = newCode!,
            ["NewPassword"] = "recovered password", ["ConfirmPassword"] = "recovered password" });
        Assert.Contains("Your new recovery code", recovered.Html);
        Assert.Equal(HttpStatusCode.Redirect, (await web.GetAsync("/account")).Status); // every session ended

        // Deletion: confirmation, password, then everything personal goes.
        Assert.Equal(HttpStatusCode.Redirect, (await web.LoginAsync("Settler", "recovered password")).Status);
        var token = (await host.LoginAsync("Settler", "recovered password")).Token;
        await CharacterAndUnlockAsync(host, token);
        Assert.Contains("Tick the box", (await web.PostAsync("/account?handler=Delete", new() { ["CurrentPassword"] = "recovered password" }, "/account")).Html);
        Assert.Contains("The current password is wrong.", (await web.PostAsync("/account?handler=Delete", new() { ["CurrentPassword"] = "nope",
            ["ConfirmDelete"] = "true" }, "/account")).Html);
        var deleted = await web.PostAsync("/account?handler=Delete", new() { ["CurrentPassword"] = "recovered password", ["ConfirmDelete"] = "true" }, "/account");
        Assert.Equal(HttpStatusCode.Redirect, deleted.Status);
        Assert.Equal(HttpStatusCode.NotFound, (await web.GetAsync("/u/Settler")).Status);
        Assert.Contains("User name or password is wrong.", (await web.LoginAsync("Settler", "recovered password")).Html);
        await using (var sql = await host.SqlAsync())
        {
            var row = await sql.QuerySingleAsync<(string Username, string Status, string? Email)>(
                "SELECT username, status, email FROM accounts WHERE username LIKE 'deleted\\_%'");
            Assert.Equal("deleted", row.Status);
            Assert.Null(row.Email);
            foreach (var table in new[] { "sessions", "online_characters", "online_character_history", "achievement_unlocks", "room_members" })
                Assert.Equal(0, await sql.ExecuteScalarAsync<int>($"SELECT COUNT(*) FROM {table}"));
            Assert.Equal(0, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM audit_log a JOIN accounts c ON c.id = a.account_id WHERE c.status = 'deleted' AND a.ip IS NOT NULL"));
        }
        // The anonymised name cannot be registered.
        using var copycat = new WebBrowser(host);
        Assert.Contains("reserved", (await copycat.PostAsync("/register", new() { ["Username"] = "deleted_1", ["Password"] = "a decent password",
            ["ConfirmPassword"] = "a decent password" })).Html);
    }

    [DbFact]
    public async Task Profile_achievements_and_instances_pages()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, showcase) = await host.RegisterAsync("Showcase");
        await CharacterAndUnlockAsync(host, token);
        using var web = new WebBrowser(host);

        var profile = await web.GetAsync("/u/showcase");
        Assert.Equal(HttpStatusCode.OK, profile.Status);
        Assert.Contains("Agumon", profile.Html);
        Assert.Contains("Kevin", profile.Html);
        Assert.Contains("1 h 02 min", profile.Html);
        Assert.Contains("Connected", profile.Html);
        Assert.Contains("2026-11-01 12:00 UTC", profile.Html);
        Assert.Contains("1 of 72 unlocked", profile.Html);
        Assert.Equal(HttpStatusCode.NotFound, (await web.GetAsync("/u/nobody")).Status);

        var achievements = await web.GetAsync("/achievements");
        Assert.Contains("Connected", achievements.Html);
        Assert.Contains("100 %", achievements.Html);
        Assert.Contains("Hidden achievement", achievements.Html);

        // /rooms gave way to /instances (round I1): signed out only the counts per area, no names.
        host.World!.Set([FakeWorldRelay.Instance(9, showcase, "Showcase", "dry_land", "stag0201")]);
        var moved = await web.GetAsync("/rooms");
        Assert.Equal(HttpStatusCode.MovedPermanently, moved.Status);
        Assert.EndsWith("/instances", moved.Location);
        var counts = await web.GetAsync("/instances");
        Assert.Contains("Dry Land", counts.Html);
        Assert.Contains("1 instances, 1 players", counts.Html);
        Assert.DoesNotContain("Showcase", counts.Html);
        Assert.DoesNotContain("Mummy Desert", counts.Html);
        await web.RegisterAsync("Viewer");
        var listed = await web.GetAsync("/instances");
        Assert.Contains("/u/Showcase", listed.Html);
        Assert.Contains("Dry Land: Mummy Desert", listed.Html);

        var home = await web.GetAsync("/");
        Assert.Contains("players online", home.Html);
        Assert.Contains("href=\"/instances\">instances", home.Html);
        Assert.Contains("Privacy", home.Html);
        Assert.Contains("GDPR", (await web.GetAsync("/privacy")).Html);
    }

    [DbFact]
    public async Task Admin_pages_moderate_accounts_and_unlocks()
    {
        await using var host = await TestHost.StartAsync();
        var (playerToken, _, playerId) = await host.RegisterAsync("Player1");
        await CharacterAndUnlockAsync(host, playerToken);
        using var boss = new WebBrowser(host);
        await boss.RegisterAsync("Boss");
        using var anonymous = new WebBrowser(host);
        Assert.Equal(HttpStatusCode.Redirect, (await anonymous.GetAsync("/admin")).Status);
        var notStaff = await boss.GetAsync("/admin");
        Assert.Equal(HttpStatusCode.Forbidden, notStaff.Status);
        Assert.Contains("Not allowed", notStaff.Html);
        await using (var sql = await host.SqlAsync())
            await sql.ExecuteAsync("UPDATE accounts SET role = 'admin' WHERE username = 'Boss'");

        var search = await boss.GetAsync("/admin?q=play");
        Assert.Contains("/admin/account/Player1", search.Html);
        Assert.Contains("0 players online, 0 instances", search.Html);

        // Flagged unlocks disappear from the public profile and the rarity.
        var page = "/admin/account/Player1";
        Assert.Contains("online_join is flagged.", (await boss.PostAsync(page + "?handler=Flag",
            new() { ["Code"] = "online_join", ["Ruleset"] = "standard", ["Flagged"] = "true" }, page)).Html);
        Assert.DoesNotContain("Connected", (await anonymous.GetAsync("/u/Player1")).Html);
        Assert.Contains("0 %", (await anonymous.GetAsync("/achievements")).Html);
        await boss.PostAsync(page + "?handler=Flag", new() { ["Code"] = "online_join", ["Ruleset"] = "standard", ["Flagged"] = "false" }, page);
        Assert.Contains("Connected", (await anonymous.GetAsync("/u/Player1")).Html);

        // Lock ends the sessions; unlock lets the player back in.
        Assert.Contains("The account is now locked.", (await boss.PostAsync(page + "?handler=Status", new() { ["Status"] = "locked", ["Reason"] = "test" }, page)).Html);
        Assert.Equal(HttpStatusCode.Unauthorized, (await host.Client(playerToken).GetAsync("/api/v1/me")).StatusCode);
        using var player = new WebBrowser(host);
        Assert.Contains("locked", (await player.LoginAsync("Player1")).Html);
        await boss.PostAsync(page + "?handler=Status", new() { ["Status"] = "active" }, page);
        Assert.Equal(HttpStatusCode.Redirect, (await player.LoginAsync("Player1")).Status);

        // A temporary password must be changed before anything else.
        var reset = await boss.PostAsync(page + "?handler=ResetPassword", [], page);
        var temporary = WebBrowser.Secret(reset.Html);
        Assert.NotNull(temporary);
        Assert.Equal(HttpStatusCode.Redirect, (await player.GetAsync("/account")).Status); // the reset ended the session
        var login = await player.LoginAsync("Player1", temporary!, returnUrl: "/achievements");
        Assert.Equal("/account", login.Location);
        Assert.Contains("temporary password", (await player.GetAsync("/account")).Html);
        Assert.Equal("/account", (await player.GetAsync("/achievements")).Location);

        // Locking and unlocking told the relay (it re-checks the member, 19.14).
        Assert.Equal(2, host.World!.Notes.Count(n => n == $"account {playerId}"));

        // Revoke, end an instance (Private ones are listed for staff), remove a member, delete the account.
        Assert.Contains("online_join was removed", (await boss.PostAsync(page + "?handler=Revoke", new() { ["Code"] = "online_join", ["Ruleset"] = "standard" }, page)).Html);
        var (_, _, roomyId) = await host.RegisterAsync("Roomy");
        host.World.Set([FakeWorldRelay.Instance(9, roomyId, "Roomy", "venom_jungle", "dng307b1", "private", [(playerId, "Player1", "dng307b1")])]);
        var instances = await boss.GetAsync("/admin/instances");
        Assert.Contains("#9", instances.Html);
        Assert.Contains("private", instances.Html);
        Assert.Contains("Venom Jungle: Geko Path", instances.Html);
        // fix-019: the forms post the relay run with the wid; a page from before a server restart is refused for the same wid.
        var run = host.World.Run;
        Assert.Contains($"name=\"Run\" value=\"{run}\"", instances.Html);
        Assert.Contains("The server restarted since this list was loaded",
            (await boss.PostAsync("/admin/instances?handler=End", new() { ["Wid"] = "9", ["Run"] = "00000000deadbeef" }, "/admin/instances")).Html);
        Assert.Contains("Name the instance by its run",
            (await boss.PostAsync("/admin/instances?handler=Remove", new() { ["Wid"] = "9", ["Username"] = "player1" }, "/admin/instances")).Html);
        Assert.Empty(host.World.Ended);
        Assert.Empty(host.World.Removed);
        Assert.Contains("Player1 was removed from instance 9",
            (await boss.PostAsync("/admin/instances?handler=Remove", new() { ["Wid"] = "9", ["Run"] = run, ["Username"] = "player1" }, "/admin/instances")).Html);
        Assert.Contains("Instance 9 was ended",
            (await boss.PostAsync("/admin/instances?handler=End", new() { ["Wid"] = "9", ["Run"] = run }, "/admin/instances")).Html);
        Assert.Contains("No such instance",
            (await boss.PostAsync("/admin/instances?handler=End", new() { ["Wid"] = "10", ["Run"] = run }, "/admin/instances")).Html);
        Assert.Single(host.World.Ended);
        Assert.Equal((9u, playerId), (host.World.Removed.Single().Wid, host.World.Removed.Single().Account));
        await using (var sql = await host.SqlAsync())
            Assert.Equal(2, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM audit_log WHERE event IN ('staff_end_instance', 'staff_remove')"));
        var gone = await boss.PostAsync(page + "?handler=Delete", new() { ["ConfirmDelete"] = "true" }, page);
        Assert.Equal(HttpStatusCode.Redirect, gone.Status);
        await using (var sql = await host.SqlAsync())
        {
            Assert.Equal("deleted", await sql.ExecuteScalarAsync<string>("SELECT status FROM accounts WHERE username LIKE 'deleted\\_%'"));
            Assert.True(await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM audit_log WHERE event IN ('admin_unlock_flag', 'admin_unlock_revoke', 'admin_status', 'admin_account_delete')") >= 4);
        }
    }

    [DbFact]
    public async Task Form_rate_limits_headers_and_error_pages()
    {
        await using var host = await TestHost.StartAsync(new() { ["RateLimits:WebLoginPerMinute"] = "3" });
        using var web = new WebBrowser(host);
        for (var i = 0; i < 3; ++i) Assert.Equal(HttpStatusCode.OK, (await web.LoginAsync("Nobody", "wrong password")).Status);
        var limited = await web.LoginAsync("Nobody", "wrong password");
        Assert.Equal(HttpStatusCode.TooManyRequests, limited.Status);
        Assert.Contains("Too many attempts", limited.Html);
        Assert.Equal(HttpStatusCode.OK, (await web.GetAsync("/login")).Status); // looking at the form is free

        var home = await web.GetAsync("/");
        Assert.Contains("default-src 'none'", home.Response.Headers.GetValues("Content-Security-Policy").Single());
        Assert.Equal("nosniff", home.Response.Headers.GetValues("X-Content-Type-Options").Single());
        Assert.Equal("DENY", home.Response.Headers.GetValues("X-Frame-Options").Single());
        var css = await web.GetAsync("/css/site.css");
        Assert.Equal("text/css", css.Response.Content.Headers.ContentType?.MediaType);
        Assert.Contains("--accent", css.Html);

        var missing = await web.GetAsync("/no-such-page");
        Assert.Equal(HttpStatusCode.NotFound, missing.Status);
        Assert.Contains("Page not found", missing.Html);
        var api = await host.Client().GetAsync("/api/v1/no-such-endpoint");
        Assert.Equal(HttpStatusCode.NotFound, api.StatusCode);
        Assert.Equal("application/problem+json", api.Content.Headers.ContentType?.MediaType);
        Assert.False(api.Headers.Contains("Content-Security-Policy"));
    }

    [DbFact]
    public async Task Api_profile_switch_and_account_deletion()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, id) = await host.RegisterAsync("ApiGdpr");
        var client = host.Client(token);
        await CharacterAndUnlockAsync(host, token);
        var hidden = await client.PutAsJsonAsync("/api/v1/me/profile", new { @public = false });
        Assert.False((await hidden.Json()).GetProperty("profilePublic").GetBoolean());
        using (var web = new WebBrowser(host))
            Assert.Contains("This profile is private.", (await web.GetAsync("/u/ApiGdpr")).Html);

        Assert.Equal("wrong_password", await (await client.PostAsJsonAsync("/api/v1/me/delete", new { currentPassword = "nope" })).ProblemCode());
        Assert.Equal(HttpStatusCode.NoContent, (await client.PostAsJsonAsync("/api/v1/me/delete", new { currentPassword = "a decent password" })).StatusCode);
        Assert.Equal(HttpStatusCode.Unauthorized, (await client.GetAsync("/api/v1/me")).StatusCode);
        await using var sql = await host.SqlAsync();
        Assert.Equal($"deleted_{id}", await sql.ExecuteScalarAsync<string>("SELECT username FROM accounts WHERE id = @id", new { id }));
        Assert.Equal(1, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM audit_log WHERE event = 'account_delete' AND account_id = @id AND ip IS NULL", new { id }));
        // The relay heard of the deletion before and after the scrub.
        Assert.Equal(2, host.World!.Notes.Count(n => n == $"account {id}"));
    }
}
