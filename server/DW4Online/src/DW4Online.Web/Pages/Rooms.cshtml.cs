using DW4Online.Domain.Rooms;
using DW4Online.Domain.Services;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages;

/// <summary>The open rooms, read-only.</summary>
public sealed class RoomsModel(RoomService rooms, TimeProvider time) : PageModel
{
    public IReadOnlyList<RoomSummary> Rooms { get; private set; } = [];
    public DateTime Now { get; private set; }

    public async Task OnGetAsync(CancellationToken ct)
    {
        Now = time.GetUtcNow().UtcDateTime;
        Rooms = await rooms.ListAsync(ct: ct);
    }
}
