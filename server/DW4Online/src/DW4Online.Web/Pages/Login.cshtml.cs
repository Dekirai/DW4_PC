using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Services;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.AspNetCore.RateLimiting;

namespace DW4Online.Web.Pages;

[EnableRateLimiting(WebPolicies.LoginLimit)]
public sealed class LoginModel(AccountService accounts) : PageModel
{
    [BindProperty] public string? Username { get; set; }
    [BindProperty] public string? Password { get; set; }
    [BindProperty] public bool Remember { get; set; }
    [BindProperty(SupportsGet = true)] public string? ReturnUrl { get; set; }

    public string? Error { get; private set; }

    public IActionResult OnGet() => HttpContext.WebSession() is null ? Page() : Redirect(SafeReturn());

    public async Task<IActionResult> OnPostAsync(CancellationToken ct)
    {
        var result = await accounts.LoginAsync(Username, Password, SessionKind.Web, HttpContext.Caller(WebSessions.DeviceLabel(Request)), ct);
        Password = null;
        if (!result.Ok)
        {
            Error = result.Error!.Extra?.TryGetValue("retryAfterSeconds", out var retry) == true
                ? $"{result.Error.Message} (about {retry} seconds)"
                : result.Error.Message;
            return Page();
        }
        WebSessions.SignIn(HttpContext, result.Value!.Session, Remember);
        return Redirect(result.Value.Account.MustChangePassword ? "~/account" : SafeReturn());
    }

    /// <summary>
    /// A local return address; below a path base (a shared host) only one below it: another site's path on the same
    /// host ("/elsewhere") is no page of this site.
    /// </summary>
    private string SafeReturn()
    {
        if (string.IsNullOrEmpty(ReturnUrl) || !Url.IsLocalUrl(ReturnUrl)) return "~/account";
        if (Request.PathBase.HasValue && !ReturnUrl.StartsWith("~/", StringComparison.Ordinal) &&
            !new PathString(ReturnUrl.Split('?', '#')[0]).StartsWithSegments(Request.PathBase))
            return "~/account";
        return ReturnUrl;
    }
}
