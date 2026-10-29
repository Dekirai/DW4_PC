using System.Collections.Concurrent;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace DW4Online.Relay;

/// <summary>Relay configuration (section "Relay").</summary>
public sealed class RelayOptions
{
    /// <summary>Off until the room service issues tickets.</summary>
    public bool Enabled { get; set; }
    /// <summary>"0.0.0.0" on the server, "127.0.0.1" for local tests.</summary>
    public string ListenAddress { get; set; } = "127.0.0.1";
    public int Port { get; set; } = 52041;
    /// <summary>The HMAC-SHA256 key for tickets, base64, at least 32 bytes. A secret: local / production config only.</summary>
    public string TicketKey { get; set; } = "";
    public int TicketLifetimeSeconds { get; set; } = 60;
    public int HandshakeTimeoutSeconds { get; set; } = 10;
    /// <summary>A spliced pair with no traffic in either direction for this long is closed.</summary>
    public int IdleTimeoutSeconds { get; set; } = 60;
    public int MaxConnectionsPerIp { get; set; } = 16;
    public int MaxRooms { get; set; } = 200;
    /// <summary>Sustained bytes per second per direction (lockstep needs about 2.1 KB/s per player).</summary>
    public int BytesPerSecond { get; set; } = 65536;
    /// <summary>Burst allowance per direction (the START message carries a save, about 90 KB today).</summary>
    public int BurstBytes { get; set; } = 1024 * 1024;
}

public static class RelayRoles
{
    public const string Host = "host";
    public const string Guest = "guest";
}

/// <summary>What a valid ticket says.</summary>
public sealed record TicketClaims(string Room, long Account, string Role, DateTimeOffset Expires);

/// <summary>
/// Relay tickets: base64url("room|account|role|expiry|nonce") + "." + base64url(HMAC-SHA256). Issued by the room API
/// after its checks; single use (the nonce is remembered until expiry); the session token never reaches the relay.
/// </summary>
public sealed partial class RelayTickets
{
    private readonly byte[] _key;
    private readonly TimeProvider _time;
    private readonly TimeSpan _lifetime;
    private readonly ConcurrentDictionary<string, DateTimeOffset> _used = new(StringComparer.Ordinal);
    private long _lastPurge;

    [GeneratedRegex("^[A-Z0-9]{4,8}$", RegexOptions.CultureInvariant)]
    private static partial Regex RoomCode();

    public RelayTickets(byte[] key, TimeSpan lifetime, TimeProvider time)
    {
        if (key.Length < 32) throw new ArgumentException("The relay ticket key needs at least 32 bytes.", nameof(key));
        _key = key;
        _lifetime = lifetime;
        _time = time;
    }

    public static bool IsRoomCode(string room) => RoomCode().IsMatch(room);

    public string Issue(string room, long account, string role)
    {
        if (!IsRoomCode(room)) throw new ArgumentException("Room codes are 4-8 of A-Z 0-9.", nameof(room));
        if (role is not (RelayRoles.Host or RelayRoles.Guest)) throw new ArgumentException("Unknown role.", nameof(role));
        var expires = _time.GetUtcNow().Add(_lifetime).ToUnixTimeSeconds();
        var nonce = Convert.ToHexString(RandomNumberGenerator.GetBytes(12));
        var body = Encoding.ASCII.GetBytes(FormattableString.Invariant($"{room}|{account}|{role}|{expires}|{nonce}"));
        return B64(body) + "." + B64(HMACSHA256.HashData(_key, body));
    }

    /// <summary>Checks signature, expiry and single use. A ticket is consumed by a successful check.</summary>
    public TicketClaims? Redeem(string ticket)
    {
        var dot = ticket.IndexOf('.');
        if (dot <= 0 || ticket.Length > 512) return null;
        byte[] body, mac;
        try
        {
            body = Unb64(ticket[..dot]);
            mac = Unb64(ticket[(dot + 1)..]);
        }
        catch (FormatException)
        {
            return null;
        }
        if (!CryptographicOperations.FixedTimeEquals(mac, HMACSHA256.HashData(_key, body))) return null;
        var parts = Encoding.ASCII.GetString(body).Split('|');
        if (parts.Length != 5 || !long.TryParse(parts[1], out var account) || !long.TryParse(parts[3], out var expiry)) return null;
        var now = _time.GetUtcNow();
        var expires = DateTimeOffset.FromUnixTimeSeconds(expiry);
        if (expires <= now) return null;
        Purge(now);
        if (!_used.TryAdd(parts[4], expires)) return null; // replayed
        return new TicketClaims(parts[0], account, parts[2], expires);
    }

    private void Purge(DateTimeOffset now)
    {
        var last = Interlocked.Read(ref _lastPurge);
        if (now.ToUnixTimeSeconds() - last < 30 || Interlocked.CompareExchange(ref _lastPurge, now.ToUnixTimeSeconds(), last) != last) return;
        foreach (var (nonce, expires) in _used)
            if (expires <= now) _used.TryRemove(nonce, out _);
    }

    private static string B64(byte[] b) => Convert.ToBase64String(b).TrimEnd('=').Replace('+', '-').Replace('/', '_');

    private static byte[] Unb64(string s)
    {
        s = s.Replace('-', '+').Replace('_', '/');
        return Convert.FromBase64String(s.PadRight(s.Length + (4 - s.Length % 4) % 4, '='));
    }
}
