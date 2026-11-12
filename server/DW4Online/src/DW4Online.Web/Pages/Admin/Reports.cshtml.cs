using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Services;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages.Admin;

/// <summary>
/// Staff: the players' reports (round I1b, SEAMLESS.md 19.16, 19.20): open ones first (newest first) with the reporter, the
/// reported player (and how many open reports there are about him), the reason, where it came from and the instance both
/// shared; *Done* marks one handled (audited report_handled), *Reopen* opens a done one again (report_reopened). What staff do
/// about the player (lock, ban, the instance's End or Remove) is on his account page and /admin/instances.
/// </summary>
public sealed class ReportsModel(ReportService reports, WorldService world, TimeProvider time) : PageModel
{
    [BindProperty(SupportsGet = true)] public string State { get; set; } = ReportStates.Open;
    [BindProperty(SupportsGet = true, Name = "p")] public int PageNumber { get; set; } = 1;
    [BindProperty] public long Id { get; set; }

    public ReportPage Reports { get; private set; } = new([], 0, 1, 50);
    /// <summary>The reported players who are in the world now: their live instance (wid) for the link to /admin/instances.</summary>
    public IReadOnlyDictionary<long, uint> LiveInstance { get; private set; } = new Dictionary<long, uint>();
    public DateTime Now { get; private set; }
    /// <summary>The signed-in staff member (a report about him gets no Done / Reopen here: another moderator handles it).</summary>
    public long Me { get; private set; }
    public string? Message { get; private set; }
    public string? Error { get; private set; }

    public async Task OnGetAsync(CancellationToken ct) => await LoadAsync(ct);

    public async Task<IActionResult> OnPostDoneAsync(CancellationToken ct)
    {
        var result = await reports.DoneAsync(Id, HttpContext.WebSession()!.AccountId, HttpContext.Caller().Ip, ct);
        if (result.Ok) Message = $"Report {Id} is done.";
        else Error = result.Error!.Message;
        await LoadAsync(ct);
        return Page();
    }

    public async Task<IActionResult> OnPostReopenAsync(CancellationToken ct)
    {
        var result = await reports.ReopenAsync(Id, HttpContext.WebSession()!.AccountId, HttpContext.Caller().Ip, ct);
        if (result.Ok) Message = $"Report {Id} is open again.";
        else Error = result.Error!.Message;
        await LoadAsync(ct);
        return Page();
    }

    private async Task LoadAsync(CancellationToken ct)
    {
        ModelState.Clear();
        State = State is ReportStates.Done or "all" ? State : ReportStates.Open;
        Now = time.GetUtcNow().UtcDateTime;
        Me = HttpContext.WebSession()?.AccountId ?? 0;
        Reports = await reports.ListAsync(State == "all" ? null : State, PageNumber, Me, ct);
        var live = new Dictionary<long, uint>();
        foreach (var i in world.AdminInstances())
            foreach (var m in i.Members)
                live[m.AccountId] = i.Wid;
        LiveInstance = live;
    }
}
