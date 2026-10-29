using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using Dapper;
using MySqlConnector;

namespace DW4Online.Data.Migrations;

/// <summary>One numbered SQL migration (sql/NNNN_description.sql, embedded in the assembly).</summary>
public sealed record Migration(int Version, string Description, string Sql, byte[] Checksum);

public sealed class MigrationException(string message) : Exception(message);

/// <summary>
/// Applies the numbered SQL migrations in order and records each in schema_version with the SHA-256 of the file.
/// It refuses to run when the database holds tables but no schema_version, when an applied migration's file
/// changed, or when the database is newer than this build. MariaDB commits DDL implicitly, so back up first:
/// a migration that fails half way leaves the database between versions (deploy/DEPLOY.md, "Updates").
/// </summary>
public sealed partial class Migrator(Database database, Action<string>? log = null)
{
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

    /// <summary>The schema version of the database (0 when empty).</summary>
    public async Task<int> CurrentVersionAsync(CancellationToken ct = default)
    {
        await using var c = await database.OpenAsync(ct);
        return await HasSchemaVersionAsync(c) ? await c.ExecuteScalarAsync<int>("SELECT COALESCE(MAX(version), 0) FROM schema_version") : 0;
    }

    /// <summary>Brings the database to the newest embedded migration; returns the versions applied now.</summary>
    public async Task<IReadOnlyList<int>> MigrateAsync(CancellationToken ct = default)
    {
        var migrations = LoadEmbedded();
        await using var c = await database.OpenAsync(ct);
        if (await c.ExecuteScalarAsync<long?>("SELECT GET_LOCK('dw4online_migrate', 60)") != 1)
            throw new MigrationException("Another process is migrating this database.");
        try
        {
            var applied = new Dictionary<int, byte[]>();
            if (await HasSchemaVersionAsync(c))
            {
                foreach (var row in await c.QueryAsync<(long Version, byte[] Checksum)>("SELECT CAST(version AS SIGNED) AS version, checksum FROM schema_version"))
                    applied[(int)row.Version] = row.Checksum;
            }
            else
            {
                var tables = await c.ExecuteScalarAsync<long>(
                    "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE()");
                if (tables > 0)
                    throw new MigrationException($"Database '{database.DatabaseName}' has tables but no schema_version; refusing to touch it.");
            }
            var newest = migrations.Count == 0 ? 0 : migrations[^1].Version;
            if (applied.Count > 0 && applied.Keys.Max() > newest)
                throw new MigrationException($"The database is at schema {applied.Keys.Max()}, newer than this build ({newest}). Update the server.");
            foreach (var m in migrations)
            {
                if (applied.TryGetValue(m.Version, out var checksum) && !checksum.AsSpan().SequenceEqual(m.Checksum))
                    throw new MigrationException($"Migration {m.Version} ({m.Description}) changed after it was applied. Add a new migration instead.");
            }
            var done = new List<int>();
            foreach (var m in migrations.Where(m => !applied.ContainsKey(m.Version)))
            {
                log?.Invoke($"Applying migration {m.Version:D4} {m.Description}");
                await using (var cmd = new MySqlCommand(m.Sql, c) { CommandTimeout = 600 })
                    await cmd.ExecuteNonQueryAsync(ct);
                await c.ExecuteAsync(
                    "INSERT INTO schema_version (version, description, checksum, applied_at) VALUES (@v, @d, @c, UTC_TIMESTAMP(3))",
                    new { v = m.Version, d = m.Description, c = m.Checksum });
                done.Add(m.Version);
            }
            return done;
        }
        finally
        {
            await c.ExecuteAsync("SELECT RELEASE_LOCK('dw4online_migrate')");
        }
    }

    private static async Task<bool> HasSchemaVersionAsync(MySqlConnection c) =>
        await c.ExecuteScalarAsync<long>(
            "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE() AND table_name = 'schema_version'") > 0;
}
