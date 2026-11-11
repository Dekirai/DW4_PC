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
    /// world it must not end the owner's instance (no StoryReplaced) until a kept revision is put back. (feat-i1a-web dcd6f3ae; lost
    /// in the merge 4b3d8a82, restored by fix-019.)</summary>
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
}
