using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Services;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages.Admin;

/// <summary>
/// Staff: the audit list (SEAMLESS.md 19.20): the staff rows of the audit log (joins, looks, ends, removals, account and
/// story actions) with both names. A moderator sees his own rows, an admin everybody's.
/// </summary>
public sealed class AuditModel(WorldService world) : PageModel
{
    [BindProperty(SupportsGet = true)] public string? Event { get; set; }

    public IReadOnlyList<StaffAuditRow> Rows { get; private set; } = [];
    public bool All { get; private set; }

    public async Task OnGetAsync(CancellationToken ct)
    {
        var session = HttpContext.WebSession()!;
        All = session.Role == Roles.Admin;
        Rows = await world.StaffAuditAsync(session, Event, 200, ct);
    }
}
