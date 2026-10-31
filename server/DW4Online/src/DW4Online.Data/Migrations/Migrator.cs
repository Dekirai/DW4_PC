using System.Diagnostics;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using Dapper;
using MySqlConnector;

namespace DW4Online.Data.Migrations;

/// <summary>One numbered SQL migration (sql/NNNN_description.sql, embedded in the assembly).</summary>
public sealed record Migration(int Version, string Description, string Sql, byte[] Checksum);

/// <summary>The migrator refused or failed; the message names the state of the database and the way out.</summary>
public sealed class MigrationException : Exception
{
    public MigrationException(string message) : base(message)
    {
    }

    public MigrationException(string message, Exception inner) : base(message, inner)
    {
    }
}

/// <summary>
/// Applies the numbered SQL migrations in order and records each in schema_version with the SHA-256 of the file.
/// MariaDB commits every DDL statement at once, so a migration that stops half way (a failing statement, a killed
/// process) leaves the database between two versions. The migrator therefore marks a migration as started in
/// schema_version before it runs it (a row with an all-zero checksum, replaced by the real one when it is done) and
/// refuses to touch a database
/// <list type="bullet">
/// <item>with such a mark (an interrupted or failed migration: restore the backup made before the update),</item>
/// <item>with an empty schema_version (the first migration, which creates that table first, was interrupted),</item>
/// <item>with tables but no schema_version (not a DW4Online database, or one whose schema_version was lost),</item>
/// <item>whose applied migration files changed, or which is newer than this build.</item>
/// </list>
/// Every refusal says whether the tables hold rows and what to do; the migrator itself never drops or rewrites data
/// to get out of such a state. Back up before an update (deploy/DEPLOY.md, "Migrations").
/// </summary>
public sealed partial class Migrator(Database database, Action<string>? log = null)
{
    /// <summary>The checksum of a migration that was started but has not finished (a real SHA-256 is never all zero).</summary>
    private static readonly byte[] StartedMark = new byte[32];

    [GeneratedRegex(@"^sql/(\d{4})_([a-z0-9_]+)\.sql$", RegexOptions.CultureInvariant)]
    private static partial Regex ResourceName();

    public static IReadOnlyList<Migration> LoadEmbedded()
    {
        var assembly = typeof(Migrator).Assembly;
        var list = new List<Migration>();
        foreach (var name in assembly.GetManifestResourceNames())
        {
            var m = ResourceName().Match(name);
            if (!m.Success) continue;
            using var stream = assembly.GetManifestResourceStream(name)!;
            using var reader = new StreamReader(stream, Encoding.UTF8);
            var sql = reader.ReadToEnd().Replace("\r\n", "\n");
            list.Add(new Migration(int.Parse(m.Groups[1].Value), m.Groups[2].Value.Replace('_', ' '), sql,
                SHA256.HashData(Encoding.UTF8.GetBytes(sql))));
        }
        list.Sort((a, b) => a.Version.CompareTo(b.Version));
        for (var i = 0; i < list.Count; ++i)
            if (list[i].Version != i + 1) throw new MigrationException($"Migrations must be numbered 1..n without gaps (found {list[i].Version} at position {i + 1}).");
        return list;
    }

    /// <summary>The schema version of the database (0 when empty; a started but unfinished migration does not count).</summary>
    public async Task<int> CurrentVersionAsync(CancellationToken ct = default)
    {
        await using var c = await database.OpenAsync(ct);
        return await HasSchemaVersionAsync(c)
            ? await c.ExecuteScalarAsync<int>("SELECT COALESCE(MAX(version), 0) FROM schema_version WHERE checksum <> @mark", new { mark = StartedMark })
            : 0;
    }

