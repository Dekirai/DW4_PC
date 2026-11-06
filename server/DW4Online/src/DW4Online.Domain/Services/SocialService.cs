using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Accounts;

namespace DW4Online.Domain.Services;

/// <summary>A friend in the list (presence comes from the relay from round I1 on).</summary>
public sealed record FriendEntry(string Username, DateTime Since);

/// <summary>A friend request as one side sees it: incoming requests are pending; outgoing ones are all "sent".</summary>
public sealed record RequestEntry(string Username, DateTime SentAt, DateTime ExpiresAt);

public sealed record BlockEntry(string Username, DateTime Since);

public sealed record FriendLimits(int MaxFriends, int MaxPendingOut, int MaxPendingIn, int MaxBlocks, int RequestDays);

/// <summary>Everything the friends page and GET /friends show of one account.</summary>
public sealed record FriendsOverview(
    IReadOnlyList<FriendEntry> Friends,
    IReadOnlyList<RequestEntry> RequestsIn,
    IReadOnlyList<RequestEntry> RequestsOut,
    IReadOnlyList<BlockEntry> Blocks,
    FriendLimits Limits);

/// <summary>The answer to a request or an accept: <see cref="Result"/> is "sent" or "friends".</summary>
public sealed record RequestOutcome(string Result, string Message)
{
    public const string Sent = "sent";
    public const string Friends = "friends";
}

/// <summary>How a player stands to another, as the viewer may know it (a block by the other side is never shown).</summary>
public static class Relations
{
    public const string Self = "self";
    public const string Friends = "friends";
    public const string RequestSent = "request_sent";
    public const string RequestReceived = "request_received";
    /// <summary>The viewer blocks the other player.</summary>
    public const string Blocked = "blocked";
    public const string None = "none";
}

