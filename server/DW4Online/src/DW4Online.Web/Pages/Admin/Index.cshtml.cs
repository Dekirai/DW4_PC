using DW4Online.Domain.Services;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages.Admin;

/// <summary>Staff: account search, the world's counts (the instances themselves: /admin/instances) and the open reports.</summary>
public sealed class IndexModel(AccountService accounts, WorldService world, ReportService reports) : PageModel
{
    [BindProperty(SupportsGet = true)] public string? Q { get; set; }

    public IReadOnlyList<AccountView> Results { get; private set; } = [];
    public WorldCounts Counts { get; private set; } = new(0, 0, []);
    /// <summary>Reports no staff member has handled yet (round I1b).</summary>
    public int OpenReports { get; private set; }

    public async Task OnGetAsync(CancellationToken ct)
    {
        Results = await accounts.SearchAsync(Q, 50, ct);
        Counts = world.Counts();
        OpenReports = await reports.OpenCountAsync(ct);
    }
}
