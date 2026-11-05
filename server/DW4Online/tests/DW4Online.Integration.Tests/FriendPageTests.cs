using System.Net;
using Dapper;

namespace DW4Online.Integration.Tests;

/// <summary>
/// The website's friends (SEAMLESS.md 19.20): /account/friends, the profile's friend and block buttons for each
/// relation (never showing the other side's block), the navigation and the privacy page.
/// </summary>
public class FriendPageTests
{
    private static Task<WebBrowser.Page> PostFriendsAsync(WebBrowser web, string handler, string username) =>
        web.PostAsync($"/account/friends?handler={handler}", new() { ["Username"] = username }, formPage: "/account/friends");

    private static Task<WebBrowser.Page> PostProfileAsync(WebBrowser web, string name, string handler) =>
        web.PostAsync($"/u/{name}?handler={handler}", [], formPage: $"/u/{name}");

    /// <summary>The rows of one table on the friends page: the part of the page between its heading and the next one.</summary>
    private static string Section(string html, string heading)
    {
        var start = html.IndexOf("<h2>" + heading, StringComparison.Ordinal);
        Assert.True(start >= 0, $"no section {heading}");
        var end = html.IndexOf("<h2>", start + 4, StringComparison.Ordinal);
        return end < 0 ? html[start..] : html[start..end];
    }

    [DbFact]
    public async Task The_friends_page_needs_a_login_and_is_in_the_navigation()
    {
        await using var host = await TestHost.StartAsync();
        using var anonymous = new WebBrowser(host);
        var page = await anonymous.GetAsync("/account/friends");
        Assert.Equal(HttpStatusCode.Redirect, page.Status);
        Assert.Equal("/login?returnUrl=%2Faccount%2Ffriends", page.Location);
        Assert.DoesNotContain("href=\"/account/friends\"", (await anonymous.GetAsync("/")).Html);

        using var web = new WebBrowser(host);
        await web.RegisterAsync("Navi");
        Assert.Contains("href=\"/account/friends\"", (await web.GetAsync("/")).Html);
        var friends = await web.GetAsync("/account/friends");
        Assert.Equal(HttpStatusCode.OK, friends.Status);
        Assert.Contains("No friends yet.", friends.Html);
        Assert.Contains("Your friends (0 of 200)", friends.Html);
        Assert.Contains("You block nobody.", friends.Html);
        Assert.Contains("there is no search", friends.Html);
    }

    [DbFact]
    public async Task Friends_requests_and_blocks_on_the_friends_page()
    {
        await using var host = await TestHost.StartAsync();
        using var kim = new WebBrowser(host);
        using var lee = new WebBrowser(host);
        await kim.RegisterAsync("Kim");
        await lee.RegisterAsync("Lee");

        var sent = await PostFriendsAsync(kim, "Add", " lee ");
        Assert.Equal(HttpStatusCode.OK, sent.Status);
        Assert.Contains("<div class=\"notice ok\">Request sent.</div>", sent.Html);
        Assert.Contains("href=\"/u/Lee\"", Section(sent.Html, "Sent requests"));
        Assert.Contains("No friend requests.", sent.Html);
        Assert.Contains("No player has this exact name.", (await PostFriendsAsync(kim, "Add", "Le")).Html);

        var leePage = await lee.GetAsync("/account/friends");
        Assert.Contains("href=\"/u/Kim\"", Section(leePage.Html, "Friend requests"));
        Assert.Contains(">Accept</button>", leePage.Html);
        var accepted = await PostFriendsAsync(lee, "Accept", "Kim");
        Assert.Contains("You are now friends with Kim.", accepted.Html);
        Assert.Contains("Your friends (1 of 200)", accepted.Html);
        Assert.Contains("href=\"/u/Kim\"", Section(accepted.Html, "Your friends"));
        Assert.Contains("href=\"/u/Lee\"", Section((await kim.GetAsync("/account/friends")).Html, "Your friends"));

        var removed = await PostFriendsAsync(kim, "Remove", "lee");
        Assert.Contains("Lee is no longer on your friend list.", removed.Html);
        Assert.Contains("No friends yet.", (await lee.GetAsync("/account/friends")).Html);

        // Lee declines Kim's next request: silent for Kim, whose page still says it was sent.
        await PostFriendsAsync(kim, "Add", "Lee");
        Assert.Contains("You declined the request from Kim. They are not told.", (await PostFriendsAsync(lee, "Decline", "Kim")).Html);
        Assert.Contains("href=\"/u/Lee\"", Section((await kim.GetAsync("/account/friends")).Html, "Sent requests"));
        Assert.Contains("Your request to Lee was cancelled.", (await PostFriendsAsync(kim, "Cancel", "Lee")).Html);

        // Blocks by name, listed with Unblock; the blocked player is never told.
        var blocked = await PostFriendsAsync(lee, "Block", "KIM");
        Assert.Contains("You blocked Kim. They are not told.", blocked.Html);
        Assert.Contains("href=\"/u/Kim\"", Section(blocked.Html, "Blocked players"));
        var silent = await PostFriendsAsync(kim, "Add", "Lee");
        Assert.Contains("<div class=\"notice ok\">Request sent.</div>", silent.Html);
        Assert.DoesNotContain("href=\"/u/Kim\"", Section((await lee.GetAsync("/account/friends")).Html, "Friend requests"));
        Assert.Contains("You unblocked Kim.", (await PostFriendsAsync(lee, "Unblock", "Kim")).Html);
        // Lee's earlier decline of Kim's request still holds after the unblock (until it expires).
        Assert.DoesNotContain("href=\"/u/Kim\"", Section((await lee.GetAsync("/account/friends")).Html, "Friend requests"));

        // Forms without the anti-forgery token are refused.
        Assert.Equal(HttpStatusCode.BadRequest,
            (await kim.PostAsync("/account/friends?handler=Add", new() { ["Username"] = "Lee" }, withToken: false)).Status);
    }

