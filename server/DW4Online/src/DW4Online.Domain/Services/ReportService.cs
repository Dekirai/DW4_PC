using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Accounts;

namespace DW4Online.Domain.Services;

/// <summary>The answer to a report: always "sent" for the reporter (a repeat about the same player counts for nothing).</summary>
public sealed record ReportOutcome(string Result, string Message, long? ReportId)
{
    public const string Sent = "sent";
    public const string SentMessage = "Report sent. Thank you.";
}

/// <summary>A page of the admin list.</summary>
public sealed record ReportPage(IReadOnlyList<ReportRow> Reports, int Total, int Page, int PageSize);

/// <summary>
/// Report player (round I1b, SEAMLESS.md 19.16, 19.19, 19.26 idea 5). A player reports another with a reason from a fixed
/// list (no free text), from the game's player card (REPORT through the relay, <see cref="ReportSources.Game"/>) or POST
/// /api/v1/reports (<see cref="ReportSources.Web"/>). Rules: never oneself, never a deleted account; at most
/// <see cref="ReportOptions.PerDay"/> reports in 24 hours (counted in the table: 429 report_limit); a second report about the
/// same player within <see cref="ReportOptions.RepeatHours"/> answers "sent" and adds no row. The instance both shared is
/// the relay's live one, else the newest of the instance history within <see cref="ReportOptions.SharedInstanceHours"/>.
/// A reporter at his limit is answered "limit" without a query for <see cref="LimitKnownMinutes"/> (also a repeat then).
/// Staff list them and mark them done (audited report_handled) or open again (report_reopened), never one about their own
/// account (403 own_report). Writes of one reporter are serialized (the daily count and the repeat check cannot race).
/// </summary>
public sealed class ReportService(
    IAccountRepository accounts,
    IReportRepository reports,
    IAuditLog audit,
    ReportOptions options,
    TimeProvider time)
{
    private const int LockStripes = 16;
    private readonly SemaphoreSlim[] _locks = Enumerable.Range(0, LockStripes).Select(_ => new SemaphoreSlim(1, 1)).ToArray();
    /// <summary>Reporters at their daily limit, until when they are answered "limit" without a query (round I1b review: a flood of
    /// REPORTs or POSTs past the limit costs the database nothing). Re-checked in the table after <see cref="LimitKnownMinutes"/>.</summary>
    private readonly System.Collections.Concurrent.ConcurrentDictionary<long, DateTime> _limited = new();
    public const int LimitKnownMinutes = 10;
    private const string LimitMessage = "You can't send more reports today.";

    public ReportOptions Options => options;

    /// <summary>POST /reports: the reported player by his exact user name.</summary>
    public async Task<Result<ReportOutcome>> ReportByNameAsync(long reporter, string? username, string? reason, CancellationToken ct = default)
    {
        var name = username is null ? null : UsernamePolicy.Canonical(username);
        var target = name is null ? null : await accounts.FindByUsernameAsync(UsernamePolicy.Normalize(name), ct);
        if (target is null || target.Status == AccountStatus.Deleted)
            return DomainError.NotFound("player_not_found", "No player has this exact name.");
        return await ReportAsync(reporter, target.Id, reason, null, ReportSources.Web, ct);
    }

    /// <summary>
    /// A report about <paramref name="reported"/> (the relay's REPORT names the account; <paramref name="instanceId"/> is the
    /// history id of the instance both share, when the relay knows it).
    /// </summary>
    public async Task<Result<ReportOutcome>> ReportAsync(long reporter, long reported, string? reason, long? instanceId, string source,
        CancellationToken ct = default)
    {
        if (!ReportReasons.IsValid(reason))
            return DomainError.Invalid("invalid_reason", "Choose a reason: griefing, cheating, name or other.");
        if (reporter == reported) return DomainError.Invalid("own_account", "You can't report yourself.");
        if (_limited.TryGetValue(reporter, out var until))
        {
            if (time.GetUtcNow().UtcDateTime < until) return DomainError.TooMany("report_limit", LimitMessage, 3600);
            _limited.TryRemove(new KeyValuePair<long, DateTime>(reporter, until));
        }
        var target = await accounts.GetAsync(reported, ct);
        if (target is null || target.Status == AccountStatus.Deleted)
            return DomainError.NotFound("player_not_found", "No player has this exact name.");
        var gate = _locks[(int)((ulong)reporter % LockStripes)];
        await gate.WaitAsync(ct);
        try
        {
            var now = time.GetUtcNow().UtcDateTime;
            if (await reports.LastAboutAsync(reporter, reported, now.AddHours(-Math.Max(0, options.RepeatHours)), ct) is not null)
                return new ReportOutcome(ReportOutcome.Sent, ReportOutcome.SentMessage, null);
            var day = now.AddDays(-1);
            if (await reports.CountByReporterAsync(reporter, day, ct) >= Math.Max(1, options.PerDay))
            {
                _limited[reporter] = now.AddMinutes(LimitKnownMinutes);
                return DomainError.TooMany("report_limit", LimitMessage, 3600);
            }
            instanceId ??= await reports.SharedInstanceAsync(reporter, reported, now.AddHours(-Math.Max(0, options.SharedInstanceHours)), ct);
            var id = await reports.InsertAsync(new ReportInsert(reporter, reported, reason!, source, instanceId, now), ct);
            if (id is null) return DomainError.NotFound("player_not_found", "No player has this exact name.");
            return new ReportOutcome(ReportOutcome.Sent, ReportOutcome.SentMessage, id);
        }
        finally
        {
            gate.Release();
        }
    }

    /// <summary>The admin list for the staff member <paramref name="viewer"/>: <paramref name="state"/> open, done or null (all), page 1
    /// first; a report about the viewer himself without its reporter (<see cref="ReportRow.WithoutReporter"/>).</summary>
    public async Task<ReportPage> ListAsync(string? state, int page, long viewer, CancellationToken ct = default)
    {
        state = state is ReportStates.Open or ReportStates.Done ? state : null;
        var size = Math.Clamp(options.PageSize, 1, 500);
        page = Math.Max(1, page);
        var total = await reports.CountAsync(state, ct);
        var rows = await reports.ListAsync(state, (page - 1) * size, size, ct);
        return new ReportPage(ForViewer(rows, viewer), total, page, size);
    }

    public Task<int> OpenCountAsync(CancellationToken ct = default) => reports.CountAsync(ReportStates.Open, ct);

    /// <summary>The last 50 reports about <paramref name="reported"/> (the account page) for the staff member <paramref name="viewer"/>:
    /// without their reporters when he looks at his own account.</summary>
    public async Task<IReadOnlyList<ReportRow>> AboutAsync(long reported, long viewer, CancellationToken ct = default) =>
        ForViewer(await reports.AboutAsync(reported, 50, ct), viewer);

    /// <summary>A staff member never learns who reported him (19.16 "staff abuse": retaliation; review 2026-11-12).</summary>
    private static IReadOnlyList<ReportRow> ForViewer(IReadOnlyList<ReportRow> rows, long viewer) =>
        rows.Any(r => r.ReportedAccountId == viewer) ? rows.Select(r => r.ReportedAccountId == viewer ? r.WithoutReporter() : r).ToList() : rows;

    /// <summary>A report about the staff member's own account is handled by another moderator (19.16 "staff abuse").</summary>
    private static DomainError OwnReport() =>
        DomainError.Forbidden("own_report", "This report is about your own account: another moderator handles it.");

    /// <summary>Staff mark a report done (audited report_handled); never one about themselves (403 own_report).</summary>
    public async Task<Result<bool>> DoneAsync(long id, long staff, byte[]? ip, CancellationToken ct = default)
    {
        var row = await reports.GetAsync(id, ct);
        if (row is null) return DomainError.NotFound("report_not_found", "No report has this number.");
        if (row.ReportedAccountId == staff) return OwnReport();
        var now = time.GetUtcNow().UtcDateTime;
        if (!await reports.MarkDoneAsync(id, staff, now, ct)) return DomainError.Conflict("report_done", "This report is done already.");
        await audit.WriteAsync("report_handled", row.ReportedAccountId, staff, ip,
            new { reportId = id, reason = row.Reason, reportedId = row.ReportedAccountId, reporterId = row.ReporterAccountId }, now, ct);
        return true;
    }

    /// <summary>Staff open a done report again (audited report_reopened); never one about themselves (403 own_report).</summary>
    public async Task<Result<bool>> ReopenAsync(long id, long staff, byte[]? ip, CancellationToken ct = default)
    {
        var row = await reports.GetAsync(id, ct);
        if (row is null) return DomainError.NotFound("report_not_found", "No report has this number.");
        if (row.ReportedAccountId == staff) return OwnReport();
        if (!await reports.ReopenAsync(id, ct)) return DomainError.Conflict("report_open", "This report is open already.");
        await audit.WriteAsync("report_reopened", row.ReportedAccountId, staff, ip, new { reportId = id, reportedId = row.ReportedAccountId },
            time.GetUtcNow().UtcDateTime, ct);
        return true;
    }
}
