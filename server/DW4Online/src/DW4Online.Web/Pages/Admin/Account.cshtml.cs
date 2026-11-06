using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Services;
using DW4Online.Domain.Story;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages.Admin;

/// <summary>
/// Staff actions on one account: lock / unlock, ban / unban, temporary password, flag / unflag / revoke unlocks, put a
/// kept revision of the online story back (damage from a forged or faulty merge); admins also change roles and delete
/// accounts. Every action is written to the audit log by the services.
/// </summary>
public sealed class AccountModel(AccountService accounts, OnlineCharacterService characters, AchievementService achievements,
    WorldService world, WebOptions web) : PageModel
{
    [BindProperty] public string? Status { get; set; }
    [BindProperty] public string? Reason { get; set; }
    [BindProperty] public string? Code { get; set; }
    [BindProperty] public string? Ruleset { get; set; }
    [BindProperty] public bool Flagged { get; set; }
    [BindProperty] public string? Role { get; set; }
    [BindProperty] public bool ConfirmDelete { get; set; }
    [BindProperty] public long StoryRevision { get; set; }

    public AccountView Target { get; private set; } = null!;
    public CharacterView? Character { get; private set; }
    public IReadOnlyList<(AchievementView Achievement, UnlockView Unlock)> Unlocks { get; private set; } = [];
    public IReadOnlyList<StoryHistoryEntry> Story { get; private set; } = [];
    /// <summary>The account's instances (hosted, joined, played as staff or watched), newest first (19.20).</summary>
    public IReadOnlyList<InstanceHistoryEntry> Instances { get; private set; } = [];
    public bool IsAdmin { get; private set; }
    public string? Message { get; private set; }
    public string? Error { get; private set; }
    public string? TemporaryPassword { get; private set; }
    public string DigimonName(string model) => DigimonNames.Of(model, web);

    public async Task<IActionResult> OnGetAsync(string name, CancellationToken ct) => await ShowAsync(name, ct);

    public async Task<IActionResult> OnPostStatusAsync(string name, CancellationToken ct)
    {
        var result = await accounts.AdminSetStatusAsync(HttpContext.WebSession()!, name, Status ?? "", Reason, HttpContext.Caller(), ct);
        if (result.Ok) world.AccountChanged(result.Value!.Id);
        return await ShowAsync(name, ct, result.Ok ? $"The account is now {result.Value!.Status}." : null, result.Error?.Message);
    }

    public async Task<IActionResult> OnPostResetPasswordAsync(string name, CancellationToken ct)
    {
        var result = await accounts.AdminResetPasswordAsync(HttpContext.WebSession()!, name, HttpContext.Caller(), ct);
        TemporaryPassword = result.Value;
        return await ShowAsync(name, ct, result.Ok ? "A temporary password was set; every session of the account was logged out." : null, result.Error?.Message);
    }

    public async Task<IActionResult> OnPostFlagAsync(string name, CancellationToken ct)
    {
        if (await accounts.FindAccountAsync(name, ct) is not { } target) return NotFound();
        var result = await achievements.AdminSetFlagAsync(HttpContext.WebSession()!.AccountId, target.Id, Code, Ruleset, Flagged, HttpContext.Caller().Ip, ct);
        return await ShowAsync(name, ct, result.Ok ? (Flagged ? $"{Code} is flagged." : $"{Code} is no longer flagged.") : null, result.Error?.Message);
    }

    public async Task<IActionResult> OnPostRevokeAsync(string name, CancellationToken ct)
    {
        if (await accounts.FindAccountAsync(name, ct) is not { } target) return NotFound();
        var result = await achievements.AdminRevokeAsync(HttpContext.WebSession()!.AccountId, target.Id, Code, Ruleset, HttpContext.Caller().Ip, ct);
        return await ShowAsync(name, ct, result.Ok ? $"{Code} was removed from the account." : null, result.Error?.Message);
    }

    public async Task<IActionResult> OnPostRestoreStoryAsync(string name, CancellationToken ct)
    {
        if (await accounts.FindAccountAsync(name, ct) is not { } target) return NotFound();
        if (!await characters.HasStoryRevisionAsync(target.Id, StoryRevision, ct))
            return await ShowAsync(name, ct, error: "No such kept revision of the online story.");
        var result = await characters.RestoreStoryAsync(target.Id, StoryRevision, HttpContext.WebSession()!.AccountId, HttpContext.Caller().Ip, ct);
        // The relay's keeper reloads the restored story and the account's hosted instance ends (19.8.1).
        if (result.Ok) world.StoryReplaced(target.Id);
        return await ShowAsync(name, ct, result.Ok ? $"Story revision {StoryRevision} is back as revision {result.Value}." : null, result.Error?.Message);
    }

    public async Task<IActionResult> OnPostRoleAsync(string name, CancellationToken ct)
    {
        var result = await accounts.AdminSetRoleAsync(HttpContext.WebSession()!, name, Role ?? "", HttpContext.Caller(), ct);
        if (result.Ok) world.AccountChanged(result.Value!.Id); // a demotion counts in the world at once (19.14)
        return await ShowAsync(name, ct, result.Ok ? $"The role is now {result.Value!.Role}." : null, result.Error?.Message);
    }

    public async Task<IActionResult> OnPostDeleteAsync(string name, CancellationToken ct)
    {
        var actor = HttpContext.WebSession()!;
        if (!ConfirmDelete) return await ShowAsync(name, ct, error: "Tick the box to confirm the deletion.");
        if (actor.Role != Roles.Admin) return Forbid();
        if (await accounts.FindAccountAsync(name, ct) is not { } target) return NotFound();
        if (target.Role == Roles.Admin || target.Id == actor.AccountId)
            return await ShowAsync(name, ct, error: "Admin accounts cannot be deleted here.");
        world.AccountChanged(target.Id);
        var result = await accounts.AdminDeleteAccountAsync(actor, name, ct);
        if (!result.Ok) return await ShowAsync(name, ct, error: result.Error!.Message);
        world.AccountChanged(target.Id); // (a world session that started meanwhile ends)
        return Redirect("~/admin?q=deleted_" + target.Id);
    }

    private async Task<IActionResult> ShowAsync(string name, CancellationToken ct, string? message = null, string? error = null)
    {
        ModelState.Clear();
        var target = await accounts.FindAccountAsync(name, ct);
        if (target is null) return NotFound();
        Target = target;
        IsAdmin = HttpContext.WebSession()!.Role == Roles.Admin;
        Character = await characters.GetAsync(target.Id, ct);
        Story = await characters.StoryHistoryAsync(target.Id, ct);
        Instances = await world.HistoryAsync(target.Id, 50, withSpectating: true, ct);
        Unlocks = (await achievements.ListAsync(target.Id, ct))
            .SelectMany(a => a.Unlocks.Select(u => (a, u)))
            .OrderByDescending(x => x.u.UnlockedAt)
            .ToList();
        Message = message;
        Error = error;
        return Page();
    }
}
