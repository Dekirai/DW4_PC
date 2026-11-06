using DW4Online.Domain.Accounts;
using DW4Online.Domain.Achievements;

namespace DW4Online.Domain.Services;

/// <summary>
/// Rejects reports from clients older than the minimum protocol / version (HTTP 426). The minimum protocol is
/// max(Play:MinimumProtocol, the world's protocol): a floor in code (SEAMLESS.md 19.17.3), so a forgotten configuration
/// edit never lets a room client (48) past the gate.
/// </summary>
public sealed class VersionGate(PlayOptions options)
{
    /// <summary>The protocol every call needs at least (<see cref="PlayOptions.EffectiveMinimumProtocol"/>).</summary>
    public int MinimumProtocol => options.EffectiveMinimumProtocol;

    public DomainError? Check(int? protocol, string? clientVersion)
    {
        if (protocol is null || protocol < MinimumProtocol)
            return Outdated();
        if (ClientVersion.TryParse(options.MinimumClientVersion, out var minimum) &&
            (!ClientVersion.TryParse(clientVersion, out var actual) || actual < minimum))
            return Outdated();
        return null;
    }

    public DomainError? Check(PlayContext? context) => Check(context?.Protocol, context?.ClientVersion);

    private DomainError Outdated() => new(ErrorKind.Outdated, "client_outdated", "This game version is too old for the server. Please update.",
        new Dictionary<string, object?>
        {
            ["minimumProtocol"] = MinimumProtocol,
            ["minimumClientVersion"] = options.MinimumClientVersion,
        });
}
