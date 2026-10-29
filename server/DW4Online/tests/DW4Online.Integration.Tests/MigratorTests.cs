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
            Assert.Equal([1, 2], await migrator.MigrateAsync());
            Assert.Empty(await migrator.MigrateAsync());
            Assert.Equal(2, await migrator.CurrentVersionAsync());
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
