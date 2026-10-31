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
    /// <summary>How long the start waits for a database that does not answer yet (MariaDB still starting after a reboot).</summary>
    public int StartupWaitSeconds { get; set; } = 60;
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
}
