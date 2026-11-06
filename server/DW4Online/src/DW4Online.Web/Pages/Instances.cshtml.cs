using DW4Online.Domain.Services;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages;

/// <summary>
/// The world's instances (SEAMLESS.md 19.20; /rooms redirects here). Signed-in players see the instances open to them
/// (Visible(), 19.7.2) by area with host, stage, players, story point, tags and age; staff see every instance with its
/// visibility. Signed-out visitors see the counts per area only: no names, no rows.
/// </summary>
public sealed class InstancesModel(WorldService world, TimeProvider time) : PageModel
{
    public WorldCounts Counts { get; private set; } = new(0, 0, []);
    /// <summary>Per area key: the rows the viewer may see (null for a signed-out visitor).</summary>
    public IReadOnlyDictionary<string, IReadOnlyList<InstanceListing>>? Rows { get; private set; }
    public bool Staff { get; private set; }
    public DateTime Now { get; private set; }

    public async Task OnGetAsync(CancellationToken ct)
    {
        Now = time.GetUtcNow().UtcDateTime;
        Counts = world.Counts();
        if (HttpContext.WebSession() is not { } session) return;
        var viewer = await world.ViewerAsync(session.AccountId, session.Role, ct);
        Staff = viewer.Staff;
        Rows = world.Instances(viewer).GroupBy(i => i.Area).ToDictionary(g => g.Key, g => (IReadOnlyList<InstanceListing>)g.ToList());
    }

    public static string Difficulty(int d) => d switch { 1 => "Hard", 2 => "Very Hard", _ => "Normal" };

    public static string Age(int seconds) => seconds < 120 ? "just started" : seconds < 7200 ? $"{seconds / 60} min" : $"{seconds / 3600} h";
}
