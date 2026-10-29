using System.Globalization;
using System.Net;
using System.Net.Http.Json;
using System.Net.Security;
using System.Security.Cryptography;
using System.Text.Json;
using DW4Online.FakeClient;

// DW4Online.FakeClient: fake DW4R2 clients, the impairment proxy and the loss matrix (SEAMLESS.md 6.4, 16).
//
//   client  --port N [--host 127.0.0.1] (--ticket T | --api URL --token BEARER --room CODE [--pin BASE64])
//           [--protocol 43] [--data-hash 1] [--settings-hash HEX] [--name NAME] [--quest N] [--seed N] [--script FILE]
//           [--duration SECONDS] [--replay-rate N] [--entry-ticks 120] [--depart-ticks 120] [--log FILE] [--stats FILE]
//   proxy   --listen PORT --target PORT [--one-way MS] [--jitter MS] [--loss P]
//   matrix  [--ports 53100-53199] [--clients 3] [--warmup 20] [--seconds 40] [--parallel 3] [--out FILE.json] [--md FILE.md] [--quick]
var args2 = args.Skip(1).ToArray();
string Opt(string name, string fallback = "")
{
    var i = Array.IndexOf(args2, name);
    return i >= 0 && i + 1 < args2.Length ? args2[i + 1] : fallback;
}
bool Flag(string name) => args2.Contains(name);

using var cts = new CancellationTokenSource();
Console.CancelKeyPress += (_, e) =>
{
    e.Cancel = true;
    cts.Cancel();
};

