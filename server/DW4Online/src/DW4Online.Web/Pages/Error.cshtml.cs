using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages;

/// <summary>HTML error pages for the website (status code pages re-execute here; exceptions land here outside Development).</summary>
[IgnoreAntiforgeryToken]
public sealed class ErrorModel : PageModel
{
    public string Title { get; private set; } = "Something went wrong";
    public string Text { get; private set; } = "The server could not handle this request. Please try again later.";

    public void OnGet(int? code) => Describe(code);

    public void OnPost(int? code) => Describe(code);

    private void Describe(int? code)
    {
        (Title, Text) = code switch
        {
            400 => ("Bad request", "The request could not be processed. If you sent a form, reload the page and try again."),
            403 => ("Not allowed", "Your account may not open this page."),
            404 => ("Page not found", "There is no page at this address."),
            405 => ("Not allowed", "This page does not accept that kind of request."),
            _ => (Title, Text),
        };
        if (code is >= 400 and < 600) Response.StatusCode = code.Value;
    }
}
