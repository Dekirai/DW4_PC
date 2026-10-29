using DW4Online.Domain;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Services;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.AspNetCore.RateLimiting;

namespace DW4Online.Web.Pages;

[EnableRateLimiting(WebPolicies.RegisterLimit)]
public sealed class RegisterModel(AccountService accounts, AccountOptions options) : PageModel
{
    [BindProperty] public string? Username { get; set; }
    [BindProperty] public string? Password { get; set; }
    [BindProperty] public string? ConfirmPassword { get; set; }
    [BindProperty] public string? Email { get; set; }
    /// <summary>Honeypot: hidden from people, filled by bots.</summary>
    [BindProperty] public string? Website { get; set; }

    public string? Error { get; private set; }
    public string? RecoveryCode { get; private set; }
    public string? CreatedName { get; private set; }
    public bool Open => string.Equals(options.Registration, "open", StringComparison.OrdinalIgnoreCase);

    public IActionResult OnGet() => HttpContext.WebSession() is null ? Page() : Redirect("/account");

    public async Task<IActionResult> OnPostAsync(CancellationToken ct)
    {
        if (!Open) return Page();
        if (!string.IsNullOrEmpty(Website))
        {
            Error = "The request could not be processed.";
            return Page();
        }
        if (Password != ConfirmPassword)
        {
            Error = "The two passwords do not match.";
            return Page();
        }
        var result = await accounts.RegisterAsync(Username, Password, Email, SessionKind.Web,
            HttpContext.Caller(WebSessions.DeviceLabel(Request)), ct);
        Password = ConfirmPassword = null;
        if (!result.Ok)
        {
            Error = result.Error!.Message;
            return Page();
        }
        WebSessions.SignIn(HttpContext, result.Value!.Session, persistent: false);
        RecoveryCode = result.Value.RecoveryCode;
        CreatedName = result.Value.Account.Username;
        return Page();
    }
}
