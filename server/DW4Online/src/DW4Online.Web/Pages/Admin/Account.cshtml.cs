using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Services;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages.Admin;

/// <summary>
/// Staff actions on one account: lock / unlock, ban / unban, temporary password, flag / unflag / revoke unlocks;
/// admins also change roles and delete accounts. Every action is written to the audit log by the services.
/// </summary>
public sealed class AccountModel(AccountService accounts, OnlineCharacterService characters, AchievementService achievements,
    RoomService rooms, WebOptions web) : PageModel
{
    [BindProperty] public string? Status { get; set; }
    [BindProperty] public string? Reason { get; set; }
    [BindProperty] public string? Code { get; set; }
    [BindProperty] public string? Ruleset { get; set; }
    [BindProperty] public bool Flagged { get; set; }
    [BindProperty] public string? Role { get; set; }
    [BindProperty] public bool ConfirmDelete { get; set; }

    public AccountView Target { get; private set; } = null!;
    public CharacterView? Character { get; private set; }
    public IReadOnlyList<(AchievementView Achievement, UnlockView Unlock)> Unlocks { get; private set; } = [];
    public bool IsAdmin { get; private set; }
    public string? Message { get; private set; }
    public string? Error { get; private set; }
    public string? TemporaryPassword { get; private set; }
    public string DigimonName(string model) => DigimonNames.Of(model, web);

    public async Task<IActionResult> OnGetAsync(string name, CancellationToken ct) => await ShowAsync(name, ct);

    public async Task<IActionResult> OnPostStatusAsync(string name, CancellationToken ct)
    {
        var result = await accounts.AdminSetStatusAsync(HttpContext.WebSession()!, name, Status ?? "", Reason, HttpContext.Caller(), ct);
        if (result.Ok && result.Value!.Status != AccountStatus.Active) await rooms.RemoveAccountAsync(result.Value.Id, ct);
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

    public async Task<IActionResult> OnPostRoleAsync(string name, CancellationToken ct)
    {
        var result = await accounts.AdminSetRoleAsync(HttpContext.WebSession()!, name, Role ?? "", HttpContext.Caller(), ct);
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
        await rooms.RemoveAccountAsync(target.Id, ct);
        var result = await accounts.AdminDeleteAccountAsync(actor, name, ct);
        if (!result.Ok) return await ShowAsync(name, ct, error: result.Error!.Message);
        return Redirect("/admin?q=deleted_" + target.Id);
    }

    private async Task<IActionResult> ShowAsync(string name, CancellationToken ct, string? message = null, string? error = null)
    {
        ModelState.Clear();
        var target = await accounts.FindAccountAsync(name, ct);
        if (target is null) return NotFound();
        Target = target;
        IsAdmin = HttpContext.WebSession()!.Role == Roles.Admin;
        Character = await characters.GetAsync(target.Id, ct);
        Unlocks = (await achievements.ListAsync(target.Id, ct))
            .SelectMany(a => a.Unlocks.Select(u => (a, u)))
            .OrderByDescending(x => x.u.UnlockedAt)
            .ToList();
        Message = message;
        Error = error;
        return Page();
    }
}
