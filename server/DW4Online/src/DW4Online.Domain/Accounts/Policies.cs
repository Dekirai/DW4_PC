using System.Text.RegularExpressions;

namespace DW4Online.Domain.Accounts;

/// <summary>User names: 3-20 of A-Z a-z 0-9 _ -, unique ignoring case (the DB has the same CHECK on username_norm).</summary>
public static partial class UsernamePolicy
{
    private static readonly HashSet<string> Reserved = new(StringComparer.Ordinal)
    {
        "admin", "administrator", "root", "system", "moderator", "mod", "support", "staff", "server",
        "dw4", "dw4online", "dw4_pc", "official", "null", "undefined", "anonymous", "guest", "host",
    };

    // \z, not $: "$" also matches before a final line break ("alice\n").
    [GeneratedRegex(@"^[A-Za-z0-9_-]{3,20}\z", RegexOptions.CultureInvariant)]
    private static partial Regex Shape();

    /// <summary>Deleted accounts are renamed to "deleted_&lt;id&gt;"; nobody can register such a name.</summary>
    public const string DeletedPrefix = "deleted_";

    public static string Normalize(string username) => username.ToLowerInvariant();

    /// <summary>
    /// The account name a typed login name can stand for: trimmed and lower case when it has the shape of a user name,
    /// null when no account can have it. Logins and recovery look up and count only this form, never the typed text:
    /// the database collation folds accents and full-width letters ("álice" and "ａlice" find "alice"), so each such
    /// variant would otherwise get its own failed-login count, and a long typed name would sit in the throttle's memory.
    /// </summary>
    public static string? Canonical(string? typed)
    {
        if (typed is null) return null;
        var trimmed = typed.Trim();
        return Shape().IsMatch(trimmed) ? Normalize(trimmed) : null;
    }

    public static string? Check(string? username)
    {
        if (string.IsNullOrEmpty(username) || !Shape().IsMatch(username))
            return "User names are 3 to 20 characters: letters, digits, '_' and '-'.";
        if (Reserved.Contains(Normalize(username)) || Normalize(username).StartsWith(DeletedPrefix, StringComparison.Ordinal))
            return "This user name is reserved.";
        return null;
    }
}

/// <summary>Passwords: 10 to 128 characters, not the user name. Length beats composition rules (NIST SP 800-63B).</summary>
public static class PasswordPolicy
{
    public const int MinLength = 10;
    public const int MaxLength = 128;

    private static readonly HashSet<string> Common = new(StringComparer.OrdinalIgnoreCase)
    {
        "1234567890", "12345678910", "0123456789", "password123", "password1!", "qwertyuiop", "qwertzuiop",
        "1q2w3e4r5t", "iloveyou12", "passwort123", "digimon123", "digimonworld", "abcdefghij", "aaaaaaaaaa",
    };

    public static string? Check(string? password, string? username)
    {
        if (password is null || password.Length < MinLength) return $"Passwords need at least {MinLength} characters.";
        if (password.Length > MaxLength) return $"Passwords have at most {MaxLength} characters.";
        if (username is not null && string.Equals(password, username, StringComparison.OrdinalIgnoreCase))
            return "The password must not be the user name.";
        if (Common.Contains(password) || password.Distinct().Count() < 4) return "This password is too easy to guess.";
        return null;
    }
}

/// <summary>Optional e-mail addresses (only for password resets once SMTP exists): a light shape check, unique ignoring case.</summary>
public static partial class EmailPolicy
{
    [GeneratedRegex(@"^[^@\s]{1,64}@[^@\s]+\.[^@\s]{2,}$", RegexOptions.CultureInvariant)]
    private static partial Regex Shape();

    public static string Normalize(string email) => email.Trim().ToLowerInvariant();

    public static string? Check(string? email)
    {
        if (email is null) return null;
        var trimmed = email.Trim();
        if (trimmed.Length > 254 || !Shape().IsMatch(trimmed)) return "This does not look like an e-mail address.";
        return null;
    }
}

/// <summary>Client versions as "major.minor.patch" (extra labels after '-' or '+' are ignored for ordering).</summary>
public readonly record struct ClientVersion(int Major, int Minor, int Patch) : IComparable<ClientVersion>
{
    public static bool TryParse(string? text, out ClientVersion version)
    {
        version = default;
        if (string.IsNullOrWhiteSpace(text)) return false;
        var core = text.Split('-', '+')[0];
        var parts = core.Split('.');
        if (parts.Length is < 2 or > 3) return false;
        var numbers = new int[3];
        for (var i = 0; i < parts.Length; ++i)
            if (!int.TryParse(parts[i], System.Globalization.NumberStyles.None, null, out numbers[i])) return false;
        version = new ClientVersion(numbers[0], numbers[1], numbers[2]);
        return true;
    }

    public int CompareTo(ClientVersion other) =>
        Major != other.Major ? Major.CompareTo(other.Major) :
        Minor != other.Minor ? Minor.CompareTo(other.Minor) : Patch.CompareTo(other.Patch);

    public static bool operator <(ClientVersion a, ClientVersion b) => a.CompareTo(b) < 0;
    public static bool operator >(ClientVersion a, ClientVersion b) => a.CompareTo(b) > 0;
    public static bool operator <=(ClientVersion a, ClientVersion b) => a.CompareTo(b) <= 0;
    public static bool operator >=(ClientVersion a, ClientVersion b) => a.CompareTo(b) >= 0;

    public override string ToString() => $"{Major}.{Minor}.{Patch}";
}
