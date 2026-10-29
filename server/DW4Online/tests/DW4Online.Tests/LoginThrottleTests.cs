using System.Net;
using DW4Online.Domain;
using DW4Online.Domain.Accounts;
using DW4Online.Domain.Security;

namespace DW4Online.Tests;

public class LoginThrottleTests
{
    private static byte[] Ip(string text) => IPAddress.Parse(text).GetAddressBytes();

    [Fact]
    public void Keys_use_the_typed_name_and_the_address_or_its_ipv6_network()
    {
        Assert.Equal(LoginThrottle.Key("Kevin", Ip("203.0.113.5")), LoginThrottle.Key(" kevin ", Ip("203.0.113.5")));
        Assert.NotEqual(LoginThrottle.Key("kevin", Ip("203.0.113.5")), LoginThrottle.Key("kevin", Ip("203.0.113.6")));
        Assert.Equal(LoginThrottle.Key("kevin", Ip("2001:db8:1:2::10")), LoginThrottle.Key("kevin", Ip("2001:db8:1:2:aaaa::1")));
        Assert.NotEqual(LoginThrottle.Key("kevin", Ip("2001:db8:1:2::10")), LoginThrottle.Key("kevin", Ip("2001:db8:1:3::10")));
        Assert.Equal("kevin|-", LoginThrottle.Key("kevin", null));
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
            for (var i = 0; i < 5; ++i) Assert.Null(throttle.FailAccount(42));
            time.Advance(TimeSpan.FromMinutes(16));
        }
        for (var i = 0; i < 5; ++i) Assert.Null(throttle.FailAccount(42));
        Assert.Equal(time.GetUtcNow().UtcDateTime.AddMinutes(15), throttle.FailAccount(42)); // the 6th in one window
        Assert.Null(throttle.FailAccount(43)); // another account is not affected

        time.Advance(TimeSpan.FromMinutes(16));
        for (var i = 0; i < 5; ++i) Assert.Null(throttle.FailAccount(42));
        throttle.ResetAccount(42); // a success clears the count
        for (var i = 0; i < 5; ++i) Assert.Null(throttle.FailAccount(42));
        Assert.NotNull(throttle.FailAccount(42));

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