    [DbFact]
    public async Task Profile_buttons_follow_the_relation_and_never_show_the_other_sides_block()
    {
        await using var host = await TestHost.StartAsync();
        using var kim = new WebBrowser(host);
        using var lee = new WebBrowser(host);
        await kim.RegisterAsync("Kim");
        await lee.RegisterAsync("Lee");

        var none = await kim.GetAsync("/u/Lee");
        Assert.Contains(">Add friend</button>", none.Html);
        Assert.Contains(">Block</button>", none.Html);
        Assert.Contains("This is your profile.", (await kim.GetAsync("/u/Kim")).Html);
        Assert.DoesNotContain(">Add friend</button>", (await kim.GetAsync("/u/Kim")).Html);

        var sent = await PostProfileAsync(kim, "Lee", "AddFriend");
        Assert.Contains("Request sent.", sent.Html);
        Assert.Contains("<span class=\"badge\">Request sent</span>", sent.Html);
        Assert.Contains(">Cancel request</button>", sent.Html);

        var received = await lee.GetAsync("/u/Kim");
        Assert.Contains(">Accept request</button>", received.Html);
        Assert.Contains(">Decline</button>", received.Html);
        var friends = await PostProfileAsync(lee, "Kim", "Accept");
        Assert.Contains("You are now friends with Kim.", friends.Html);
        Assert.Contains("<span class=\"badge ok\">Friends</span>", friends.Html);
        Assert.Contains(">Remove friend</button>", friends.Html);

        // Lee blocks Kim from Kim's profile: Lee sees "Blocked" with Unblock; Kim sees a plain stranger.
        var blocked = await PostProfileAsync(lee, "Kim", "Block");
        Assert.Contains("You blocked Kim. They are not told.", blocked.Html);
        Assert.Contains("<span class=\"badge bad\">Blocked</span>", blocked.Html);
        Assert.Contains(">Unblock</button>", blocked.Html);
        var kimSees = await kim.GetAsync("/u/Lee");
        Assert.Contains(">Add friend</button>", kimSees.Html);
        Assert.DoesNotContain("Blocked", kimSees.Html);
        Assert.DoesNotContain(">Friends</span>", kimSees.Html); // the friendship ended (as a removal would end it)
        var silent = await PostProfileAsync(kim, "Lee", "AddFriend");
        Assert.Contains("Request sent.", silent.Html);
        Assert.Contains(">Cancel request</button>", silent.Html);
        Assert.Contains("<span class=\"badge bad\">Blocked</span>", (await lee.GetAsync("/u/Kim")).Html);

        // A private profile still has the buttons (requests name exact user names; nothing else is shown).
        Assert.Equal(HttpStatusCode.OK, (await lee.PostAsync("/account?handler=Visibility", new() { ["Public"] = "false" }, "/account")).Status);
        var hidden = await kim.GetAsync("/u/Lee");
        Assert.Contains("This profile is private.", hidden.Html);
        Assert.Contains(">Cancel request</button>", hidden.Html);

        // Signed-out visitors get a login link; a forged POST goes to the login and changes nothing.
        using var anonymous = new WebBrowser(host);
        var page = await anonymous.GetAsync("/u/Kim");
        Assert.Contains("to add Kim as a friend.", page.Html);
        Assert.Contains("href=\"/login?returnUrl=%2Fu%2FKim\"", page.Html);
        Assert.DoesNotContain("handler=", page.Html);
        var post = await anonymous.PostAsync("/u/Kim?handler=AddFriend", [], formPage: "/login");
        Assert.Equal(HttpStatusCode.Redirect, post.Status);
        Assert.StartsWith("/login?returnUrl=", post.Location);
        await using var sql = await host.SqlAsync();
        Assert.Equal(1, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM friend_requests")); // Kim's silent one only
    }

    [DbFact]
    public async Task The_privacy_page_says_what_friends_see_and_that_blocks_are_never_revealed()
    {
        await using var host = await TestHost.StartAsync();
        using var web = new WebBrowser(host);
        var html = WebUtility.HtmlDecode((await web.GetAsync("/privacy")).Html);
        Assert.Contains("Friends and blocks: who your friends are", html);
        Assert.Contains("your friends see your user name in their friend list", html);
        Assert.Contains("There is no search for players", html);
        Assert.Contains("a block is never revealed", html);
        Assert.Contains("expire 30 days after they were sent", html);
        Assert.Contains("friendships, friend requests and blocks (yours and those about you)", html);
    }
}
