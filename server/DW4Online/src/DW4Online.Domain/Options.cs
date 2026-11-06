using DW4Online.Domain.Save;

namespace DW4Online.Domain;

/// <summary>Accounts and sessions (configuration section "Accounts").</summary>
public sealed class AccountOptions
{
    /// <summary>"open" or "closed".</summary>
    public string Registration { get; set; } = "open";
    public int PasswordIterations { get; set; } = Security.PasswordHasher.DefaultIterations;
    /// <summary>
    /// Failed attempts (password, recovery code, password confirmations) from one client address on one user name within
    /// <see cref="LockoutMinutes"/> before that address is refused for <see cref="LockoutMinutes"/> (Accounts.LoginThrottle).
    /// </summary>
    public int MaxFailedLogins { get; set; } = 10;
    /// <summary>The window of <see cref="MaxFailedLogins"/> and the length of both locks.</summary>
    public int LockoutMinutes { get; set; } = 15;
    /// <summary>
    /// Failed attempts on one account from all addresses together within <see cref="LockoutMinutes"/> (a success clears
    /// them) before the account is locked for <see cref="LockoutMinutes"/> against guessing from many addresses; slow
    /// guessing from one network (at most <see cref="MaxFailedLogins"/> per window: IPv4 the address, IPv6 its /48)
    /// never reaches it. During that lock only addresses the account created or last used a session from within
    /// <see cref="KnownAddressDays"/> may still try, so strangers cannot lock the owner out.
    /// </summary>
    public int MaxFailedLoginsPerAccount { get; set; } = 50;
    /// <summary>How long a client address that created a session or last used one counts as known for the account-wide lock.</summary>
    public int KnownAddressDays { get; set; } = 30;
    /// <summary>Sliding lifetime of game-client sessions.</summary>
    public int GameSessionDays { get; set; } = 30;
    /// <summary>Sliding lifetime of website sessions.</summary>
    public int WebSessionDays { get; set; } = 14;
    /// <summary>A session's expiry moves forward at most this often (saves a write per request).</summary>
    public int SessionTouchMinutes { get; set; } = 5;
    /// <summary>Active sessions per account; the oldest are revoked beyond this.</summary>
    public int MaxSessionsPerAccount { get; set; } = 20;
}

/// <summary>The online character (section "OnlineCharacter").</summary>
public sealed class OnlineCharacterOptions
{
    /// <summary>Older revisions kept for restore and rollback.</summary>
    public int HistoryRevisions { get; set; } = 20;
    /// <summary>
    /// Days the revisions of a reset character stay in the history (undoing a mistaken reset, reviewing a flagged
    /// character) before the housekeeping deletes them; account deletion removes them at once.
    /// </summary>
    public int ResetHistoryDays { get; set; } = 30;
    /// <summary>The newest revisions of an account's online story kept in full for a staff restore (online_story_history).</summary>
    public int StoryHistoryRevisions { get; set; } = 100;
    /// <summary>Older revisions of a room's story are thinned to the first of every this many minutes.</summary>
    public int StoryHistoryMinutes { get; set; } = 10;
    /// <summary>Thinned revisions and session starts older than this many days go (the newest StoryHistorySessions session
    /// starts, resets and restores stay whatever their age).</summary>
    public int StoryHistoryDays { get; set; } = 90;
    /// <summary>The story every room session started from (and every reset and restore) is kept; the newest this many of them
    /// stay beyond StoryHistoryDays.</summary>
    public int StoryHistorySessions { get; set; } = 20;
    /// <summary>A room story revision that changed only counters is written at most this often per account (seconds).</summary>
    public int StoryCounterSaveSeconds { get; set; } = 60;
    public CharacterRules Rules { get; set; } = new();
}

/// <summary>The private cloud backup of the local save (section "CloudSave").</summary>
public sealed class CloudSaveOptions
{
    public int MaxBytes { get; set; } = 1024 * 1024;
    /// <summary>Older revisions kept next to the latest one.</summary>
    public int HistoryRevisions { get; set; } = 5;
}

/// <summary>
/// Friends and blocks (section "Friends", SEAMLESS.md 19.11): the limits per account. Requests name the exact user name;
/// a declined request stays "sent" to its sender until it expires, and a block is never revealed.
/// </summary>
public sealed class FriendsOptions
{
    /// <summary>Friend requests an account may send per hour (counted in memory; a restart starts afresh).</summary>
    public int RequestsPerHour { get; set; } = 20;
    /// <summary>Friend requests an account may send per day (counted in memory).</summary>
    public int RequestsPerDay { get; set; } = 100;
    /// <summary>Open outgoing requests (pending and declined ones look the same to the sender: both "sent").</summary>
    public int MaxPendingOut { get; set; } = 50;
    /// <summary>
    /// Open requests that reach an account at a time; further ones answer "Request sent." like any other and wait
    /// undelivered until a place frees (oldest first), so the cap never tells a sender anything. Requests from blocked
    /// senders neither count nor reach the account.
    /// </summary>
    public int MaxPendingIn { get; set; } = 100;
    public int MaxFriends { get; set; } = 200;
    public int MaxBlocks { get; set; } = 500;
    /// <summary>Pending and declined requests expire this many days after they were sent; the housekeeping purges them.</summary>
    public int RequestDays { get; set; } = 30;
}

