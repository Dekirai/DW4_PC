using System.Net;
using DW4Online.Domain;
using DW4Online.Domain.Accounts;
using DW4Online.Domain.Security;

namespace DW4Online.Tests;

public class LoginThrottleTests
{
    private static byte[] Ip(string text) => IPAddress.Parse(text).GetAddressBytes();

    private int _next;

    /// <summary>Another IPv4 address on each call (each its own network).</summary>
    private byte[] Next() => [198, 51, (byte)(100 + _next / 250), (byte)(1 + _next++ % 250)];

    [Fact]
    public void Keys_use_the_canonical_name_and_the_address_or_its_ipv6_network()
    {
        Assert.Equal(LoginThrottle.Key("Kevin", Ip("203.0.113.5")), LoginThrottle.Key(" kevin ", Ip("203.0.113.5")));
        Assert.NotEqual(LoginThrottle.Key("kevin", Ip("203.0.113.5")), LoginThrottle.Key("kevin", Ip("203.0.113.6")));
        Assert.Equal(LoginThrottle.Key("kevin", Ip("2001:db8:1:2::10")), LoginThrottle.Key("kevin", Ip("2001:db8:1:2:aaaa::1")));
        Assert.NotEqual(LoginThrottle.Key("kevin", Ip("2001:db8:1:2::10")), LoginThrottle.Key("kevin", Ip("2001:db8:1:3::10")));
        Assert.Equal("kevin|-", LoginThrottle.Key("kevin", null));
    }

    /// <summary>
    /// Text that cannot be a user name (accents and full-width letters the database collation would fold into a real
    /// name, 60,000 characters) shares one "?" key per address: no variant gets its own count, no key grows with the input.
    /// </summary>
    [Fact]
    public void Text_that_cannot_be_a_user_name_shares_one_short_key()
    {
        var ip = Ip("203.0.113.5");
        var shared = LoginThrottle.Key("?", ip);
        foreach (var typed in new[] { "álice", "ａlice", "alice​", "a", "a b", "", null, new string('x', 60_000), "alice\nbob" })
            Assert.Equal(shared, LoginThrottle.Key(typed, ip));
        Assert.Equal("?|CB007105", shared);
        Assert.Equal("alice|CB007105", LoginThrottle.Key("\tALICE \n", ip));
        Assert.Null(DW4Online.Domain.Accounts.UsernamePolicy.Canonical("ａlice"));
        Assert.Equal("a-b_c", DW4Online.Domain.Accounts.UsernamePolicy.Canonical(" A-b_C "));
        Assert.NotNull(DW4Online.Domain.Accounts.UsernamePolicy.Check("alice\n")); // "$" would have let the line break through
    }

    [Fact]
    public void Rate_limit_addresses_count_ipv6_by_its_64_network()
    {
        Assert.Equal(LoginThrottle.AddressKey(IPAddress.Parse("2001:db8:1:2::1")), LoginThrottle.AddressKey(IPAddress.Parse("2001:db8:1:2:ffff::9")));
        Assert.NotEqual(LoginThrottle.AddressKey(IPAddress.Parse("2001:db8:1:2::1")), LoginThrottle.AddressKey(IPAddress.Parse("2001:db8:1:3::1")));
        Assert.Equal(LoginThrottle.AddressKey(IPAddress.Parse("203.0.113.5")), LoginThrottle.AddressKey(IPAddress.Parse("::ffff:203.0.113.5")));
        Assert.Equal("-", LoginThrottle.AddressKey((IPAddress?)null));
        Assert.Equal(LoginThrottle.NetworkKey(Ip("2001:db8:1:2::1")), LoginThrottle.NetworkKey(Ip("2001:db8:1:ff::1")));
        Assert.NotEqual(LoginThrottle.NetworkKey(Ip("2001:db8:1::1")), LoginThrottle.NetworkKey(Ip("2001:db8:2::1")));
    }

    /// <summary>One network adds at most MaxFailedLogins failures per window to an account; other networks add their own.</summary>
    [Fact]
    public void One_network_adds_at_most_its_share_to_the_account()
    {
        var time = new ManualTime(new DateTimeOffset(2026, 11, 1, 12, 0, 0, TimeSpan.Zero));
        var throttle = new LoginThrottle(new AccountOptions { MaxFailedLogins = 3, MaxFailedLoginsPerAccount = 6, LockoutMinutes = 15 }, time);
        for (var net = 0; net < 50; ++net) // 50 /64 networks of one /48, 3 failures each
            for (var i = 0; i < 3; ++i)
                Assert.Null(throttle.FailAccount(7, Ip($"2001:db8:bb:{net:x}::1")));
        for (var i = 0; i < 2; ++i) Assert.Null(throttle.FailAccount(7, Ip("203.0.113.5")));
        Assert.NotNull(throttle.FailAccount(7, Ip("203.0.113.5"))); // 3 + 3: another network's share completes the lock

        time.Advance(TimeSpan.FromMinutes(16)); // a new window: the /48's share is back
        throttle.ResetAccount(7);
        for (var i = 0; i < 3; ++i) Assert.Null(throttle.FailAccount(7, Ip("2001:db8:bb:1::1")));
        for (var i = 0; i < 2; ++i) Assert.Null(throttle.FailAccount(7, Ip("2001:db8:cc:1::1")));
        Assert.NotNull(throttle.FailAccount(7, Ip("2001:db8:cc:2::1")));
    }

