using System.Security.Cryptography;
using System.Text;

namespace DW4Online.Domain.Security;

public enum PasswordCheck
{
    Failed,
    Success,
    /// <summary>Correct, but stored with weaker parameters than configured: store a new hash.</summary>
    SuccessRehashNeeded,
}

/// <summary>
/// PBKDF2-HMAC-SHA512 (OWASP Password Storage Cheat Sheet: at least 210,000 iterations; default 220,000,
/// about 150 ms on the development PC). Stored as a PHC-style string:
/// <c>$pbkdf2-sha512$i=220000$&lt;salt&gt;$&lt;hash&gt;</c> with unpadded base64.
/// </summary>
public sealed class PasswordHasher
{
    public const int DefaultIterations = 220_000;
    private const string Prefix = "$pbkdf2-sha512$";
    private const int SaltBytes = 16;
    private const int HashBytes = 32;
    private const int MinIterations = 1_000; // anything weaker is treated as corrupt

    private readonly int _iterations;
    private readonly Lazy<string> _dummy;

    public PasswordHasher(int iterations = DefaultIterations)
    {
        if (iterations < MinIterations) throw new ArgumentOutOfRangeException(nameof(iterations));
        _iterations = iterations;
        _dummy = new Lazy<string>(() => Hash("dummy password for unknown accounts"));
    }

    public int Iterations => _iterations;

    public string Hash(string password)
    {
        var salt = RandomNumberGenerator.GetBytes(SaltBytes);
        var hash = Derive(password, salt, _iterations);
        return $"{Prefix}i={_iterations}${B64(salt)}${B64(hash)}";
    }

    public PasswordCheck Verify(string stored, string password)
    {
        if (!TryParse(stored, out var iterations, out var salt, out var expected)) return PasswordCheck.Failed;
        var actual = Derive(password, salt, iterations);
        if (!CryptographicOperations.FixedTimeEquals(actual, expected)) return PasswordCheck.Failed;
        return iterations < _iterations ? PasswordCheck.SuccessRehashNeeded : PasswordCheck.Success;
    }

    /// <summary>Spends the same time as a real check, so unknown user names do not answer faster.</summary>
    public void VerifyDummy(string password) => Verify(_dummy.Value, password);

    private static byte[] Derive(string password, byte[] salt, int iterations) =>
        Rfc2898DeriveBytes.Pbkdf2(Encoding.UTF8.GetBytes(password), salt, iterations, HashAlgorithmName.SHA512, HashBytes);

    private static bool TryParse(string stored, out int iterations, out byte[] salt, out byte[] hash)
    {
        iterations = 0;
        salt = hash = [];
        if (!stored.StartsWith(Prefix, StringComparison.Ordinal)) return false;
        var parts = stored[Prefix.Length..].Split('$');
        if (parts.Length != 3 || !parts[0].StartsWith("i=", StringComparison.Ordinal)) return false;
        if (!int.TryParse(parts[0].AsSpan(2), System.Globalization.NumberStyles.None, null, out iterations) || iterations < MinIterations)
            return false;
        try
        {
            salt = Unb64(parts[1]);
            hash = Unb64(parts[2]);
        }
        catch (FormatException)
        {
            return false;
        }
        return salt.Length >= 8 && hash.Length == HashBytes;
    }

    private static string B64(byte[] bytes) => Convert.ToBase64String(bytes).TrimEnd('=');

    private static byte[] Unb64(string s) => Convert.FromBase64String(s.PadRight(s.Length + (4 - s.Length % 4) % 4, '='));
}