switch (args.FirstOrDefault())
{
    case "client":
    {
        var logPath = Opt("--log");
        StreamWriter? log = logPath.Length > 0 ? new StreamWriter(logPath, append: false) { AutoFlush = true } : null;
        var logLock = new Lock();
        var script = Opt("--script") is { Length: > 0 } sp
            ? JsonSerializer.Deserialize<List<ScriptAction>>(File.ReadAllText(sp), new JsonSerializerOptions { PropertyNameCaseInsensitive = true }) ?? []
            : [];
        Func<CancellationToken, Task<string>> ticket;
        if (Opt("--ticket") is { Length: > 0 } fixedTicket) ticket = _ => Task.FromResult(fixedTicket);
        else
        {
            var api = Opt("--api").TrimEnd('/');
            var token = Opt("--token");
            var room = Opt("--room");
            var pin = Opt("--pin");
            ticket = async ct =>
            {
                using var handler = new HttpClientHandler();
                if (pin.Length > 0)
                    handler.ServerCertificateCustomValidationCallback = (_, cert, _, _) =>
                        cert is not null && Convert.ToBase64String(SHA256.HashData(cert.PublicKey.ExportSubjectPublicKeyInfo())) == pin;
                using var http = new HttpClient(handler);
                http.DefaultRequestHeaders.Authorization = new System.Net.Http.Headers.AuthenticationHeaderValue("Bearer", token);
                var r = await http.PostAsync(api + "/api/v1/rooms/" + room + "/ticket", null, ct);
                r.EnsureSuccessStatusCode();
                var body = await r.Content.ReadFromJsonAsync<JsonElement>(ct);
                return body.GetProperty("relayTicket").GetString()!;
            };
        }
        var client = new FakeClient(new FakeClientOptions
        {
            Host = Opt("--host", "127.0.0.1"),
            Port = int.Parse(Opt("--port"), CultureInfo.InvariantCulture),
            Ticket = ticket,
            Protocol = int.Parse(Opt("--protocol", "43"), CultureInfo.InvariantCulture),
            DataHash = ulong.Parse(Opt("--data-hash", "1"), CultureInfo.InvariantCulture),
            SettingsHash = ulong.Parse(Opt("--settings-hash", "0"), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture),
            Name = Opt("--name", "fake"),
            Quest = byte.Parse(Opt("--quest", "0"), CultureInfo.InvariantCulture),
            Seed = int.Parse(Opt("--seed", "1"), CultureInfo.InvariantCulture),
            Script = script,
            Duration = TimeSpan.FromSeconds(double.Parse(Opt("--duration", "30"), CultureInfo.InvariantCulture)),
            ReplayRate = int.Parse(Opt("--replay-rate", "3000"), CultureInfo.InvariantCulture),
            EntryTicks = int.Parse(Opt("--entry-ticks", "120"), CultureInfo.InvariantCulture),
            DepartTicks = int.Parse(Opt("--depart-ticks", "120"), CultureInfo.InvariantCulture),
            Log = line =>
            {
                lock (logLock) log?.WriteLine(line);
            },
        });
        var stats = await client.RunAsync(cts.Token);
        if (Opt("--stats") is { Length: > 0 } statsPath)
        {
            var dump = new
            {
                stats.SealedExecuted, stats.Hitches, stats.StallMs, stats.OwnPadsSent, stats.OwnPadsSealed, stats.OwnPadsFilled,
                stats.Delays, stats.Frames, stats.CloseReason,
                fingerprints = stats.Fingerprints.Select(kv => new { inst = kv.Key.Inst, visit = kv.Key.Visit, tick = kv.Key.Tick, fp = kv.Value.ToString("x16") }),
            };
            await File.WriteAllTextAsync(statsPath, JsonSerializer.Serialize(dump));
        }
        log?.Dispose();
        return stats.CloseReason is null || stats.CloseReason.StartsWith("Left", StringComparison.Ordinal) ? 0 : 2;
    }
    case "proxy":
    {
        var imp = new Impairment(double.Parse(Opt("--one-way", "0"), CultureInfo.InvariantCulture),
            double.Parse(Opt("--jitter", "0"), CultureInfo.InvariantCulture), double.Parse(Opt("--loss", "0"), CultureInfo.InvariantCulture));
        await using var proxy = new ImpairmentProxy(new IPEndPoint(IPAddress.Loopback, int.Parse(Opt("--listen"), CultureInfo.InvariantCulture)),
            new IPEndPoint(IPAddress.Loopback, int.Parse(Opt("--target"), CultureInfo.InvariantCulture)), imp, imp, Environment.ProcessId);
        Console.WriteLine($"proxy {proxy.Port} -> {Opt("--target")} ({imp})");
        try
        {
            await Task.Delay(Timeout.Infinite, cts.Token);
        }
        catch (OperationCanceledException)
        {
        }
        return 0;
    }
    case "matrix":
    {
        var range = Opt("--ports", "53100-53199").Split('-');
        var ports = new PortPool(int.Parse(range[0], CultureInfo.InvariantCulture), int.Parse(range[1], CultureInfo.InvariantCulture));
        var cells = LossMatrix.DefaultCells().ToList();
        if (Flag("--quick")) cells = [new MatrixCell(30, 10, 0.01), new MatrixCell(10, 0, 0)];
        var outPath = Opt("--out", "loss_matrix.json");
        var lines = new List<string>();
        var results = await LossMatrix.RunAsync(cells, ports, int.Parse(Opt("--clients", "3"), CultureInfo.InvariantCulture),
            TimeSpan.FromSeconds(double.Parse(Opt("--warmup", "20"), CultureInfo.InvariantCulture)),
            TimeSpan.FromSeconds(double.Parse(Opt("--seconds", "40"), CultureInfo.InvariantCulture)),
            int.Parse(Opt("--parallel", "3"), CultureInfo.InvariantCulture), line =>
            {
                lock (lines) lines.Add(line);
                Console.WriteLine(line);
            }, cts.Token);
        await File.WriteAllTextAsync(outPath, JsonSerializer.Serialize(results, new JsonSerializerOptions { WriteIndented = true }));
        var md = LossMatrix.Markdown(results);
        if (Opt("--md") is { Length: > 0 } mdPath) await File.WriteAllTextAsync(mdPath, md);
        Console.WriteLine(md);
        return 0;
    }
    default:
        Console.Error.WriteLine("usage: DW4Online.FakeClient client|proxy|matrix ... (see Program.cs)");
        return 1;
}