/// <summary>
/// Friends and blocks (SEAMLESS.md 19.11). A request from A to B that B accepts makes them friends; a request from B
/// while A's to B is open (pending, or declined by B and not cancelled by A) makes them friends at once. A decline is
/// silent: the row stays "sent" to the sender until it expires (<see cref="FriendsOptions.RequestDays"/>), and a new
/// request from him meanwhile answers "Request sent." and changes nothing; cancelling a declined request hides it from
/// him but keeps the decline. A block removes the friendship and the blocker's request; the blocked player's request to
/// the blocker stays "sent" to him but is no longer shown to the blocker (delivered = 0), and his further requests answer
/// "Request sent." the same way: a block is never revealed. At most <see cref="FriendsOptions.MaxPendingIn"/> requests
/// reach an account at a time; further ones answer "Request sent." too and wait undelivered until a place frees (oldest
/// first), so the cap tells a sender nothing about a block or a decline. Requests name the exact user name (no search).
/// Every write holds the locks of both accounts (striped by id), so crossing requests and the caps cannot race while
/// writes of other accounts run on.
/// </summary>
public sealed class SocialService(
    IAccountRepository accounts,
    ISocialRepository social,
    IWorldNotifier notifier,
    IAuditLog audit,
    FriendsOptions options,
    TimeProvider time)
{
    private const int LockStripes = 64;
    private readonly SemaphoreSlim[] _locks = Enumerable.Range(0, LockStripes).Select(_ => new SemaphoreSlim(1, 1)).ToArray();
    private readonly Dictionary<long, Queue<DateTime>> _sent = [];
    private readonly Dictionary<(long Account, string Limit), DateTime> _limitLogged = [];
    private readonly Lock _sentGate = new();

    public const string RequestSentMessage = "Request sent.";

    /// <summary>How long a write waits for its accounts' locks before it answers <c>social_busy</c> (tests shorten it).</summary>
    internal TimeSpan LockWait { get; set; } = TimeSpan.FromSeconds(10);

    public FriendLimits Limits => new(options.MaxFriends, options.MaxPendingOut, MaxPendingIn, options.MaxBlocks, options.RequestDays);

    public async Task<FriendsOverview> OverviewAsync(long accountId, CancellationToken ct = default)
    {
        var since = Since(Clock.Now(time));
        var friends = await social.ListFriendsAsync(accountId, ct);
        var incoming = await social.ListIncomingAsync(accountId, since, ct);
        var outgoing = await social.ListOutgoingAsync(accountId, since, ct);
        return new FriendsOverview(
            friends.OrderBy(f => f.Username, StringComparer.OrdinalIgnoreCase).Select(f => new FriendEntry(f.Username, f.At)).ToList(),
            incoming.OrderByDescending(r => r.At).Select(Entry).ToList(),
            outgoing.OrderByDescending(r => r.At).Select(Entry).ToList(),
            await BlocksAsync(accountId, ct),
            Limits);
    }

    public async Task<IReadOnlyList<BlockEntry>> BlocksAsync(long accountId, CancellationToken ct = default) =>
        (await social.ListBlocksAsync(accountId, ct)).OrderBy(b => b.Username, StringComparer.OrdinalIgnoreCase)
            .Select(b => new BlockEntry(b.Username, b.At)).ToList();

    /// <summary>
    /// Sends a friend request to the account named exactly <paramref name="username"/> (any case). The answer is
    /// "Request sent." whether the other player blocks the sender or not, whether he declined an earlier request or not,
    /// and whether his open requests are full or not (then it waits): only the sender's own limits refuse a request.
    /// </summary>
    public async Task<Result<RequestOutcome>> SendRequestAsync(long accountId, string? username, CallerInfo caller, CancellationToken ct = default)
    {
        var target = await FindAsync(username, ct);
        if (target is null) return NoSuchPlayer();
        if (target.Id == accountId) return DomainError.Invalid("own_account", "That is your own name.");
        using var held = await LockAsync(accountId, target.Id, ct);
        if (held is null) return Busy();
        var now = Clock.Now(time);
        var since = Since(now);
        if (await social.IsBlockedAsync(accountId, target.Id, ct))
            return DomainError.Conflict("you_blocked", $"You blocked {target.Username}. Unblock them first.");
        if (await social.AreFriendsAsync(accountId, target.Id, ct))
            return DomainError.Conflict("already_friends", $"You are already friends with {target.Username}.");

        // Crossing requests: theirs is open as they see it, so both want it.
        var theirs = await social.GetRequestAsync(target.Id, accountId, ct);
        if (theirs is not null && theirs.CreatedAt >= since && SeenAsSentBy(theirs) && !await social.IsBlockedAsync(target.Id, accountId, ct))
            return await BefriendAsync(accountId, target, caller, now, ct);

        var mine = await social.GetRequestAsync(accountId, target.Id, ct);
        var mineLive = mine is not null && mine.CreatedAt >= since;
        if (mineLive && SeenAsSentBy(mine!)) return Sent(); // already sent (pending, silent or declined): nothing changes

        // A new request as the sender sees it: only his own limits can refuse it. The receiver's cap never answers
        // (a "full" answer would let a sender probe a block or a decline by whether his own row takes a place).
        if (RetryAfter(accountId, now) is { } retry)
        {
            await LimitAsync(accountId, "requests_per_time", caller, now, ct);
            return DomainError.TooMany("friend_request_rate", "You sent too many friend requests. Try again later.", retry);
        }
        if (await social.CountFriendsAsync(accountId, ct) >= options.MaxFriends)
        {
            await LimitAsync(accountId, "friends", caller, now, ct);
            return OwnFriendLimit();
        }
        if (await social.CountOutgoingAsync(accountId, since, ct) >= options.MaxPendingOut)
        {
            await LimitAsync(accountId, "outgoing", caller, now, ct);
            return DomainError.Conflict("outgoing_limit",
                $"You have {options.MaxPendingOut} open friend requests, the most you can have. Cancel some or wait for answers.");
        }
        CountSent(accountId, now);
        if (mineLive && mine!.State == FriendRequestState.Declined)
        {
            // He cancelled a request the other player had declined: it is "sent" again, the decline still holds.
            await social.ResendDeclinedRequestAsync(accountId, target.Id, now, ct);
        }
        else if (await social.PutPendingRequestAsync(accountId, target.Id, now, ct))
        {
            // It reaches the receiver now unless he blocks the sender (silent until an unblock) or his open
            // requests are full (it waits; older waiting requests go first).
            await DeliverAsync(target.Id, since, ct);
        }
        return Sent();
    }

    /// <summary>Accepts the open request from <paramref name="username"/>: the two become friends.</summary>
    public async Task<Result<RequestOutcome>> AcceptAsync(long accountId, string? username, CallerInfo caller, CancellationToken ct = default)
    {
        var target = await FindAsync(username, ct);
        if (target is null) return NoSuchPlayer();
        using var held = await LockAsync(accountId, target.Id, ct);
        if (held is null) return Busy();
        var now = Clock.Now(time);
        if (!VisibleToReceiver(await social.GetRequestAsync(target.Id, accountId, ct), Since(now)))
            return NoRequestFrom(target);
        return await BefriendAsync(accountId, target, caller, now, ct);
    }

    /// <summary>Declines the open request from <paramref name="username"/>; its sender is not told.</summary>
    public async Task<Result<string>> DeclineAsync(long accountId, string? username, CancellationToken ct = default)
    {
        var target = await FindAsync(username, ct);
        if (target is null) return NoSuchPlayer();
        using var held = await LockAsync(accountId, target.Id, ct);
        if (held is null) return Busy();
        var now = Clock.Now(time);
        if (!VisibleToReceiver(await social.GetRequestAsync(target.Id, accountId, ct), Since(now)))
            return NoRequestFrom(target);
        await social.DeclineRequestAsync(target.Id, accountId, now, ct);
        notifier.FriendsChanged(accountId, target.Id, otherSees: false); // the sender is never told
        await DeliverAsync(accountId, Since(now), ct); // its place goes to the oldest waiting request
        return target.Username;
    }

    /// <summary>Cancels the caller's own request to <paramref name="username"/> (a declined one keeps its decline).</summary>
    public async Task<Result<string>> CancelAsync(long accountId, string? username, CancellationToken ct = default)
    {
        var target = await FindAsync(username, ct);
        if (target is null) return NoSuchPlayer();
        using var held = await LockAsync(accountId, target.Id, ct);
        if (held is null) return Busy();
        var now = Clock.Now(time);
        var mine = await social.GetRequestAsync(accountId, target.Id, ct);
        if (mine is null || mine.CreatedAt < Since(now) || !SeenAsSentBy(mine))
            return DomainError.NotFound("request_not_found", $"No open friend request to {target.Username}.");
        if (mine.State == FriendRequestState.Declined)
        {
            await social.CancelDeclinedRequestAsync(accountId, target.Id, now, ct);
        }
        else
        {
            await social.DeleteRequestAsync(accountId, target.Id, ct);
            if (mine.Delivered)
            {
                notifier.FriendsChanged(accountId, target.Id, otherSees: true); // gone from the receiver's list
                await DeliverAsync(target.Id, Since(now), ct); // its place goes to the oldest waiting request
            }
        }
        return target.Username;
    }

    /// <summary>Ends a friendship; the other player is not told and sees it in his list.</summary>
    public async Task<Result<string>> RemoveFriendAsync(long accountId, string? username, CancellationToken ct = default)
    {
        var target = await FindAsync(username, ct);
        if (target is null) return NoSuchPlayer();
        using var held = await LockAsync(accountId, target.Id, ct);
        if (held is null) return Busy();
        if (!await social.RemoveFriendshipAsync(accountId, target.Id, ct))
            return DomainError.NotFound("not_friends", $"You are not friends with {target.Username}.");
        notifier.FriendsChanged(accountId, target.Id, otherSees: true); // not told, but gone from his list
        return target.Username;
    }

    /// <summary>Blocks <paramref name="username"/> (repeating it changes nothing); the blocked player is never told.</summary>
    public async Task<Result<string>> BlockAsync(long accountId, string? username, CallerInfo caller, CancellationToken ct = default)
    {
        var target = await FindAsync(username, ct);
        if (target is null) return NoSuchPlayer();
        if (target.Id == accountId) return DomainError.Invalid("own_account", "You can't block yourself.");
        using var held = await LockAsync(accountId, target.Id, ct);
        if (held is null) return Busy();
        var now = Clock.Now(time);
        if (await social.IsBlockedAsync(accountId, target.Id, ct)) return target.Username;
        if (await social.CountBlocksAsync(accountId, ct) >= options.MaxBlocks)
        {
            await LimitAsync(accountId, "blocks", caller, now, ct);
            return DomainError.Conflict("block_limit", $"You block {options.MaxBlocks} players, the most you can. Unblock one first.");
        }
        if (!await social.AddBlockAsync(accountId, target.Id, now, ct)) return NoSuchPlayer();
        notifier.BlocksChanged(accountId, target.Id);
        // The blocked player's request no longer takes one of the blocker's places, nor the blocker's own one of his.
        await DeliverAsync(accountId, Since(now), ct);
        await DeliverAsync(target.Id, Since(now), ct);
        return target.Username;
    }

    /// <summary>
    /// Lifts a block; the formerly blocked player's pending request reaches the caller now when he has a place (else it
    /// waits in line with the others, by its date).
    /// </summary>
    public async Task<Result<string>> UnblockAsync(long accountId, string? username, CancellationToken ct = default)
    {
        var target = await FindAsync(username, ct);
        if (target is null) return NoSuchPlayer();
        using var held = await LockAsync(accountId, target.Id, ct);
        if (held is null) return Busy();
        if (!await social.RemoveBlockAsync(accountId, target.Id, ct))
            return DomainError.NotFound("not_blocked", $"You do not block {target.Username}.");
        notifier.BlocksChanged(accountId, target.Id);
        await DeliverAsync(accountId, Since(Clock.Now(time)), ct);
        return target.Username;
    }

    /// <summary>The relation of <paramref name="targetId"/> to <paramref name="viewerId"/> as the viewer may know it (<see cref="Relations"/>).</summary>
    public async Task<string> RelationAsync(long viewerId, long targetId, CancellationToken ct = default)
    {
        if (viewerId == targetId) return Relations.Self;
        if (await social.IsBlockedAsync(viewerId, targetId, ct)) return Relations.Blocked;
        if (await social.AreFriendsAsync(viewerId, targetId, ct)) return Relations.Friends;
        var since = Since(Clock.Now(time));
        if (VisibleToReceiver(await social.GetRequestAsync(targetId, viewerId, ct), since)) return Relations.RequestReceived;
        var mine = await social.GetRequestAsync(viewerId, targetId, ct);
        if (mine is not null && mine.CreatedAt >= since && SeenAsSentBy(mine)) return Relations.RequestSent;
        return Relations.None;
    }

    /// <summary>
    /// The hourly housekeeping: deletes requests that expired (pending and declined; returns their number), then
    /// delivers waiting requests to accounts that have places again through an expiry or a deleted account (every
    /// other change delivers at once).
    /// </summary>
    public async Task<int> PurgeExpiredAsync(CancellationToken ct = default)
    {
        var purged = await social.PurgeRequestsAsync(Since(Clock.Now(time)), ct);
        foreach (var to in await social.ListWaitingReceiversAsync(Since(Clock.Now(time)), MaxPendingIn, 1000, ct))
        {
            using var held = await LockAsync(to, to, ct);
            if (held is not null) await DeliverAsync(to, Since(Clock.Now(time)), ct);
        }
        return purged;
    }

    // ------------------------------------------------------------------------------------------

    private async Task<Result<RequestOutcome>> BefriendAsync(long accountId, AccountRecord target, CallerInfo caller, DateTime now, CancellationToken ct)
    {
        if (await social.CountFriendsAsync(accountId, ct) >= options.MaxFriends)
        {
            await LimitAsync(accountId, "friends", caller, now, ct);
            return OwnFriendLimit();
        }
        if (await social.CountFriendsAsync(target.Id, ct) >= options.MaxFriends)
            return DomainError.Conflict("other_friend_limit", $"{target.Username} can't take more friends right now.");
        if (!await social.AddFriendshipAsync(accountId, target.Id, now, ct)) return NoSuchPlayer();
        notifier.FriendsChanged(accountId, target.Id, otherSees: true);
        // The requests between the two are gone: their places go to the oldest waiting requests.
        await DeliverAsync(accountId, Since(now), ct);
        await DeliverAsync(target.Id, Since(now), ct);
        return new RequestOutcome(RequestOutcome.Friends, $"You are now friends with {target.Username}.");
    }

    /// <summary>
    /// Delivers requests that wait for <paramref name="to"/> (sent while his open requests were full, or by a player he
    /// blocked and no longer blocks), oldest first, while fewer than <see cref="FriendsOptions.MaxPendingIn"/> reach
    /// him; requests from players he blocks stay silent. The caller holds <paramref name="to"/>'s lock.
    /// </summary>
    private async Task DeliverAsync(long to, DateTime since, CancellationToken ct)
    {
        var places = MaxPendingIn - await social.CountIncomingAsync(to, since, ct);
        if (places <= 0) return;
        foreach (var from in await social.DeliverWaitingAsync(to, since, places, ct))
            notifier.FriendsChanged(to, from, otherSees: false); // the sender never learns when it arrived
    }

    private int MaxPendingIn => Math.Max(1, options.MaxPendingIn);

    /// <summary>
    /// The write locks of both accounts: striped by account id and taken in stripe order, so writes of other accounts
    /// run on and two writes never wait on each other in a circle. Null when they are not free within
    /// <see cref="LockWait"/> (the caller answers <c>social_busy</c>).
    /// </summary>
    private async Task<IDisposable?> LockAsync(long a, long b, CancellationToken ct)
    {
        var first = Stripe(a);
        var second = Stripe(b);
        if (first > second) (first, second) = (second, first);
        if (!await _locks[first].WaitAsync(LockWait, ct)) return null;
        if (second == first) return new Held(_locks[first], null);
        bool got;
        try
        {
            got = await _locks[second].WaitAsync(LockWait, ct);
        }
        catch
        {
            _locks[first].Release();
            throw;
        }
        if (got) return new Held(_locks[first], _locks[second]);
        _locks[first].Release();
        return null;
    }

    private static int Stripe(long accountId) => (int)((ulong)accountId % LockStripes);

    private sealed class Held(SemaphoreSlim first, SemaphoreSlim? second) : IDisposable
    {
        private int _released;

        public void Dispose()
        {
            if (Interlocked.Exchange(ref _released, 1) != 0) return;
            second?.Release();
            first.Release();
        }
    }

    private static DomainError Busy() =>
        new(ErrorKind.Unavailable, "social_busy", "Friends are busy right now. Try again in a moment.");

    /// <summary>What the sender sees as "sent": pending (delivered or silent), or declined and not cancelled by him.</summary>
    private static bool SeenAsSentBy(FriendRequestRow row) =>
        row.State == FriendRequestState.Pending || row.CancelledAt is null;

    /// <summary>What the receiver sees: a pending, delivered request that has not expired.</summary>
    private static bool VisibleToReceiver(FriendRequestRow? row, DateTime since) =>
        row is { State: FriendRequestState.Pending, Delivered: true } && row.CreatedAt >= since;

    private DateTime Since(DateTime now) => now.AddDays(-Math.Max(1, options.RequestDays));

    private RequestEntry Entry(SocialListRow r) => new(r.Username, r.At, r.At.AddDays(Math.Max(1, options.RequestDays)));

    /// <summary>The account a typed name names exactly (trimmed, any case); null for a malformed name or a deleted account.</summary>
    private async Task<AccountRecord?> FindAsync(string? username, CancellationToken ct)
    {
        var name = UsernamePolicy.Canonical(username);
        if (name is null) return null;
        var account = await accounts.FindByUsernameAsync(name, ct);
        return account is null || account.Status == AccountStatus.Deleted ? null : account;
    }

    /// <summary>Seconds until the account may send the next request, or null when it may now.</summary>
    private int? RetryAfter(long accountId, DateTime now)
    {
        lock (_sentGate)
        {
            if (!_sent.TryGetValue(accountId, out var sent)) return null;
            Trim(accountId, sent, now);
            TimeSpan? wait = null;
            var perDay = Math.Max(1, options.RequestsPerDay);
            if (sent.Count >= perDay) wait = sent.ElementAt(sent.Count - perDay).AddDays(1) - now;
            var hour = sent.Where(t => t > now.AddHours(-1)).ToList();
            if (hour.Count >= Math.Max(1, options.RequestsPerHour))
            {
                var untilHour = hour[hour.Count - Math.Max(1, options.RequestsPerHour)].AddHours(1) - now;
                if (wait is null || untilHour > wait) wait = untilHour;
            }
            return wait is { } w ? (int)Math.Ceiling(Math.Max(1, w.TotalSeconds)) : null;
        }
    }

    private void CountSent(long accountId, DateTime now)
    {
        lock (_sentGate)
        {
            if (!_sent.TryGetValue(accountId, out var sent)) _sent[accountId] = sent = new Queue<DateTime>();
            sent.Enqueue(now);
            if (_sent.Count > 4096)
                foreach (var (id, queue) in _sent.ToList()) Trim(id, queue, now);
        }
    }

    private void Trim(long accountId, Queue<DateTime> sent, DateTime now)
    {
        while (sent.Count > 0 && sent.Peek() <= now.AddDays(-1)) sent.Dequeue();
        if (sent.Count == 0) _sent.Remove(accountId);
    }

    /// <summary>
    /// A refusal by a limit goes into the security log, once per account and limit an hour: a client that keeps trying
    /// would otherwise grow the log at will (friend and block changes themselves are not audited).
    /// </summary>
    private Task LimitAsync(long accountId, string limit, CallerInfo caller, DateTime now, CancellationToken ct)
    {
        lock (_sentGate)
        {
            if (_limitLogged.TryGetValue((accountId, limit), out var last) && now - last < TimeSpan.FromHours(1))
                return Task.CompletedTask;
            _limitLogged[(accountId, limit)] = now;
            if (_limitLogged.Count > 4096)
                foreach (var old in _limitLogged.Where(e => now - e.Value >= TimeSpan.FromHours(1)).Select(e => e.Key).ToList())
                    _limitLogged.Remove(old);
        }
        return audit.WriteAsync("social_limit", accountId, accountId, caller.Ip, new { limit }, now, ct);
    }

    private DomainError OwnFriendLimit() =>
        DomainError.Conflict("friend_limit", $"You have {options.MaxFriends} friends, the most you can have. Remove one first.");

    private static RequestOutcome Sent() => new(RequestOutcome.Sent, RequestSentMessage);

    private static DomainError NoSuchPlayer() => DomainError.NotFound("player_not_found", "No player has this exact name.");

    private static DomainError NoRequestFrom(AccountRecord target) =>
        DomainError.NotFound("request_not_found", $"No friend request from {target.Username}.");
}
