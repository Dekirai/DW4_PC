using Dapper;
using System.Buffers.Binary;
using System.Diagnostics;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Story;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using Microsoft.Extensions.DependencyInjection;

namespace DW4Online.Integration.Tests;

/// <summary>
/// The world relay over loopback with the real server and a private database (round I1a, SEAMLESS.md 19): world tickets from
/// IWorldRelay, the protocol floor at the handshake, the admission's role from the database and its live re-check, the automatic
/// join of a public instance, a guest's STORY with the host's owner key reaching the host's online story, the owner refusal, the
/// DISSOLVE home on the own story, the browser snapshot and the play context.
/// </summary>
public class WorldRelayTests
{
    private static async Task<(TestHost Host, int Port)> StartAsync(Dictionary<string, string?>? more = null)
    {
        var host = await TestHost.StartAsync(RelaySettings.On(more ?? new() { ["Instances:StoryMinVisitSeconds"] = "1" }), fakeWorld: false);
        var port = await host.App.Services.GetRequiredService<RelayHub>().Started.WaitAsync(TimeSpan.FromSeconds(10));
        return (host, port);
    }

    // (Integration: tickets through web's WorldService, whose admission knows only the nonces it issued.)
    private static string Ticket(TestHost host, long account, string role = RelayRoles.Member) =>
        host.App.Services.GetRequiredService<DW4Online.Domain.Services.WorldService>()
            .EnterAsync(new DW4Online.Domain.Abstractions.AuthenticatedSession { AccountId = account }, null, null, null).GetAwaiter().GetResult().Value!.Ticket;

    [DbFact]
    public async Task The_handshake_takes_only_world_tickets_at_protocol_49()
    {
        var (host, port) = await StartAsync();
        await using var scope = host;
        var (_, _, id) = await host.RegisterAsync("FloorPlayer");
        await using (var old = await RelayConnection.OpenAsync(port, $"DW4R2 {Ticket(host, id)} 48 1 0"))
            Assert.Equal("ERR protocol", await old.ReadLineAsync());
        var tickets = host.App.Services.GetRequiredService<RelayTickets>();
        await using (var room = await RelayConnection.OpenAsync(port, $"DW4R2 {tickets.Issue("ABCD12", id, RelayRoles.Member)} 49 1 0"))
            Assert.Equal("ERR ticket", await room.ReadLineAsync());
        await using var ok = await WorldMember.ConnectAsync(port, Ticket(host, id));
        Assert.NotEqual(0, ok.Welcome.Flags & WelcomeMsg.World);
        Assert.Equal((0u, (byte)1), (ok.Welcome.Wid, ok.Welcome.Box));
        Assert.Equal((ok.Welcome.Member, (byte)1), (ok.FirstStory.OwnerMember, ok.FirstStory.OwnerBox));
    }

