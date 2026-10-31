using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Security.Cryptography;
using Dapper;
using DW4Online.Domain.Save;
using DW4Online.Tests;

namespace DW4Online.Integration.Tests;

public class CloudSaveTests
{
    private static byte[] Save(int bits)
    {
        var record = Samples.Fresh();
        record.Bits = bits;
        var file = SaveFile.Write(new byte[SaveFile.HeaderSize], [record, new SaveRecord(), new SaveRecord(), new SaveRecord()]);
        return new GciEntry().Encode().Concat(file).ToArray();
    }

    private static Task<HttpResponseMessage> PutAsync(HttpClient client, byte[] data, string? ifMatch = null, bool create = false, string? sha = null)
    {
        var request = new HttpRequestMessage(HttpMethod.Put, "/api/v1/cloud-save") { Content = new ByteArrayContent(data) };
        request.Content.Headers.ContentType = new MediaTypeHeaderValue("application/octet-stream");
        request.Headers.TryAddWithoutValidation("X-Sha256", sha ?? Convert.ToHexString(SHA256.HashData(data)));
        request.Headers.TryAddWithoutValidation("X-Device-Name", "KEVIN-PC");
        if (ifMatch is not null) request.Headers.TryAddWithoutValidation("If-Match", ifMatch);
        if (create) request.Headers.TryAddWithoutValidation("If-None-Match", "*");
        return client.SendAsync(request);
    }

    [DbFact]
    public async Task First_backups_of_many_accounts_at_once_never_conflict()
    {
        // As the online character: another account's deadlock is no concurrent upload of this account.
        await using var host = await TestHost.StartAsync();
        var clients = new List<HttpClient>();
        for (var i = 0; i < 12; ++i) clients.Add(host.Client((await host.RegisterAsync("Backup" + i)).Item1));
        var created = await Task.WhenAll(clients.Select((client, i) => PutAsync(client, Save(i), create: true)));
        Assert.All(created, r => Assert.Equal(HttpStatusCode.Created, r.StatusCode));
        var updated = await Task.WhenAll(clients.Select((client, i) => PutAsync(client, Save(100 + i), ifMatch: "\"1\"")));
        Assert.All(updated, r => Assert.Equal(HttpStatusCode.OK, r.StatusCode));
    }

