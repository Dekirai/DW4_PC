using DW4Online.Domain.Save;

namespace DW4Online.Domain;

/// <summary>Accounts and sessions (configuration section "Accounts").</summary>
public sealed class AccountOptions
{
    /// <summary>"open" or "closed".</summary>
    public string Registration { get; set; } = "open";
    public int PasswordIterations { get; set; } = Security.PasswordHasher.DefaultIterations;
    /// <summary>Failed logins in a row before the account is locked for <see cref="LockoutMinutes"/>.</summary>
    public int MaxFailedLogins { get; set; } = 10;
    public int LockoutMinutes { get; set; } = 15;
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
}
