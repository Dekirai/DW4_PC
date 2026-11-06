using DW4Online.Domain.Services;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages.Admin;

/// <summary>Staff: account search and the world's counts (the instances themselves: /admin/instances).</summary>
public sealed class IndexModel(AccountService accounts, WorldService world) : PageModel
{
    [BindProperty(SupportsGet = true)] public string? Q { get; set; }

    public IReadOnlyList<AccountView> Results { get; private set; } = [];
    public WorldCounts Counts { get; private set; } = new(0, 0, []);

    public async Task OnGetAsync(CancellationToken ct)
    {
        Results = await accounts.SearchAsync(Q, 50, ct);
        Counts = world.Counts();
    }
}
