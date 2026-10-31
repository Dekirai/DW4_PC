using System.Security.Cryptography;
using DW4Online.Data;
using DW4Online.Data.Migrations;
using DW4Online.Domain.Services;
using DW4Online.Server;

// DW4Online: API, website and relay in one process.
//   DW4Online.Server                         run in the console
//   DW4Online.Server --service               run under the Service Control Manager (deploy/DEPLOY.md; ServerStartup)
//   DW4Online.Server migrate                 apply pending database migrations and exit (the operator's way, before a start)
//   DW4Online.Server set-role <user> <role>  player | moderator | admin (bootstraps the first admin)
//   DW4Online.Server reset-password <user>   print a temporary password the user must change
//   DW4Online.Server gen-relay-key           print a new random Relay:TicketKey
//   DW4Online.Server audit-tag <name>        print the security log's tag of a user name (failed logins with unknown names)
//   DW4Online.Server tls-pin --host <name or IP> | --thumbprint <hex> | --file <certificate>
//                                            print a certificate's SHA-256 SPKI pin (self-signed certificate, pinned by the client)
// Every form takes "--settings <file>": an extra JSON settings file (the server keeps its secrets outside the app folder).
var command = args.Length > 0 && !args[0].StartsWith('-') ? args[0] : null;
var rest = (command is null ? args : args[1..]).ToList();
var asService = rest.Remove("--service");
string? settings = null;
var at = rest.IndexOf("--settings");
if (at >= 0 && at + 1 < rest.Count)
{
    settings = Path.GetFullPath(rest[at + 1]);
    rest.RemoveRange(at, 2);
    if (!File.Exists(settings))
    {
        Console.Error.WriteLine($"Settings file not found: {settings}");
        return 2;
    }
}
var hostArgs = rest.ToArray();

switch (command)
{
    case null:
        // As a service: connect to the SCM first, then the database wait and the migrations, then the host (ServerStartup).
        return await ServerStartup.RunAsync(ServerApp.Build(hostArgs, asService, settings), asService);
    case "tls-pin":
        return TlsPinCommand.Run(hostArgs, Console.Out, Console.Error);
    case "gen-relay-key":
        Console.WriteLine(Convert.ToBase64String(RandomNumberGenerator.GetBytes(32)));
        return 0;
    case "migrate":
    {
        // The operator's way to migrate (deploy/DEPLOY.md, "Migrations"): in a console, before the service starts, with
        // every step, its time and on failure the state of the database and the way out on screen.
        await using var app = ServerApp.Build(hostArgs, settingsFile: settings);
        try
        {
            var target = new MySqlConnector.MySqlConnectionStringBuilder(app.Services.GetRequiredService<DatabaseOptions>().ConnectionString);
            Console.WriteLine($"Database '{target.Database}' on {target.Server}:{target.Port} as {target.UserID}");
            var watch = System.Diagnostics.Stopwatch.StartNew();
            var applied = await new Migrator(app.Services.GetRequiredService<Database>(), Console.WriteLine,
                warning => Console.WriteLine($"WARNING: {warning}")).MigrateAsync();
            Console.WriteLine(applied.Count == 0
                ? "The database schema is current."
                : $"Applied {applied.Count} migration(s) in {watch.Elapsed.TotalSeconds.ToString("0.00", System.Globalization.CultureInfo.InvariantCulture)} s.");
            return 0;
        }
        catch (MigrationException e)
        {
            Console.Error.WriteLine($"FAIL: {e.Message}");
            if (e.InnerException is not null) Console.Error.WriteLine($"      ({e.InnerException.GetType().Name}: {e.InnerException.Message})");
            return 1;
        }
        catch (Exception e) when (e is MySqlConnector.MySqlException or InvalidOperationException or ArgumentException)
        {
            Console.Error.WriteLine($"FAIL: {e.Message}");
            Console.Error.WriteLine("      Nothing was changed. Check Database:ConnectionString in the settings file and that MariaDB runs.");
            return 1;
        }
    }
    case "set-role" when hostArgs.Length >= 2:
    {
        await using var app = ServerApp.Build(hostArgs[2..], settingsFile: settings);
        var result = await app.Services.GetRequiredService<AccountService>()
            .AdminSetRoleAsync(null, hostArgs[0], hostArgs[1], new CallerInfo(null, "console"));
        Console.WriteLine(result.Ok ? $"{result.Value!.Username} is now {result.Value.Role}." : result.Error!.Message);
        return result.Ok ? 0 : 1;
    }
    case "reset-password" when hostArgs.Length >= 1:
    {
        await using var app = ServerApp.Build(hostArgs[1..], settingsFile: settings);
        var accounts = app.Services.GetRequiredService<AccountService>();
        var account = await accounts.FindAccountAsync(hostArgs[0]);
        if (account is null)
        {
            Console.WriteLine("No such account.");
            return 1;
        }
        var console = new DW4Online.Domain.Abstractions.AuthenticatedSession { AccountId = 0, Role = DW4Online.Domain.Abstractions.Roles.Admin, Username = "console" };
        var result = await accounts.AdminResetPasswordAsync(console, account.Username, new CallerInfo(null, "console"));
        Console.WriteLine(result.Ok ? $"Temporary password for {account.Username}: {result.Value}" : result.Error!.Message);
        return result.Ok ? 0 : 1;
    }
    case "audit-tag" when hostArgs.Length >= 1:
    {
        // The same key as the running service (Relay:TicketKey from the settings): search audit_log.detail for the tag.
        await using var app = ServerApp.Build(hostArgs[1..], settingsFile: settings);
        Console.WriteLine(app.Services.GetRequiredService<DW4Online.Domain.Security.AuditNameHasher>().Tag(hostArgs[0]));
        return 0;
    }
    default:
        Console.Error.WriteLine("Unknown command. Use: migrate | set-role <user> <role> | reset-password <user> | gen-relay-key | audit-tag <name> | tls-pin ... | --service | --settings <file>");
        return 2;
}
