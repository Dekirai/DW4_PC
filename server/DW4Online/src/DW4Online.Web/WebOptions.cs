namespace DW4Online.Web;

/// <summary>The website (section "Web").</summary>
public sealed class WebOptions
{
    /// <summary>Redirect website pages from http to https and mark the session cookie Secure (production: true).</summary>
    public bool RequireHttps { get; set; }
    /// <summary>Send Strict-Transport-Security on https (production: true once the certificate is stable).</summary>
    public bool Hsts { get; set; }
    /// <summary>
    /// Where the Data Protection keys (anti-forgery tokens) live; on Windows they are encrypted with DPAPI. Empty: the
    /// ASP.NET Core default (the service account's profile). "memory": throw-away keys (tests).
    /// </summary>
    public string DataProtectionKeysDirectory { get; set; } = "";
    /// <summary>The controller named on the privacy page (GDPR Art. 13): a name, a postal address and a contact.</summary>
    public string OperatorName { get; set; } = "";
    public string OperatorAddress { get; set; } = "";
    public string OperatorContact { get; set; } = "";
    /// <summary>
    /// Service providers beside the server's hosting provider that process the website's data on the operator's behalf,
    /// one text each, shown on the privacy page under "Who receives data" (GDPR Art. 13(1)(e) and (f): who, what for, what
    /// they see, where). For example the content delivery network in front of the website (DEPLOY.md section 6.2, case 2:
    /// Cloudflare). Empty: the page names the hosting provider only.
    /// </summary>
    public string[] Processors { get; set; } = [];
    /// <summary>Display names of the partner forms by model (p_agumon = Agumon); overrides the built-in list.</summary>
    public Dictionary<string, string> DigimonNames { get; set; } = new(StringComparer.Ordinal);
}

/// <summary>Website form limits, read from the "RateLimits" section (POST requests only; viewing a form is free).</summary>
public sealed class WebRateLimits
{
    public int WebLoginPerMinute { get; set; } = 5;     // per IP: login and recovery
    public int WebRegisterPerHour { get; set; } = 3;    // per IP
    public int WebAccountChangesPerMinute { get; set; } = 10; // per account: the account page's forms (password confirmations)
    public int WebSocialPerMinute { get; set; } = 60; // per account: the friends page's and the profile's friend and block buttons
}

public static class WebPolicies
{
    public const string User = "web-user";
    public const string Staff = "web-staff";
    public const string Admin = "web-admin";
    public const string LoginLimit = "web-login";
    public const string RegisterLimit = "web-register";
    public const string AccountChangeLimit = "web-account-change";
    public const string SocialLimit = "web-social";
}

/// <summary>Display names of the 16 partner forms (Species.Models order); unknown models show their model name.</summary>
public static class DigimonNames
{
    private static readonly Dictionary<string, string> BuiltIn = new(StringComparer.Ordinal)
    {
        ["p_agumon"] = "Agumon",
        ["p_vmon"] = "Veemon",
        ["p_girumon"] = "Guilmon",
        ["p_dorumon"] = "Dorumon",
        ["p_weregaru"] = "WereGarurumon",
        ["p_hekabut"] = "HerculesKabuterimon",
        ["p_wargrey"] = "WarGreymon",
        ["p_beelzeb"] = "Beelzemon",
        ["p_alpha"] = "Alphamon",
        ["p_bwargrey"] = "BlackWarGreymon",
        ["p_impdrafm"] = "Imperialdramon Fighter Mode",
        ["p_impdrapm"] = "Imperialdramon Paladin Mode",
        ["p_metalgaru"] = "MetalGarurumon",
        ["p_dukecrim"] = "Gallantmon Crimson Mode",
        ["p_susanoo"] = "Susanoomon",
    };

    public static string Of(string model, WebOptions options) =>
        options.DigimonNames.TryGetValue(model, out var name) ? name : BuiltIn.TryGetValue(model, out var builtIn) ? builtIn : model;
}
