namespace DW4Online.Domain.Abstractions;

public static class FriendRequestState
{
    public const string Pending = "pending";
    public const string Declined = "declined";
}

/// <summary>A row of friend_requests as stored (SEAMLESS.md 19.17.1); what each side sees is the service's rule.</summary>
public sealed class FriendRequestRow
{
    public long FromAccountId { get; init; }
    public long ToAccountId { get; init; }
    public string State { get; init; } = FriendRequestState.Pending;
    /// <summary>
    /// False: not shown to the receiver: he blocks the sender (silent until an unblock), or his open requests were full
    /// when it came (it waits for a place, oldest first). The sender sees "sent" either way.
    /// </summary>
    public bool Delivered { get; init; } = true;
    public DateTime CreatedAt { get; init; }
    public DateTime? DecidedAt { get; init; }
    /// <summary>Declined rows only: the sender cancelled it (hidden from him; the decline holds until it expires).</summary>
    public DateTime? CancelledAt { get; init; }
}

/// <summary>Another account in one of the social lists, with its user name and the time of the row.</summary>
public sealed class SocialListRow
{
    public long AccountId { get; init; }
    public string Username { get; init; } = "";
    public DateTime At { get; init; }
}

/// <summary>
/// friendships, friend_requests and account_blocks. A plain store: which rows count (expiry, delivery, the sender's and
/// the receiver's view) is <see cref="Services.SocialService"/>'s rule; <c>since</c> is the expiry cut-off (requests
/// sent before it count as gone). Each method is one transaction. Rows are never written for a deleted account.
/// </summary>
public interface ISocialRepository
{
    Task<bool> AreFriendsAsync(long a, long b, CancellationToken ct = default);
    Task<int> CountFriendsAsync(long accountId, CancellationToken ct = default);
    Task<IReadOnlyList<SocialListRow>> ListFriendsAsync(long accountId, CancellationToken ct = default);
    /// <summary>Makes the pair friends and deletes the requests between them (either direction); false when either account is deleted.</summary>
    Task<bool> AddFriendshipAsync(long a, long b, DateTime now, CancellationToken ct = default);
    Task<bool> RemoveFriendshipAsync(long a, long b, CancellationToken ct = default);

    Task<FriendRequestRow?> GetRequestAsync(long from, long to, CancellationToken ct = default);
    /// <summary>
    /// Writes a new pending request, not yet delivered (replacing an expired or cancelled row of the pair; the service
    /// delivers it with <see cref="DeliverWaitingAsync"/>); false when either account is deleted.
    /// </summary>
    Task<bool> PutPendingRequestAsync(long from, long to, DateTime now, CancellationToken ct = default);
    /// <summary>
    /// Delivers up to <paramref name="max"/> waiting requests to <paramref name="to"/>: pending, undelivered, sent since
    /// <paramref name="since"/>, from senders <paramref name="to"/> does not block; oldest first. Returns their senders.
    /// </summary>
    Task<IReadOnlyList<long>> DeliverWaitingAsync(long to, DateTime since, int max, CancellationToken ct = default);
    /// <summary>
    /// Accounts with waiting requests (as <see cref="DeliverWaitingAsync"/> would deliver them) and fewer than
    /// <paramref name="maxPendingIn"/> delivered ones; at most <paramref name="limit"/>.
    /// </summary>
    Task<IReadOnlyList<long>> ListWaitingReceiversAsync(DateTime since, int maxPendingIn, int limit, CancellationToken ct = default);
    /// <summary>A declined request the sender cancelled is shown to him again as sent at <paramref name="now"/> (it stays declined).</summary>
    Task<bool> ResendDeclinedRequestAsync(long from, long to, DateTime now, CancellationToken ct = default);
    Task<bool> DeclineRequestAsync(long from, long to, DateTime now, CancellationToken ct = default);
    Task<bool> CancelDeclinedRequestAsync(long from, long to, DateTime now, CancellationToken ct = default);
    Task<bool> DeleteRequestAsync(long from, long to, CancellationToken ct = default);
    /// <summary>Requests from <paramref name="from"/> sent since <paramref name="since"/> that its sender sees (not cancelled).</summary>
    Task<int> CountOutgoingAsync(long from, DateTime since, CancellationToken ct = default);
    /// <summary>Pending, delivered requests to <paramref name="to"/> sent since <paramref name="since"/>.</summary>
    Task<int> CountIncomingAsync(long to, DateTime since, CancellationToken ct = default);
    /// <summary>The sender's view: requests from the account sent since <paramref name="since"/>, not cancelled, any state.</summary>
    Task<IReadOnlyList<SocialListRow>> ListOutgoingAsync(long from, DateTime since, CancellationToken ct = default);
    /// <summary>The receiver's view: pending, delivered requests to the account sent since <paramref name="since"/>.</summary>
    Task<IReadOnlyList<SocialListRow>> ListIncomingAsync(long to, DateTime since, CancellationToken ct = default);
    /// <summary>Deletes requests sent before <paramref name="cut"/> (pending and declined alike); returns the number.</summary>
    Task<int> PurgeRequestsAsync(DateTime cut, CancellationToken ct = default);

    Task<bool> IsBlockedAsync(long blocker, long blocked, CancellationToken ct = default);
    Task<int> CountBlocksAsync(long accountId, CancellationToken ct = default);
    Task<IReadOnlyList<SocialListRow>> ListBlocksAsync(long accountId, CancellationToken ct = default);
    /// <summary>
    /// Writes the block and, in the same transaction, removes the friendship and the blocker's own request to the
    /// blocked account, and turns the blocked account's pending request to the blocker silent (delivered = 0: it stays
    /// "sent" to its sender). False when either account is deleted.
    /// </summary>
    Task<bool> AddBlockAsync(long blocker, long blocked, DateTime now, CancellationToken ct = default);
    /// <summary>
    /// Removes the block; false when there was none. The formerly blocked account's pending request becomes a waiting
    /// one (the service delivers it when the blocker has a place).
    /// </summary>
    Task<bool> RemoveBlockAsync(long blocker, long blocked, CancellationToken ct = default);
}
