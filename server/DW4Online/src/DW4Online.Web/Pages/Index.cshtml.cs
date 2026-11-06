using DW4Online.Domain;
using DW4Online.Domain.Services;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages;

public sealed class IndexModel(ServiceOptions service, AccountOptions accountOptions, AccountService accounts, AchievementService achievements,
    WorldService world) : PageModel
{
    public ServiceOptions Service => service;
    public int PlayersOnline { get; private set; }
    public int Instances { get; private set; }
    public IReadOnlyList<AreaCount> Areas { get; private set; } = [];
    public long Accounts { get; private set; }
    public int AchievementCount { get; private set; }
    public bool RegistrationOpen => string.Equals(accountOptions.Registration, "open", StringComparison.OrdinalIgnoreCase);
    public bool SignedIn => HttpContext.WebSession() is not null;

    public async Task OnGetAsync(CancellationToken ct)
    {
        var counts = world.Counts();
        (PlayersOnline, Instances, Areas) = (counts.PlayersOnline, counts.Instances, counts.Areas);
        Accounts = await accounts.CountActiveAsync(ct);
        AchievementCount = (await achievements.DefinitionsAsync(ct)).Count;
    }
}
