using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Services;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages.Admin;

/// <summary>
/// Staff: every instance of the world (SEAMLESS.md 19.20), Private ones included, with its host, members, spectators,
/// visibility, settings and age; End (the instance ends for everyone, its guests go home) and Remove (a member goes
/// home). Both are audited (staff_end_instance, staff_remove). Each form names the instance by the relay run the page was
/// loaded in and its wid (fix-019: wids count from 1 again after a server restart; a page from before one is refused).
/// </summary>
public sealed class InstancesModel(WorldService world, TimeProvider time) : PageModel
{
    [BindProperty] public long Wid { get; set; }
    /// <summary>The relay run the posting page listed the instance in.</summary>
    [BindProperty] public string? Run { get; set; }
    [BindProperty] public string? Username { get; set; }

    public IReadOnlyList<WorldInstanceRow> Rows { get; private set; } = [];
    /// <summary>The relay run of <see cref="Rows"/> (the forms post it back).</summary>
    public string CurrentRun { get; private set; } = "";
    public DateTime Now { get; private set; }
    public string? Message { get; private set; }
    public string? Error { get; private set; }

    public void OnGet() => Load();

    public async Task<IActionResult> OnPostEndAsync(CancellationToken ct)
    {
        if (Wid is >= 1 and <= uint.MaxValue)
        {
            var result = await world.EndInstanceAsync(HttpContext.WebSession()!.AccountId, (uint)Wid, Run, HttpContext.Caller().Ip, ct);
            if (result.Ok) Message = $"Instance {Wid} was ended; its players go home.";
            else Error = result.Error!.Message;
        }
        else Error = "No such instance (it may have ended).";
        Load();
        return Page();
    }

    public async Task<IActionResult> OnPostRemoveAsync(CancellationToken ct)
    {
        if (Wid is >= 1 and <= uint.MaxValue)
        {
            var result = await world.RemoveMemberAsync(HttpContext.WebSession()!.AccountId, (uint)Wid, Run, Username, HttpContext.Caller().Ip, ct);
            if (result.Ok) Message = $"{result.Value} was removed from instance {Wid} and goes home.";
            else Error = result.Error!.Message;
        }
        else Error = "No such instance (it may have ended).";
        Load();
        return Page();
    }

    private void Load()
    {
        ModelState.Clear();
        Now = time.GetUtcNow().UtcDateTime;
        (CurrentRun, Rows) = world.AdminInstances();
    }
}
