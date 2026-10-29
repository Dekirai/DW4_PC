using System.Net;

namespace DW4Online.Integration.Tests;

public class ServerHostTests
{
    [DbFact]
    public async Task Settings_file_and_acme_challenge_folder()
    {
        var dir = Path.Combine(Path.GetTempPath(), "dw4online-it-" + Guid.NewGuid().ToString("N")[..8]);
        Directory.CreateDirectory(Path.Combine(dir, "acme"));
        try
        {
            await File.WriteAllTextAsync(Path.Combine(dir, "acme", "tok-EN_123"), "tok-EN_123.key");
            var settings = Path.Combine(dir, "appsettings.Production.json");
            await File.WriteAllTextAsync(settings,
                "{ \"Service\": { \"Motd\": \"from the settings file\" }, \"Tls\": { \"AcmeChallengeDirectory\": " +
                System.Text.Json.JsonSerializer.Serialize(Path.Combine(dir, "acme")) + " } }");
            await using var host = await TestHost.StartAsync(settingsFile: settings);
            var status = await (await host.Client().GetAsync("/api/v1/status")).Json();
            Assert.Equal("from the settings file", status.GetProperty("motd").GetString());
            var challenge = await host.Client().GetAsync("/.well-known/acme-challenge/tok-EN_123");
            Assert.Equal(HttpStatusCode.OK, challenge.StatusCode);
            Assert.Equal("tok-EN_123.key", await challenge.Content.ReadAsStringAsync());
            Assert.Equal(HttpStatusCode.NotFound, (await host.Client().GetAsync("/.well-known/acme-challenge/missing")).StatusCode);
            Assert.Equal(HttpStatusCode.NotFound, (await host.Client().GetAsync("/.well-known/acme-challenge/..%5Cappsettings.Production.json")).StatusCode);
        }
        finally
        {
            Directory.Delete(dir, true);
        }
    }
}
