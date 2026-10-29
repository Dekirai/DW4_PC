using DW4Online.Domain;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages;

public sealed class PrivacyModel(WebOptions web, ServiceOptions service) : PageModel
{
    public WebOptions Web => web;
    public string ServiceName => service.Name;
    public bool HasOperator => web.OperatorName.Length > 0;

    public void OnGet()
    {
    }
}
