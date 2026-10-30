using Dapper;
using MySqlConnector;

namespace DW4Online.Data;

/// <summary>Configuration section "Database".</summary>
public sealed class DatabaseOptions
{
    /// <summary>The MariaDB connection string (keep it in appsettings.Local.json / appsettings.Production.json, never in Git).</summary>
    public string ConnectionString { get; set; } = "";
    /// <summary>Run pending migrations at startup.</summary>
    public bool AutoMigrate { get; set; } = true;
}

/// <summary>The pooled MariaDB data source shared by all repositories.</summary>
public sealed class Database : IAsyncDisposable
{
    private readonly MySqlDataSource _source;

    static Database()
    {
        DefaultTypeMap.MatchNamesWithUnderscores = true;
    }

    public Database(string connectionString)
    {
        if (string.IsNullOrWhiteSpace(connectionString))
            throw new InvalidOperationException("Database:ConnectionString is not configured (see server/DW4Online/README.md).");
        var builder = new MySqlConnectionStringBuilder(connectionString)
        {
            DateTimeKind = MySqlDateTimeKind.Utc, // every DATETIME column holds UTC
            AllowUserVariables = false,
            CharacterSet = "utf8mb4",
        };
        DatabaseName = builder.Database;
        _source = new MySqlDataSource(builder.ConnectionString);
    }

    public string DatabaseName { get; }

    public async Task<MySqlConnection> OpenAsync(CancellationToken ct = default) => await _source.OpenConnectionAsync(ct);

    public ValueTask DisposeAsync() => _source.DisposeAsync();
}

internal static class MySqlErrors
{
    public static bool IsDuplicate(MySqlException e) => e.ErrorCode == MySqlErrorCode.DuplicateKeyEntry;

    public static bool IsDeadlockOrTimeout(MySqlException e) =>
        e.ErrorCode is MySqlErrorCode.LockDeadlock or MySqlErrorCode.LockWaitTimeout;

    /// <summary>
    /// The isolation of a revisioned write (online character, cloud save). Under REPEATABLE READ the FOR UPDATE read
    /// of an account's missing row takes a gap lock, the first writes of two accounts at once lock the same gap and
    /// their INSERTs deadlock: InnoDB made one the victim, a 409 although nobody else had written that account's data
    /// (round S2: a client took it for another PC's save). READ COMMITTED takes no gap locks for these reads; the row
    /// lock of an existing revision and the duplicate key of a concurrent first write still decide every real race.
    /// </summary>
    public const System.Data.IsolationLevel RevisionedWrite = System.Data.IsolationLevel.ReadCommitted;

    /// <summary>Attempts of a revisioned write before a deadlock (or a lock wait timeout) counts as a lost race.</summary>
    public const int DeadlockAttempts = 3;

    /// <summary>A deadlock victim that should run its transaction again.</summary>
    public static bool RetryDeadlock(MySqlException e, int attempt) => IsDeadlockOrTimeout(e) && attempt < DeadlockAttempts;
}
