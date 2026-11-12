using DW4Online.Domain.Abstractions;

namespace DW4Online.Domain.World;

/// <summary>
/// Who looks at the world's lists: the account, whether it is staff, its friends and the accounts it blocks or that
/// block it (both directions together: a block hides either way, 19.7.2).
/// </summary>
public sealed record WorldViewer(long AccountId, bool Staff, IReadOnlySet<long> Friends, IReadOnlySet<long> Blocks)
{
    /// <summary>A signed-out visitor: sees counts only, never a row.</summary>
    public static readonly WorldViewer Anonymous = new(0, false, new HashSet<long>(), new HashSet<long>());
}

/// <summary>
/// Visible() of SEAMLESS.md 19.7.2 for the lists the website and the API show (GET /instances, /instances): no block in
/// either direction between the viewer and any member of the instance; and the instance is Public, or Friends and the
/// viewer is a friend of its host (the host's party with round I4), or the viewer is staff. Private instances are never
/// listed to players (their party from round I4). Staff see every instance in the lists (19.14: "Seeing"); a member
/// always sees the instance he is in. A player under the instance's removal bar does not see it (round I1b, 19.6.6). The
/// relay recomputes the same rule at its decisions from its own social cache.
/// </summary>
public static class WorldVisibility
{
    public static bool Visible(WorldInstanceRow instance, WorldViewer viewer)
    {
        if (viewer.AccountId == 0) return false;
        if (viewer.Staff) return true;
        if (instance.HostAccountId == viewer.AccountId || instance.Members.Any(m => m.AccountId == viewer.AccountId)) return true;
        if (instance.Barred.Contains(viewer.AccountId)) return false;
        if (viewer.Blocks.Contains(instance.HostAccountId) || instance.Members.Any(m => viewer.Blocks.Contains(m.AccountId))) return false;
        return instance.Visibility switch
        {
            VisibilityNames.Public => true,
            VisibilityNames.Friends => viewer.Friends.Contains(instance.HostAccountId),
            _ => false,
        };
    }
}
