using System.Collections.Concurrent;
using DW4Online.Domain.Services;

namespace DW4Online.Domain.Accounts;

/// <summary>
/// Failed password and recovery-code attempts per user name and client address (in memory). After
/// <see cref="AccountOptions.MaxFailedLogins"/> failures within <see cref="AccountOptions.LockoutMinutes"/> that pair is
/// refused for <see cref="AccountOptions.LockoutMinutes"/>: a stranger who guesses passwords locks out only their own
/// address, never the account's owner, and a name that belongs to no account locks the same way (the answer does not
/// tell whether an account exists). The account-wide count (<see cref="FailAccount"/>) covers guesses spread over many
/// addresses: <see cref="AccountOptions.MaxFailedLoginsPerAccount"/> failures within the same window lock the account
/// (AccountService stores that lock), so slow guessing from one address never adds up to it. An IPv6 address counts by
/// its /64 network (privacy extensions change the rest). A service restart forgets the counters (a lock already stored
/// stays); the login rate limit per address still applies.
/// </summary>
public sealed class LoginThrottle(AccountOptions options, TimeProvider time)
{
    private readonly ConcurrentDictionary<string, Entry> _entries = new(StringComparer.Ordinal);
    private long _lastPurgeTicks;

    private TimeSpan Window => TimeSpan.FromMinutes(Math.Max(1, options.LockoutMinutes));

    /// <summary>The key of a user name (as typed: trimmed, lower case) and a client address.</summary>
    public static string Key(string? username, byte[]? ip) => (username ?? "").Trim().ToLowerInvariant() + "|" + AddressKey(ip);

    /// <summary>IPv4 as a whole, IPv6 as its /64 network, no address as "-".</summary>
    public static string AddressKey(byte[]? ip) => ip switch
    {
        null or { Length: 0 } => "-",
        { Length: 16 } => Convert.ToHexString(ip, 0, 8) + "/64",
        _ => Convert.ToHexString(ip),
    };

    /// <summary>When the pair is refused until, or null.</summary>
    public DateTime? LockedUntil(string key)
    {
        if (!_entries.TryGetValue(key, out var entry)) return null;
        var now = Clock.Now(time);
        lock (entry) return entry.LockedUntil is { } until && until > now ? until : null;
    }

    /// <summary>Counts a failure of the pair; returns the end of the lock when this failure starts one.</summary>
    public DateTime? Fail(string key) => Count(key, options.MaxFailedLogins);

    /// <summary>A correct password or code from the pair clears its failures.</summary>
    public void Reset(string key) => _entries.TryRemove(key, out _);

    /// <summary>
    /// Counts a failure on the account from any address; returns the end of the account-wide lock when this failure is
    /// the <see cref="AccountOptions.MaxFailedLoginsPerAccount"/>th within <see cref="AccountOptions.LockoutMinutes"/>.
    /// </summary>
    public DateTime? FailAccount(long accountId) => Count(AccountKey(accountId), options.MaxFailedLoginsPerAccount);

    /// <summary>A correct password or code clears the account's count.</summary>
    public void ResetAccount(long accountId) => _entries.TryRemove(AccountKey(accountId), out _);

    // Pair keys always hold a '|', account keys never.
    private static string AccountKey(long accountId) => "#" + accountId.ToString(System.Globalization.CultureInfo.InvariantCulture);

    private DateTime? Count(string key, int limit)
    {
        var now = Clock.Now(time);
        Purge(now);
        var entry = _entries.GetOrAdd(key, _ => new Entry());
        lock (entry)
        {
            if (entry.LockedUntil is { } until && until > now) return until;
            if (entry.Count == 0 || now - entry.First > Window)
            {
                entry.Count = 0;
                entry.First = now;
            }
            entry.LockedUntil = null;
            if (++entry.Count < Math.Max(1, limit)) return null;
            entry.Count = 0;
            entry.LockedUntil = now + Window;
            return entry.LockedUntil;
        }
    }

    private void Purge(DateTime now)
    {
        var ticks = now.Ticks;
        var last = Interlocked.Read(ref _lastPurgeTicks);
        if (ticks - last < TimeSpan.TicksPerMinute || Interlocked.CompareExchange(ref _lastPurgeTicks, ticks, last) != last) return;
        foreach (var (key, entry) in _entries)
        {
            bool stale;
            lock (entry) stale = (entry.LockedUntil is null || entry.LockedUntil <= now) && now - entry.First > Window;
            if (stale) _entries.TryRemove(key, out _);
        }
    }

    private sealed class Entry
    {
        public int Count;
        public DateTime First;
        public DateTime? LockedUntil;
    }
}
