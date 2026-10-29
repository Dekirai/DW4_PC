using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Services;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages;

/// <summary>Every achievement with its rarity (share of players holding it); a signed-in visitor sees their own unlocks.</summary>
public sealed class AchievementsModel(AchievementService achievements) : PageModel
{
    public sealed record Row(AchievementDefinition Definition, string Name, string Description, long Holders, double Percent, bool Mine);

    public IReadOnlyList<IGrouping<string, Row>> Categories { get; private set; } = [];
    public long Players { get; private set; }
    public int MineCount { get; private set; }
    public bool SignedIn { get; private set; }

    public async Task OnGetAsync(CancellationToken ct)
    {
        var (rows, players) = await achievements.RarityAsync(ct);
        Players = players;
        var session = HttpContext.WebSession();
        SignedIn = session is not null;
        var mine = session is null
            ? new Dictionary<int, AchievementView>()
            : (await achievements.ListAsync(session.AccountId, ct)).Where(a => a.Unlocks.Count > 0).ToDictionary(a => a.Id);
        MineCount = mine.Count;
        Categories = rows
            .Select(r =>
            {
                var unlocked = mine.ContainsKey(r.Definition.Id);
                var masked = r.Definition.Hidden && !unlocked;
                return new Row(r.Definition, masked ? "Hidden achievement" : r.Definition.Name,
                    masked ? "Keep playing to discover it." : r.Definition.Description, r.Holders, r.Percent, unlocked);
            })
            .GroupBy(r => r.Definition.Category)
            .ToList();
    }
}
