using DW4Online.Domain.Accounts;
using DW4Online.Domain.Achievements;

namespace DW4Online.Domain.Services;

/// <summary>Rejects reports from clients older than the configured minimum protocol / version (HTTP 426).</summary>
public sealed class VersionGate(PlayOptions options)
{
    public DomainError? Check(int? protocol, string? clientVersion)
    {
        if (options.MinimumProtocol > 0 && (protocol is null || protocol < options.MinimumProtocol))
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
            ["minimumProtocol"] = options.MinimumProtocol,
            ["minimumClientVersion"] = options.MinimumClientVersion,
        });
}
