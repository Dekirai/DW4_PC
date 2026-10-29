using DW4Online.Domain;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Services;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.AspNetCore.RateLimiting;

namespace DW4Online.Web.Pages.Account;

/// <summary>
/// Account settings: password, e-mail, recovery code, profile visibility, sessions, cloud save, deletion. Its POSTs are
/// rate limited per account, and a wrong current password counts as a failed login (AccountService).
/// </summary>
[EnableRateLimiting(WebPolicies.AccountChangeLimit)]
public sealed class IndexModel(
    AccountService accounts,
    OnlineCharacterService characters,
    CloudSaveService cloud,
    RoomService rooms,
    WebOptions web) : PageModel
{
    [BindProperty] public string? CurrentPassword { get; set; }
    [BindProperty] public string? NewPassword { get; set; }
    [BindProperty] public string? ConfirmPassword { get; set; }
    [BindProperty] public string? Email { get; set; }
    [BindProperty] public bool Public { get; set; }
    [BindProperty] public long SessionId { get; set; }
    [BindProperty] public bool ConfirmDelete { get; set; }

    public AccountView Account { get; private set; } = null!;
    public AuthenticatedSession Session { get; private set; } = null!;
    public IReadOnlyList<SessionInfo> Sessions { get; private set; } = [];
    public CharacterView? Character { get; private set; }
    public CloudSaveOverview? Cloud { get; private set; }
    public string? Message { get; private set; }
    public string? Error { get; private set; }
    public string? NewRecoveryCode { get; private set; }
    public string DigimonName(string model) => DigimonNames.Of(model, web);

    public async Task<IActionResult> OnGetAsync(CancellationToken ct) => await ShowAsync(ct);

    public async Task<IActionResult> OnPostPasswordAsync(CancellationToken ct)
    {
        if (NewPassword != ConfirmPassword) return await ShowAsync(ct, error: "The two new passwords do not match.");
        var result = await accounts.ChangePasswordAsync(HttpContext.WebSession()!, CurrentPassword, NewPassword, HttpContext.Caller(), ct);
        return await ShowAsync(ct, result.Ok ? "Your password was changed. Every other session was logged out." : null, result.Error?.Message,
            mustChangeDone: result.Ok);
    }

    public async Task<IActionResult> OnPostEmailAsync(CancellationToken ct)
    {
        if (Blocked() is { } blocked) return blocked;
        var result = await accounts.SetEmailAsync(HttpContext.WebSession()!, CurrentPassword, Email, HttpContext.Caller(), ct);
        return await ShowAsync(ct, result.Ok ? (result.Value!.Email is null ? "Your e-mail address was removed." : "Your e-mail address was saved.") : null,
            result.Error?.Message);
    }

    public async Task<IActionResult> OnPostRecoveryAsync(CancellationToken ct)
    {
        if (Blocked() is { } blocked) return blocked;
        var result = await accounts.RegenerateRecoveryCodeAsync(HttpContext.WebSession()!, CurrentPassword, HttpContext.Caller(), ct);
        NewRecoveryCode = result.Value;
        return await ShowAsync(ct, result.Ok ? "A new recovery code was made; the old one no longer works." : null, result.Error?.Message);
    }

    public async Task<IActionResult> OnPostVisibilityAsync(CancellationToken ct)
    {
        if (Blocked() is { } blocked) return blocked;
        var result = await accounts.SetProfilePublicAsync(HttpContext.WebSession()!, Public, HttpContext.Caller(), ct);
        return await ShowAsync(ct, result.Ok ? (Public ? "Your profile is public." : "Your profile is private.") : null, result.Error?.Message);
    }

    public async Task<IActionResult> OnPostRevokeAsync(CancellationToken ct)
    {
        if (Blocked() is { } blocked) return blocked;
        var session = HttpContext.WebSession()!;
        if (SessionId == session.SessionId) return await ShowAsync(ct, error: "Use \"Log out\" to end this browser's session.");
        var done = await accounts.RevokeSessionAsync(session.AccountId, SessionId, ct);
        return await ShowAsync(ct, done ? "The session was logged out." : null, done ? null : "No such active session.");
    }

    public async Task<IActionResult> OnPostRevokeOthersAsync(CancellationToken ct)
    {
        if (Blocked() is { } blocked) return blocked;
        var count = await accounts.LogoutOthersAsync(HttpContext.WebSession()!, HttpContext.Caller(), ct);
        return await ShowAsync(ct, count == 1 ? "1 other session was logged out." : $"{count} other sessions were logged out.");
    }

    /// <summary>The private cloud backup as a file (the latest revision).</summary>
    public async Task<IActionResult> OnGetCloudSaveAsync(CancellationToken ct)
    {
        if (Blocked() is { } blocked) return blocked;
        var data = await cloud.DownloadAsync(HttpContext.WebSession()!.AccountId, null, ct);
        if (data is null) return NotFound();
        return File(data.Data, "application/octet-stream", data.Format == "gci" ? "savedata.dat" : "NDMWSaveData.bin");
    }

    public async Task<IActionResult> OnPostDeleteAsync(CancellationToken ct)
    {
        var session = HttpContext.WebSession()!;
        if (!ConfirmDelete) return await ShowAsync(ct, error: "Tick the box to confirm that the account and its data should be deleted.");
        if (session.Role == Roles.Admin) return await ShowAsync(ct, error: "Admins cannot delete their own account; remove the admin role first.");
        if (await accounts.ConfirmPasswordAsync(session.AccountId, CurrentPassword, HttpContext.Caller(), ct) is { Ok: false } wrong)
            return await ShowAsync(ct, error: wrong.Error!.Message);
        await rooms.RemoveAccountAsync(session.AccountId, ct);
        var result = await accounts.DeleteAccountAsync(session, CurrentPassword, HttpContext.Caller(), ct);
        if (!result.Ok) return await ShowAsync(ct, error: result.Error!.Message);
        WebSessions.SignOut(HttpContext);
        return Redirect("/?deleted=1");
    }

    // While a temporary password is in force only the password form works.
    private IActionResult? Blocked() => HttpContext.WebSession() is { MustChangePassword: true } ? Redirect("/account") : null;

    private async Task<IActionResult> ShowAsync(CancellationToken ct, string? message = null, string? error = null, bool mustChangeDone = false)
    {
        ModelState.Clear(); // the forms show the stored values, never the posted ones
        Session = HttpContext.WebSession()!;
        var account = await accounts.GetAccountAsync(Session.AccountId, ct);
        if (account is null) return Redirect("/login");
        Account = account;
        if (mustChangeDone && account.MustChangePassword) Account = account with { MustChangePassword = false };
        Sessions = await accounts.ListSessionsAsync(Session.AccountId, ct);
        Character = await characters.GetAsync(Session.AccountId, ct);
        Cloud = await cloud.GetAsync(Session.AccountId, ct);
        Public = Account.ProfilePublic;
        Email = Account.Email;
        CurrentPassword = NewPassword = ConfirmPassword = null;
        Message = message;
        Error = error;
        return Page();
    }
}
