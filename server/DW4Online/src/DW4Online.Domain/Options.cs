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
    /// <summary>Revisions of an account's online story kept for a staff restore (online_story_history).</summary>
    public int StoryHistoryRevisions { get; set; } = 100;
    public CharacterRules Rules { get; set; } = new();
}

/// <summary>The private cloud backup of the local save (section "CloudSave").</summary>
public sealed class CloudSaveOptions
{
    public int MaxBytes { get; set; } = 1024 * 1024;
    /// <summary>Older revisions kept next to the latest one.</summary>
    public int HistoryRevisions { get; set; } = 5;
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
    /// </summary>
    public string PathBase { get; set; } = "";
    /// <summary>
    /// Behind a reverse proxy on the same machine (IIS, nginx, Caddy forwarding to 127.0.0.1): take the client address and
    /// the scheme from X-Forwarded-For / X-Forwarded-Proto, but only from a loopback proxy. Off when Kestrel faces the
    /// internet itself (a client could fake its address otherwise).
    /// </summary>
    public bool BehindProxy { get; set; }

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
