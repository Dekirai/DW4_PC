using DW4Online.Domain.Services;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages;

/// <summary>Ends this browser's web session (POST only, with an anti-forgery token).</summary>
public sealed class LogoutModel(AccountService accounts) : PageModel
{
    public void OnGet()
    {
    }

    public async Task<IActionResult> OnPostAsync(CancellationToken ct)
    {
        if (HttpContext.WebSession() is { } session) await accounts.LogoutAsync(session, ct);
        WebSessions.SignOut(HttpContext);
        return Redirect("~/");
    }
}
