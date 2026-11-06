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
    private static object Upload(SaveRecord record, long playSeconds) =>
        new { record = Convert.ToBase64String(RecordCodec.Encode(record)), playSeconds, context = new { protocol = TestHost.Protocol } };

    /// <summary>The world's Play (round I1): the world session keeps the story it starts from (reason session).</summary>
    private static object Enter() => new { box = 1, protocol = TestHost.Protocol, dataHash = 1234567890123UL, clientVersion = "0.60.0" };

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

    [DbFact(Skip = "Round I1a: rooms are retired (410); the relay lane ports these relay story tests to the world (lane contract).")]
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
    [DbFact(Skip = "Round I1a: rooms are retired (410); the relay lane ports these relay story tests to the world (lane contract).")]
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
        Assert.Equal(HttpStatusCode.OK, (await client.PostAsJsonAsync("/api/v1/world/enter", Enter())).StatusCode);
        var stories = host.App.Services.GetRequiredService<OnlineStoryService>();
        for (uint r = 1; r <= 3; ++r)
        {
            var s = new RoomStoryData { Revision = r };
            s.Flags[(int)r] = 1; // phase r (one-hot)
            s.Flags[505] = 1;
            s.Folder[0] = 1;
            Assert.True(await stories.SaveAsync(id, s));
        }
        // The reset tells the relay (its keeper reloads, a hosted instance ends: StoryReplaced); the record's story is empty,
        // the story's revision 4 is empty, 3 is kept.
        Assert.Equal(HttpStatusCode.NoContent,
            (await client.PostAsJsonAsync("/api/v1/online-character/reset", new { currentPassword = "a decent password" })).StatusCode);
        Assert.Contains($"story {id}/1", host.World!.Notes);
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

    /// <summary>
    /// The user's decision of 31.10 through the real relay (SEAMLESS.md 8.6): a room whose owner has no online story starts
    /// at zero. A guest's lobby flag and a phase claim two steps above the room before the owner's game is in a stage are
    /// dropped (not_started, the phase jump too: round 59's verification; the room API says the story has not started),
    /// the owner's lobby starts it, a value the guest's old visit offers only after the start is dropped as well, the
    /// guest's next barrier removes them all, and a value set anew afterwards becomes the owner's revision 1.
    /// </summary>
    [DbFact(Skip = "Round I1a: rooms are retired (410); the relay lane ports these relay story tests to the world (lane contract).")]
    public async Task A_new_owners_room_starts_at_zero_through_the_relay()
    {
        await using var host = await TestHost.StartAsync(RelaySettings.On(new() { ["Instances:StoryMinVisitSeconds"] = "1" }));
        var relayPort = await host.App.Services.GetRequiredService<RelayHub>().Started.WaitAsync(TimeSpan.FromSeconds(10));
        var (ownerToken, _, ownerId) = await host.RegisterAsync("ZeroHost");
        var (guestToken, _, _) = await host.RegisterAsync("ZeroGuest");
        var owner = host.Client(ownerToken);
        var guest = host.Client(guestToken);
        var created = await (await owner.PostAsJsonAsync("/api/v1/rooms", NewRoom())).Json();
        var code = created.GetProperty("room").GetProperty("code").GetString()!;
        var settingsHash = created.GetProperty("settingsHash").GetString()!;
        var joined = await (await guest.PostAsJsonAsync($"/api/v1/rooms/{code}/join", Join())).Json();
        await using var a = await Member.ConnectAsync(relayPort, created.GetProperty("relayTicket").GetString()!, settingsHash);
        await using var b = await Member.ConnectAsync(relayPort, joined.GetProperty("relayTicket").GetString()!, settingsHash);
        Assert.Equal(0u, b.FirstStory.Revision);

        // The guest plays its lobby while the owner sits at the title.
        await b.SendAsync(new BarrierMsg(b.Inst, b.Visit, 0, "stagml12", 0, 1, 1, 5, [1]).Frame());
        var ok = BarrierOkMsg.Parse(await b.NextAsync(Msg.BarrierOk));
        await Task.Delay(1200);
        await b.SendAsync(new StoryMsg(ok.NewInst, ok.NewVisit, 0, new StoryDelta { Phase = 2, Flags = [393] }).Frame());
        var refused = StoryAckMsg.Parse(await b.NextAsync(Msg.StoryAck));
        Assert.Equal([new(StoryKeyKind.Phase, 2, StoryRefusal.NotStarted), new StoryRefused(StoryKeyKind.Flag, 393, StoryRefusal.NotStarted)],
            refused.Refused);
        var view = await (await guest.GetAsync($"/api/v1/rooms/{code}/story")).Json();
        Assert.False(view.GetProperty("started").GetBoolean());
        Assert.Equal([2, 393], view.GetProperty("droppedFlags").EnumerateArray().Select(e => e.GetInt32()));

        // The owner's game enters its lobby: the story starts (at zero).
        await a.SendAsync(new BarrierMsg(a.Inst, a.Visit, 0, "stagml11", 0, 1, 1, 5, [0]).Frame());
        BarrierOkMsg.Parse(await a.NextAsync(Msg.BarrierOk));
        Assert.True((await (await guest.GetAsync($"/api/v1/rooms/{code}/story")).Json()).GetProperty("started").GetBoolean());
        // The guest's visit from before the start offers 392 only now: dropped as well.
        await b.SendAsync(new StoryMsg(ok.NewInst, ok.NewVisit, 0, new StoryDelta { Flags = [392] }).Frame());
        Assert.Equal(new StoryRefused(StoryKeyKind.Flag, 392, StoryRefusal.NotStarted), StoryAckMsg.Parse(await b.NextAsync(Msg.StoryAck)).Refused.Single());

        // The guest's barrier removes them (the dropped set); set anew in its next visit, 393 counts.
        await b.SendAsync(new BarrierMsg(ok.NewInst, ok.NewVisit, 0, "stagml13", 0, 1, 1, 5, [1]).Frame());
        var next = BarrierOkMsg.Parse(await b.NextAsync(Msg.BarrierOk));
        Assert.Equal([2, 392, 393], StoryKeys.FromBlob(next.Dropped).Flags);
        await b.SendAsync(new BarrierMsg(next.NewInst, next.NewVisit, 0, "stagml12", 0, 1, 1, 5, [1]).Frame());
        var again = BarrierOkMsg.Parse(await b.NextAsync(Msg.BarrierOk));
        await Task.Delay(1200);
        await b.SendAsync(new StoryMsg(again.NewInst, again.NewVisit, 0, new StoryDelta { Flags = [393] }).Frame());
        var taken = StoryAckMsg.Parse(await b.NextAsync(Msg.StoryAck));
        Assert.Empty(taken.Refused);
        Assert.Equal(1u, taken.Revision);
        var stories = host.App.Services.GetRequiredService<OnlineStoryService>();
        var watch = Stopwatch.StartNew();
        while ((await stories.LoadAsync(ownerId)).Revision < 1 && watch.Elapsed < TimeSpan.FromSeconds(10)) await Task.Delay(50);
        Assert.Equal(1, (await stories.LoadAsync(ownerId)).Flags[393]);
    }

    /// <summary>
    /// Round 59 review: a member forging a revision a second pushed every honest revision out of a history that kept the
    /// last 100, so staff could not put the owner's story back. The story a room session starts from is kept as a session
    /// start, the newest revisions in full, older room revisions as the first of every ten minutes for 90 days; session
    /// starts, resets and restores stay (the newest few for good).
    /// </summary>
    [DbFact]
    public async Task The_history_keeps_every_session_start_and_thins_old_revisions_by_time()
    {
        await using var host = await TestHost.StartAsync(new() { ["OnlineCharacter:StoryHistoryRevisions"] = "10", ["OnlineCharacter:StoryHistorySessions"] = "3" });
        var (adminToken, _, _) = await host.RegisterAsync("HistoryStaff");
        Assert.True((await host.App.Services.GetRequiredService<DW4Online.Domain.Services.AccountService>()
            .AdminSetRoleAsync(null, "HistoryStaff", "admin", new DW4Online.Domain.Services.CallerInfo(null, "tests"))).Ok);
        var (token, _, id) = await host.RegisterAsync("HistoryHost");
        var admin = host.Client(adminToken);
        var stories = host.App.Services.GetRequiredService<OnlineStoryService>();
        var honest = new RoomStoryData { Revision = 1 };
        honest.Flags[1] = 1;
        honest.Flags[505] = 1;
        Assert.True(await stories.SaveAsync(id, honest));
        Assert.Equal(HttpStatusCode.OK, (await host.Client(token).PostAsJsonAsync("/api/v1/world/enter", Enter())).StatusCode);
        // A session start of the same revision (Play pressed again and again without play) is kept once.
        await stories.KeepSessionStartAsync(id);
        await stories.KeepSessionStartAsync(id);
        await using (var check = await host.SqlAsync())
            Assert.Equal(1, await check.ExecuteScalarAsync<int>(
                "SELECT COUNT(*) FROM online_story_history WHERE account_id = @id AND reason = 'session'", new { id }));
        // 140 forged revisions in a little over two minutes.
        for (uint r = 2; r <= 141; ++r)
        {
            host.Clock.Advance(TimeSpan.FromSeconds(1));
            var forged = new RoomStoryData { Revision = r };
            forged.Flags[86] = 1;
            forged.Counters[1] = (int)r;
            Assert.True(await stories.SaveAsync(id, forged));
        }
        var history = (await (await admin.GetAsync("/api/v1/admin/accounts/HistoryHost/online-story/history")).Json()).EnumerateArray().ToList();
        Assert.Equal(12, history.Count); // the newest 10, the first room revision of the ten minutes, the session start
        Assert.Contains(history, h => h.GetProperty("revision").GetInt32() == 1 && h.GetProperty("reason").GetString() == "session");
        Assert.Equal(142, (await (await admin.PostAsync("/api/v1/admin/accounts/HistoryHost/online-story/restore/1", null)).Json()).GetProperty("revision").GetInt32());
        var back = await stories.LoadAsync(id);
        Assert.Equal((142u, (byte)1, (byte)1, (byte)0), (back.Revision, back.Flags[1], back.Flags[505], back.Flags[86]));

        // An hour of revisions every two minutes, then a hundred days later one more: the room revisions older than 90 days
        // go, the session start and the restore stay (the newest three such).
        for (uint r = 143; r <= 172; ++r)
        {
            host.Clock.Advance(TimeSpan.FromMinutes(2));
            Assert.True(await stories.SaveAsync(id, new RoomStoryData { Revision = r }));
        }
        await using var sql = await host.SqlAsync();
        var roomRows = await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM online_story_history WHERE account_id = @id AND reason = 'room'", new { id });
        Assert.InRange(roomRows, 14, 16); // the newest 10, then the first of every ten minutes (5)
        host.Clock.Advance(TimeSpan.FromDays(100));
        Assert.True(await stories.SaveAsync(id, new RoomStoryData { Revision = 173 }));
        var reasons = (await sql.QueryAsync<(long Revision, string Reason)>(
            "SELECT revision, reason FROM online_story_history WHERE account_id = @id ORDER BY id", new { id })).ToList();
        Assert.Equal(12, reasons.Count);
        Assert.Equal((1L, "session"), reasons[0]);
        Assert.Equal((142L, "admin_restore"), reasons[1]);
        Assert.All(reasons.Skip(2), r => Assert.Equal("room", r.Reason));
    }

    /// <summary>Round 59 review: a staff restore of a revision that is not kept closed the owner's room before it failed; in the
    /// world it must not end the owner's instance (no StoryReplaced) until a kept revision is put back.</summary>
    [DbFact]
    public async Task A_restore_of_a_revision_that_is_not_kept_leaves_the_owners_instance_running()
    {
        await using var host = await TestHost.StartAsync();
        var (adminToken, _, _) = await host.RegisterAsync("RestoreStaff");
        Assert.True((await host.App.Services.GetRequiredService<DW4Online.Domain.Services.AccountService>()
            .AdminSetRoleAsync(null, "RestoreStaff", "admin", new DW4Online.Domain.Services.CallerInfo(null, "tests"))).Ok);
        var (token, _, id) = await host.RegisterAsync("RestoreHost");
        var s = new RoomStoryData { Revision = 1 };
        s.Flags[505] = 1;
        Assert.True(await host.App.Services.GetRequiredService<OnlineStoryService>().SaveAsync(id, s));
        var owner = host.Client(token);
        Assert.Equal(HttpStatusCode.OK, (await owner.PostAsJsonAsync("/api/v1/world/enter", Enter())).StatusCode); // keeps revision 1 (session)
        var admin = host.Client(adminToken);
        Assert.Equal("revision_not_found", await (await admin.PostAsync("/api/v1/admin/accounts/RestoreHost/online-story/restore/999999", null)).ProblemCode());
        Assert.DoesNotContain($"story {id}/1", host.World!.Notes);
        Assert.Equal(HttpStatusCode.OK, (await admin.PostAsync("/api/v1/admin/accounts/RestoreHost/online-story/restore/1", null)).StatusCode);
        Assert.Contains($"story {id}/1", host.World.Notes);
    }

    /// <summary>
    /// Round 59 review: a guest in a Normal room without a clear in the room's story got clear_normal and the speedrun
    /// board's first place with one request, and mp_fullhouse / mp_couch alone in its lobby. A story clear in a room now
    /// needs the room's story to hold that difficulty's clear flag, and the players claim is capped by the relay's instance.
    /// </summary>
    [DbFact(Skip = "Round I1a: rooms are retired (410); the relay lane ports these relay story tests to the world (lane contract).")]
    public async Task A_story_clear_in_a_room_needs_the_rooms_story_and_players_count_as_the_relay_saw_them()
    {
        await using var host = await TestHost.StartAsync(RelaySettings.On(new() { ["Play:ClearStoryWaitSeconds"] = "1" }));
        var relayPort = await host.App.Services.GetRequiredService<RelayHub>().Started.WaitAsync(TimeSpan.FromSeconds(10));
        var (ownerToken, _, _) = await host.RegisterAsync("ClearHost");
        var (clearedToken, _, clearedId) = await host.RegisterAsync("ClearedHost");
        var (guestToken, _, _) = await host.RegisterAsync("ClearGuest");
        var guest = host.Client(guestToken);
        var created = await (await host.Client(ownerToken).PostAsJsonAsync("/api/v1/rooms", NewRoom())).Json();
        var code = created.GetProperty("room").GetProperty("code").GetString()!;
        var joined = await (await guest.PostAsJsonAsync($"/api/v1/rooms/{code}/join", Join())).Json();
        await using var b = await Member.ConnectAsync(relayPort, joined.GetProperty("relayTicket").GetString()!, created.GetProperty("settingsHash").GetString()!);
        object Claim(string what, string room, int players) =>
            new { code = what, context = new { roomCode = room, players }, evidence = new { igtMs = 2_700_001L } };
        Assert.Equal("requirement_not_met", await (await guest.PostAsJsonAsync("/api/v1/achievements/unlock", Claim("clear_normal", code, 1))).ProblemCode());
        Assert.Equal("requirement_not_met", await (await guest.PostAsJsonAsync("/api/v1/achievements/unlock", Claim("mp_couch", code, 2))).ProblemCode());

        // A room whose owner cleared Normal: the guest's clear counts there.
        var cleared = new RoomStoryData { Revision = 3 };
        cleared.Flags[86] = 1;
        cleared.Flags[713] = 1;
        cleared.Flags[406] = 1;
        Assert.True(await host.App.Services.GetRequiredService<OnlineStoryService>().SaveAsync(clearedId, cleared));
        var second = await (await host.Client(clearedToken).PostAsJsonAsync("/api/v1/rooms", NewRoom())).Json();
        var code2 = second.GetProperty("room").GetProperty("code").GetString()!;
        var joined2 = await (await guest.PostAsJsonAsync($"/api/v1/rooms/{code2}/join", Join())).Json();
        await using var b2 = await Member.ConnectAsync(relayPort, joined2.GetProperty("relayTicket").GetString()!, second.GetProperty("settingsHash").GetString()!);
        var ok = await guest.PostAsJsonAsync("/api/v1/achievements/unlock", Claim("clear_normal", code2, 1));
        Assert.Equal(HttpStatusCode.OK, ok.StatusCode);
        Assert.True((await ok.Json()).GetProperty("newlyUnlocked").GetBoolean());
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
