using DW4Online.Domain.Rooms;
using DW4Online.Domain.Services;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages.Admin;

/// <summary>Staff: account search and the open rooms (close one).</summary>
public sealed class IndexModel(AccountService accounts, RoomService rooms, TimeProvider time) : PageModel
{
    [BindProperty(SupportsGet = true)] public string? Q { get; set; }
    [BindProperty] public string? Code { get; set; }

    public IReadOnlyList<AccountView> Results { get; private set; } = [];
    public IReadOnlyList<RoomSummary> Rooms { get; private set; } = [];
    public DateTime Now { get; private set; }
    public string? Message { get; private set; }
    public string? Error { get; private set; }

    public async Task OnGetAsync(CancellationToken ct) => await LoadAsync(ct);

    public async Task<IActionResult> OnPostCloseRoomAsync(CancellationToken ct)
    {
        var result = await rooms.CloseAsync(HttpContext.WebSession()!.AccountId, true, Code ?? "", HttpContext.Caller().Ip, ct);
        if (result.Ok) Message = $"Room {Code} was closed.";
        else Error = result.Error!.Message;
        await LoadAsync(ct);
        return Page();
    }

    private async Task LoadAsync(CancellationToken ct)
    {
        Now = time.GetUtcNow().UtcDateTime;
        Results = await accounts.SearchAsync(Q, 50, ct);
        Rooms = await rooms.ListAsync(ct: ct);
    }
}