/// <summary>What counts as online play (section "Play").</summary>
public sealed class PlayOptions
{
    /// <summary>Achievement unlocks and stats need a room the account is a member of (rooms written by the room service).</summary>
    public bool RequireRoom { get; set; } = true;
    /// <summary>Oldest netplay protocol (kProtocol, src/game/netplay.cpp) the server accepts; 0 accepts any.</summary>
    public int MinimumProtocol { get; set; }
    /// <summary>The protocol of the current release, announced by the status endpoint.</summary>
    public int CurrentProtocol { get; set; }
    /// <summary>Oldest client version accepted ("0.48.0"); empty accepts any.</summary>
    public string MinimumClientVersion { get; set; } = "";
    /// <summary>The current release's version, announced by the status endpoint.</summary>
    public string CurrentClientVersion { get; set; } = "";
    /// <summary>
    /// The least in-game time (seconds) a story clear can take, per speedrun board (story_normal, story_hard,
    /// story_veryhard); default 45 minutes each (Achievements.AchievementRules.DefaultSpeedrunFloorSeconds). A clear
    /// reported faster keeps no time, its new unlock is flagged for review, and speed goals refuse it. 0 = no floor.
    /// </summary>
    public Dictionary<string, long> SpeedrunFloorSeconds { get; set; } = Achievements.AchievementRules.DefaultSpeedrunFloors();

    /// <summary>The floor of one board in milliseconds (0 when it has none).</summary>
    public long SpeedrunFloorMs(string board) => Math.Max(0, SpeedrunFloorSeconds.GetValueOrDefault(board)) * 1000;

    /// <summary>The lowest floor of any board in milliseconds: a story clear on any difficulty takes at least this long.</summary>
    public long StoryFloorMs => SpeedrunFloorSeconds.Count == 0 ? 0 : Math.Max(0, SpeedrunFloorSeconds.Values.Min()) * 1000;
    /// <summary>A story clear reported in a room counts when the room's story holds that difficulty's clear flag; the report
    /// may come before the STORY that brings it, so the server waits up to this long for it (seconds).</summary>
    public double ClearStoryWaitSeconds { get; set; } = 30;
}

/// <summary>Service-wide settings announced to clients and shown on the website (section "Service").</summary>
public sealed class ServiceOptions
{
    public string Name { get; set; } = "DW4Online";
    /// <summary>Message of the day shown in the game's online menu and on the website's home page.</summary>
    public string Motd { get; set; } = "";
    /// <summary>Maintenance mode: announced in the status; the room service stops opening rooms.</summary>
    public bool Maintenance { get; set; }
    /// <summary>Public host name or IP clients use for the relay (announced in the status); empty = the host name of the request.</summary>
    public string RelayHost { get; set; } = "";
    /// <summary>
    /// The relay port announced to clients; 0 (the default) = the port the relay actually listens on (Relay:Port). Set it
    /// only when clients reach the relay through a different public port (a port forward).
    /// </summary>
    public int RelayPort { get; set; }
    /// <summary>
    /// A path the whole service (website and API) lives below, for a host shared with other sites, e.g. "/dw4online" for
    /// https://dekirai.crygod.de/dw4online. Requests under it see their links, redirects and cookies below it; requests
    /// without it keep working at the root (so clients of an earlier root address go on working). Empty = the root.
    /// Behind a proxy that strips the path (Traefik's stripPrefix) it is needed as well: its X-Forwarded-Prefix counts only
    /// when it names this path.
    /// </summary>
    public string PathBase { get; set; } = "";
    /// <summary>
    /// Behind a reverse proxy on the same machine (Traefik, IIS, nginx, Caddy forwarding to http://127.0.0.1:5280): take
    /// the client address (the last X-Forwarded-For entry) and the scheme (X-Forwarded-Proto), and a stripping proxy's
    /// X-Forwarded-Prefix when it is <see cref="PathBase"/>, but only on plain-HTTP connections from this machine (never
    /// from a direct client, never on the HTTPS endpoint). Off when Kestrel faces the internet itself (a client could fake
    /// its address otherwise). Kestrel's HTTPS endpoint keeps serving direct clients (the game) either way. DEPLOY.md
    /// section 6.2; DW4Online.Server.ProxyHeaders.
    /// </summary>
    public bool BehindProxy { get; set; }
    /// <summary>
    /// With <see cref="BehindProxy"/>: Cloudflare's networks (CIDR, its published ranges, https://www.cloudflare.com/ips/).
    /// When the local proxy saw an address of these networks (the last X-Forwarded-For entry), the client's address is
    /// the one in Cloudflare's CF-Connecting-IP header (one public address; else the edge's stays). Only behind the local
    /// proxy: an address of these networks connecting to the service directly is not trusted. The local proxy keeps its
    /// default (Traefik needs no forwardedHeaders.trustedIPs). Empty (the default): the address the local proxy saw counts.
    /// </summary>
    public string[] TrustedProxyNetworks { get; set; } = [];

    /// <summary><see cref="PathBase"/> as "/name" (leading slash, no trailing slash), or "" for the root.</summary>
    public string NormalizedPathBase()
    {
        var p = PathBase.Trim().Trim('/');
        if (p.Length == 0) return "";
        if (p.Any(c => char.IsWhiteSpace(c) || c is '?' or '#' or '\\' or '%'))
            throw new InvalidOperationException($"Service:PathBase '{PathBase}' is not a plain path such as /dw4online.");
        return "/" + p;
    }
}
