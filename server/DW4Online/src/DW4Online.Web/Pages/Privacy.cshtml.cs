using DW4Online.Domain;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages;

public sealed class PrivacyModel(WebOptions web, ServiceOptions service) : PageModel
{
    public WebOptions Web => web;
    public string ServiceName => service.Name;
    public bool HasOperator => web.OperatorName.Length > 0;
    /// <summary>Web:Processors without empty entries.</summary>
    public IReadOnlyList<string> Processors => (web.Processors ?? []).Where(p => !string.IsNullOrWhiteSpace(p)).Select(p => p.Trim()).ToList();

    public void OnGet()
    {
    }
}
