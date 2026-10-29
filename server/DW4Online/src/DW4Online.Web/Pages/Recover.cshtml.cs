using DW4Online.Domain.Services;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.AspNetCore.RateLimiting;

namespace DW4Online.Web.Pages;

[EnableRateLimiting(WebPolicies.LoginLimit)]
public sealed class RecoverModel(AccountService accounts) : PageModel
{
    [BindProperty] public string? Username { get; set; }
    [BindProperty] public string? RecoveryCode { get; set; }
    [BindProperty] public string? NewPassword { get; set; }
    [BindProperty] public string? ConfirmPassword { get; set; }

    public string? Error { get; private set; }
    public string? NextCode { get; private set; }

    public void OnGet()
    {
    }

    public async Task<IActionResult> OnPostAsync(CancellationToken ct)
    {
        if (NewPassword != ConfirmPassword)
        {
            Error = "The two passwords do not match.";
            return Page();
        }
        var result = await accounts.RecoverAsync(Username, RecoveryCode, NewPassword, HttpContext.Caller(), ct);
        NewPassword = ConfirmPassword = RecoveryCode = null;
        if (!result.Ok)
        {
            Error = result.Error!.Message;
            return Page();
        }
        WebSessions.SignOut(HttpContext);
        NextCode = result.Value;
        return Page();
    }
}
