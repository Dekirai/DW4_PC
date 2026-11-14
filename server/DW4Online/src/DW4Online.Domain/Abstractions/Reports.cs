namespace DW4Online.Domain.Abstractions;

/// <summary>The reasons a player picks when he reports another (SEAMLESS.md 19.16: a fixed list, no free text).</summary>
public static class ReportReasons
{
    public const string Griefing = "griefing";
    public const string Cheating = "cheating";
    public const string Name = "name";
    public const string Other = "other";

    /// <summary>In the order of the game's list (the wire's numbers 1-4, REPORT 0x1E).</summary>
    public static readonly IReadOnlyList<string> All = [Griefing, Cheating, Name, Other];

    /// <summary>The reason of a wire number 1-4; null for any other.</summary>
    public static string? FromWire(int reason) => reason is >= 1 and <= 4 ? All[reason - 1] : null;

    public static bool IsValid(string? reason) => reason is not null && All.Contains(reason);

    /// <summary>The words the website and the game show ("Griefing", "Cheating", "Name", "Other").</summary>
    public static string Title(string reason) => reason switch
    {
        Griefing => "Griefing",
        Cheating => "Cheating",
        Name => "Name",
        _ => "Other",
    };
}

public static class ReportStates
{
    public const string Open = "open";
    public const string Done = "done";
}

/// <summary>Where a report came from (player_reports.source).</summary>
public static class ReportSources
{
    public const string Game = "game";
    public const string Web = "web";
}

/// <summary>A row of player_reports with the names of the accounts (the admin list; v_player_reports).</summary>
public sealed record ReportRow
{
    public long Id { get; init; }
    public long ReporterAccountId { get; init; }
    public string Reporter { get; init; } = "";
    public long ReportedAccountId { get; init; }
    public string Reported { get; init; } = "";
    public string Reason { get; init; } = ReportReasons.Other;
    public string Source { get; init; } = ReportSources.Game;
    public long? InstanceId { get; init; }
    public DateTime CreatedAt { get; init; }
    public string State { get; init; } = ReportStates.Open;
    public long? HandledBy { get; init; }
    public string? HandledByName { get; init; }
    public DateTime? HandledAt { get; init; }
    /// <summary>The open reports about the reported account (the admin list shows how many there are).</summary>
    public int OpenAboutReported { get; init; }
    /// <summary>The instance both shared (instance_history): its host's name, the stage the host entered first, its start; null
    /// without an instance (or after the history was pruned).</summary>
    public string? InstanceHost { get; init; }
    public string? InstanceStage { get; init; }
    public DateTime? InstanceStarted { get; init; }
    /// <summary>The viewer is the reported account (a staff member looking at a report about himself, 19.16 "staff abuse"): who
    /// reported him is hidden, and so is the instance both shared (its host may be the reporter, its few members narrow it down).</summary>
    public bool ReporterHidden { get; init; }

    /// <summary>This row as the reported account itself may see it (<see cref="ReporterHidden"/>).</summary>
    public ReportRow WithoutReporter() => this with
    {
        ReporterAccountId = 0, Reporter = "", InstanceId = null, InstanceHost = null, InstanceStage = null, InstanceStarted = null,
        ReporterHidden = true,
    };
}

public sealed record ReportInsert(long Reporter, long Reported, string Reason, string Source, long? InstanceId, DateTime At);

/// <summary>
/// player_reports (round I1b, migration 0014). A plain store: the limits and who may report whom are
/// <see cref="Services.ReportService"/>'s rules. Rows are never written for a deleted account.
/// </summary>
public interface IReportRepository
{
    /// <summary>Inserts a report; false when either account is deleted (nothing written).</summary>
    Task<long?> InsertAsync(ReportInsert report, CancellationToken ct = default);
    /// <summary>The reporter's reports since <paramref name="since"/> (the daily limit).</summary>
    Task<int> CountByReporterAsync(long reporter, DateTime since, CancellationToken ct = default);
    /// <summary>When the reporter's report number <paramref name="skip"/> + 1 since <paramref name="since"/> came, the oldest first
    /// (null: fewer): the daily limit's retryAfterSeconds.</summary>
    Task<DateTime?> ReportTimeAsync(long reporter, DateTime since, int skip, CancellationToken ct = default);
    /// <summary>Deletes done reports handled before <paramref name="handledBefore"/> (Reports:DoneDays, the hourly housekeeping); open
    /// ones stay until staff handle them.</summary>
    Task<int> DeleteDoneAsync(DateTime handledBefore, CancellationToken ct = default);
    /// <summary>The newest report of the reporter about the reported account since <paramref name="since"/> (a repeat adds no row).</summary>
    Task<DateTime?> LastAboutAsync(long reporter, long reported, DateTime since, CancellationToken ct = default);
    /// <summary>The newest instance both accounts played in (as players) that started or ended since <paramref name="since"/>.</summary>
    Task<long?> SharedInstanceAsync(long a, long b, DateTime since, CancellationToken ct = default);
    Task<ReportRow?> GetAsync(long id, CancellationToken ct = default);
    /// <summary>Reports in a state (null: all), newest first.</summary>
    Task<IReadOnlyList<ReportRow>> ListAsync(string? state, int offset, int limit, CancellationToken ct = default);
    Task<int> CountAsync(string? state, CancellationToken ct = default);
    /// <summary>Reports about an account, newest first (the admin account page).</summary>
    Task<IReadOnlyList<ReportRow>> AboutAsync(long reported, int limit, CancellationToken ct = default);
    /// <summary>open -> done; false when the report is not open (or gone).</summary>
    Task<bool> MarkDoneAsync(long id, long staff, DateTime at, CancellationToken ct = default);
    /// <summary>done -> open; false when the report is not done (or gone).</summary>
    Task<bool> ReopenAsync(long id, CancellationToken ct = default);
}
