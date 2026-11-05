using DW4Online.Domain;
using DW4Online.Domain.Services;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.AspNetCore.RateLimiting;

namespace DW4Online.Web.Pages.Account;

/// <summary>
/// The signed-in player's friends (SEAMLESS.md 19.20): friends with Remove and Block, requests in (Accept, Decline) and
/// out (Cancel), blocked players (Unblock), and requests and blocks by exact user name. Its POSTs are rate limited per
/// account (RateLimits:WebSocialPerMinute) on top of the friend service's own limits. The online preferences and
/// "Recently played with" join this page with round I1 (they need its tables).
/// </summary>
[EnableRateLimiting(WebPolicies.SocialLimit)]
public sealed class FriendsModel(SocialService social) : PageModel
{
    /// <summary>The name a form names: typed (add, block) or the row's (every other button).</summary>
    [BindProperty] public string? Username { get; set; }

    public FriendsOverview Overview { get; private set; } = null!;
    public string? Message { get; private set; }
    public string? Error { get; private set; }

    public Task<IActionResult> OnGetAsync(CancellationToken ct) => ShowAsync(ct);

    public async Task<IActionResult> OnPostAddAsync(CancellationToken ct)
    {
        var result = await social.SendRequestAsync(Me, Username, HttpContext.Caller(), ct);
        return await ShowAsync(ct, result.Value?.Message, result.Error);
    }

    public async Task<IActionResult> OnPostAcceptAsync(CancellationToken ct)
    {
        var result = await social.AcceptAsync(Me, Username, HttpContext.Caller(), ct);
        return await ShowAsync(ct, result.Value?.Message, result.Error);
    }

    public async Task<IActionResult> OnPostDeclineAsync(CancellationToken ct) =>
        await ShowAsync(ct, await social.DeclineAsync(Me, Username, ct), name => $"You declined the request from {name}. They are not told.");

    public async Task<IActionResult> OnPostCancelAsync(CancellationToken ct) =>
        await ShowAsync(ct, await social.CancelAsync(Me, Username, ct), name => $"Your request to {name} was cancelled.");

    public async Task<IActionResult> OnPostRemoveAsync(CancellationToken ct) =>
        await ShowAsync(ct, await social.RemoveFriendAsync(Me, Username, ct), name => $"{name} is no longer on your friend list.");

    public async Task<IActionResult> OnPostBlockAsync(CancellationToken ct) =>
        await ShowAsync(ct, await social.BlockAsync(Me, Username, HttpContext.Caller(), ct), name => $"You blocked {name}. They are not told.");

    public async Task<IActionResult> OnPostUnblockAsync(CancellationToken ct) =>
        await ShowAsync(ct, await social.UnblockAsync(Me, Username, ct), name => $"You unblocked {name}.");

    private long Me => HttpContext.WebSession()!.AccountId;

    /// <summary>Shows the page after an action that answers with the other player's user name.</summary>
    private Task<IActionResult> ShowAsync(CancellationToken ct, Result<string> result, Func<string, string> done) =>
        ShowAsync(ct, result.Ok ? done(result.Value!) : null, result.Error);

    private async Task<IActionResult> ShowAsync(CancellationToken ct, string? message = null, DomainError? error = null)
    {
        ModelState.Clear(); // the forms start empty again
        Overview = await social.OverviewAsync(Me, ct);
        Message = message;
        Error = error?.Message;
        Username = null;
        return Page();
    }
}
