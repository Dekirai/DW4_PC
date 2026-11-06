namespace DW4Online.Domain.World;

/// <summary>
/// The protocol of the world (round I1, SEAMLESS.md 19.18): the floor in code. The version gate refuses clients below it
/// whatever Play:MinimumProtocol says (max of both, 19.17.3), and the relay's handshake answers ERR protocol below the same
/// number (its Wire.WorldProtocol is this constant), so a forgotten configuration edit can never admit a room client.
/// </summary>
public static class WorldProtocol
{
    /// <summary>Round I1's protocol: 49 (round J's READY check took 48).</summary>
    public const int Version = 49;
}

/// <summary>The visibility of an instance (19.6.2): who may see and join it.</summary>
public static class VisibilityNames
{
    public const string Public = "public";
    public const string Friends = "friends";
    public const string Private = "private";
    public static readonly string[] All = [Public, Friends, Private];

    public static bool IsValid(string? value) => value is Public or Friends or Private;
}

/// <summary>The relay ticket's role (19.14): staff for moderators and admins, member otherwise. The client never claims it.</summary>
public static class WorldRoles
{
    public const string Member = "member";
    public const string Staff = "staff";

    /// <summary>The ticket role of an account role (Roles.Player / Moderator / Admin).</summary>
    public static string Of(string accountRole) =>
        accountRole is Abstractions.Roles.Moderator or Abstractions.Roles.Admin ? Staff : Member;
}

/// <summary>Why an instance ended (instance_history.end_reason, 19.6.5).</summary>
public static class InstanceEndReasons
{
    public const string HostLeft = "host_left";
    public const string HostDefeated = "host_defeated";
    public const string HostLost = "host_lost";
    public const string Staff = "staff";
    public const string Empty = "empty";
    public const string ServerStop = "server_stop";
    public static readonly string[] All = [HostLeft, HostDefeated, HostLost, Staff, Empty, ServerStop];
}

/// <summary>A member's role in an instance (instance_history_members.role).</summary>
public static class InstanceRoles
{
    public const string Host = "host";
    public const string Guest = "guest";
    public const string Staff = "staff";
    public const string Spectator = "spectator";
    public static readonly string[] All = [Host, Guest, Staff, Spectator];
}

/// <summary>Why a member left an instance (instance_history_members.leave_reason).</summary>
public static class InstanceLeaveReasons
{
    public const string Left = "left";
    public const string Home = "home";
    public const string Removed = "removed";
    public const string Ended = "ended";
    public const string Lost = "lost";
    public static readonly string[] All = [Left, Home, Removed, Ended, Lost];
}
