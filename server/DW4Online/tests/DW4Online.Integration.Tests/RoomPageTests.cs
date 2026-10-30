using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using DW4Online.FakeClient;
using DW4Online.Relay.Hub;
using Microsoft.Extensions.DependencyInjection;

namespace DW4Online.Integration.Tests;

/// <summary>
/// The website's room page shows where a room's players are (SEAMLESS.md 14): the relay's instances per stage with the
/// game client's stage names (data/online/areas.json); names only for signed-in members of the room.
/// </summary>
public class RoomPageTests
{
    private static object NewRoom(string name) =>
        new { name, maxPlayers = 4, protocol = 41, dataHash = 1234567890123UL, clientVersion = "0.48.0" };

    [DbFact]
    public async Task Rooms_page_shows_instances_per_stage()
    {
        await using var host = await TestHost.StartAsync(RelaySettings.On());
        var relayPort = await host.App.Services.GetRequiredService<RelayHub>().Started.WaitAsync(TimeSpan.FromSeconds(10));
        var (ownerToken, _, _) = await host.RegisterAsync("Pathfinder");
        var (guestToken, _, _) = await host.RegisterAsync("Companion");
        var owner = host.Client(ownerToken);
        var guest = host.Client(guestToken);
        var created = await (await owner.PostAsJsonAsync("/api/v1/rooms", NewRoom("Stage tour"))).Json();
        var code = created.GetProperty("room").GetProperty("code").GetString()!;
        var settingsHash = ulong.Parse(created.GetProperty("settingsHash").GetString()!, System.Globalization.NumberStyles.AllowHexSpecifier);
        Assert.Equal(HttpStatusCode.OK, (await guest.PostAsJsonAsync($"/api/v1/rooms/{code}/join",
            new { protocol = 41, dataHash = 1234567890123UL, clientVersion = "0.48.0" })).StatusCode);

        // Both take the same exit together: one sealed instance in stag0101.
        var clients = new[] { ("owner", owner), ("guest", guest) }.Select((c, k) => new DW4Online.FakeClient.FakeClient(new FakeClientOptions
        {
            Port = relayPort,
            Ticket = async _ => (await (await c.Item2.PostAsync($"/api/v1/rooms/{code}/ticket", null)).Json()).GetProperty("relayTicket").GetString()!,
            Protocol = 41,
            DataHash = 1234567890123UL,
            SettingsHash = settingsHash,
            Name = c.Item1,
            Seed = k + 1,
            Duration = TimeSpan.FromSeconds(9),
            Script = [new ScriptAction(0.8, "exit", "stag0101", 0)],
        })).ToList();
        var runs = clients.Select(c => c.RunAsync(CancellationToken.None)).ToList();
        for (var i = 0; i < 100; ++i)
        {
            await Task.Delay(100);
            var state = await (await guest.GetAsync($"/api/v1/rooms/{code}")).Json();
            if (state.GetProperty("instances").EnumerateArray().Any(x => x.GetProperty("mode").GetString() == "sealed")) break;
        }

        using var anonymous = new WebBrowser(host);
        var page = await anonymous.GetAsync("/rooms");
        Assert.Equal(HttpStatusCode.OK, page.Status);
        Assert.Contains("Stage tour", page.Html);
        Assert.Contains("Where they play:", page.Html);
        Assert.Contains("Death Valley: Goblin Pass", page.Html);
        Assert.Contains("2 together", page.Html);
        Assert.DoesNotContain("Companion", page.Html); // the room list names only the room and its host

        using var member = new WebBrowser(host);
        await member.LoginAsync("Companion");
        var asMember = await member.GetAsync("/rooms");
        Assert.Contains("Death Valley: Goblin Pass", asMember.Html);
        Assert.Contains("(Pathfinder, Companion)", asMember.Html);

        using var outsider = new WebBrowser(host);
        await outsider.RegisterAsync("Onlooker");
        Assert.DoesNotContain("Companion", (await outsider.GetAsync("/rooms")).Html);
        await Task.WhenAll(runs);
    }

    [Fact]
    public void Stage_names_come_from_the_game_clients_table()
    {
        Assert.True(DW4Online.Web.StageNames.Count >= 90, $"{DW4Online.Web.StageNames.Count} stage names");
        Assert.Equal("Death Valley: Goblin Pass", DW4Online.Web.StageNames.Of("stag0101"));
        Assert.Equal("Death Valley: Goblin Pass", DW4Online.Web.StageNames.Of("STAG0101"));
        Assert.Equal("Home Server: Main Lobby: Terminal Area", DW4Online.Web.StageNames.Of("stagML11"));
        Assert.Equal("Title screen", DW4Online.Web.StageNames.Of(""));
        Assert.Equal("stagXX99", DW4Online.Web.StageNames.Of("stagXX99"));
        Assert.True(DW4Online.Web.StageNames.IsHub("stagML11") && DW4Online.Web.StageNames.IsHub("stagis10") && !DW4Online.Web.StageNames.IsHub("dng101b1"));

        // Fields first (most players first), then the hub stages; a hub stage sums its solo instances.
        var groups = DW4Online.Web.Pages.RoomsModel.Group(
        [
            new(1, "stagML11", 1, "hub", [new(1, "a", 0, true, false)], false, false, false, 0, null),
            new(2, "stagML11", 1, "hub", [new(2, "b", 0, true, false)], false, false, false, 0, null),
            new(3, "dng101b1", 2, "sealed", [new(3, "c", 1, true, false), new(4, "d", 0, true, false)], false, true, false, 900, null),
            new(4, "stag0101", 1, "solo", [new(5, "e", 0, true, false)], false, false, true, 300, null),
        ], withNames: true);
        Assert.Equal(["dng101b1", "stag0101", "stagML11"], groups.Select(g => g.Stage));
        Assert.Equal(["d", "c"], groups[0].Instances[0].Names!);
        Assert.True(groups[0].Instances[0].BossLock);
        Assert.Equal("2 together", DW4Online.Web.Pages.RoomsModel.Describe(groups[0].Instances[0]));
        Assert.Equal("alone", DW4Online.Web.Pages.RoomsModel.Describe(groups[1].Instances[0]));
        Assert.Equal(2, groups[2].Players);
        Assert.Null(DW4Online.Web.Pages.RoomsModel.Group([new(9, "stag0101", 1, "solo", [new(9, "x", 0, true, false)], false, false, true, 0, null)], false)[0]
            .Instances[0].Names);
    }
}
