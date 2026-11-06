using DW4Online.Domain;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Services;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.AspNetCore.RateLimiting;

namespace DW4Online.Web.Pages;

/// <summary>
/// A player's profile: the online character summary and the achievements with unlock dates and progress. Private
/// profiles show only to their owner and staff; flagged unlocks (under review) only to them as well. A signed-in visitor
/// gets the friend and block buttons for his relation to the player (SEAMLESS.md 19.20), on private profiles too; they
/// never show whether the player blocks the visitor. Round I1a: a friend sees where the player is and when he was last
/// online, as the player's whereabouts preference allows (19.20); nobody else does.
/// </summary>
[EnableRateLimiting(WebPolicies.SocialLimit)]
public sealed class ProfileModel(AccountService accounts, OnlineCharacterService characters, AchievementService achievements,
    SocialService social, WorldService world, WebOptions web, TimeProvider time) : PageModel
{
    public AccountView Account { get; private set; } = null!;
    /// <summary>The visitor's relation to the player (<see cref="Relations"/>); null when nobody is signed in.</summary>
    public string? Relation { get; private set; }
    public string? Message { get; private set; }
    public string? Error { get; private set; }
    public bool Hidden { get; private set; }
    /// <summary>Where the player is (friends only, by his whereabouts preference); null otherwise.</summary>
    public FriendPresence? Presence { get; private set; }
    public DateTime Now { get; private set; }
    public bool IsOwner { get; private set; }
    public bool IsStaff { get; private set; }
    public CharacterView? Character { get; private set; }
    public IReadOnlyList<(AchievementView Achievement, UnlockView First, IReadOnlyList<UnlockView> All)> Unlocked { get; private set; } = [];
    public IReadOnlyList<AchievementView> InProgress { get; private set; } = [];
    public int Total { get; private set; }
    public int Points { get; private set; }

    public string DigimonName(string model) => DigimonNames.Of(model, web);

    public Task<IActionResult> OnGetAsync(string name, CancellationToken ct) => ShowAsync(name, ct);

    public Task<IActionResult> OnPostAddFriendAsync(string name, CancellationToken ct) =>
        ActAsync(name, ct, async (me, caller) => Done(await social.SendRequestAsync(me, name, caller, ct)));

    public Task<IActionResult> OnPostCancelRequestAsync(string name, CancellationToken ct) =>
        ActAsync(name, ct, async (me, _) => Done(await social.CancelAsync(me, name, ct), n => $"Your request to {n} was cancelled."));

    public Task<IActionResult> OnPostAcceptAsync(string name, CancellationToken ct) =>
        ActAsync(name, ct, async (me, caller) => Done(await social.AcceptAsync(me, name, caller, ct)));

    public Task<IActionResult> OnPostDeclineAsync(string name, CancellationToken ct) =>
        ActAsync(name, ct, async (me, _) => Done(await social.DeclineAsync(me, name, ct), n => $"You declined the request from {n}. They are not told."));

    public Task<IActionResult> OnPostRemoveFriendAsync(string name, CancellationToken ct) =>
        ActAsync(name, ct, async (me, _) => Done(await social.RemoveFriendAsync(me, name, ct), n => $"{n} is no longer on your friend list."));

    public Task<IActionResult> OnPostBlockAsync(string name, CancellationToken ct) =>
        ActAsync(name, ct, async (me, caller) => Done(await social.BlockAsync(me, name, caller, ct), n => $"You blocked {n}. They are not told."));

    public Task<IActionResult> OnPostUnblockAsync(string name, CancellationToken ct) =>
        ActAsync(name, ct, async (me, _) => Done(await social.UnblockAsync(me, name, ct), n => $"You unblocked {n}."));

    /// <summary>A friend or block button: signed-in visitors only (others go to the login and come back here).</summary>
    private async Task<IActionResult> ActAsync(string name, CancellationToken ct, Func<long, CallerInfo, Task<(string? Message, string? Error)>> action)
    {
        if (HttpContext.WebSession() is not { } viewer)
            return Redirect("~/login?returnUrl=" + Uri.EscapeDataString(Request.PathBase + "/u/" + name));
        (Message, Error) = await action(viewer.AccountId, HttpContext.Caller());
        return await ShowAsync(name, ct);
    }

    private static (string?, string?) Done(Result<RequestOutcome> result) => (result.Value?.Message, result.Error?.Message);

    private static (string?, string?) Done(Result<string> result, Func<string, string> message) =>
        (result.Ok ? message(result.Value!) : null, result.Error?.Message);

    private async Task<IActionResult> ShowAsync(string name, CancellationToken ct)
    {
        var account = await accounts.FindAccountAsync(name, ct);
        if (account is null || account.Status == AccountStatus.Deleted) return NotFound();
        var viewer = HttpContext.WebSession();
        IsOwner = viewer?.AccountId == account.Id;
        IsStaff = viewer.IsStaff();
        Account = account;
        Relation = viewer is null ? null : await social.RelationAsync(viewer.AccountId, account.Id, ct);
        Now = time.GetUtcNow().UtcDateTime;
        if (Relation == Relations.Friends) Presence = (await world.PresenceAsync([account.Id], ct)).GetValueOrDefault(account.Id);
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
