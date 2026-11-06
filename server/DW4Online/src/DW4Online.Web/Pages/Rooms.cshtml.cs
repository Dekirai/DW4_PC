using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages;

/// <summary>The room era's page: rooms gave way to the world's instances (round I1, SEAMLESS.md 19.20); /rooms redirects to /instances.</summary>
public sealed class RoomsModel : PageModel
{
    public IActionResult OnGet() => RedirectPermanent("~/instances");
}
