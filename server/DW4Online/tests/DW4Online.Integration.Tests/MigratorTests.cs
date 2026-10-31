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
            var lines = new List<string>();
            var warnings = new List<string>();
            var migrator = new Migrator(db, lines.Add, warnings.Add);
            Assert.Equal(0, await migrator.CurrentVersionAsync());
            Assert.Equal([1, 2, 3, 4, 5, 6, 7], await migrator.MigrateAsync());
            Assert.Empty(await migrator.MigrateAsync());
            Assert.Equal(7, await migrator.CurrentVersionAsync());
            await using var c = new MySqlConnection(TestDb.ConnectionString(name));

            // The server warning (once per run) follows the server's setting: none on a private mysqld that runs
            // with innodb_use_native_aio=0 (scripts/local-db.ps1, or XAMPP's my.ini with the line of DEPLOY.md 1.1).
            var version = await c.ExecuteScalarAsync<string>("SELECT VERSION()");
            var risky = await c.ExecuteScalarAsync<long>("SELECT @@innodb_use_native_aio") == 1 && OperatingSystem.IsWindows() &&
                        version!.StartsWith("10.", StringComparison.Ordinal) && int.Parse(version.Split('.')[1]) < 6;
            Assert.Equal(risky ? 2 : 0, warnings.Count);
            Assert.Equal(72, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM achievements"));
            Assert.Equal(16, await c.ExecuteScalarAsync<int>(
                "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE()"));

            // What the log shows: the plan, each migration with its time, the total.
            Assert.Equal(16, lines.Count);
            Assert.Equal($"Migrating database '{name}' from schema 0 to 7 (7 migration(s)) ...", lines[0]);
            Assert.Equal("Applying migration 0001 initial", lines[1]);
            Assert.Matches(@"^Applied migration 0001 initial in \d+\.\d\d s$", lines[2]);
            Assert.Matches(@"^Applied migration 0007 session address revision flag in \d+\.\d\d s$", lines[14]);
            Assert.Matches($@"^Migrating database '{name}' done in \d+\.\d\d s: schema 7$", lines[15]);
            Assert.Equal(0, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM schema_version WHERE description LIKE '%not finished%'"));
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
            Assert.Equal([6, 7], await new Migrator(db).MigrateAsync());
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

    /// <summary>
    /// Migration 0007 on a database at schema 6: sessions start with their creation address as the last one; kept
    /// revisions of a live character take its flag, those of a reset character the flag its reset noted.
    /// </summary>
    [DbFact]
    public async Task Migration_7_fills_in_last_addresses_and_revision_flags()
    {
        var name = await TestDb.CreateAsync();
        try
        {
            await using var c = new MySqlConnection(TestDb.ConnectionString(name));
            await c.OpenAsync();
            foreach (var m in Migrator.LoadEmbedded().Where(m => m.Version <= 6))
            {
                await using (var cmd = new MySqlCommand(m.Sql, c)) await cmd.ExecuteNonQueryAsync();
                await c.ExecuteAsync("INSERT INTO schema_version (version, description, checksum, applied_at) VALUES (@v, @d, @c, UTC_TIMESTAMP(3))",
                    new { v = m.Version, d = m.Description, c = m.Checksum });
            }
            var t = new DateTime(2026, 11, 1, 12, 0, 0);
            var reset1 = t.AddDays(-3);
            var reset2 = t.AddDays(-2);
            await c.ExecuteAsync("INSERT INTO accounts (username, username_norm, password_hash, created_at, online_reset_at) VALUES " +
                "('Aaa', 'aaa', 'x', @t, @reset2), ('Bbb', 'bbb', 'x', @t, NULL)", new { t = t.AddDays(-10), reset2 });
            var ids = (await c.QueryAsync<long>("SELECT CAST(id AS SIGNED) FROM accounts ORDER BY username_norm")).ToList(); // aaa, bbb
            await c.ExecuteAsync("INSERT INTO sessions (account_id, token_hash, kind, created_ip, created_at, expires_at) VALUES (@a, @h, 'game', @ip, @t, @e)",
                new { a = ids[0], h = new byte[32], ip = new byte[] { 203, 0, 113, 9 }, t, e = t.AddDays(30) });
            const string history = "INSERT INTO online_character_history (account_id, revision, record, record_sha256, level, play_seconds, reason, saved_at, ended_at) " +
                "VALUES (@a, @r, @rec, @sha, 1, 0, 'upload', @at, @ended)";
            var rec = new byte[3316];
            var sha = new byte[32];
            await c.ExecuteAsync(history, new { a = ids[0], r = 1, rec, sha, at = reset1.AddHours(-1), ended = (DateTime?)reset1 }); // reset, flagged
            await c.ExecuteAsync(history, new { a = ids[0], r = 2, rec, sha, at = reset2.AddHours(-1), ended = (DateTime?)reset2 }); // reset, clean
            await c.ExecuteAsync(history, new { a = ids[0], r = 3, rec, sha, at = t, ended = (DateTime?)null });                     // live, clean
            await c.ExecuteAsync(history, new { a = ids[1], r = 1, rec, sha, at = t, ended = (DateTime?)null });                     // live, flagged
            await c.ExecuteAsync("INSERT INTO audit_log (at, account_id, event, detail) VALUES (@reset1, @a, 'character_reset', '{\"revision\":1,\"flagged\":true}'), " +
                "(@reset2, @a, 'character_reset', '{\"revision\":2,\"flagged\":false}')", new { reset1, reset2, a = ids[0] });
            const string live = "INSERT INTO online_characters (account_id, record, record_sha256, revision, unique_id, digimon, species_row, player_name, " +
                "level, highest_level, hp, max_hp, mp, max_mp, bits, bank_bits, forms_reached, cards, clears, flagged, created_at, updated_at) " +
                "VALUES (@a, @rec, @sha, @r, 1, 'p_agumon', 0, 'K', 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, @flagged, @t, @t)";
            await c.ExecuteAsync(live, new { a = ids[0], rec, sha, r = 3, flagged = false, t });
            await c.ExecuteAsync(live, new { a = ids[1], rec, sha, r = 1, flagged = true, t });

            await using var db = new Database(TestDb.ConnectionString(name));
            Assert.Equal([7], await new Migrator(db).MigrateAsync());
            Assert.Equal(new byte[] { 203, 0, 113, 9 }, await c.ExecuteScalarAsync<byte[]>("SELECT last_used_ip FROM sessions"));
            var flags = (await c.QueryAsync<(long Account, int Revision, bool Flagged)>(
                "SELECT CAST(account_id AS SIGNED), CAST(revision AS SIGNED), flagged FROM online_character_history ORDER BY account_id, revision")).ToList();
            Assert.Equal([(ids[0], 1, true), (ids[0], 2, false), (ids[0], 3, false), (ids[1], 1, true)],
                flags.Select(f => (f.Account, f.Revision, f.Flagged)).ToList());
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
            Assert.Contains("None of its 1 tables holds a row", foreign.Message);
            Assert.Contains($"DROP DATABASE `{name}`", foreign.Message);
            await c.ExecuteAsync("INSERT INTO someone_elses VALUES (1)");
            var withData = await Assert.ThrowsAsync<MigrationException>(() => new Migrator(db).MigrateAsync());
            Assert.Contains("has tables but no schema_version; refusing to touch it", withData.Message); // unchanged for real data
            Assert.Contains("Its tables hold rows (someone_elses)", withData.Message);
            Assert.DoesNotContain("DROP DATABASE", withData.Message);
            Assert.Equal(1, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM someone_elses"));
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

    /// <summary>
    /// What a process killed during migration 0001 leaves (the server's first start on 31.10: the SCM ended it after
    /// 30 s): schema_version, which 0001 creates first, and some more tables, all empty. The migrator refuses it with the
    /// way out (DROP / CREATE, then migrate), refuses it without that advice once a table holds a row, and migrates
    /// the re-created database.
    /// </summary>
    [DbFact]
    public async Task An_interrupted_first_migration_is_refused_with_the_way_out()
    {
        var name = await TestDb.CreateAsync();
        try
        {
            await using (var c = new MySqlConnection(TestDb.ConnectionString(name)))
            {
                var initial = Migrator.LoadEmbedded()[0].Sql;
                await c.ExecuteAsync(initial[..initial.IndexOf("CREATE TABLE sessions", StringComparison.Ordinal)]); // schema_version, accounts

                await using (var db = new Database(TestDb.ConnectionString(name)))
                {
                    var empty = await Assert.ThrowsAsync<MigrationException>(() => new Migrator(db).MigrateAsync());
                    Assert.Contains("has an empty schema_version: migration 0001 (initial)", empty.Message);
                    Assert.Contains("None of its 2 tables holds a row, so nothing is lost by starting over", empty.Message);
                    Assert.Contains($"DROP DATABASE `{name}`; CREATE DATABASE `{name}` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;", empty.Message);
                    Assert.Contains("'DW4Online.Server.exe migrate --settings <settings file>' in a console", empty.Message);
                    Assert.Equal(0, await new Migrator(db).CurrentVersionAsync());

                    await c.ExecuteAsync("INSERT INTO accounts (username, username_norm, password_hash, created_at) VALUES ('Kevin', 'kevin', 'x', UTC_TIMESTAMP(3))");
                    var data = await Assert.ThrowsAsync<MigrationException>(() => new Migrator(db).MigrateAsync());
                    Assert.Contains("yet tables hold rows (accounts): refusing to touch it. Do not drop it", data.Message);
                    Assert.Contains($"restore-db.ps1 -Backup <file> -Database {name} -Replace", data.Message);
                    Assert.DoesNotContain("DROP DATABASE", data.Message);
                    Assert.Equal(2, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE()"));
                }
            }

            // The way out for the empty case: drop and re-create, then migrate.
            await TestDb.DropAsync(name);
            await using (var admin = new MySqlConnection(TestDb.AdminConnectionString))
                await admin.ExecuteAsync($"CREATE DATABASE `{name}` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci");
            await using var fresh = new Database(TestDb.ConnectionString(name));
            Assert.Equal([1, 2, 3, 4, 5, 6, 7], await new Migrator(fresh).MigrateAsync());
        }
        finally
        {
            await TestDb.DropAsync(name);
        }
    }

    /// <summary>
    /// A migration that fails half way (0003: its ALTER TABLE goes through, its CREATE INDEX fails) leaves the mark
    /// "started, not finished" in schema_version; the database counts as the version before, and every later run
    /// refuses it with the way back (the backup from before the update). An interrupted process leaves the same mark.
    /// </summary>
    [DbFact]
    public async Task A_migration_that_fails_half_way_is_marked_and_refused_until_the_backup_is_back()
    {
        var name = await TestDb.CreateAsync();
        try
        {
            await using var c = new MySqlConnection(TestDb.ConnectionString(name));
            await c.OpenAsync();
            foreach (var m in Migrator.LoadEmbedded().Where(m => m.Version <= 2)) // what the migrator did up to schema 2
            {
                await using (var cmd = new MySqlCommand(m.Sql, c)) await cmd.ExecuteNonQueryAsync();
                await c.ExecuteAsync("INSERT INTO schema_version (version, description, checksum, applied_at) VALUES (@v, @d, @c, UTC_TIMESTAMP(3))",
                    new { v = m.Version, d = m.Description, c = m.Checksum });
            }
            // The index 0003 creates in its second statement, so that statement fails after the first went through.
            await c.ExecuteAsync("CREATE INDEX ix_unlocks_rarity ON achievement_unlocks (achievement_id, flagged, account_id)");
            await using var db = new Database(TestDb.ConnectionString(name));

            var failed = await Assert.ThrowsAsync<MigrationException>(() => new Migrator(db).MigrateAsync());
            Assert.StartsWith("Migration 0003 (rooms) failed after ", failed.Message);
            Assert.Contains("ix_unlocks_rarity", failed.Message);
            Assert.Contains($"database '{name}' is between schema 2 and 3", failed.Message);
            Assert.Contains($"restore-db.ps1 -Backup <file> -Database {name} -Replace", failed.Message);
            Assert.IsType<MySqlException>(failed.InnerException);
            Assert.Equal(1, await c.ExecuteScalarAsync<int>(
                "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = 'rooms' AND column_name = 'randomizer'"));
            Assert.Equal("rooms (started, not finished)", await c.ExecuteScalarAsync<string>("SELECT description FROM schema_version WHERE version = 3"));
            Assert.Equal(2, await new Migrator(db).CurrentVersionAsync());

            var refused = await Assert.ThrowsAsync<MigrationException>(() => new Migrator(db).MigrateAsync());
            Assert.StartsWith("Migration 0003 (rooms) was started at ", refused.Message);
            Assert.Contains("did not finish", refused.Message);
            Assert.Contains("between schema 2 and 3: refusing to touch it", refused.Message);
            Assert.Contains("restore the backup made before the update", refused.Message);
            Assert.Equal(3, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM schema_version"));
        }
        finally
        {
            await TestDb.DropAsync(name);
        }
    }

    /// <summary>
    /// The connection breaks while a migration runs (killed while 0003's ALTER TABLE waits for another session's
    /// metadata lock on rooms): the migrator reports that migration and the state it leaves, not the error of releasing
    /// its lock on the dead connection (which hid it before, and 'migrate' then said "Nothing was changed").
    /// </summary>
    [DbFact]
    public async Task A_connection_that_breaks_during_a_migration_reports_that_migration()
    {
        var name = await TestDb.CreateAsync();
        try
        {
            await using var c = new MySqlConnection(TestDb.ConnectionString(name));
            await c.OpenAsync();
            foreach (var m in Migrator.LoadEmbedded().Where(m => m.Version <= 2))
            {
                await using (var cmd = new MySqlCommand(m.Sql, c)) await cmd.ExecuteNonQueryAsync();
                await c.ExecuteAsync("INSERT INTO schema_version (version, description, checksum, applied_at) VALUES (@v, @d, @c, UTC_TIMESTAMP(3))",
                    new { v = m.Version, d = m.Description, c = m.Checksum });
            }
            await using var holder = new MySqlConnection(TestDb.ConnectionString(name));
            await holder.OpenAsync();
            await using var transaction = await holder.BeginTransactionAsync();
            await holder.ExecuteAsync("SELECT COUNT(*) FROM rooms", transaction: transaction); // a shared metadata lock until the rollback

            await using var db = new Database(TestDb.ConnectionString(name));
            var migrate = new Migrator(db).MigrateAsync();
            await using var admin = new MySqlConnection(TestDb.AdminConnectionString);
            long waiting = 0;
            for (var i = 0; i < 400 && waiting == 0; i++)
            {
                waiting = await admin.ExecuteScalarAsync<long>(
                    "SELECT COALESCE(MAX(id), 0) FROM information_schema.processlist WHERE db = @name AND state = 'Waiting for table metadata lock'",
                    new { name });
                if (waiting == 0) await Task.Delay(50);
            }
            Assert.NotEqual(0, waiting);
            await admin.ExecuteAsync($"KILL CONNECTION {waiting}");

            var failed = await Assert.ThrowsAsync<MigrationException>(() => migrate);
            Assert.StartsWith("Migration 0003 (rooms) failed after ", failed.Message);
            Assert.Contains($"database '{name}' is between schema 2 and 3", failed.Message);
            await transaction.RollbackAsync();
            Assert.Equal("rooms (started, not finished)", await c.ExecuteScalarAsync<string>("SELECT description FROM schema_version WHERE version = 3"));
            var refused = await Assert.ThrowsAsync<MigrationException>(() => new Migrator(db).MigrateAsync());
            Assert.StartsWith("Migration 0003 (rooms) was started at ", refused.Message);
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
