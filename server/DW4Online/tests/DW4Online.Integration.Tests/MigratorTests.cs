using Dapper;
using DW4Online.Data;
using DW4Online.Data.Migrations;
using MySqlConnector;

namespace DW4Online.Integration.Tests;

public class MigratorTests
{
    [DbFact]
    public async Task Migrates_an_empty_database_once()
    {
        var name = await TestDb.CreateAsync();
        try
        {
            await using var db = new Database(TestDb.ConnectionString(name));
            var migrator = new Migrator(db);
            Assert.Equal(0, await migrator.CurrentVersionAsync());
            Assert.Equal([1, 2, 3, 4, 5, 6], await migrator.MigrateAsync());
            Assert.Empty(await migrator.MigrateAsync());
            Assert.Equal(6, await migrator.CurrentVersionAsync());
            await using var c = new MySqlConnection(TestDb.ConnectionString(name));
            Assert.Equal(72, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM achievements"));
            Assert.Equal(16, await c.ExecuteScalarAsync<int>(
                "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE()"));
        }
        finally
        {
            await TestDb.DropAsync(name);
        }
    }

    /// <summary>
    /// Migration 0006 on a database at schema 5: the revisions saved up to an account's last reset belong to an ended
    /// character (ended_at = that reset), later ones and those of never-reset accounts stay live.
    /// </summary>
    [DbFact]
    public async Task Migration_6_marks_the_revisions_of_reset_characters_as_ended()
    {
        var name = await TestDb.CreateAsync();
        try
        {
            await using var c = new MySqlConnection(TestDb.ConnectionString(name));
            await c.OpenAsync();
            foreach (var m in Migrator.LoadEmbedded().Where(m => m.Version <= 5)) // what the migrator did up to schema 5
            {
                await using (var cmd = new MySqlCommand(m.Sql, c)) await cmd.ExecuteNonQueryAsync();
                await c.ExecuteAsync("INSERT INTO schema_version (version, description, checksum, applied_at) VALUES (@v, @d, @c, UTC_TIMESTAMP(3))",
                    new { v = m.Version, d = m.Description, c = m.Checksum });
            }
            var reset = new DateTime(2026, 11, 1, 12, 0, 0);
            await c.ExecuteAsync("INSERT INTO accounts (username, username_norm, password_hash, created_at, online_reset_at) VALUES " +
                "('Reset', 'reset', 'x', @t, @reset), ('Never', 'never', 'x', @t, NULL)", new { t = reset.AddDays(-10), reset });
            var ids = (await c.QueryAsync<long>("SELECT CAST(id AS SIGNED) FROM accounts ORDER BY username_norm")).ToList(); // never, reset
            const string history = "INSERT INTO online_character_history (account_id, revision, record, record_sha256, level, play_seconds, reason, saved_at) " +
                "VALUES (@a, @r, @rec, @sha, 1, 0, 'upload', @at)";
            var rec = new byte[3316];
            var sha = new byte[32];
            await c.ExecuteAsync(history, new { a = ids[1], r = 1, rec, sha, at = reset.AddDays(-1) });  // the reset character
            await c.ExecuteAsync(history, new { a = ids[1], r = 2, rec, sha, at = reset });              // its last upload
            await c.ExecuteAsync(history, new { a = ids[1], r = 3, rec, sha, at = reset.AddHours(1) });  // the next character
            await c.ExecuteAsync(history, new { a = ids[0], r = 1, rec, sha, at = reset });              // never reset

            await using var db = new Database(TestDb.ConnectionString(name));
            Assert.Equal([6], await new Migrator(db).MigrateAsync());
            var rows = (await c.QueryAsync<(long Account, int Revision, DateTime? Ended)>(
                "SELECT CAST(account_id AS SIGNED), CAST(revision AS SIGNED), ended_at FROM online_character_history ORDER BY account_id, revision")).ToList();
            var expected = new List<(long, int, DateTime?)> { (ids[0], 1, null), (ids[1], 1, reset), (ids[1], 2, reset), (ids[1], 3, null) }
                .OrderBy(t => t.Item1).ThenBy(t => t.Item2).ToList();
            Assert.Equal(expected, rows.Select(r => (r.Account, r.Revision, r.Ended)).ToList());
        }
        finally
        {
            await TestDb.DropAsync(name);
        }
    }

    [DbFact]
    public async Task Refuses_foreign_databases_changed_migrations_and_newer_schemas()
    {
        var name = await TestDb.CreateAsync();
        try
        {
            await using var db = new Database(TestDb.ConnectionString(name));
            await using var c = new MySqlConnection(TestDb.ConnectionString(name));
            await c.ExecuteAsync("CREATE TABLE someone_elses (id INT PRIMARY KEY)");
            var foreign = await Assert.ThrowsAsync<MigrationException>(() => new Migrator(db).MigrateAsync());
            Assert.Contains("no schema_version", foreign.Message);
            await c.ExecuteAsync("DROP TABLE someone_elses");

            await new Migrator(db).MigrateAsync();
            await c.ExecuteAsync("UPDATE schema_version SET checksum = UNHEX(SHA2('changed', 256)) WHERE version = 2");
            var changed = await Assert.ThrowsAsync<MigrationException>(() => new Migrator(db).MigrateAsync());
            Assert.Contains("changed after it was applied", changed.Message);

            var real = Migrator.LoadEmbedded()[1].Checksum;
            await c.ExecuteAsync("UPDATE schema_version SET checksum = @real WHERE version = 2", new { real });
            await c.ExecuteAsync("INSERT INTO schema_version (version, description, checksum, applied_at) VALUES (99, 'future', @real, UTC_TIMESTAMP(3))", new { real });
            var newer = await Assert.ThrowsAsync<MigrationException>(() => new Migrator(db).MigrateAsync());
            Assert.Contains("newer than this build", newer.Message);
        }
        finally
        {
            await TestDb.DropAsync(name);
        }
    }

    [DbFact]
    public async Task Constraints_hold()
    {
        var name = await TestDb.CreateAsync();
        try
        {
            await using var db = new Database(TestDb.ConnectionString(name));
            await new Migrator(db).MigrateAsync();
            await using var c = new MySqlConnection(TestDb.ConnectionString(name));
            const string insert = "INSERT INTO accounts (username, username_norm, password_hash, created_at) VALUES (@u, @n, 'x', UTC_TIMESTAMP(3))";
            await c.ExecuteAsync(insert, new { u = "Kevin", n = "kevin" });
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync(insert, new { u = "KEVIN", n = "kevin" }));   // unique
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync(insert, new { u = "Bad", n = "Bad" }));       // lower case only
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync(insert, new { u = "a b", n = "a b" }));       // shape
            var id = await c.ExecuteScalarAsync<long>("SELECT id FROM accounts WHERE username_norm = 'kevin'");
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync(
                "INSERT INTO online_characters (account_id, record, record_sha256, revision, unique_id, digimon, species_row, player_name, " +
                "level, highest_level, hp, max_hp, mp, max_mp, bits, bank_bits, forms_reached, cards, clears, created_at, updated_at) " +
                "VALUES (@id, @rec, @sha, 1, 1, 'p_agumon', 0, 'K', 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, UTC_TIMESTAMP(3), UTC_TIMESTAMP(3))",
                new { id, rec = new byte[3315], sha = new byte[32] }));                                                  // record size
        }
        finally
        {
            await TestDb.DropAsync(name);
        }
    }
}