    [DbFact]
    public async Task Upload_download_history_restore_delete()
    {
        await using var host = await TestHost.StartAsync();
        var (token, _, _) = await host.RegisterAsync("Saver");
        var client = host.Client(token);

        Assert.Equal("no_cloud_save", await (await client.GetAsync("/api/v1/cloud-save/meta")).ProblemCode());
        Assert.Equal(HttpStatusCode.PreconditionRequired, (await PutAsync(client, Save(1))).StatusCode);
        Assert.Equal("sha256_mismatch", await (await PutAsync(client, Save(1), create: true, sha: new string('0', 64))).ProblemCode());
        Assert.Equal("invalid_save", await (await PutAsync(client, new byte[90176], create: true)).ProblemCode());

        var created = await PutAsync(client, Save(1), create: true);
        Assert.Equal(HttpStatusCode.Created, created.StatusCode);
        Assert.Equal("\"1\"", created.Headers.ETag?.Tag);
        Assert.Equal("cloud_save_exists", await (await PutAsync(client, Save(2), create: true)).ProblemCode());

        for (var rev = 1; rev <= 6; ++rev)
            Assert.Equal(HttpStatusCode.OK, (await PutAsync(client, Save(100 + rev), ifMatch: rev.ToString())).StatusCode);
        Assert.Equal(HttpStatusCode.Conflict, (await PutAsync(client, Save(9), ifMatch: "3")).StatusCode);

        var meta = await (await client.GetAsync("/api/v1/cloud-save/meta")).Json();
        Assert.Equal(7, meta.GetProperty("latest").GetProperty("revision").GetInt32());
        Assert.Equal("gci", meta.GetProperty("latest").GetProperty("format").GetString());
        Assert.Equal("KEVIN-PC", meta.GetProperty("latest").GetProperty("deviceName").GetString());
        Assert.Equal([6, 5, 4, 3, 2], meta.GetProperty("history").EnumerateArray().Select(h => h.GetProperty("revision").GetInt32()));
        Assert.Equal("Kevin", meta.GetProperty("boxes")[0].GetProperty("playerName").GetString());

        var latest = await client.GetAsync("/api/v1/cloud-save");
        var bytes = await latest.Content.ReadAsByteArrayAsync();
        Assert.Equal(Save(106), bytes);
        Assert.Equal(Convert.ToHexString(SHA256.HashData(bytes)), latest.Headers.GetValues("X-Sha256").Single());
        Assert.Equal(Save(102), await (await client.GetAsync("/api/v1/cloud-save?revision=3")).Content.ReadAsByteArrayAsync());
        Assert.Equal(HttpStatusCode.NotFound, (await client.GetAsync("/api/v1/cloud-save?revision=1")).StatusCode); // pruned

        var restored = await client.PostAsync("/api/v1/cloud-save/restore/3", null);
        Assert.Equal(HttpStatusCode.OK, restored.StatusCode);
        Assert.Equal(8, (await restored.Json()).GetProperty("revision").GetInt32());
        Assert.Equal(Save(102), await (await client.GetAsync("/api/v1/cloud-save")).Content.ReadAsByteArrayAsync());

        // Deleting needs the current password: a token alone does not destroy the backup.
        Assert.Equal(HttpStatusCode.MethodNotAllowed, (await client.DeleteAsync("/api/v1/cloud-save")).StatusCode);
        Assert.Equal("wrong_password", await (await client.PostAsJsonAsync("/api/v1/cloud-save/delete", new { currentPassword = "not my password" })).ProblemCode());
        Assert.Equal("wrong_password", await (await client.PostAsJsonAsync("/api/v1/cloud-save/delete", new { })).ProblemCode());
        Assert.Equal(HttpStatusCode.OK, (await client.GetAsync("/api/v1/cloud-save")).StatusCode);
        Assert.Equal(HttpStatusCode.NoContent, (await client.PostAsJsonAsync("/api/v1/cloud-save/delete", new { currentPassword = "a decent password" })).StatusCode);
        Assert.Equal(HttpStatusCode.NotFound, (await client.GetAsync("/api/v1/cloud-save")).StatusCode);
        Assert.Equal(HttpStatusCode.NotFound, (await client.GetAsync("/api/v1/cloud-save?revision=3")).StatusCode);
        Assert.Equal("no_cloud_save", await (await client.PostAsJsonAsync("/api/v1/cloud-save/delete", new { currentPassword = "a decent password" })).ProblemCode());
        await using var sql = await host.SqlAsync();
        Assert.Equal(2, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM audit_log WHERE event = 'confirm_fail'"));
        Assert.Equal(1, await sql.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM audit_log WHERE event = 'cloud_delete'"));
    }

    [DbFact]
    public async Task Website_deletes_the_cloud_save_only_with_the_password()
    {
        await using var host = await TestHost.StartAsync();
        using var web = new WebBrowser(host);
        await web.RegisterAsync("WebSaver");
        var (token, _) = await host.LoginAsync("WebSaver");
        Assert.Equal(HttpStatusCode.Created, (await PutAsync(host.Client(token), Save(7), create: true)).StatusCode);

        var page = await web.GetAsync("/account");
        Assert.Contains("Delete cloud save", page.Html);
        var unticked = await web.PostAsync("/account?handler=DeleteCloud", new() { ["CurrentPassword"] = "a decent password" }, "/account");
        Assert.Contains("Tick the box", unticked.Html);
        var wrong = await web.PostAsync("/account?handler=DeleteCloud",
            new() { ["CurrentPassword"] = "a wrong password", ["ConfirmCloudDelete"] = "true" }, "/account");
        Assert.Contains("The current password is wrong.", wrong.Html);
        Assert.Equal(HttpStatusCode.OK, (await host.Client(token).GetAsync("/api/v1/cloud-save")).StatusCode);

        var done = await web.PostAsync("/account?handler=DeleteCloud",
            new() { ["CurrentPassword"] = "a decent password", ["ConfirmCloudDelete"] = "true" }, "/account");
        Assert.Contains("Your cloud save and its older revisions were deleted.", done.Html);
        Assert.Contains("No cloud save yet.", done.Html);
        Assert.Equal(HttpStatusCode.NotFound, (await host.Client(token).GetAsync("/api/v1/cloud-save")).StatusCode);
    }

    [DbFact]
    public async Task Size_limit_and_privacy()
    {
        await using var host = await TestHost.StartAsync(new() { ["CloudSave:MaxBytes"] = "100000" });
        var (token, _, _) = await host.RegisterAsync("Big");
        var tooLarge = await PutAsync(host.Client(token), new byte[100_001], create: true);
        Assert.Equal(HttpStatusCode.RequestEntityTooLarge, tooLarge.StatusCode);
        Assert.Equal("payload_too_large", await tooLarge.ProblemCode());
        Assert.Equal(HttpStatusCode.Created, (await PutAsync(host.Client(token), Save(5), create: true)).StatusCode);

        var (other, _, _) = await host.RegisterAsync("Nosy");
        Assert.Equal(HttpStatusCode.NotFound, (await host.Client(other).GetAsync("/api/v1/cloud-save")).StatusCode); // only one's own
        Assert.Equal(HttpStatusCode.Unauthorized, (await host.Client().GetAsync("/api/v1/cloud-save")).StatusCode);
    }
}