    /// <summary>Brings the database to the newest embedded migration; returns the versions applied now.</summary>
    public async Task<IReadOnlyList<int>> MigrateAsync(CancellationToken ct = default)
    {
        var migrations = LoadEmbedded();
        await using var c = await database.OpenAsync(ct);
        if (await c.ExecuteScalarAsync<long?>("SELECT GET_LOCK('dw4online_migrate', 60)") != 1)
            throw new MigrationException("Another process is migrating this database (no lock 'dw4online_migrate' within 60 s). " +
                                         "Wait until it is done (its output ends with \"Migrating ... done\"), then try again.");
        try
        {
            var hasSchemaVersion = await HasSchemaVersionAsync(c);
            var applied = new Dictionary<int, byte[]>();
            if (hasSchemaVersion)
            {
                var rows = (await c.QueryAsync<(long Version, byte[] Checksum, DateTime AppliedAt)>(
                    "SELECT CAST(version AS SIGNED) AS version, checksum, applied_at FROM schema_version ORDER BY version")).ToList();
                foreach (var row in rows)
                    if (row.Checksum.AsSpan().SequenceEqual(StartedMark))
                        throw new MigrationException(Interrupted((int)row.Version, migrations, row.AppliedAt));
                if (rows.Count == 0) throw new MigrationException(await EmptySchemaVersionAsync(c));
                foreach (var row in rows) applied[(int)row.Version] = row.Checksum;
            }
            else
            {
                var tables = await c.ExecuteScalarAsync<long>(
                    "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE()");
                if (tables > 0) throw new MigrationException(await NoSchemaVersionAsync(c));
            }
            var newest = migrations.Count == 0 ? 0 : migrations[^1].Version;
            if (applied.Count > 0 && applied.Keys.Max() > newest)
                throw new MigrationException($"The database is at schema {applied.Keys.Max()}, newer than this build ({newest}). Update the server.");
            foreach (var m in migrations)
            {
                if (applied.TryGetValue(m.Version, out var checksum) && !checksum.AsSpan().SequenceEqual(m.Checksum))
                    throw new MigrationException($"Migration {m.Version} ({m.Description}) changed after it was applied. Add a new migration instead.");
            }

            var pending = migrations.Where(m => !applied.ContainsKey(m.Version)).ToList();
            if (pending.Count == 0) return [];
            var current = applied.Count == 0 ? 0 : applied.Keys.Max();
            log?.Invoke($"Migrating database '{database.DatabaseName}' from schema {current} to {newest} ({pending.Count} migration(s)) ...");
            var total = Stopwatch.StartNew();
            var done = new List<int>();
            foreach (var m in pending)
            {
                log?.Invoke($"Applying migration {m.Version:D4} {m.Description}");
                var watch = Stopwatch.StartNew();
                try
                {
                    if (hasSchemaVersion)
                    {
                        // The mark: until it is replaced below, every later run refuses this database (see Interrupted).
                        await c.ExecuteAsync(
                            "INSERT INTO schema_version (version, description, checksum, applied_at) VALUES (@v, @d, @mark, UTC_TIMESTAMP(3))",
                            new { v = m.Version, d = m.Description + " (started, not finished)", mark = StartedMark });
                    }
                    await using (var cmd = new MySqlCommand(m.Sql, c) { CommandTimeout = 600 })
                        await cmd.ExecuteNonQueryAsync(ct);
                    await c.ExecuteAsync(
                        "INSERT INTO schema_version (version, description, checksum, applied_at) VALUES (@v, @d, @c, UTC_TIMESTAMP(3)) " +
                        "ON DUPLICATE KEY UPDATE description = VALUES(description), checksum = VALUES(checksum), applied_at = VALUES(applied_at)",
                        new { v = m.Version, d = m.Description, c = m.Checksum });
                }
                catch (MySqlException e)
                {
                    throw new MigrationException(Failed(m, watch.Elapsed, e), e);
                }
                hasSchemaVersion = true; // migration 0001 creates the table
                log?.Invoke($"Applied migration {m.Version:D4} {m.Description} in {Seconds(watch.Elapsed)} s");
                done.Add(m.Version);
            }
            log?.Invoke($"Migrating database '{database.DatabaseName}' done in {Seconds(total.Elapsed)} s: schema {newest}");
            return done;
        }
        finally
        {
            await c.ExecuteAsync("SELECT RELEASE_LOCK('dw4online_migrate')");
        }
    }

    // --- Refusals and failures: the state of the database and the way out ----------------------------------------------

    private string Recreate() =>
        $"drop and re-create it empty (as root, for example in HeidiSQL: DROP DATABASE `{database.DatabaseName}`; " +
        $"CREATE DATABASE `{database.DatabaseName}` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;), then run " +
        "'DW4Online.Server.exe migrate --settings <settings file>' in a console before you start the service";

