using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Services;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages;

/// <summary>
/// A player's profile: the online character summary and the achievements with unlock dates and progress. Private
/// profiles show only to their owner and staff; flagged unlocks (under review) only to them as well.
/// </summary>
public sealed class ProfileModel(AccountService accounts, OnlineCharacterService characters, AchievementService achievements, WebOptions web) : PageModel
{
    public AccountView Account { get; private set; } = null!;
    public bool Hidden { get; private set; }
    public bool IsOwner { get; private set; }
    public bool IsStaff { get; private set; }
    public CharacterView? Character { get; private set; }
    public IReadOnlyList<(AchievementView Achievement, UnlockView First, IReadOnlyList<UnlockView> All)> Unlocked { get; private set; } = [];
    public IReadOnlyList<AchievementView> InProgress { get; private set; } = [];
    public int Total { get; private set; }
    public int Points { get; private set; }

    public string DigimonName(string model) => DigimonNames.Of(model, web);

    public async Task<IActionResult> OnGetAsync(string name, CancellationToken ct)
    {
        var account = await accounts.FindAccountAsync(name, ct);
        if (account is null || account.Status == AccountStatus.Deleted) return NotFound();
        var viewer = HttpContext.WebSession();
        IsOwner = viewer?.AccountId == account.Id;
        IsStaff = viewer.IsStaff();
        Account = account;
        var visible = IsOwner || IsStaff || (account.ProfilePublic && account.Status != AccountStatus.Banned);
        if (!visible)
        {
            Hidden = true;
            return Page();
        }

        Character = await characters.GetAsync(account.Id, ct);
        var list = await achievements.ListAsync(account.Id, ct);
        Total = list.Count;
        var showFlagged = IsOwner || IsStaff;
        var unlocked = new List<(AchievementView, UnlockView, IReadOnlyList<UnlockView>)>();
        foreach (var a in list)
        {
            var shown = a.Unlocks.Where(u => showFlagged || !u.Flagged).OrderBy(u => u.UnlockedAt).ToList();
            if (shown.Count > 0) unlocked.Add((a, shown[0], shown));
        }
        Unlocked = unlocked.OrderByDescending(u => u.Item2.UnlockedAt).ThenBy(u => u.Item1.Id).ToList();
        Points = Unlocked.Sum(u => u.Achievement.Points);
        InProgress = list.Where(a => a.Unlocks.Count == 0 && a.Progress is > 0 && a.Goal is > 0).ToList();
        return Page();
    }
}
