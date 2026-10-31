using System.Buffers.Binary;
using System.Diagnostics;
using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Dapper;
using DW4Online.Domain.Save;
using DW4Online.Domain.Story;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Tests;
using Microsoft.Extensions.DependencyInjection;

namespace DW4Online.Integration.Tests;

/// <summary>
/// The online story (SEAMLESS.md 8.4) with MariaDB: online_story keeps newer revisions only; the online character's record
/// carries the account's own story (GET composes it, PUT ignores the uploaded story bytes, so a borrowed clear never
/// unlocks anything); and through the real relay a guest's story flag made in its own lobby instance reaches the room
/// owner's online story within ten seconds, the room API shows it, and a new room of the owner starts from it.
/// </summary>
public class OnlineStoryTests
{
    private static object Upload(SaveRecord record, long playSeconds) => new { record = Convert.ToBase64String(RecordCodec.Encode(record)), playSeconds };

    private static object NewRoom() =>
        new { name = "Story night", maxPlayers = 4, protocol = 41, dataHash = 1234567890123UL, clientVersion = "0.48.0", settings = new { difficulty = 0 } };

    private static object Join() => new { protocol = 41, dataHash = 1234567890123UL, clientVersion = "0.48.0" };

    [DbFact]
    public async Task The_character_record_carries_the_accounts_own_story()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, id) = await host.RegisterAsync("StoryOwn");
        var client = host.Client(token);
        var character = Samples.Fresh("Kev");
        character.Flags[406] = 1; // a clear the guest's engine had from a room owner's story
        character.Flags[392] = 1;
        character.Counters[1] = 99;
        var created = await client.PutJson("/api/v1/online-character", Upload(character, 600), create: true);
        Assert.Equal(HttpStatusCode.Created, created.StatusCode);
        Assert.Equal(0, (await created.Json()).GetProperty("summary").GetProperty("clears").GetInt32());
        var stored = Convert.FromBase64String((await (await client.GetAsync("/api/v1/online-character")).Json()).GetProperty("record").GetString()!);
        Assert.Equal(0, stored[792 + 406]);
        Assert.Equal(0, stored[792 + 392]);
        Assert.Equal(0, BinaryPrimitives.ReadInt32BigEndian(stored.AsSpan(1828 + 4)));
        Assert.Equal(RecordCodec.Encode(character)[..792], stored[..792]);

        // The room service writes the account's own story; only newer revisions count.
        var stories = host.App.Services.GetRequiredService<OnlineStoryService>();
        var own = new RoomStoryData { Revision = 3 };
        own.Flags[406] = 1;
        own.Flags[19] = 1;
        own.Folder[2] = 1;
        own.Counters[7] = 4;
        Assert.True(await stories.SaveAsync(id, own));
        Assert.False(await stories.SaveAsync(id, new RoomStoryData { Revision = 2 }));
        Assert.Equal(3u, (await stories.LoadAsync(id)).Revision);
        var got = await (await client.GetAsync("/api/v1/online-character")).Json();
        var record = Convert.FromBase64String(got.GetProperty("record").GetString()!);
        Assert.Equal(1, record[792 + 406]);
        Assert.Equal(1, record[792 + 19]);
        Assert.Equal(1, record[1816 + 2]);
        Assert.Equal(4, BinaryPrimitives.ReadInt32BigEndian(record.AsSpan(1828 + 28)));
        Assert.Equal(1, got.GetProperty("summary").GetProperty("clears").GetInt32());
        // An upload without the clear keeps the account's own story (and its clear).
        character.Flags[406] = 0;
        var updated = await client.PutJson("/api/v1/online-character", Upload(character, 700), ifMatch: "\"1\"");
        Assert.Equal(HttpStatusCode.OK, updated.StatusCode);
        Assert.Equal(1, (await updated.Json()).GetProperty("summary").GetProperty("clears").GetInt32());
        await using var sql = await host.SqlAsync();
        Assert.Equal(1, await sql.ExecuteScalarAsync<int>("SELECT clears FROM online_characters WHERE account_id = @id", new { id }));
    }

    [DbFact]
    public async Task A_guests_story_flag_reaches_the_owners_online_story_through_the_relay()
    {
        // (A visit counts after one second here: the per-visit rules of SEAMLESS.md 8.7 with a short minimum.)
        await using var host = await TestHost.StartAsync(RelaySettings.On(new() { ["Instances:StoryMinVisitSeconds"] = "1" }));
        var relayPort = await host.App.Services.GetRequiredService<RelayHub>().Started.WaitAsync(TimeSpan.FromSeconds(10));
        var (ownerToken, _, ownerId) = await host.RegisterAsync("StoryHost");
        var (guestToken, _, guestId) = await host.RegisterAsync("StoryGuest");
        var owner = host.Client(ownerToken);
        var guest = host.Client(guestToken);
        // The owner's online story so far (an earlier room).
        var start = new RoomStoryData { Revision = 7 };
        start.Flags[392] = 1;
        await host.App.Services.GetRequiredService<OnlineStoryService>().SaveAsync(ownerId, start);

        var created = await (await owner.PostAsJsonAsync("/api/v1/rooms", NewRoom())).Json();
        var code = created.GetProperty("room").GetProperty("code").GetString()!;
        var settingsHash = created.GetProperty("settingsHash").GetString()!;
        var joined = await (await guest.PostAsJsonAsync($"/api/v1/rooms/{code}/join", Join())).Json();

        await using var a = await Member.ConnectAsync(relayPort, created.GetProperty("relayTicket").GetString()!, settingsHash);
        await using var b = await Member.ConnectAsync(relayPort, joined.GetProperty("relayTicket").GetString()!, settingsHash);
        Assert.Equal(7u, a.FirstStory.Revision);
        Assert.Equal(1, RoomStoryData.FromBlob(b.FirstStory.Story, 7).Flags[392]);

        // The guest goes to its lobby (stagML12, its own solo instance) and makes a lobby flag there.
        await b.SendAsync(new BarrierMsg(b.Inst, b.Visit, 0, "stagml12", 0, 1, 1, 5, [1]).Frame());
        var ok = BarrierOkMsg.Parse(await b.NextAsync(Msg.BarrierOk));
        Assert.Equal(BarrierDecision.Dissolve, ok.Decision);
        Assert.Equal(7u, ok.StoryRevision);
        var watch = Stopwatch.StartNew();
        // Right after the barrier the visit is too young: the flag waits (the client sends it again).
        await b.SendAsync(new StoryMsg(ok.NewInst, ok.NewVisit, 7, new StoryDelta { Flags = [393] }).Frame());
        var early = StoryAckMsg.Parse(await b.NextAsync(Msg.StoryAck));
        Assert.Equal(StoryRefusal.Wait, early.Refused.Single().Reason);
        await Task.Delay(1200);
        await b.SendAsync(new StoryMsg(ok.NewInst, ok.NewVisit, 7, new StoryDelta { Flags = [393] }).Frame());
        var answer = StoryAckMsg.Parse(await b.NextAsync(Msg.StoryAck));
        Assert.Equal((8u, StoryAckMsg.Answer), (answer.Revision, answer.Flags));
        Assert.Empty(answer.Refused);
        var broadcast = StoryAckMsg.Parse(await a.NextAsync(Msg.StoryAck));
        Assert.Equal(8u, broadcast.Revision);

        // The owner's online story follows within ten seconds.
        var stories = host.App.Services.GetRequiredService<OnlineStoryService>();
        RoomStoryData saved;
        do
        {
            saved = await stories.LoadAsync(ownerId);
            if (saved.Revision < 8) await Task.Delay(50);
        } while (saved.Revision < 8 && watch.Elapsed < TimeSpan.FromSeconds(10));
        Assert.Equal(8u, saved.Revision);
        Assert.Equal(1, saved.Flags[393]);
        Assert.Equal(1, saved.Flags[392]);
        Assert.Equal(0u, (await stories.LoadAsync(guestId)).Revision); // the guest's own story is untouched

        var view = await (await guest.GetAsync($"/api/v1/rooms/{code}/story")).Json();
        Assert.Equal(8, view.GetProperty("revision").GetInt32());
        Assert.Contains(393, view.GetProperty("flags").EnumerateArray().Select(e => e.GetInt32()));
        Assert.Equal("drop", view.GetProperty("policy").GetString());
        var (otherToken, _, _) = await host.RegisterAsync("StoryOut");
        Assert.Equal("not_in_room", await (await host.Client(otherToken).GetAsync($"/api/v1/rooms/{code}/story")).ProblemCode());
    }

    /// <summary>
    /// Round 56 review: deleting a room owner's account left its room running with the deleted account's story keeper,
    /// and a guest's STORY afterwards made a new online_story row for the erased account. Now the owner's rooms close
    /// with the deletion (the guests get CLOSE, the room list drops it), and a save still on its way writes nothing.
    /// </summary>
    [DbFact]
    public async Task Deleting_a_room_owner_closes_its_room_and_no_story_is_saved_for_it()
    {
        await using var host = await TestHost.StartAsync(RelaySettings.On());
        var relayPort = await host.App.Services.GetRequiredService<RelayHub>().Started.WaitAsync(TimeSpan.FromSeconds(10));
        var (ownerToken, _, ownerId) = await host.RegisterAsync("GoneHost");
        var (guestToken, _, _) = await host.RegisterAsync("LeftGuest");
        var owner = host.Client(ownerToken);
        var guest = host.Client(guestToken);
        var created = await (await owner.PostAsJsonAsync("/api/v1/rooms", NewRoom())).Json();
        var code = created.GetProperty("room").GetProperty("code").GetString()!;
        var settingsHash = created.GetProperty("settingsHash").GetString()!;
        var joined = await (await guest.PostAsJsonAsync($"/api/v1/rooms/{code}/join", Join())).Json();
        await using var b = await Member.ConnectAsync(relayPort, joined.GetProperty("relayTicket").GetString()!, settingsHash);

        Assert.Equal(HttpStatusCode.NoContent, (await owner.PostAsJsonAsync("/api/v1/me/delete", new { currentPassword = "a decent password" })).StatusCode);
        // The guest's connection closes with the room; the public room list no longer shows the room or the name.
        await b.NextAsync(Msg.Close);
        var rooms = await (await host.Client().GetAsync("/api/v1/rooms")).Json();
        Assert.DoesNotContain(rooms.EnumerateArray(), r => r.GetProperty("code").GetString() == code);
        Assert.Equal("not_in_room", await (await guest.GetAsync($"/api/v1/rooms/{code}/story")).ProblemCode());
        await using var sql = await host.SqlAsync();
        Assert.Equal("host_left", await sql.ExecuteScalarAsync<string>("SELECT close_reason FROM rooms WHERE public_code = @code", new { code }));

        // A room story save of the owner that was still on its way: nothing is written for the erased account.
        var stories = host.App.Services.GetRequiredService<OnlineStoryService>();
        var late = new RoomStoryData { Revision = 3 };
        late.Flags[506] = 1;
        Assert.False(await stories.SaveAsync(ownerId, late));
        stories.Enqueue(ownerId, late);
        Assert.True(await stories.FlushAsync(TimeSpan.FromSeconds(5)));
        Assert.Equal(0, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM online_story WHERE account_id = @ownerId", new { ownerId }));
        Assert.Equal(0, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM online_story_history WHERE account_id = @ownerId", new { ownerId }));
    }

    /// <summary>
    /// Round 56 review: the online story had no history, no reset and no restore; a reset character started in the old
    /// story. Every revision is now kept; the character's reset empties the story (a new revision, the old one kept) and
    /// closes the account's own room; staff put a kept revision back as the next revision.
    /// </summary>
    [DbFact]
    public async Task The_story_keeps_its_revisions_is_emptied_by_the_characters_reset_and_staff_restore_one()
    {
        await using var host = await TestHost.StartAsync();
        var (adminToken, _, _) = await host.RegisterAsync("StoryStaff");
        var (token, _, id) = await host.RegisterAsync("StoryReset");
        Assert.True((await host.App.Services.GetRequiredService<DW4Online.Domain.Services.AccountService>()
            .AdminSetRoleAsync(null, "StoryStaff", "admin", new DW4Online.Domain.Services.CallerInfo(null, "tests"))).Ok);
        var client = host.Client(token);
        Assert.Equal(HttpStatusCode.Created, (await client.PutJson("/api/v1/online-character", Upload(Samples.Fresh("Kev"), 600), create: true)).StatusCode);
        var created = await (await client.PostAsJsonAsync("/api/v1/rooms", NewRoom())).Json();
        var code = created.GetProperty("room").GetProperty("code").GetString()!;
        var stories = host.App.Services.GetRequiredService<OnlineStoryService>();
        for (uint r = 1; r <= 3; ++r)
        {
            var s = new RoomStoryData { Revision = r };
            s.Flags[(int)r] = 1; // phase r (one-hot)
            s.Flags[505] = 1;
            s.Folder[0] = 1;
            Assert.True(await stories.SaveAsync(id, s));
        }
        // The reset closes the account's own room; the record's story is empty, the story's revision 4 is empty, 3 is kept.
        Assert.Equal(HttpStatusCode.NoContent,
            (await client.PostAsJsonAsync("/api/v1/online-character/reset", new { currentPassword = "a decent password" })).StatusCode);
        Assert.Equal("room_not_found", await (await client.GetAsync($"/api/v1/rooms/{code}")).ProblemCode());
        Assert.Equal(HttpStatusCode.Created, (await client.PutJson("/api/v1/online-character", Upload(Samples.Fresh("Ann"), 60), create: true)).StatusCode);
        var record = Convert.FromBase64String((await (await client.GetAsync("/api/v1/online-character")).Json()).GetProperty("record").GetString()!);
        Assert.Equal(0, record[792 + 3]);
        Assert.Equal(0, record[792 + 505]);
        Assert.Equal(0, record[1816]);
        var now = await stories.LoadAsync(id);
        Assert.Equal(4u, now.Revision);
        Assert.All(now.Flags, f => Assert.Equal(0, f));

        var admin = host.Client(adminToken);
        var history = await (await admin.GetAsync("/api/v1/admin/accounts/StoryReset/online-story/history")).Json();
        Assert.Equal([4, 3, 2, 1], history.EnumerateArray().Select(h => h.GetProperty("revision").GetInt32()));
        Assert.Equal("reset", history[0].GetProperty("reason").GetString());
        Assert.Equal(3, history[1].GetProperty("phase").GetInt32());
        Assert.Equal([0], history[1].GetProperty("keys").EnumerateArray().Select(k => k.GetInt32()));
        Assert.Equal(HttpStatusCode.Forbidden, (await client.GetAsync("/api/v1/admin/accounts/StoryReset/online-story/history")).StatusCode);

        // Staff put revision 2 back: revision 5 with its content.
        var restored = await (await admin.PostAsync("/api/v1/admin/accounts/StoryReset/online-story/restore/2", null)).Json();
        Assert.Equal(5, restored.GetProperty("revision").GetInt32());
        var back = await stories.LoadAsync(id);
        Assert.Equal((5u, (byte)1, (byte)0, (byte)1), (back.Revision, back.Flags[2], back.Flags[3], back.Flags[505]));
        Assert.Equal("revision_not_found", await (await admin.PostAsync("/api/v1/admin/accounts/StoryReset/online-story/restore/77", null)).ProblemCode());
        await using var sql = await host.SqlAsync();
        Assert.Equal(1, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM audit_log WHERE event = 'story_restore' AND account_id = @id", new { id }));
        // A room save older than the restore writes nothing.
        Assert.False(await stories.SaveAsync(id, new RoomStoryData { Revision = 4 }));
    }

    /// <summary>A raw DW4R2 member: handshake, WELCOME, the first STORY_ACK.</summary>
    private sealed class Member : IAsyncDisposable
    {
        private readonly RelayConnection _c;
        public uint Inst { get; private set; }
        public uint Visit { get; private set; }
        public StoryAckMsg FirstStory { get; private set; } = null!;

        private Member(RelayConnection c) => _c = c;

        public static async Task<Member> ConnectAsync(int port, string ticket, string settingsHash)
        {
            var m = new Member(await RelayConnection.OpenAsync(port, $"DW4R2 {ticket} 41 1234567890123 {settingsHash}"));
            Assert.StartsWith("OK ", await m._c.ReadLineAsync());
            var welcome = WelcomeMsg.Parse(await m.NextAsync(Msg.Welcome));
            (m.Inst, m.Visit) = (welcome.Inst, welcome.Visit);
            m.FirstStory = StoryAckMsg.Parse(await m.NextAsync(Msg.StoryAck));
            return m;
        }

        public async Task SendAsync(byte[] frame) => await _c.Stream.WriteAsync(frame);

        /// <summary>The payload of the next frame of that type (others are skipped).</summary>
        public async Task<byte[]> NextAsync(byte type)
        {
            for (var k = 0; k < 200; ++k)
            {
                var header = await _c.ReadExactlyAsync(Wire.HeaderBytes);
                var payload = await _c.ReadExactlyAsync((int)BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(1)));
                if (header[0] == type) return payload;
            }
            throw new InvalidOperationException("no frame " + Msg.Name(type));
        }

        public ValueTask DisposeAsync() => _c.DisposeAsync();
    }
}
