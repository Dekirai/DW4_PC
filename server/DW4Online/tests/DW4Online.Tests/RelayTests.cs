using System.Security.Cryptography;
using System.Text;
using DW4Online.Relay;

namespace DW4Online.Tests;

public sealed class ManualTime(DateTimeOffset start) : TimeProvider
{
    private DateTimeOffset _now = start;
    public override DateTimeOffset GetUtcNow() => _now;
    public void Advance(TimeSpan by) => _now += by;
}

public class RelayTicketTests
{
    private static readonly byte[] Key = RandomNumberGenerator.GetBytes(32);

    [Fact]
    public void Tickets_redeem_once()
    {
        var tickets = new RelayTickets(Key, TimeSpan.FromSeconds(60), TimeProvider.System);
        var t = tickets.Issue("AB12CD", 42, RelayRoles.Member);
        var claims = tickets.Redeem(t);
        Assert.NotNull(claims);
        Assert.Equal("AB12CD", claims.Room);
        Assert.Equal(42, claims.Account);
        Assert.Equal(RelayRoles.Member, claims.Role);
        Assert.Null(tickets.Redeem(t)); // single use
    }

    [Fact]
    public void Tampered_foreign_and_expired_tickets_fail()
    {
        var time = new ManualTime(DateTimeOffset.UtcNow);
        var tickets = new RelayTickets(Key, TimeSpan.FromSeconds(60), time);
        var t = tickets.Issue("AB12CD", 42, RelayRoles.Member);
        var body = t[..t.IndexOf('.')];
        var forged = Convert.ToBase64String(Encoding.ASCII.GetBytes("AB12CD|43|host|99999999999|00")).TrimEnd('=') + t[t.IndexOf('.')..];
        Assert.Null(tickets.Redeem(forged));
        Assert.Null(tickets.Redeem(body + ".AAAA"));
        Assert.Null(tickets.Redeem("garbage"));
        Assert.Null(new RelayTickets(RandomNumberGenerator.GetBytes(32), TimeSpan.FromSeconds(60), time).Redeem(t));
        time.Advance(TimeSpan.FromSeconds(61));
        Assert.Null(tickets.Redeem(t));
    }

    [Fact]
    public void Bad_input_is_refused()
    {
        Assert.Throws<ArgumentException>(() => new RelayTickets(new byte[8], TimeSpan.FromSeconds(60), TimeProvider.System));
        var tickets = new RelayTickets(Key, TimeSpan.FromSeconds(60), TimeProvider.System);
        Assert.Throws<ArgumentException>(() => tickets.Issue("ab|cd", 1, RelayRoles.Member));
        Assert.Throws<ArgumentException>(() => tickets.Issue("ABCD", 1, "admin"));
    }
}