    [Fact]
    public void A_pair_locks_after_the_limit_and_unlocks_after_the_window()
    {
        var time = new ManualTime(new DateTimeOffset(2026, 11, 1, 12, 0, 0, TimeSpan.Zero));
        var throttle = new LoginThrottle(new AccountOptions { MaxFailedLogins = 3, LockoutMinutes = 15 }, time);
        var stranger = LoginThrottle.Key("kevin", Ip("198.51.100.1"));
        var owner = LoginThrottle.Key("kevin", Ip("203.0.113.5"));

        Assert.Null(throttle.Fail(stranger));
        Assert.Null(throttle.Fail(stranger));
        var until = throttle.Fail(stranger);
        Assert.Equal(time.GetUtcNow().UtcDateTime.AddMinutes(15), until);
        Assert.Equal(until, throttle.LockedUntil(stranger));
        Assert.Null(throttle.LockedUntil(owner)); // the owner's address is not affected

        time.Advance(TimeSpan.FromMinutes(16));
        Assert.Null(throttle.LockedUntil(stranger));
        Assert.Null(throttle.Fail(stranger)); // counting starts again
    }

    [Fact]
    public void Failures_older_than_the_window_do_not_add_up_and_a_success_clears_them()
    {
        var time = new ManualTime(new DateTimeOffset(2026, 11, 1, 12, 0, 0, TimeSpan.Zero));
        var throttle = new LoginThrottle(new AccountOptions { MaxFailedLogins = 3, LockoutMinutes = 15 }, time);
        var key = LoginThrottle.Key("typo", Ip("203.0.113.5"));
        throttle.Fail(key);
        throttle.Fail(key);
        time.Advance(TimeSpan.FromMinutes(20));
        Assert.Null(throttle.Fail(key));
        Assert.Null(throttle.Fail(key));
        throttle.Reset(key);
        Assert.Null(throttle.Fail(key));
        Assert.Null(throttle.Fail(key));
        Assert.NotNull(throttle.Fail(key));
    }

    /// <summary>The account-wide count only adds up within one window: slow guessing never locks the account.</summary>
    [Fact]
    public void The_account_count_locks_only_within_one_window()
    {
        var time = new ManualTime(new DateTimeOffset(2026, 11, 1, 12, 0, 0, TimeSpan.Zero));
        var throttle = new LoginThrottle(new AccountOptions { MaxFailedLogins = 3, MaxFailedLoginsPerAccount = 6, LockoutMinutes = 15 }, time);
        for (var round = 0; round < 4; ++round) // 5 failures every 16 minutes: 20 in all, never 6 in one window
        {
            for (var i = 0; i < 5; ++i) Assert.Null(throttle.FailAccount(42, Next()));
            time.Advance(TimeSpan.FromMinutes(16));
        }
        for (var i = 0; i < 5; ++i) Assert.Null(throttle.FailAccount(42, Next()));
        Assert.Equal(time.GetUtcNow().UtcDateTime.AddMinutes(15), throttle.FailAccount(42, Next())); // the 6th in one window
        Assert.Null(throttle.FailAccount(43, Next())); // another account is not affected

        time.Advance(TimeSpan.FromMinutes(16));
        for (var i = 0; i < 5; ++i) Assert.Null(throttle.FailAccount(42, Next()));
        throttle.ResetAccount(42); // a success clears the count
        for (var i = 0; i < 5; ++i) Assert.Null(throttle.FailAccount(42, Next()));
        Assert.NotNull(throttle.FailAccount(42, Next()));

        // Account counts and pair counts never mix.
        var pair = LoginThrottle.Key("42", Ip("203.0.113.5"));
        Assert.Null(throttle.Fail(pair));
        Assert.Null(throttle.LockedUntil(pair));
    }

    [Fact]
    public void Audit_name_tags_are_keyed_and_normalised()
    {
        var a = new AuditNameHasher(new byte[32]);
        var b = new AuditNameHasher(Enumerable.Repeat((byte)1, 32).ToArray());
        Assert.Equal(a.Tag("Kevin"), a.Tag(" kevin "));
        Assert.NotEqual(a.Tag("kevin"), b.Tag("kevin")); // another server key, another tag
        Assert.NotEqual(a.Tag("kevin"), a.Tag("kevin2"));
        Assert.Null(a.Tag(""));
        Assert.DoesNotContain("kevin", a.Tag("kevin")!);
    }
}
