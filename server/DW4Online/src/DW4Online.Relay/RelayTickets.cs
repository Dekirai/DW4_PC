using System.Collections.Concurrent;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace DW4Online.Relay;

/// <summary>Relay configuration (section "Relay").</summary>
public sealed class RelayOptions
{
    /// <summary>Off: no relay (rooms still work, nobody can play together).</summary>
    public bool Enabled { get; set; }
    /// <summary>"0.0.0.0" on the server, "127.0.0.1" for local tests.</summary>
    public string ListenAddress { get; set; } = "127.0.0.1";
    public int Port { get; set; } = 52041;
    /// <summary>The HMAC-SHA256 key for tickets, base64, at least 32 bytes. A secret: local / production config only.</summary>
    public string TicketKey { get; set; } = "";
    public int TicketLifetimeSeconds { get; set; } = 60;
    public int HandshakeTimeoutSeconds { get; set; } = 10;
    /// <summary>A connection that sends nothing (not even PONG) for this long is closed.</summary>
    public int IdleTimeoutSeconds { get; set; } = 60;
    public int MaxConnectionsPerIp { get; set; } = 16;
    /// <summary>Rooms with a live relay connection at a time.</summary>
    public int MaxRooms { get; set; } = 200;
    /// <summary>Sustained inbound bytes per second of a connection's game frames (a member sends about 0.6 KB/s).</summary>
    public int BytesPerSecond { get; set; } = 65536;
    /// <summary>Inbound burst allowance of a connection (a snapshot upload, a long LOG after a hitch).</summary>
    public int BurstBytes { get; set; } = 1024 * 1024;
    /// <summary>Sustained inbound frames per second of a connection (INPUT 60/s, PRESENCE 5/s, the rest rare).</summary>
    public int FramesPerSecond { get; set; } = 400;
    /// <summary>Inbound frame burst of a connection.</summary>
    public int FrameBurst { get; set; } = 2000;
    /// <summary>State transfers (STATE_DATA and the LIVE frames behind it) per connection, outside the game frames.</summary>
    public int StateBytesPerSecond { get; set; } = 4 * 1024 * 1024;
    public int StateBurstBytes { get; set; } = 16 * 1024 * 1024;
    /// <summary>A connection whose unsent game frames exceed this is closed (it does not read; a member gets about 5 KB/s).</summary>
    public int MaxQueuedBytes { get; set; } = 4 * 1024 * 1024;
    /// <summary>A connection whose unsent state transfers (STATE_DATA and the LIVE frames behind it) exceed this is closed.</summary>
    public int MaxQueuedStateBytes { get; set; } = 24 * 1024 * 1024;
    /// <summary>State transfers a member may be sent per minute (a burst of this size, refilled over a minute; joins, resyncs).</summary>
    public int StateBytesPerMinute { get; set; } = 64 * 1024 * 1024;
    /// <summary>State transfers built at the same time in the whole relay (each decompresses a whole log).</summary>
    public int MaxConcurrentStateBuilds { get; set; } = 2;
    /// <summary>A sealed member's slot is filled this long after its connection closed, then the server stamps LEAVE.</summary>
    public int ReconnectSeconds { get; set; } = 10;
    /// <summary>After this long of continuous filling the server stamps LEAVE for a lagging member.</summary>
    public int LagLeaveSeconds { get; set; } = 10;
    /// <summary>
    /// M4 (e), round I1a: game frames produced by a received frame, a sealing pass or the housekeeping are written by that
    /// thread right after the hub's lock (non-blocking; a full socket buffer and state transfers go to the asynchronous
    /// writer). Off: every frame goes through the thread pool's writer (the relay before I1a).
    /// </summary>
    public bool DirectSends { get; set; } = true;
    /// <summary>
    /// With DirectSends: a presence frame (PRESENCE / PRESENCE2 to a receiver) waits at most this long to go out with the
    /// receiver's other frames in one send (0: at once). A figure's update is 100 ms apart; this adds 0 to this many ms.
    /// </summary>
    public int PresenceFlushMs { get; set; } = 16;
    /// <summary>Folder for one JSON-lines membership log per room (diagnostics and the online test stack); empty = off.</summary>
    public string DiagnosticsDirectory { get; set; } = "";
}

public static class RelayRoles
{
    /// <summary>A room member's ticket (DW4R2: one connection per member and room).</summary>
    public const string Member = "member";
    /// <summary>Round I1 (SEAMLESS.md 19.14): a moderator's or admin's world ticket (from the database at POST /world/enter).</summary>
    public const string Staff = "staff";
}

/// <summary>What a valid ticket says. <paramref name="Nonce"/> names this ticket (the room service keeps the live one per player).</summary>
public sealed record TicketClaims(string Room, long Account, string Role, DateTimeOffset Expires, string Nonce = "");

/// <summary>A new ticket and its nonce.</summary>
public sealed record IssuedTicket(string Ticket, string Nonce, DateTimeOffset Expires);

/// <summary>
/// Relay tickets: base64url("room|account|role|expiry|nonce") + "." + base64url(HMAC-SHA256). Issued by the room
/// service (room create / join / ticket) after its checks; single use (the nonce is remembered until expiry); the session
/// token never reaches the relay. DW4R2 tickets name the room code; the role is "member".
/// </summary>
public sealed partial class RelayTickets
{
    private readonly byte[] _key;
    private readonly TimeProvider _time;
    private readonly TimeSpan _lifetime;
    private readonly ConcurrentDictionary<string, DateTimeOffset> _used = new(StringComparer.Ordinal);
    private long _lastPurge;

    // The instance number has up to 9 digits (RoomService.MaxInstanceId).
    [GeneratedRegex("^[A-Z0-9]{4,8}(-[0-9]{1,9})?$", RegexOptions.CultureInvariant)]
    private static partial Regex ChannelName();

    public RelayTickets(byte[] key, TimeSpan lifetime, TimeProvider time)
    {
        if (key.Length < 32) throw new ArgumentException("The relay ticket key needs at least 32 bytes.", nameof(key));
        _key = key;
        _lifetime = lifetime;
        _time = time;
    }

    public static bool IsChannel(string channel) => ChannelName().IsMatch(channel);

    public string Issue(string room, long account, string role) => Create(room, account, role).Ticket;

    /// <summary>A new ticket with its nonce (the room service remembers it to accept only the newest ticket per player).</summary>
    public IssuedTicket Create(string room, long account, string role)
    {
        if (!IsChannel(room)) throw new ArgumentException("Channels are a room code (4-8 of A-Z 0-9) with an optional '-<instance>' (1-9 digits).", nameof(room));
        if (role is not (RelayRoles.Member or RelayRoles.Staff)) throw new ArgumentException("Unknown role.", nameof(role));
        var expires = _time.GetUtcNow().Add(_lifetime).ToUnixTimeSeconds();
        var nonce = Convert.ToHexString(RandomNumberGenerator.GetBytes(12));
        var body = Encoding.ASCII.GetBytes(FormattableString.Invariant($"{room}|{account}|{role}|{expires}|{nonce}"));
        return new IssuedTicket(B64(body) + "." + B64(HMACSHA256.HashData(_key, body)), nonce, DateTimeOffset.FromUnixTimeSeconds(expires));
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
        return new TicketClaims(parts[0], account, parts[2], expires, parts[4]);
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
