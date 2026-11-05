namespace DW4Online.Domain.Abstractions;

/// <summary>
/// How the domain services tell the relay about changes that its in-memory world state caches (SEAMLESS.md 19.17.2):
/// API and relay run in one process, so this is a plain in-process call: no database read under the hub's lock and no
/// polling. Implementations must return at once (queue the work); they are called after the change is committed.
/// Round I2-social ships only the interface with <see cref="NullWorldNotifier"/>; round I1's relay implements it and
/// registers it before ServerApp's <c>TryAddSingleton&lt;IWorldNotifier&gt;(NullWorldNotifier.Instance)</c>, or replaces
/// that registration after it (a TryAdd after it would be silently ignored).
/// </summary>
public interface IWorldNotifier
{
    /// <summary>
    /// The friendship of <paramref name="a"/> and <paramref name="b"/>, or a request between them that the receiver can
    /// see, changed (delivered, accepted, declined, cancelled, removed): reload the pair. A decline, a silent request
    /// (the receiver blocks the sender) and the moment a waiting request is delivered are never shown to the sender: the
    /// relay must not tell him.
    /// </summary>
    void FriendsChanged(long a, long b);

    /// <summary><paramref name="blocker"/> blocked or unblocked <paramref name="blocked"/>: reload the pair. Never told to the blocked player.</summary>
    void BlocksChanged(long blocker, long blocked);

    /// <summary>The account's online preferences changed (round I1's online_prefs).</summary>
    void PrefsChanged(long account);

    /// <summary>The account's role or status changed, or it was deleted (round I1: a demotion, lock, ban or deletion counts at once).</summary>
    void AccountChanged(long account);

    /// <summary>The account's online story of <paramref name="box"/> was replaced (a reset or a staff restore, round I1).</summary>
    void StoryReplaced(long account, int box);
}

/// <summary>The notifier while no relay listens (round I2-social, or the relay switched off): does nothing.</summary>
public sealed class NullWorldNotifier : IWorldNotifier
{
    public static readonly NullWorldNotifier Instance = new();

    public void FriendsChanged(long a, long b)
    {
    }

    public void BlocksChanged(long blocker, long blocked)
    {
    }

    public void PrefsChanged(long account)
    {
    }

    public void AccountChanged(long account)
    {
    }

    public void StoryReplaced(long account, int box)
    {
    }
}
