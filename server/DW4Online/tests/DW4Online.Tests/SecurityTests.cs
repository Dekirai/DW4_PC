using DW4Online.Domain.Accounts;
using DW4Online.Domain.Security;

namespace DW4Online.Tests;

public class SecurityTests
{
    [Fact]
    public void Password_hash_verifies_and_rejects()
    {
        var hasher = new PasswordHasher(2_000);
        var hash = hasher.Hash("correct horse battery");
        Assert.StartsWith("$pbkdf2-sha512$i=2000$", hash, StringComparison.Ordinal);
        Assert.Equal(PasswordCheck.Success, hasher.Verify(hash, "correct horse battery"));
        Assert.Equal(PasswordCheck.Failed, hasher.Verify(hash, "correct horse batterY"));
        Assert.NotEqual(hash, hasher.Hash("correct horse battery")); // salted
    }

    [Fact]
    public void Stronger_settings_ask_for_a_rehash()
    {
        var old = new PasswordHasher(2_000).Hash("a long password");
        Assert.Equal(PasswordCheck.SuccessRehashNeeded, new PasswordHasher(4_000).Verify(old, "a long password"));
    }

    [Theory]
    [InlineData("")]
    [InlineData("plain")]
    [InlineData("$pbkdf2-sha512$i=10$AAAAAAAAAAA$AAAA")]
    [InlineData("$pbkdf2-sha512$i=2000$!!$!!")]
    [InlineData("AQAAAAIAAYagAAAAE")] // an ASP.NET Identity hash is not accepted
    public void Malformed_hashes_never_verify(string stored)
    {
        Assert.Equal(PasswordCheck.Failed, new PasswordHasher(2_000).Verify(stored, "anything"));
    }

    [Fact]
    public void Default_iterations_follow_owasp()
    {
        Assert.True(PasswordHasher.DefaultIterations >= 210_000);
        Assert.Throws<ArgumentOutOfRangeException>(() => new PasswordHasher(10));
    }

    [Fact]
    public void Tokens_have_a_fixed_shape_and_hash()
    {
        var token = OpaqueToken.Create();
        Assert.True(OpaqueToken.LooksValid(token));
        Assert.Equal(47, token.Length);
        Assert.NotEqual(token, OpaqueToken.Create());
        Assert.Equal(32, OpaqueToken.Hash(token).Length);
        Assert.Equal(OpaqueToken.Hash(token), OpaqueToken.Hash(token));
        Assert.False(OpaqueToken.LooksValid(null));
        Assert.False(OpaqueToken.LooksValid("dw4_short"));
        Assert.False(OpaqueToken.LooksValid("xx4_" + token[4..]));
        Assert.False(OpaqueToken.LooksValid(token[..46] + "!"));
    }

    [Fact]
    public void Recovery_codes_normalise()
    {
        var code = RecoveryCode.Create();
        Assert.Matches("^[0-9A-Z]{4}(-[0-9A-Z]{4}){4}$", code);
        var normalized = RecoveryCode.Normalize(code);
        Assert.NotNull(normalized);
        Assert.Equal(normalized, RecoveryCode.Normalize(code.ToLowerInvariant().Replace("-", " ")));
        Assert.Equal("01100000000000000000", RecoveryCode.Normalize("OIL0-0000-0000-0000-0000"));
        Assert.Null(RecoveryCode.Normalize("UUUU-0000-0000-0000-0000")); // U is not in the alphabet
        Assert.Null(RecoveryCode.Normalize("0000-0000"));
        Assert.Equal(RecoveryCode.Hash(normalized!), RecoveryCode.Hash(RecoveryCode.Normalize(code)!));
    }

    [Theory]
    [InlineData("Kevin", true)]
    [InlineData("dw4_fan-1", true)]
    [InlineData("ab", false)]
    [InlineData("a_name_that_is_far_too_long", false)]
    [InlineData("bad name", false)]
    [InlineData("Ümlaut", false)]
    [InlineData("Admin", false)]
    [InlineData(null, false)]
    public void Usernames(string? name, bool ok) => Assert.Equal(ok, UsernamePolicy.Check(name) is null);

    [Theory]
    [InlineData("long enough pass", "kevin", true)]
    [InlineData("short", "kevin", false)]
    [InlineData("kevin12345", "Kevin12345", false)]
    [InlineData("1234567890", "kevin", false)]
    [InlineData("aaaaaaaaaaaa", "kevin", false)]
    public void Passwords(string password, string user, bool ok) => Assert.Equal(ok, PasswordPolicy.Check(password, user) is null);

    [Theory]
    [InlineData("kevin@example.org", true)]
    [InlineData(null, true)]
    [InlineData("no-at-sign", false)]
    [InlineData("a@b", false)]
    public void Emails(string? email, bool ok) => Assert.Equal(ok, EmailPolicy.Check(email) is null);

    [Fact]
    public void Client_versions_compare()
    {
        Assert.True(ClientVersion.TryParse("0.48.0", out var a));
        Assert.True(ClientVersion.TryParse("0.48.1-rc1", out var b));
        Assert.True(ClientVersion.TryParse("1.2", out var c));
        Assert.True(a < b && b < c);
        Assert.False(ClientVersion.TryParse("v1", out _));
        Assert.False(ClientVersion.TryParse("", out _));
        Assert.Equal("1.2.0", c.ToString());
    }
}