    [DbFact]
    public async Task A_stranger_joins_a_public_instance_and_its_story_goes_to_the_host_whose_online_story_is_saved()
    {
        // (Round I1a's automatic join of the first row: the prompt switched off; WorldPromptTests answers it.)
        var (host, port) = await StartAsync(new() { ["Instances:StoryMinVisitSeconds"] = "1", ["World:ArrivalPrompt"] = "false" });
        await using var scope = host;
        var (_, _, hostId) = await host.RegisterAsync("WorldHost");
        var (_, _, guestId) = await host.RegisterAsync("WorldGuest");
        var stories = host.App.Services.GetRequiredService<OnlineStoryService>();
        var start = new RoomStoryData { Revision = 7 };
        start.Flags[392] = 1;
        await stories.SaveAsync(hostId, start);

        await using var a = await WorldMember.ConnectAsync(port, Ticket(host, hostId));
        await using var b = await WorldMember.ConnectAsync(port, Ticket(host, guestId));
        Assert.Equal(7u, a.FirstStory.Revision);
        Assert.Equal(0u, b.FirstStory.Revision);
        // a leaves home for stag0101: a new public instance it hosts.
        await a.SendAsync(new BarrierMsg(a.Inst, a.Visit, 0, "stag0101", 0, 1, 1, 5, [1]).Frame());
        var okA = BarrierOkMsg.Parse(await a.NextAsync(Msg.BarrierOk));
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance, a.Welcome.Member), (okA.Decision, okA.World.Arrival, okA.World.HostMember));
        a.Enter(okA);
        await a.SendAsync(new SnapshotMsg(a.Inst, a.Visit, 4, 0, [1, 2, 3, 4]).Frame());
        await a.SendAsync(new SnapHashMsg(a.Inst, a.Visit, 0xAA).Frame());
        await a.SendAsync(new LogMsg(a.Inst, a.Visit, 0, Enumerable.Range(0, 60).Select(_ => new Pad(0, 0, 0, 0, 0)).ToArray()).Frame());
        await a.SendAsync(new StateMsg(a.Inst, a.Visit, 0, StateMsg.Joinable | StateMsg.MembershipSafe | StateMsg.LoadEnded, 5, StateReason.None).Frame());
        await Task.Delay(100);
        // b, a stranger, takes the gate there: round I1a joins it automatically (the first row).
        await b.SendAsync(new BarrierMsg(b.Inst, b.Visit, 0, "stag0101", 0, 1, 1, 5, [2]).Frame());
        var okB = BarrierOkMsg.Parse(await b.NextAsync(Msg.BarrierOk));
        Assert.Equal((BarrierDecision.Join, ArrivalKind.AutoJoin, okA.World.Wid, a.Welcome.Member), (okB.Decision, okB.World.Arrival, okB.World.Wid,
            okB.World.OwnerMember));
        Assert.Equal(1, RoomStoryData.FromBlob(okB.Story, okB.StoryRevision).Flags[392]); // the host's story
        // The browser snapshot (every 250 ms) and the play context name the instance with both.
        var world = host.App.Services.GetRequiredService<IWorldRelay>();
        var watch = Stopwatch.StartNew();
        while (world.Snapshot().Instances.FirstOrDefault()?.Members.Count != 2 && watch.Elapsed < TimeSpan.FromSeconds(5)) await Task.Delay(50);
        var row = world.Snapshot().Instances.Single();
        Assert.Equal((okA.World.Wid, hostId, "death_valley", "public"), (row.Wid, row.HostAccountId, row.Area, row.Visibility));
        Assert.Equal(2, world.Snapshot().Online.Count);
        Assert.True(world.PlayContext(hostId)!.IsHost);
        Assert.Equal(okA.World.Wid, world.PlayContext(guestId)!.Wid);

        // The host makes a story value in the field (owner rules: not spaced) after the visit's minimum age.
        await Task.Delay(1200);
        await a.SendAsync(new StoryMsg(a.Inst, a.Visit, 7, new StoryDelta { Flags = [505] }, a.Welcome.Member, 1).Frame());
        var answer = StoryAckMsg.Parse(await a.NextAsync(Msg.StoryAck));
        Assert.Equal((8u, StoryAckMsg.Answer, a.Welcome.Member), (answer.Revision, answer.Flags, answer.OwnerMember));
        Assert.Empty(answer.Refused);
        // A STORY with another owner key (b's own while it plays on a's story) is refused: owner.
        await b.SendAsync(new StoryMsg(okB.NewInst, okB.NewVisit, 0, new StoryDelta { Flags = [506] }, b.Welcome.Member, 1).Frame());
        var refused = StoryAckMsg.Parse(await b.NextAsync(Msg.StoryAck, ack => (StoryAckMsg.Parse(ack).Flags & StoryAckMsg.Answer) != 0));
        Assert.Equal(StoryRefusal.Owner, refused.Refused.Single().Reason);
        // The host's online story follows within ten seconds; the guest's own story is untouched.
        RoomStoryData saved;
        watch.Restart();
        do
        {
            saved = await stories.LoadAsync(hostId);
            if (saved.Revision < 8) await Task.Delay(50);
        } while (saved.Revision < 8 && watch.Elapsed < TimeSpan.FromSeconds(10));
        Assert.Equal((8u, (byte)1, (byte)1), (saved.Revision, saved.Flags[505], saved.Flags[392]));
        Assert.Equal(0u, (await stories.LoadAsync(guestId)).Revision);
    }

    [DbFact]
    public async Task The_role_comes_from_the_database_and_a_lock_closes_the_member_at_once()
    {
        var (host, port) = await StartAsync();
        await using var scope = host;
        var (_, _, modId) = await host.RegisterAsync("WorldMod");
        var accounts = host.App.Services.GetRequiredService<IAccountRepository>();
        await accounts.SetRoleAsync(modId, Roles.Moderator);
        await using var m = await WorldMember.ConnectAsync(port, Ticket(host, modId));
        Assert.NotEqual(0, m.Welcome.Flags & WelcomeMsg.Staff);
        // A lock: the domain tells the relay (IWorldNotifier.AccountChanged); the re-check closes the connection (CLOSE account).
        await accounts.SetStatusAsync(modId, AccountStatus.Locked);
        host.App.Services.GetRequiredService<IWorldNotifier>().AccountChanged(modId);
        var close = CloseMsg.Parse(await m.NextAsync(Msg.Close));
        Assert.Equal(CloseReason.Account, close.Reason);
    }

    [DbFact]
    public async Task A_recheck_with_an_older_ticket_closes_nobody_but_a_lock_still_closes_the_member()
    {
        var (host, port) = await StartAsync();
        await using var scope = host;
        var (_, _, id) = await host.RegisterAsync("RecheckPlayer");
        var relay = host.App.Services.GetRequiredService<IWorldRelay>();
        var hub = host.App.Services.GetRequiredService<RelayHub>();
        // Web's admission (19.17.3, web 12:50): only the account's newest nonce, and only an active account.
        string? newest = null;
        var locked = false;
        relay.SetAdmission(a => ValueTask.FromResult(!locked && a.Nonce == newest
            ? new WorldAdmitted(a.AccountId, "RecheckPlayer", a.Role, 1, Domain.World.OnlinePrefs.Default) : null));
        var first = relay.IssueTicket(id, RelayRoles.Member);
        newest = first.Nonce;
        await using var m = await WorldMember.ConnectAsync(port, first.Ticket);
        // The client fetched a newer ticket (its reconnect in the making) when a change runs the re-check with the old one: no CLOSE.
        newest = relay.IssueTicket(id, RelayRoles.Member).Nonce;
        host.App.Services.GetRequiredService<IWorldNotifier>().PrefsChanged(id);
        await Task.Delay(1000);
        Assert.True(hub.Locked((h, _) => h.MemberOf(id) is { Connected: true }));
        // A lock: refused with the newest nonce too: CLOSE account.
        locked = true;
        host.App.Services.GetRequiredService<IWorldNotifier>().AccountChanged(id);
        var close = CloseMsg.Parse(await m.NextAsync(Msg.Close));
        Assert.Equal(CloseReason.Account, close.Reason);
    }

    /// <summary>Round I1b: REPORT over the relay is stored by the report service (source game) and answered with WORLD_NOTICE 14; a
    /// member the reporter was never told about (review 2026-11-11) and the own id are "not available" without a name.</summary>
    [DbFact]
    public async Task A_report_over_the_relay_is_stored_and_answered_and_the_own_id_is_not_available()
    {
        var (host, port) = await StartAsync();
        await using var scope = host;
        var (_, _, kimId) = await host.RegisterAsync("RelayKim");
        var (_, _, leeId) = await host.RegisterAsync("RelayLee");
        await using var kim = await WorldMember.ConnectAsync(port, Ticket(host, kimId));
        await using var lee = await WorldMember.ConnectAsync(port, Ticket(host, leeId));
        static bool IsReport(byte[] p) => WorldNoticeMsg.Parse(p).Kind == WorldNoticeKind.Report;
        await kim.SendAsync(new ReportMsg(lee.Welcome.Member, 1, 0).Frame());
        var stranger = WorldNoticeMsg.Parse(await kim.NextAsync(Msg.WorldNotice, IsReport));
        Assert.Equal((lee.Welcome.Member, "", (uint)ReportResult.NotAvailable), (stranger.Member, stranger.Name, stranger.Argument));
        // Lee hosts stag0101, Kim joins: Kim's client is told about Lee (the Instance page's card).
        await lee.SendAsync(new BarrierMsg(lee.Inst, lee.Visit, 0, "stag0101", 0, 1, 1, 5, [1]).Frame());
        var okLee = BarrierOkMsg.Parse(await lee.NextAsync(Msg.BarrierOk));
        lee.Enter(okLee);
        await lee.SendAsync(new SnapshotMsg(lee.Inst, lee.Visit, 4, 0, [1, 2, 3, 4]).Frame());
        await lee.SendAsync(new SnapHashMsg(lee.Inst, lee.Visit, 0xAA).Frame());
        await lee.SendAsync(new LogMsg(lee.Inst, lee.Visit, 0, Enumerable.Range(0, 60).Select(_ => new Pad(0, 0, 0, 0, 0)).ToArray()).Frame());
        await lee.SendAsync(new StateMsg(lee.Inst, lee.Visit, 0, StateMsg.Joinable | StateMsg.MembershipSafe | StateMsg.LoadEnded, 5, StateReason.None).Frame());
        await Task.Delay(100);
        await kim.SendAsync(new BarrierMsg(kim.Inst, kim.Visit, 0, "stag0101", 0, 1, 1, 5, [2]).Frame());
        Assert.Equal(BarrierDecision.Join, BarrierOkMsg.Parse(await kim.NextAsync(Msg.BarrierOk)).Decision);
        await Task.Delay(500);
        await kim.SendAsync(new ReportMsg(lee.Welcome.Member, 1, 0).Frame());
        var answer = WorldNoticeMsg.Parse(await kim.NextAsync(Msg.WorldNotice, IsReport));
        Assert.Equal((lee.Welcome.Member, "RelayLee", (uint)ReportResult.Sent), (answer.Member, answer.Name, answer.Argument));
        await kim.SendAsync(new ReportMsg(lee.Welcome.Member, 2, 0).Frame()); // a repeat: sent, no second row
        Assert.Equal((uint)ReportResult.Sent, WorldNoticeMsg.Parse(await kim.NextAsync(Msg.WorldNotice, IsReport)).Argument);
        await Task.Delay(6100); // (REPORT's bucket: three at once, then ten a minute)
        await kim.SendAsync(new ReportMsg(kim.Welcome.Member, 3, 0).Frame());
        Assert.Equal((uint)ReportResult.NotAvailable, WorldNoticeMsg.Parse(await kim.NextAsync(Msg.WorldNotice, IsReport)).Argument);
        await using var sql = await host.SqlAsync();
        Assert.Equal(("RelayKim", "RelayLee", "griefing", "game"), await sql.QuerySingleAsync<(string, string, string, string)>(
            "SELECT reporter, reported, reason, source FROM v_player_reports"));
    }

    /// <summary>
    /// Round I1b (19.11, 19.6.6): the host blocks his guest on the website / API (POST /blocks): the relay's live re-check removes
    /// the guest (WORLD_NOTICE removed, the seat gone, the bar), audited instance_remove with block. Before, the guest's report about the
    /// host names their instance (the staff list: its host and first stage).
    /// </summary>
    [DbFact]
    public async Task A_hosts_block_through_the_api_removes_his_guest_from_the_instance()
    {
        var (host, port) = await StartAsync();
        await using var scope = host;
        var (hostToken, _, hostId) = await host.RegisterAsync("BlockHost");
        var (_, _, guestId) = await host.RegisterAsync("BlockGuest");
        await using var a = await WorldMember.ConnectAsync(port, Ticket(host, hostId));
        await using var b = await WorldMember.ConnectAsync(port, Ticket(host, guestId));
        await a.SendAsync(new BarrierMsg(a.Inst, a.Visit, 0, "stag0101", 0, 1, 1, 5, [1]).Frame());
        var okA = BarrierOkMsg.Parse(await a.NextAsync(Msg.BarrierOk));
        a.Enter(okA);
        await a.SendAsync(new SnapshotMsg(a.Inst, a.Visit, 4, 0, [1, 2, 3, 4]).Frame());
        await a.SendAsync(new SnapHashMsg(a.Inst, a.Visit, 0xAA).Frame());
        await a.SendAsync(new LogMsg(a.Inst, a.Visit, 0, Enumerable.Range(0, 60).Select(_ => new Pad(0, 0, 0, 0, 0)).ToArray()).Frame());
        await a.SendAsync(new StateMsg(a.Inst, a.Visit, 0, StateMsg.Joinable | StateMsg.MembershipSafe | StateMsg.LoadEnded, 5, StateReason.None).Frame());
        await Task.Delay(100);
        await b.SendAsync(new BarrierMsg(b.Inst, b.Visit, 0, "stag0101", 0, 1, 1, 5, [2]).Frame());
        var okB = BarrierOkMsg.Parse(await b.NextAsync(Msg.BarrierOk));
        Assert.Equal((BarrierDecision.Join, okA.World.Wid), (okB.Decision, okB.World.Wid));
        // (b reports a from inside the instance: the report names the instance both share, the staff list its host and stage.)
        await Task.Delay(500);
        await b.SendAsync(new ReportMsg(a.Welcome.Member, 4, okA.World.Wid).Frame());
        Assert.Equal((uint)ReportResult.Sent, WorldNoticeMsg.Parse(await b.NextAsync(Msg.WorldNotice,
            p => WorldNoticeMsg.Parse(p).Kind == WorldNoticeKind.Report)).Argument);
        var listed = Assert.Single(await host.App.Services.GetRequiredService<Domain.Abstractions.IReportRepository>().ListAsync(null, 0, 10));
        Assert.Equal(("BlockGuest", "BlockHost", "other", "BlockHost", "stag0101"),
            (listed.Reporter, listed.Reported, listed.Reason, listed.InstanceHost, listed.InstanceStage));
        Assert.NotNull(listed.InstanceStarted);

        Assert.Equal(System.Net.HttpStatusCode.NoContent,
            (await System.Net.Http.Json.HttpClientJsonExtensions.PostAsJsonAsync(host.Client(hostToken), "/api/v1/blocks", new { username = "BlockGuest" })).StatusCode);
        var removed = WorldNoticeMsg.Parse(await b.NextAsync(Msg.WorldNotice, p => WorldNoticeMsg.Parse(p).Kind == WorldNoticeKind.Removed));
        Assert.Equal((a.Welcome.Member, "BlockHost"), (removed.Member, removed.Name));
        var hub = host.App.Services.GetRequiredService<RelayHub>();
        Assert.True(hub.Locked((h, _) => h.MemberOf(guestId)!.World is null && h.WorldInstance(okA.World.Wid)!.Bars.ContainsKey(guestId)));
        var gateway = host.App.Services.GetRequiredService<DW4Online.Server.WorldGateway>();
        Assert.True(await gateway.Queue.DrainAsync(TimeSpan.FromSeconds(5)));
        await using var sql = await host.SqlAsync();
        Assert.Contains("\"block\":true", await sql.ExecuteScalarAsync<string>(
            "SELECT detail FROM audit_log WHERE event = 'instance_remove' AND account_id = @guestId", new { guestId }));
    }

    /// <summary>A raw protocol 49 member: handshake, WELCOME, the first STORY_ACK (its own story).</summary>
    private sealed class WorldMember : IAsyncDisposable
    {
        private readonly RelayConnection _c;
        public uint Inst { get; private set; }
        public uint Visit { get; private set; }
        public WelcomeMsg Welcome { get; private set; } = null!;
        public StoryAckMsg FirstStory { get; private set; } = null!;

        private WorldMember(RelayConnection c) => _c = c;

        public static async Task<WorldMember> ConnectAsync(int port, string ticket)
        {
            var m = new WorldMember(await RelayConnection.OpenAsync(port, $"DW4R2 {ticket} {Wire.WorldProtocol} 1 0"));
            Assert.StartsWith("OK ", await m._c.ReadLineAsync());
            m.Welcome = WelcomeMsg.Parse(await m.NextAsync(Msg.Welcome));
            (m.Inst, m.Visit) = (m.Welcome.Inst, m.Welcome.Visit);
            m.FirstStory = StoryAckMsg.Parse(await m.NextAsync(Msg.StoryAck));
            return m;
        }

        public void Enter(BarrierOkMsg ok) => (Inst, Visit) = (ok.NewInst, ok.NewVisit);

        public async Task SendAsync(byte[] frame) => await _c.Stream.WriteAsync(frame);

        /// <summary>The payload of the next frame of that type (and passing <paramref name="match"/>; others are skipped).</summary>
        public async Task<byte[]> NextAsync(byte type, Func<byte[], bool>? match = null)
        {
            for (var k = 0; k < 400; ++k)
            {
                var header = await _c.ReadExactlyAsync(Wire.HeaderBytes);
                var payload = await _c.ReadExactlyAsync((int)BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(1)));
                if (header[0] == type && (match is null || match(payload))) return payload;
            }
            throw new InvalidOperationException("no frame " + Msg.Name(type));
        }

        public ValueTask DisposeAsync() => _c.DisposeAsync();
    }
}
