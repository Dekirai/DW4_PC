using DW4Online.Domain;
using DW4Online.Domain.Services;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages;

public sealed class IndexModel(AccountOptions accounts, AchievementService achievements) : PageModel
{
    public string Registration { get; private set; } = "";
    public int AchievementCount { get; private set; }

    public async Task OnGetAsync(CancellationToken ct)
    {
        Registration = accounts.Registration;
        AchievementCount = (await achievements.DefinitionsAsync(ct)).Count;
    }
}
