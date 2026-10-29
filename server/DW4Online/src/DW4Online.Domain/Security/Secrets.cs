using System.Security.Cryptography;
using System.Text;

namespace DW4Online.Domain.Security;

/// <summary>
/// Opaque session tokens: 256 random bits, base64url with a "dw4_" prefix (easy to spot in logs and
/// secret scanners). Only the SHA-256 of the token string is stored.
/// </summary>
public static class OpaqueToken
{
    public const string Prefix = "dw4_";

    public static string Create() => Prefix + Base64Url(RandomNumberGenerator.GetBytes(32));

    public static byte[] Hash(string token) => SHA256.HashData(Encoding.UTF8.GetBytes(token));

    /// <summary>Cheap shape check before any database work (prefix and 43 base64url characters).</summary>
    public static bool LooksValid(string? token) =>
        token is { Length: 47 } && token.StartsWith(Prefix, StringComparison.Ordinal) &&
        token.AsSpan(Prefix.Length).IndexOfAnyExcept(Base64UrlChars) < 0;

    internal static string Base64Url(byte[] bytes) =>
        Convert.ToBase64String(bytes).TrimEnd('=').Replace('+', '-').Replace('/', '_');

    private static readonly System.Buffers.SearchValues<char> Base64UrlChars =
        System.Buffers.SearchValues.Create("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_");
}

/// <summary>
/// One-time recovery codes for password resets without e-mail: 20 Crockford base32 characters (100 bits)
/// shown as five groups of four. Stored as SHA-256 of the normalised code; high entropy makes a slow hash unnecessary.
/// </summary>
public static class RecoveryCode
{
    private const string Alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

    public static string Create()
    {
        var chars = new char[20];
        for (var i = 0; i < chars.Length; ++i) chars[i] = Alphabet[RandomNumberGenerator.GetInt32(Alphabet.Length)];
        var s = new string(chars);
        return $"{s[..4]}-{s[4..8]}-{s[8..12]}-{s[12..16]}-{s[16..]}";
    }

    /// <summary>Upper case, separators removed, the look-alikes I/L to 1 and O to 0 (Crockford); null when malformed.</summary>
    public static string? Normalize(string? code)
    {
        if (code is null) return null;
        var o = new StringBuilder(20);
        foreach (var raw in code)
        {
            if (raw is '-' or ' ') continue;
            var c = char.ToUpperInvariant(raw) switch
            {
                'I' or 'L' => '1',
                'O' => '0',
                var x => x,
            };
            if (!Alphabet.Contains(c)) return null;
            o.Append(c);
        }
        return o.Length == 20 ? o.ToString() : null;
    }

    public static byte[] Hash(string normalized) => SHA256.HashData(Encoding.ASCII.GetBytes("dw4-recovery:" + normalized));
}

/// <summary>Temporary passwords an admin hands out (16 characters, letters and digits without look-alikes).</summary>
public static class TemporaryPassword
{
    private const string Alphabet = "abcdefghjkmnpqrstuvwxyzABCDEFGHJKMNPQRSTUVWXYZ23456789";

    public static string Create()
    {
        var chars = new char[16];
        for (var i = 0; i < chars.Length; ++i) chars[i] = Alphabet[RandomNumberGenerator.GetInt32(Alphabet.Length)];
        return new string(chars);
    }
}

/// <summary>
/// The security log never stores a user name that matches no account (people type their password there by mistake):
/// a failed login with an unknown name keeps only this keyed hash of the typed name (lower case), so repeated attempts
/// on one name can still be told apart. The key is derived from a server secret (Relay:TicketKey with HKDF); without
/// the key nobody can test guesses against the stored tags. Deleting an account clears the tags of its name.
/// </summary>
public sealed class AuditNameHasher
{
    private static readonly byte[] Info = Encoding.ASCII.GetBytes("DW4Online audit user names v1");
    private readonly byte[] _key;

    public AuditNameHasher(byte[] serverSecret)
    {
        if (serverSecret.Length < 16) throw new ArgumentException("The secret needs at least 16 bytes.", nameof(serverSecret));
        _key = HKDF.DeriveKey(HashAlgorithmName.SHA256, serverSecret, 32, info: Info);
    }

    /// <summary>32 hex characters (128 bits of HMAC-SHA256) for the typed name; null for an empty one.</summary>
    public string? Tag(string? typedName)
    {
        if (string.IsNullOrEmpty(typedName)) return null;
        var mac = HMACSHA256.HashData(_key, Encoding.UTF8.GetBytes(typedName.Trim().ToLowerInvariant()));
        return Convert.ToHexStringLower(mac.AsSpan(0, 16));
    }
}