    private string Restore() =>
        "restore the backup made before the update into it (deploy\\restore-db.ps1 -Backup <file> -Database " +
        $"{database.DatabaseName} -Replace), then run 'DW4Online.Server.exe migrate --settings <settings file>' in a console " +
        "before you start the service";

    private string Interrupted(int version, IReadOnlyList<Migration> migrations, DateTime startedAt)
    {
        var what = version >= 1 && version <= migrations.Count
            ? $"Migration {version:D4} ({migrations[version - 1].Description})"
            : $"Migration {version:D4}";
        return $"{what} was started at {startedAt.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture)} UTC and did not finish " +
               "(interrupted or failed; schema_version marks it \"started, not finished\"). MariaDB commits every DDL statement at once, " +
               $"so database '{database.DatabaseName}' is now between schema {version - 1} and {version}: refusing to touch it. " +
               $"Way out: {Restore()} (deploy/DEPLOY.md, \"Migrations\").";
    }

    private string Failed(Migration m, TimeSpan elapsed, MySqlException e)
    {
        var head = $"Migration {m.Version:D4} ({m.Description}) failed after {Seconds(elapsed)} s: {e.Message}";
        if (m.Version == 1)
            return $"{head}. Database '{database.DatabaseName}' holds no data yet (the first migration only creates tables): " +
                   $"remove the cause, then {Recreate()} (deploy/DEPLOY.md, \"Migrations\").";
        return $"{head}. MariaDB commits every DDL statement at once, so the statements before the failing one stay applied: " +
               $"database '{database.DatabaseName}' is between schema {m.Version - 1} and {m.Version}, and every later start " +
               $"refuses it. Way out: remove the cause, then {Restore()} (deploy/DEPLOY.md, \"Migrations\").";
    }

    private async Task<string> EmptySchemaVersionAsync(MySqlConnection c)
    {
        var (tables, withRows) = await TablesWithRowsAsync(c);
        var head = $"Database '{database.DatabaseName}' has an empty schema_version: migration 0001 (initial), which creates that " +
                   "table first and records itself last, was interrupted before it finished";
        if (withRows.Count == 0)
            return $"{head}. None of its {tables} tables holds a row, so nothing is lost by starting over: {Recreate()} " +
                   "(deploy/DEPLOY.md, \"Migrations\").";
        return $"{head}, yet tables hold rows ({string.Join(", ", withRows)}): refusing to touch it. Do not drop it: if it is " +
               $"DW4Online's database, {Restore()}; otherwise give DW4Online an empty database of its own.";
    }

    private async Task<string> NoSchemaVersionAsync(MySqlConnection c)
    {
        var (tables, withRows) = await TablesWithRowsAsync(c);
        var head = $"Database '{database.DatabaseName}' has tables but no schema_version; refusing to touch it";
        if (withRows.Count == 0)
            return $"{head}. None of its {tables} tables holds a row: if it is meant for DW4Online and nothing else uses it, " +
                   $"{Recreate()} (deploy/DEPLOY.md, \"Migrations\").";
        return $"{head}. Its tables hold rows ({string.Join(", ", withRows)}): if it is DW4Online's database, {Restore()}; " +
               "otherwise give DW4Online an empty database of its own (Database:ConnectionString).";
    }

    /// <summary>The number of tables and the names of those that hold at least one row.</summary>
    private static async Task<(int Tables, List<string> WithRows)> TablesWithRowsAsync(MySqlConnection c)
    {
        var tables = (await c.QueryAsync<string>(
            "SELECT table_name FROM information_schema.tables WHERE table_schema = DATABASE() AND table_type <> 'VIEW' ORDER BY table_name")).ToList();
        var withRows = new List<string>();
        foreach (var table in tables)
            if (await c.ExecuteScalarAsync<long>($"SELECT EXISTS (SELECT 1 FROM `{table.Replace("`", "``")}`)") == 1) withRows.Add(table);
        return (tables.Count, withRows);
    }

    private static string Seconds(TimeSpan t) => t.TotalSeconds.ToString("0.00", CultureInfo.InvariantCulture);

    private static async Task<bool> HasSchemaVersionAsync(MySqlConnection c) =>
        await c.ExecuteScalarAsync<long>(
            "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE() AND table_name = 'schema_version'") > 0;
}
