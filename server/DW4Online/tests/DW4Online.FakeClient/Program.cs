using System.Globalization;
using System.Net;
using System.Net.Http.Json;
using System.Net.Security;
using System.Security.Cryptography;
using System.Text.Json;
using DW4Online.FakeClient;
using DW4Online.Relay.Protocol;

// DW4Online.FakeClient: fake DW4R2 clients, the impairment proxy and the loss matrix (SEAMLESS.md 6.4, 16).
//
//   client  --port N [--host 127.0.0.1] (--ticket T | --api URL --token BEARER --room CODE [--pin BASE64])
//           [--protocol 43] [--data-hash 1] [--settings-hash HEX] [--name NAME] [--quest N] [--context HEX] [--seed N] [--script FILE]
//           [--duration SECONDS] [--replay-rate N] [--entry-ticks 120] [--depart-ticks 120] [--log FILE] [--stats FILE]
//   proxy   --listen PORT --target PORT [--one-way MS] [--jitter MS] [--loss P] [--up MS,JITTER,LOSS] [--down MS,JITTER,LOSS]
//           [--cut EPOCH,SECONDS[,half|close]] (a scheduled cut of the link, the world's host grace scenarios)
//           (--up / --down: an asymmetric link, client to server / server to client; each overrides the symmetric values)
//   matrix  [--ports 53100-53199] [--clients 3] [--warmup 20] [--seconds 40] [--parallel 3] [--out FILE.json] [--md FILE.md] [--quick]
//   loadgate [--seconds 30] [--threads 8] [--cells base,fullhub,select,storm,joins] [--out FILE.json]
//           [--sockets --port 56810] [--protocol 48]
//           round I1a's load gate (SEAMLESS.md 19.17.4): on one in-process hub (LoadGate.cs), or with --sockets against a
//           real relay in a child process on loopback, one port per cell from --port (LoadGateNet.cs; not the joins cell).
//           Fails (exit 1) when a cell's run is invalid (frames the hub refused or dropped).
//   loadgate-relay --port P --key HEX   the relay process of loadgate --sockets (commands on stdin)
//   joins   [--minutes 1,10,25] [--out FILE.json] [--protocol 48]   the lock's holds of one classic join per log length, on an idle hub
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
                // Round I1 (SEAMLESS.md 19.19): a world ticket (POST /world/ticket; --room is no longer used).
                var r = await http.PostAsJsonAsync(api + "/api/v1/world/ticket", new { protocol = Wire.WorldProtocol }, ct);
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
            Protocol = int.Parse(Opt("--protocol", Wire.WorldProtocol.ToString(CultureInfo.InvariantCulture)), CultureInfo.InvariantCulture),
            DataHash = ulong.Parse(Opt("--data-hash", "1"), CultureInfo.InvariantCulture),
            SettingsHash = ulong.Parse(Opt("--settings-hash", "0"), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture),
            Name = Opt("--name", "fake"),
            Quest = byte.Parse(Opt("--quest", "0"), CultureInfo.InvariantCulture),
            Context = Opt("--context") is { Length: > 0 } context ? ulong.Parse(context, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture) : null,
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
        static Impairment Direction(string text, Impairment fallback)
        {
            if (text.Length == 0) return fallback;
            var parts = text.Split(',');
            double At(int k) => k < parts.Length && parts[k].Length > 0 ? double.Parse(parts[k], CultureInfo.InvariantCulture) : 0;
            return new Impairment(At(0), At(1), At(2));
        }
        var up = Direction(Opt("--up"), imp);
        var down = Direction(Opt("--down"), imp);
        await using var proxy = new ImpairmentProxy(new IPEndPoint(IPAddress.Loopback, int.Parse(Opt("--listen"), CultureInfo.InvariantCulture)),
            new IPEndPoint(IPAddress.Loopback, int.Parse(Opt("--target"), CultureInfo.InvariantCulture)), up, down, Environment.ProcessId)
        {
            Cuts = Opt("--cut").Length > 0 ? [LinkCut.Parse(Opt("--cut"))] : [],
        };
        Console.WriteLine($"proxy {proxy.Port} -> {Opt("--target")} (up {up}, down {down})");
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
    case "loadgate":
    {
        // Round I1a's load gate (SEAMLESS.md 19.17.4): one in-process hub, the cells one after another.
        var seconds = double.Parse(Opt("--seconds", "30"), CultureInfo.InvariantCulture);
        var threads = int.Parse(Opt("--threads", "8"), CultureInfo.InvariantCulture);
        var only = Opt("--cells");
        var sockets = args2.Contains("--sockets");
        var port = int.Parse(Opt("--port", "56810"), CultureInfo.InvariantCulture);
        LoadGate.Protocol = int.Parse(Opt("--protocol", LoadGate.Protocol.ToString(CultureInfo.InvariantCulture)), CultureInfo.InvariantCulture);
        Console.WriteLine($"protocol {LoadGate.Protocol}");
        var results = new List<Dictionary<string, object>>();
        foreach (var cell in LoadGate.Cells())
        {
            if (only.Length > 0 && !only.Split(',').Contains(cell.Name)) continue;
            if (sockets && cell.JoinsPerMinute > 0) continue;
            var r = sockets ? await LoadGateNet.RunAsync(cell, port++, seconds, threads, Console.WriteLine) : LoadGate.Run(cell, seconds, threads, Console.WriteLine);
            Console.WriteLine(LoadGate.Json(r));
            results.Add(r);
            if (Opt("--out") is { Length: > 0 } partial) await File.WriteAllTextAsync(partial, LoadGate.Json(results));
            GC.Collect();
            await Task.Delay(1000);
        }
        if (Opt("--out") is { Length: > 0 } outFile) await File.WriteAllTextAsync(outFile, LoadGate.Json(results));
        var invalid = results.Where(r => r.TryGetValue("valid", out var v) && v is bool b ? !b : v is System.Text.Json.JsonElement e && e.ValueKind == System.Text.Json.JsonValueKind.False).ToList();
        foreach (var r in invalid) Console.Error.WriteLine($"INVALID {r["cell"]}: {string.Join("; ", (IEnumerable<string>)r["invalid_reasons"])}");
        return invalid.Count == 0 ? 0 : 1;
    }
    case "loadgate-relay":
        LoadGate.Protocol = int.Parse(Opt("--protocol", LoadGate.Protocol.ToString(CultureInfo.InvariantCulture)), CultureInfo.InvariantCulture);
        return await LoadGateNet.RelayHostAsync(int.Parse(Opt("--port"), CultureInfo.InvariantCulture), Opt("--key"));
    case "joins":
    {
        // Round I1a's load gate, the joins cell (SEAMLESS.md 19.17.4): the lock's holds of one classic join per log length.
        var minutes = Opt("--minutes", "1,10,25").Split(',').Select(s => double.Parse(s, CultureInfo.InvariantCulture)).ToArray();
        LoadGate.Protocol = int.Parse(Opt("--protocol", LoadGate.Protocol.ToString(CultureInfo.InvariantCulture)), CultureInfo.InvariantCulture);
        LoadGate.Join(1, _ => { }); // (warm-up: the first join JIT-compiles the decision and the state paths)
        var results = minutes.Select(m => LoadGate.Join(m, Console.WriteLine)).ToList();
        Console.WriteLine(LoadGate.Json(results));
        if (Opt("--out") is { Length: > 0 } outFile) await File.WriteAllTextAsync(outFile, LoadGate.Json(results));
        return 0;
    }
    default:
        Console.Error.WriteLine("usage: DW4Online.FakeClient client|proxy|matrix|loadgate|loadgate-relay|joins ... (see Program.cs)");
        return 1;
}
