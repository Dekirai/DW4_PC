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
        var host = await TestHost.StartAsync(RelaySettings.On(more ?? new() { ["Instances:StoryMinVisitSeconds"] = "1" }));
        var port = await host.App.Services.GetRequiredService<RelayHub>().Started.WaitAsync(TimeSpan.FromSeconds(10));
        return (host, port);
    }

    private static string Ticket(TestHost host, long account, string role = RelayRoles.Member) =>
        host.App.Services.GetRequiredService<IWorldRelay>().IssueTicket(account, role).Ticket;

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
        var (host, port) = await StartAsync();
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
