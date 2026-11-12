using Dapper;
using DW4Online.Data;
using DW4Online.Data.Migrations;
using MySqlConnector;

namespace DW4Online.Integration.Tests;

public class MigratorTests
{
    /// <summary>The embedded migrations from <paramref name="version"/> on (later rounds add their own).</summary>
    private static List<int> From(int version) => Migrator.LoadEmbedded().Select(m => m.Version).Where(v => v >= version).ToList();

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
            Assert.Equal(From(1), await migrator.MigrateAsync());
            Assert.Empty(await migrator.MigrateAsync());
            Assert.Equal(From(1).Last(), await migrator.CurrentVersionAsync());
            Assert.True(From(1).Last() >= 10); // 0008: online_story, 0009: the achievement catalogue, 0010: the story history
            await using var c = new MySqlConnection(TestDb.ConnectionString(name));

            // The server warning (once per run) follows the server's setting: none on a private mysqld that runs
            // with innodb_use_native_aio=0 (scripts/local-db.ps1, or XAMPP's my.ini with the line of DEPLOY.md 1.1).
            var version = await c.ExecuteScalarAsync<string>("SELECT VERSION()");
            var risky = await c.ExecuteScalarAsync<long>("SELECT @@innodb_use_native_aio") == 1 && OperatingSystem.IsWindows() &&
                        version!.StartsWith("10.", StringComparison.Ordinal) && int.Parse(version.Split('.')[1]) < 6;
            Assert.Equal(risky ? 2 : 0, warnings.Count);
            // 0002's 72 and the game client's 20 others (0009); 72 active: the client's catalogue.
            Assert.Equal(92, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM achievements"));
            Assert.Equal(72, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM achievements WHERE active = 1"));
            Assert.Equal(0, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM achievements WHERE active = 1 AND code IN ('speed_6h', 'online_ten')"));
            Assert.Equal(12, await c.ExecuteScalarAsync<int>("SELECT goal FROM achievements WHERE code = 'lowlevel_apocalymon'"));
            Assert.Equal(25, await c.ExecuteScalarAsync<int>( // (0008: online_story, 0010: online_story_history, 0012: the friend tables, 0013: the world's three, 0014: player_reports)
                "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE() AND table_type = 'BASE TABLE'"));
            Assert.Equal(6, await c.ExecuteScalarAsync<int>( // 0013's five views with user names, 0014's v_player_reports
                "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE() AND table_type = 'VIEW'"));

            // What the log shows: the plan, each migration with its time, the total.
            var all = Migrator.LoadEmbedded();
            var last = all[^1];
            Assert.Equal(2 + 2 * all.Count, lines.Count);
            Assert.Equal($"Migrating database '{name}' from schema 0 to {last.Version} ({all.Count} migration(s)) ...", lines[0]);
            Assert.Equal("Applying migration 0001 initial", lines[1]);
            Assert.Matches(@"^Applied migration 0001 initial in \d+\.\d\d s$", lines[2]);
            Assert.Matches(@"^Applied migration 0007 session address revision flag in \d+\.\d\d s$", lines[14]);
            Assert.Equal($"Applying migration {last.Version:D4} {last.Description}", lines[2 * all.Count - 1]);
            Assert.Matches($@"^Migrating database '{name}' done in \d+\.\d\d s: schema {last.Version}$", lines[2 * all.Count + 1]);
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
            Assert.Equal(From(6), await new Migrator(db).MigrateAsync());
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
            Assert.Equal(From(7), await new Migrator(db).MigrateAsync());
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
            Assert.Equal(From(1), await new Migrator(fresh).MigrateAsync());
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

    /// <summary>What the migrator did up to <paramref name="version"/> (the files and their schema_version rows).</summary>
    private static async Task ApplyUpToAsync(MySqlConnection c, int version)
    {
        foreach (var m in Migrator.LoadEmbedded().Where(m => m.Version <= version))
        {
            await using (var cmd = new MySqlCommand(m.Sql, c)) await cmd.ExecuteNonQueryAsync();
            await c.ExecuteAsync("INSERT INTO schema_version (version, description, checksum, applied_at) VALUES (@v, @d, @c, UTC_TIMESTAMP(3))",
                new { v = m.Version, d = m.Description, c = m.Checksum });
        }
    }

    /// <summary>
    /// Migration 0012 (friends, round I2-social) on a database at schema 11 with accounts: three plain tables a person can
    /// read in HeidiSQL (no binary columns, text enums, a comment on every table and column), the pair rules as CHECK
    /// constraints, foreign keys to accounts with ON DELETE CASCADE (a row deleted by hand takes its pairs along).
    /// </summary>
    [DbFact]
    public async Task Migration_12_adds_the_friend_tables_readable_in_HeidiSQL()
    {
        var name = await TestDb.CreateAsync();
        try
        {
            await using var c = new MySqlConnection(TestDb.ConnectionString(name));
            await c.OpenAsync();
            await ApplyUpToAsync(c, 11);
            await c.ExecuteAsync("INSERT INTO accounts (username, username_norm, password_hash, created_at) VALUES " +
                "('Kim', 'kim', 'x', UTC_TIMESTAMP(3)), ('Lee', 'lee', 'x', UTC_TIMESTAMP(3)), ('Max', 'max', 'x', UTC_TIMESTAMP(3))");
            var ids = (await c.QueryAsync<long>("SELECT CAST(id AS SIGNED) FROM accounts ORDER BY id")).ToList();
            await using var db = new Database(TestDb.ConnectionString(name));
            Assert.Contains(12, await new Migrator(db).MigrateAsync());

            string[] tables = ["friendships", "friend_requests", "account_blocks"];
            var columns = (await c.QueryAsync<(string Table, string Column, string Type, string Comment)>(
                "SELECT table_name, column_name, column_type, column_comment FROM information_schema.columns " +
                "WHERE table_schema = DATABASE() AND table_name IN @tables ORDER BY table_name, ordinal_position", new { tables })).ToList();
            Assert.Equal(3 + 7 + 3, columns.Count);
            Assert.All(columns, col => Assert.DoesNotMatch("blob|binary", col.Type));
            Assert.All(columns, col => Assert.False(string.IsNullOrWhiteSpace(col.Comment), $"{col.Table}.{col.Column} has no comment"));
            Assert.Equal("enum('pending','declined')", columns.Single(col => col.Column == "state").Type);
            Assert.Equal(3, await c.ExecuteScalarAsync<int>(
                "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE() AND table_name IN @tables AND table_comment <> ''", new { tables }));

            var (kim, lee, max) = (ids[0], ids[1], ids[2]);
            const string friendship = "INSERT INTO friendships (account_lo, account_hi, created_at) VALUES (@lo, @hi, UTC_TIMESTAMP(3))";
            await c.ExecuteAsync(friendship, new { lo = kim, hi = lee });
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync(friendship, new { lo = max, hi = kim }));     // lo < hi
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync(friendship, new { lo = kim, hi = kim }));
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync(friendship, new { lo = kim, hi = 999999L })); // foreign key
            const string request = "INSERT INTO friend_requests (from_account_id, to_account_id, created_at) VALUES (@a, @b, UTC_TIMESTAMP(3))";
            await c.ExecuteAsync(request, new { a = max, b = kim });
            Assert.Equal(("pending", true), await c.QuerySingleAsync<(string, bool)>("SELECT state, delivered FROM friend_requests"));
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync(request, new { a = lee, b = lee }));
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync("UPDATE friend_requests SET state = 'accepted'"));
            const string block = "INSERT INTO account_blocks (account_id, blocked_account_id, created_at) VALUES (@a, @b, UTC_TIMESTAMP(3))";
            await c.ExecuteAsync(block, new { a = lee, b = max });
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync(block, new { a = max, b = max }));

            // A hard delete (by hand; the service only anonymises) takes the pairs along.
            await c.ExecuteAsync("DELETE FROM accounts WHERE id = @max", new { max });
            Assert.Equal(0, await c.ExecuteScalarAsync<int>("SELECT (SELECT COUNT(*) FROM friend_requests) + (SELECT COUNT(*) FROM account_blocks)"));
            Assert.Equal(1, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM friendships"));
        }
        finally
        {
            await TestDb.DropAsync(name);
        }
    }

    /// <summary>
    /// The restricted service user of DEPLOY.md section 2 (SELECT, INSERT, UPDATE, DELETE, CREATE, ALTER, INDEX, REFERENCES
    /// on the database) runs every migration up to 0012; 0013 (world) brings views and needs CREATE VIEW (DEPLOY.md 9.2,
    /// SEAMLESS.md 19.22 step 7). Without it the migrator refuses before it changes anything (the database stays empty here,
    /// not half way), with the GRANT line in the message; with it everything applies and the service user can read the views.
    /// </summary>
    [DbFact]
    public async Task Migrations_need_the_restricted_service_users_grants_plus_create_view()
    {
        var name = await TestDb.CreateAsync();
        var user = "dw4it_" + Guid.NewGuid().ToString("N")[..8];
        var password = Guid.NewGuid().ToString("N");
        await using var admin = new MySqlConnection(TestDb.AdminConnectionString);
        try
        {
            foreach (var host in new[] { "127.0.0.1", "localhost" })
            {
                await admin.ExecuteAsync($"CREATE USER '{user}'@'{host}' IDENTIFIED BY '{password}'");
                await admin.ExecuteAsync($"GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, ALTER, INDEX, REFERENCES ON `{name}`.* TO '{user}'@'{host}'");
            }
            var restricted = new MySqlConnectionStringBuilder(TestDb.ConnectionString(name)) { UserID = user, Password = password }.ConnectionString;
            await using var db = new Database(restricted);
            var refused = await Assert.ThrowsAsync<MigrationException>(() => new Migrator(db).MigrateAsync());
            Assert.Contains("Migration 0013 (world) needs the right CREATE VIEW", refused.Message);
            Assert.Contains("Nothing was changed (the database stays at schema 0)", refused.Message);
            Assert.Contains($"GRANT CREATE VIEW ON `{name}`.* TO '{user}'@", refused.Message);
            await using (var check = new MySqlConnection(TestDb.ConnectionString(name)))
                Assert.Equal(0, await check.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE()"));

            foreach (var host in new[] { "127.0.0.1", "localhost" })
                await admin.ExecuteAsync($"GRANT CREATE VIEW ON `{name}`.* TO '{user}'@'{host}'");
            // Database rights count from a connection's next USE: a new pool, as the operator's next 'migrate' process has.
            await using var granted = new Database(restricted);
            Assert.Equal(From(1), await new Migrator(granted).MigrateAsync());
            await using var c = new MySqlConnection(restricted);
            Assert.Equal(0, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM account_blocks"));
            Assert.Equal(6, await c.ExecuteScalarAsync<int>(
                "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE() AND table_type = 'VIEW'"));
            foreach (var view in new[] { "v_friends", "v_friend_requests", "v_blocks", "v_instance_history", "v_staff_audit", "v_player_reports" })
                Assert.Equal(0, await c.ExecuteScalarAsync<int>($"SELECT COUNT(*) FROM {view}"));
        }
        finally
        {
            foreach (var host in new[] { "127.0.0.1", "localhost" })
                await admin.ExecuteAsync($"DROP USER IF EXISTS '{user}'@'{host}'");
            await TestDb.DropAsync(name);
        }
    }

    /// <summary>
    /// Migration 0013 (world, round I1a) on a database at schema 12 with accounts and rooms: the newest room settings of
    /// each host fill his online_prefs (camera, light, fast loading, randomizer seed and modes; question 36 (b): no story
    /// is copied), the new tables and columns carry a comment on every column and no binary column, the constraints hold,
    /// the story history takes the reason 'mirror', and the views show user names, areas, durations and players only.
    /// </summary>
    [DbFact]
    public async Task Migration_13_adds_the_world_tables_prefs_from_rooms_and_the_views()
    {
        var name = await TestDb.CreateAsync();
        try
        {
            await using var c = new MySqlConnection(TestDb.ConnectionString(name));
            await c.OpenAsync();
            await ApplyUpToAsync(c, 12);
            await c.ExecuteAsync("INSERT INTO accounts (username, username_norm, password_hash, status, created_at) VALUES " +
                "('Kim', 'kim', 'x', 'active', UTC_TIMESTAMP(3)), ('Lee', 'lee', 'x', 'active', UTC_TIMESTAMP(3)), " +
                "('Max', 'max', 'x', 'active', UTC_TIMESTAMP(3)), ('deleted_4', 'deleted_4', '!', 'deleted', UTC_TIMESTAMP(3)), " +
                "('Ann', 'ann', 'x', 'active', UTC_TIMESTAMP(3))");
            var ids = (await c.QueryAsync<long>("SELECT CAST(id AS SIGNED) FROM accounts ORDER BY id")).ToList();
            var (kim, lee, max, gone, ann) = (ids[0], ids[1], ids[2], ids[3], ids[4]);
            const string room = "INSERT INTO rooms (public_code, name, host_account_id, max_players, protocol, data_hash, client_version, settings, " +
                                "status, created_at, closed_at, close_reason) VALUES (@code, 'r', @host, 4, 48, 1, 'v', @settings, 'closed', UTC_TIMESTAMP(3), UTC_TIMESTAMP(3), 'empty')";
            // Kim: an old room with defaults, then the newest one with every host setting changed.
            await c.ExecuteAsync(room, new { code = "AAAAA1", host = kim, settings = "{\"difficulty\":0,\"cameraMode\":0,\"lightMode\":1}" });
            await c.ExecuteAsync(room, new
            {
                code = "AAAAA2", host = kim,
                settings = "{\"difficulty\":1,\"cameraMode\":1,\"lightMode\":0,\"fastLoading\":true," +
                           "\"randomizer\":{\"enabled\":true,\"seed\":77,\"drops\":true,\"enemies\":2,\"zones\":1}}",
            });
            // Lee: a randomizer that is switched off (its seed stays out), Max: a room without settings, the deleted host none.
            await c.ExecuteAsync(room, new { code = "AAAAA3", host = lee, settings = "{\"cameraMode\":1,\"randomizer\":{\"enabled\":false,\"seed\":5}}" });
            await c.ExecuteAsync(room, new { code = "AAAAA4", host = max, settings = (string?)null });
            await c.ExecuteAsync(room, new { code = "AAAAA5", host = gone, settings = "{\"cameraMode\":1}" });
            await c.ExecuteAsync("INSERT INTO room_members (room_id, account_id, slot, joined_at) SELECT id, @ann, 1, UTC_TIMESTAMP(3) FROM rooms", new { ann });
            await using var db = new Database(TestDb.ConnectionString(name));
            Assert.Equal(From(13), await new Migrator(db).MigrateAsync()); // (0013, then 0014: the reports)

            var prefs = (await c.QueryAsync<(long Account, int Camera, int Light, bool Fast, long? Seed, int Modes, string Visibility, int Box)>(
                "SELECT CAST(account_id AS SIGNED), camera_mode, light_mode, fast_loading, CAST(randomizer_seed AS SIGNED), randomizer_modes, " +
                "default_visibility, last_box FROM online_prefs ORDER BY account_id")).ToList();
            Assert.Equal(2, prefs.Count);
            Assert.Equal((kim, 1, 0, true, (long?)77, 1 | 2 << 1 | 1 << 3, "public", 1), prefs[0]);
            Assert.Equal((lee, 1, 1, false, (long?)null, 0, "public", 1), prefs[1]);
            // Question 36 (b): a guest-only account gets no story and no history row from the migration.
            Assert.Equal(0, await c.ExecuteScalarAsync<int>("SELECT (SELECT COUNT(*) FROM online_story) + (SELECT COUNT(*) FROM online_story_history)"));

            string[] tables = ["online_prefs", "instance_history", "instance_history_members"];
            var columns = (await c.QueryAsync<(string Table, string Column, string Type, string Comment)>(
                "SELECT table_name, column_name, column_type, column_comment FROM information_schema.columns " +
                "WHERE table_schema = DATABASE() AND (table_name IN @tables OR (table_name, column_name) IN " +
                "(('accounts', 'last_seen_at'), ('achievement_unlocks', 'instance_id'), ('speedrun_times', 'instance_id'), ('online_story_history', 'reason'))) " +
                "ORDER BY table_name, ordinal_position", new { tables })).ToList();
            Assert.Equal(12 + 12 + 8 + 4, columns.Count);
            Assert.All(columns, col => Assert.DoesNotMatch("blob|binary", col.Type));
            Assert.All(columns, col => Assert.False(string.IsNullOrWhiteSpace(col.Comment), $"{col.Table}.{col.Column} has no comment"));
            Assert.Equal("enum('room','reset','admin_restore','session','mirror')", columns.Single(col => col.Column == "reason").Type);
            Assert.Equal(3, await c.ExecuteScalarAsync<int>(
                "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE() AND table_name IN @tables AND table_comment <> ''", new { tables }));

            // The constraints.
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync(
                "INSERT INTO online_prefs (account_id, camera_mode, updated_at) VALUES (@max, 2, UTC_TIMESTAMP(3))", new { max }));
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync(
                "INSERT INTO online_prefs (account_id, randomizer_modes, updated_at) VALUES (@max, 6, UTC_TIMESTAMP(3))", new { max })); // enemies 3
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync(
                "INSERT INTO online_prefs (account_id, randomizer_seed, updated_at) VALUES (@max, 0, UTC_TIMESTAMP(3))", new { max }));
            await c.ExecuteAsync("INSERT INTO online_story_history (account_id, revision, flags, folder, counters, reason, saved_at) " +
                                 "VALUES (@kim, 1, '', REPEAT(CHAR(0), 12), REPEAT(CHAR(0), 64), 'mirror', UTC_TIMESTAMP(3))", new { kim });

            const string instance = "INSERT INTO instance_history (host_account_id, host_box, first_stage, visibility, difficulty, story_phase, settings, " +
                                    "started_at, ended_at, end_reason, peak_players) VALUES (@host, 1, @stage, 'public', 1, 4, 'DW4R2-settings/1', " +
                                    "'2026-11-06 10:00:00', '2026-11-06 10:42:00', 'host_left', 3)";
            await c.ExecuteAsync(instance, new { host = kim, stage = "stag0201" });
            var wid = await c.ExecuteScalarAsync<long>("SELECT CAST(MAX(id) AS SIGNED) FROM instance_history");
            const string member = "INSERT INTO instance_history_members (instance_id, account_id, box, role, joined_at, left_at, leave_reason) " +
                                  "VALUES (@wid, @account, 1, @role, '2026-11-06 10:00:00', '2026-11-06 10:42:00', 'ended')";
            await c.ExecuteAsync(member, new { wid, account = kim, role = "host" });
            await c.ExecuteAsync(member, new { wid, account = lee, role = "guest" });
            await c.ExecuteAsync(member, new { wid, account = max, role = "spectator" });
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync(member, new { wid, account = 999999L, role = "guest" }));
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync(member, new { wid, account = ann, role = "owner" }));
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync(
                "INSERT INTO instance_history (host_account_id, host_box, first_stage, visibility, difficulty, story_phase, settings, started_at) " +
                "VALUES (@kim, 1, 'stag0101', 'public', 3, 0, 's', UTC_TIMESTAMP(3))", new { kim }));                       // difficulty 0..2

            // The views: names, the area, minutes, players without the spectator.
            var history = await c.QuerySingleAsync<(string Host, string Area, string Difficulty, long Minutes, string Players)>(
                "SELECT host, area, difficulty, minutes, players FROM v_instance_history");
            Assert.Equal(("Kim", "Dry Land", "Hard", 42L, "Kim, Lee"), history);
            await c.ExecuteAsync("INSERT INTO friendships (account_lo, account_hi, created_at) VALUES (@kim, @lee, UTC_TIMESTAMP(3))", new { kim, lee });
            Assert.Equal(("Kim", "Lee"), await c.QuerySingleAsync<(string, string)>("SELECT player, friend FROM v_friends"));
            await c.ExecuteAsync("INSERT INTO audit_log (at, account_id, actor_id, event) VALUES (UTC_TIMESTAMP(3), @kim, @max, 'staff_end_instance'), " +
                                 "(UTC_TIMESTAMP(3), @kim, @kim, 'login_ok')", new { kim, max });
            Assert.Equal(("staff_end_instance", "Max", "Kim"), await c.QuerySingleAsync<(string, string, string)>("SELECT event, staff, player FROM v_staff_audit"));

            // Pruning an instance takes its members along.
            await c.ExecuteAsync("DELETE FROM instance_history WHERE id = @wid", new { wid });
            Assert.Equal(0, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM instance_history_members"));
        }
        finally
        {
            await TestDb.DropAsync(name);
        }
    }

    /// <summary>
    /// Migration 0014 (reports, round I1b) on a 0.1.9 database (schema 13) with accounts, an instance and audit rows: the
    /// players' reports with a comment on every column and no binary column, a fixed reason, never oneself, *done* only with a
    /// handler's time, the instance kept as NULL when the history is pruned, and the view with the names. 0013's rows and
    /// views stay as they were (v_staff_audit is 0013's: replacing it would need the right DROP).
    /// </summary>
    [DbFact]
    public async Task Migration_14_adds_the_player_reports_to_a_schema_13_database()
    {
        var name = await TestDb.CreateAsync();
        try
        {
            await using var c = new MySqlConnection(TestDb.ConnectionString(name));
            await c.OpenAsync();
            await ApplyUpToAsync(c, 13);
            await c.ExecuteAsync("INSERT INTO accounts (username, username_norm, password_hash, status, created_at) VALUES " +
                "('Kim', 'kim', 'x', 'active', UTC_TIMESTAMP(3)), ('Lee', 'lee', 'x', 'active', UTC_TIMESTAMP(3)), " +
                "('Max', 'max', 'x', 'active', UTC_TIMESTAMP(3))");
            var ids = (await c.QueryAsync<long>("SELECT CAST(id AS SIGNED) FROM accounts ORDER BY id")).ToList();
            var (kim, lee, max) = (ids[0], ids[1], ids[2]);
            await c.ExecuteAsync("INSERT INTO instance_history (host_account_id, host_box, first_stage, visibility, difficulty, story_phase, settings, " +
                                 "started_at, ended_at, end_reason, peak_players) VALUES (@kim, 1, 'stag0201', 'public', 1, 4, 'DW4R2-settings/1', " +
                                 "'2026-11-06 10:00:00', '2026-11-06 10:42:00', 'host_left', 2)", new { kim });
            var wid = await c.ExecuteScalarAsync<long>("SELECT CAST(MAX(id) AS SIGNED) FROM instance_history");
            await c.ExecuteAsync("INSERT INTO audit_log (at, account_id, actor_id, event) VALUES (UTC_TIMESTAMP(3), @kim, @max, 'staff_end_instance')", new { kim, max });
            await using var db = new Database(TestDb.ConnectionString(name));
            var migrator = new Migrator(db);
            Assert.Equal(13, await migrator.CurrentVersionAsync());
            Assert.Equal(new[] { 14 }, await migrator.MigrateAsync());
            Assert.Equal(14, await migrator.CurrentVersionAsync());
            Assert.Equal(1, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM instance_history"));

            var columns = (await c.QueryAsync<(string Column, string Type, string Comment)>(
                "SELECT column_name, column_type, column_comment FROM information_schema.columns " +
                "WHERE table_schema = DATABASE() AND table_name = 'player_reports' ORDER BY ordinal_position")).ToList();
            Assert.Equal(10, columns.Count);
            Assert.All(columns, col => Assert.DoesNotMatch("blob|binary", col.Type));
            Assert.All(columns, col => Assert.False(string.IsNullOrWhiteSpace(col.Comment), $"player_reports.{col.Column} has no comment"));
            Assert.Equal(1, await c.ExecuteScalarAsync<int>(
                "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE() AND table_name = 'player_reports' AND table_comment <> ''"));

            const string report = "INSERT INTO player_reports (reporter_account_id, reported_account_id, reason, instance_id, created_at, state, " +
                                  "handled_by, handled_at) VALUES (@from, @about, @reason, @wid, '2026-11-06 10:30:00', @state, @by, @at)";
            await c.ExecuteAsync(report, new { from = lee, about = kim, reason = "griefing", wid, state = "done", by = max, at = "2026-11-06 11:00:00" });
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync(report,
                new { from = lee, about = kim, reason = "spam", wid, state = "open", by = (long?)null, at = (string?)null }));     // the fixed list
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync(report,
                new { from = lee, about = lee, reason = "name", wid, state = "open", by = (long?)null, at = (string?)null }));     // oneself
            await Assert.ThrowsAsync<MySqlException>(() => c.ExecuteAsync(report,
                new { from = lee, about = kim, reason = "name", wid, state = "done", by = (long?)null, at = (string?)null }));     // done without a time
            Assert.Equal(("Lee", "Kim", "griefing", "game", "Max"), await c.QuerySingleAsync<(string, string, string, string, string)>(
                "SELECT reporter, reported, reason, source, handled_by FROM v_player_reports"));
            await c.ExecuteAsync("INSERT INTO audit_log (at, account_id, actor_id, event) VALUES (UTC_TIMESTAMP(3), @kim, @max, 'report_handled')", new { kim, max });
            Assert.Equal(2, await c.ExecuteScalarAsync<int>("SELECT COUNT(*) FROM v_staff_audit"));

            // Pruning the instance keeps the report without it.
            await c.ExecuteAsync("DELETE FROM instance_history WHERE id = @wid", new { wid });
            Assert.Null(await c.ExecuteScalarAsync<long?>("SELECT instance_id FROM player_reports"));
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
