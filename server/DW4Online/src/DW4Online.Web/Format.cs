using System.Globalization;

namespace DW4Online.Web;

/// <summary>How the website writes dates, durations and shares (English, UTC).</summary>
public static class Format
{
    public static string Date(DateTime utc) => utc.ToString("yyyy-MM-dd HH:mm", CultureInfo.InvariantCulture) + " UTC";

    public static string Date(DateTime? utc) => utc is { } d ? Date(d) : "-";

    public static string Day(DateTime utc) => utc.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture);

    /// <summary>"3 h 07 min" (play time).</summary>
    public static string Duration(long seconds)
    {
        if (seconds < 60) return seconds.ToString(CultureInfo.InvariantCulture) + " s";
        var hours = seconds / 3600;
        var minutes = seconds % 3600 / 60;
        return hours == 0
            ? minutes.ToString(CultureInfo.InvariantCulture) + " min"
            : hours.ToString(CultureInfo.InvariantCulture) + " h " + minutes.ToString("00", CultureInfo.InvariantCulture) + " min";
    }

    public static string Percent(double value) =>
        value.ToString(value is > 0 and < 10 ? "0.0" : "0", CultureInfo.InvariantCulture) + " %";

    public static string Number(long value) => value.ToString("N0", CultureInfo.InvariantCulture);

    /// <summary>"5 minutes ago" for recent times.</summary>
    public static string Ago(DateTime utc, DateTime now)
    {
        var span = now - utc;
        if (span < TimeSpan.FromMinutes(1)) return "just now";
        if (span < TimeSpan.FromHours(1)) return $"{(int)span.TotalMinutes} min ago";
        if (span < TimeSpan.FromDays(1)) return $"{(int)span.TotalHours} h ago";
        return Date(utc);
    }

    public static string Difficulty(int? difficulty) => difficulty switch
    {
        0 => "Normal",
        1 => "Hard",
        2 => "Very Hard",
        _ => "-",
    };

    public static string Category(string category) => category switch
    {
        "digivolution" => "Digivolution",
        "multiplayer" => "Multiplayer",
        _ => category.Length == 0 ? category : char.ToUpperInvariant(category[0]) + category[1..],
    };
}
