using DW4Online.Domain;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Accounts;
using DW4Online.Domain.Services;

namespace DW4Online.Tests;

/// <summary>
/// Friends and blocks (SEAMLESS.md 19.11) on an in-memory store: requests and accepts, crossing requests, the silent
/// decline that stays "sent" until it expires, cancels, exact names, blocks that are never revealed, every limit of
/// Friends:* with its default, expiry and the purge, the audit of refusals and the notifier calls. The SQL store behind
/// the same interface is covered by the integration tests (SocialApiTests, FriendPageTests, AccountScrubTests).
/// </summary>
public class SocialServiceTests
{
    private static readonly CallerInfo Caller = new([192, 0, 2, 7], "tests");

    /// <summary>The name of <see cref="World.Many"/>'s account number <paramref name="i"/> (user names have at least 3 characters).</summary>
    private static string P(int i) => $"p{i:D3}";

    private sealed class Clock : TimeProvider
    {
        public DateTimeOffset Now { get; set; } = new(2026, 11, 5, 12, 0, 0, TimeSpan.Zero);
        public override DateTimeOffset GetUtcNow() => Now;
        public void Advance(TimeSpan by) => Now += by;
    }

    private sealed class FakeAccounts : IAccountRepository
    {
        public Dictionary<long, AccountRecord> Rows { get; } = [];

        public long Add(string name, string status = AccountStatus.Active)
        {
            var id = Rows.Count + 1;
            Rows[id] = new AccountRecord { Id = id, Username = name, Status = status };
            return id;
        }

        public Task<AccountRecord?> GetAsync(long id, CancellationToken ct = default) => Task.FromResult(Rows.GetValueOrDefault(id));

        public Task<AccountRecord?> FindByUsernameAsync(string username, CancellationToken ct = default) =>
            Task.FromResult(Rows.Values.FirstOrDefault(a => UsernamePolicy.Normalize(a.Username) == UsernamePolicy.Normalize(username)));

        public Task<long> CreateAsync(NewAccount account, DateTime now, CancellationToken ct = default) => throw new NotSupportedException();
        public Task RecordFailedLoginAsync(long id, int failedLogins, DateTime? lockedUntil, CancellationToken ct = default) => throw new NotSupportedException();
        public Task RecordLoginAsync(long id, DateTime now, string? newPasswordHash, CancellationToken ct = default) => throw new NotSupportedException();
        public Task SetPasswordAsync(long id, string passwordHash, bool mustChange, DateTime now, CancellationToken ct = default) => throw new NotSupportedException();
        public Task SetRecoveryCodeAsync(long id, byte[] hash, CancellationToken ct = default) => throw new NotSupportedException();
        public Task<bool> SetEmailAsync(long id, string? email, CancellationToken ct = default) => throw new NotSupportedException();
        public Task SetStatusAsync(long id, string status, CancellationToken ct = default) => throw new NotSupportedException();
        public Task SetRoleAsync(long id, string role, CancellationToken ct = default) => throw new NotSupportedException();
        public Task<IReadOnlyList<AccountRecord>> SearchAsync(string? query, int limit, CancellationToken ct = default) => throw new NotSupportedException();
        public Task SetProfilePublicAsync(long id, bool value, CancellationToken ct = default) => throw new NotSupportedException();
        public Task ScrubAsync(long id, string anonymousName, string? nameTag, CancellationToken ct = default) => throw new NotSupportedException();
        public Task<long> CountActiveAsync(CancellationToken ct = default) => throw new NotSupportedException();
    }

    /// <summary>The store as SocialRepository implements it in SQL (the same rows, the same filters).</summary>
    private sealed class FakeSocial(FakeAccounts accounts) : ISocialRepository
    {
        public Dictionary<(long Lo, long Hi), DateTime> Friendships { get; } = [];
        public Dictionary<(long From, long To), FriendRequestRow> Requests { get; } = [];
        public Dictionary<(long Blocker, long Blocked), DateTime> Blocks { get; } = [];

        private static (long, long) Pair(long a, long b) => a < b ? (a, b) : (b, a);
        private bool Live(long id) => accounts.Rows.TryGetValue(id, out var a) && a.Status != AccountStatus.Deleted;
        private string Name(long id) => accounts.Rows[id].Username;

        public Task<bool> AreFriendsAsync(long a, long b, CancellationToken ct = default) => Task.FromResult(Friendships.ContainsKey(Pair(a, b)));

        public Task<int> CountFriendsAsync(long accountId, CancellationToken ct = default) =>
            Task.FromResult(Friendships.Keys.Count(k => k.Lo == accountId || k.Hi == accountId));

        public Task<IReadOnlyList<SocialListRow>> ListFriendsAsync(long accountId, CancellationToken ct = default) =>
            Task.FromResult<IReadOnlyList<SocialListRow>>(Friendships.Where(f => f.Key.Lo == accountId || f.Key.Hi == accountId)
                .Select(f => Row(f.Key.Lo == accountId ? f.Key.Hi : f.Key.Lo, f.Value)).ToList());

        public Task<bool> AddFriendshipAsync(long a, long b, DateTime now, CancellationToken ct = default)
        {
            if (!Live(a) || !Live(b)) return Task.FromResult(false);
            Friendships.TryAdd(Pair(a, b), now);
            Requests.Remove((a, b));
            Requests.Remove((b, a));
            return Task.FromResult(true);
        }

        public Task<bool> RemoveFriendshipAsync(long a, long b, CancellationToken ct = default) => Task.FromResult(Friendships.Remove(Pair(a, b)));

        public Task<FriendRequestRow?> GetRequestAsync(long from, long to, CancellationToken ct = default) =>
            Task.FromResult(Requests.GetValueOrDefault((from, to)));

        public Task<bool> PutPendingRequestAsync(long from, long to, bool delivered, DateTime now, CancellationToken ct = default)
        {
            if (!Live(from) || !Live(to)) return Task.FromResult(false);
            Requests[(from, to)] = new FriendRequestRow { FromAccountId = from, ToAccountId = to, Delivered = delivered, CreatedAt = now };
            return Task.FromResult(true);
        }

        public Task<bool> ResendDeclinedRequestAsync(long from, long to, DateTime now, CancellationToken ct = default) =>
            Update(from, to, r => r.State == FriendRequestState.Declined, r => With(r, createdAt: now, cancelledAt: null, clearCancelled: true));

        public Task<bool> DeclineRequestAsync(long from, long to, DateTime now, CancellationToken ct = default) =>
            Update(from, to, r => r.State == FriendRequestState.Pending, r => With(r, state: FriendRequestState.Declined, decidedAt: now));

        public Task<bool> CancelDeclinedRequestAsync(long from, long to, DateTime now, CancellationToken ct = default) =>
            Update(from, to, r => r.State == FriendRequestState.Declined && r.CancelledAt is null, r => With(r, cancelledAt: now));

        public Task<bool> DeleteRequestAsync(long from, long to, CancellationToken ct = default) => Task.FromResult(Requests.Remove((from, to)));

        public Task<int> CountOutgoingAsync(long from, DateTime since, CancellationToken ct = default) =>
            Task.FromResult(Outgoing(from, since).Count());

        public Task<int> CountIncomingAsync(long to, DateTime since, CancellationToken ct = default) =>
            Task.FromResult(Incoming(to, since).Count());

        public Task<IReadOnlyList<SocialListRow>> ListOutgoingAsync(long from, DateTime since, CancellationToken ct = default) =>
            Task.FromResult<IReadOnlyList<SocialListRow>>(Outgoing(from, since).Select(r => Row(r.ToAccountId, r.CreatedAt)).ToList());

        public Task<IReadOnlyList<SocialListRow>> ListIncomingAsync(long to, DateTime since, CancellationToken ct = default) =>
            Task.FromResult<IReadOnlyList<SocialListRow>>(Incoming(to, since).Select(r => Row(r.FromAccountId, r.CreatedAt)).ToList());

        public Task<int> PurgeRequestsAsync(DateTime cut, CancellationToken ct = default)
        {
            var old = Requests.Where(r => r.Value.CreatedAt < cut).Select(r => r.Key).ToList();
            foreach (var k in old) Requests.Remove(k);
            return Task.FromResult(old.Count);
        }

        public Task<bool> IsBlockedAsync(long blocker, long blocked, CancellationToken ct = default) => Task.FromResult(Blocks.ContainsKey((blocker, blocked)));

        public Task<int> CountBlocksAsync(long accountId, CancellationToken ct = default) => Task.FromResult(Blocks.Keys.Count(k => k.Blocker == accountId));

        public Task<IReadOnlyList<SocialListRow>> ListBlocksAsync(long accountId, CancellationToken ct = default) =>
            Task.FromResult<IReadOnlyList<SocialListRow>>(Blocks.Where(b => b.Key.Blocker == accountId).Select(b => Row(b.Key.Blocked, b.Value)).ToList());

        public Task<bool> AddBlockAsync(long blocker, long blocked, DateTime now, CancellationToken ct = default)
        {
            if (!Live(blocker) || !Live(blocked) || !Blocks.TryAdd((blocker, blocked), now)) return Task.FromResult(false);
            Friendships.Remove(Pair(blocker, blocked));
            Requests.Remove((blocker, blocked));
            if (Requests.TryGetValue((blocked, blocker), out var theirs) && theirs.State == FriendRequestState.Pending)
                Requests[(blocked, blocker)] = With(theirs, delivered: false);
            return Task.FromResult(true);
        }

        public Task<bool> RemoveBlockAsync(long blocker, long blocked, CancellationToken ct = default)
        {
            if (!Blocks.Remove((blocker, blocked))) return Task.FromResult(false);
            if (Requests.TryGetValue((blocked, blocker), out var theirs) && theirs.State == FriendRequestState.Pending)
                Requests[(blocked, blocker)] = With(theirs, delivered: true);
            return Task.FromResult(true);
        }

        private IEnumerable<FriendRequestRow> Outgoing(long from, DateTime since) =>
            Requests.Values.Where(r => r.FromAccountId == from && r.CreatedAt >= since && r.CancelledAt is null);

        private IEnumerable<FriendRequestRow> Incoming(long to, DateTime since) =>
            Requests.Values.Where(r => r.ToAccountId == to && r.State == FriendRequestState.Pending && r.CreatedAt >= since && r.Delivered);

        private SocialListRow Row(long id, DateTime at) => new() { AccountId = id, Username = Name(id), At = at };

        private Task<bool> Update(long from, long to, Func<FriendRequestRow, bool> where, Func<FriendRequestRow, FriendRequestRow> change)
        {
            if (!Requests.TryGetValue((from, to), out var row) || !where(row)) return Task.FromResult(false);
            Requests[(from, to)] = change(row);
            return Task.FromResult(true);
        }

        private static FriendRequestRow With(FriendRequestRow r, string? state = null, bool? delivered = null, DateTime? createdAt = null,
            DateTime? decidedAt = null, DateTime? cancelledAt = null, bool clearCancelled = false) => new()
        {
            FromAccountId = r.FromAccountId,
            ToAccountId = r.ToAccountId,
            State = state ?? r.State,
            Delivered = delivered ?? r.Delivered,
            CreatedAt = createdAt ?? r.CreatedAt,
            DecidedAt = decidedAt ?? r.DecidedAt,
            CancelledAt = clearCancelled ? null : cancelledAt ?? r.CancelledAt,
        };
    }

    private sealed class Audit : IAuditLog
    {
        public List<(string Event, long? Account, string Detail)> Rows { get; } = [];

        public Task WriteAsync(string evt, long? accountId, long? actorId, byte[]? ip, object? detail, DateTime now, CancellationToken ct = default)
        {
            Rows.Add((evt, accountId, System.Text.Json.JsonSerializer.Serialize(detail)));
            return Task.CompletedTask;
        }
    }

    private sealed class Notifier : IWorldNotifier
    {
        public List<string> Calls { get; } = [];
        public void FriendsChanged(long a, long b) => Calls.Add($"friends {a} {b}");
        public void BlocksChanged(long blocker, long blocked) => Calls.Add($"blocks {blocker} {blocked}");
        public void PrefsChanged(long account) => Calls.Add($"prefs {account}");
        public void AccountChanged(long account) => Calls.Add($"account {account}");
        public void StoryReplaced(long account, int box) => Calls.Add($"story {account} {box}");
    }

    private sealed class World
    {
        public World(FriendsOptions? options = null)
        {
            Options = options ?? new FriendsOptions();
            Social = new FakeSocial(Accounts);
            Service = new SocialService(Accounts, Social, Notifier, Audit, Options, Clock);
        }

        public Clock Clock { get; } = new();
        public FakeAccounts Accounts { get; } = new();
        public FakeSocial Social { get; }
        public Audit Audit { get; } = new();
        public Notifier Notifier { get; } = new();
        public FriendsOptions Options { get; }
        public SocialService Service { get; }

        public long Add(string name, string status = AccountStatus.Active) => Accounts.Add(name, status);

        /// <summary>Accounts p0, p1, ... (for the limits).</summary>
        public List<long> Many(int count, string prefix = "p") => Enumerable.Range(0, count).Select(i => Add($"{prefix}{i:D3}")).ToList();

        public async Task<RequestOutcome> SendOk(long from, string to)
        {
            var r = await Service.SendRequestAsync(from, to, Caller);
            Assert.True(r.Ok, r.Error?.Message);
            return r.Value!;
        }

        public async Task<string> Code<T>(Task<Result<T>> call) => (await call).Error?.Code ?? "ok";

        public async Task<List<string>> In(long id) => (await Service.OverviewAsync(id)).RequestsIn.Select(r => r.Username).ToList();
        public async Task<List<string>> Out(long id) => (await Service.OverviewAsync(id)).RequestsOut.Select(r => r.Username).ToList();
        public async Task<List<string>> Friends(long id) => (await Service.OverviewAsync(id)).Friends.Select(r => r.Username).ToList();
    }

    [Fact]
    public async Task A_request_that_is_accepted_makes_friends_on_both_sides()
    {
        var w = new World();
        var kim = w.Add("Kim");
        var lee = w.Add("Lee");
        var sent = await w.SendOk(kim, "Lee");
        Assert.Equal((RequestOutcome.Sent, "Request sent."), (sent.Result, sent.Message));
        Assert.Equal(["Lee"], await w.Out(kim));
        Assert.Equal(["Kim"], await w.In(lee));
        Assert.Empty(await w.In(kim));
        Assert.Equal(Relations.RequestSent, await w.Service.RelationAsync(kim, lee));
        Assert.Equal(Relations.RequestReceived, await w.Service.RelationAsync(lee, kim));

        var accepted = await w.Service.AcceptAsync(lee, "kim", Caller);
        Assert.Equal((RequestOutcome.Friends, "You are now friends with Kim."), (accepted.Value!.Result, accepted.Value.Message));
        Assert.Equal(["Lee"], await w.Friends(kim));
        Assert.Equal(["Kim"], await w.Friends(lee));
        Assert.Empty(w.Social.Requests);
        Assert.Equal(Relations.Friends, await w.Service.RelationAsync(kim, lee));
        Assert.Equal(["friends 1 2", "friends 2 1"], w.Notifier.Calls);
        Assert.Empty(w.Audit.Rows); // friend changes are not audited, only refusals by a limit

        Assert.Equal("already_friends", await w.Code(w.Service.SendRequestAsync(kim, "Lee", Caller)));
        Assert.Equal("request_not_found", await w.Code(w.Service.AcceptAsync(lee, "Kim", Caller)));
    }

    [Fact]
    public async Task Crossing_requests_accept_both_at_once()
    {
        var w = new World();
        var kim = w.Add("Kim");
        var lee = w.Add("Lee");
        await w.SendOk(kim, "Lee");
        var crossing = await w.SendOk(lee, "KIM");
        Assert.Equal((RequestOutcome.Friends, "You are now friends with Kim."), (crossing.Result, crossing.Message));
        Assert.True(await w.Social.AreFriendsAsync(kim, lee));
        Assert.Empty(w.Social.Requests);
    }

    [Fact]
    public async Task A_decline_is_silent_and_stays_sent_until_it_expires()
    {
        var w = new World();
        var kim = w.Add("Kim");
        var lee = w.Add("Lee");
        await w.SendOk(kim, "Lee");
        var sentAt = (await w.Service.OverviewAsync(kim)).RequestsOut[0].SentAt;
        w.Notifier.Calls.Clear();
        Assert.Equal("ok", await w.Code(w.Service.DeclineAsync(lee, "Kim")));
        Assert.Empty(await w.In(lee));                   // gone for the receiver
        Assert.Equal(["Lee"], await w.Out(kim));         // still "sent" for the sender
        Assert.Equal(Relations.RequestSent, await w.Service.RelationAsync(kim, lee));
        Assert.Equal(Relations.None, await w.Service.RelationAsync(lee, kim));

        // A new request meanwhile answers "Request sent." and changes nothing (not even the date, nor the counts).
        w.Clock.Advance(TimeSpan.FromDays(3));
        Assert.Equal(SocialService.RequestSentMessage, (await w.SendOk(kim, "Lee")).Message);
        Assert.Equal(FriendRequestState.Declined, w.Social.Requests[(kim, lee)].State);
        Assert.Equal(sentAt, (await w.Service.OverviewAsync(kim)).RequestsOut[0].SentAt);
        Assert.Empty(await w.In(lee));
        Assert.Equal(["friends 2 1"], w.Notifier.Calls); // only the decline itself; nothing reaches Lee again

        // After Friends:RequestDays (30) it is gone for both and purged; a new request reaches Lee again.
        w.Clock.Advance(TimeSpan.FromDays(27).Add(TimeSpan.FromMinutes(1)));
        Assert.Empty(await w.Out(kim));
        Assert.Equal(Relations.None, await w.Service.RelationAsync(kim, lee));
        Assert.Equal(1, await w.Service.PurgeExpiredAsync());
        Assert.Empty(w.Social.Requests);
        await w.SendOk(kim, "Lee");
        Assert.Equal(["Kim"], await w.In(lee));
    }

    [Fact]
    public async Task An_expired_request_is_replaced_by_a_new_one_without_a_purge()
    {
        var w = new World();
        var kim = w.Add("Kim");
        var lee = w.Add("Lee");
        await w.SendOk(kim, "Lee");
        Assert.Equal("ok", await w.Code(w.Service.DeclineAsync(lee, "Kim")));
        w.Clock.Advance(TimeSpan.FromDays(31));
        Assert.Equal("request_not_found", await w.Code(w.Service.CancelAsync(kim, "Lee"))); // expired: nothing to cancel
        await w.SendOk(kim, "Lee"); // the expired declined row is replaced by a pending one
        Assert.Equal(FriendRequestState.Pending, w.Social.Requests[(kim, lee)].State);
        Assert.Equal(["Kim"], await w.In(lee));

        // An expired pending request is no longer seen by the receiver and cannot be accepted.
        w.Clock.Advance(TimeSpan.FromDays(30).Add(TimeSpan.FromSeconds(1)));
        Assert.Empty(await w.In(lee));
        Assert.Equal("request_not_found", await w.Code(w.Service.AcceptAsync(lee, "Kim", Caller)));
        Assert.Equal("request_not_found", await w.Code(w.Service.DeclineAsync(lee, "Kim")));
    }

    [Fact]
    public async Task The_receiver_who_declined_can_still_become_friends_by_sending_a_request()
    {
        var w = new World();
        var kim = w.Add("Kim");
        var lee = w.Add("Lee");
        await w.SendOk(kim, "Lee");
        await w.Service.DeclineAsync(lee, "Kim");
        var changed = await w.SendOk(lee, "Kim");
        Assert.Equal(RequestOutcome.Friends, changed.Result); // Kim's request is still open as Kim sees it
        Assert.True(await w.Social.AreFriendsAsync(kim, lee));
    }

    [Fact]
    public async Task Cancelling_removes_a_pending_request_and_hides_a_declined_one_without_lifting_the_decline()
    {
        var w = new World();
        var kim = w.Add("Kim");
        var lee = w.Add("Lee");
        var max = w.Add("Max");
        await w.SendOk(kim, "Lee");
        Assert.Equal("ok", await w.Code(w.Service.CancelAsync(kim, "lee")));
        Assert.Empty(w.Social.Requests);
        Assert.Empty(await w.In(lee));
        Assert.Equal("request_not_found", await w.Code(w.Service.CancelAsync(kim, "Lee")));

        // Max declines; Kim cancels: gone from Kim's list, but the decline holds until it expires.
        await w.SendOk(kim, "Max");
        await w.Service.DeclineAsync(max, "Kim");
        Assert.Equal("ok", await w.Code(w.Service.CancelAsync(kim, "Max")));
        Assert.Empty(await w.Out(kim));
        Assert.Equal(Relations.None, await w.Service.RelationAsync(kim, max));
        Assert.Equal(Relations.None, await w.Service.RelationAsync(max, kim));
        w.Clock.Advance(TimeSpan.FromDays(1));
        var again = await w.SendOk(kim, "Max");
        Assert.Equal(SocialService.RequestSentMessage, again.Message);
        var shown = Assert.Single((await w.Service.OverviewAsync(kim)).RequestsOut);
        Assert.Equal(("Max", w.Clock.Now.UtcDateTime), (shown.Username, shown.SentAt)); // "sent" again, with today's date
        Assert.Empty(await w.In(max));                                                 // and still not delivered
        Assert.Equal(FriendRequestState.Declined, w.Social.Requests[(kim, max)].State);

        // Max may still answer by sending his own request: then they are friends.
        Assert.Equal(RequestOutcome.Friends, (await w.SendOk(max, "Kim")).Result);
    }

    [Fact]
    public async Task Requests_name_the_exact_user_name_in_any_case()
    {
        var w = new World();
        var kim = w.Add("Kim");
        w.Add("Leonardo");
        w.Add("deleted_9", AccountStatus.Deleted);
        Assert.Equal("ok", await w.Code(w.Service.SendRequestAsync(kim, "  leonardo ", Caller)));
        foreach (var name in new[] { "Leo", "Leonard", "leonardo%", "L*", "", null, "deleted_9", "Leo nardo", "a" })
            Assert.Equal("player_not_found", await w.Code(w.Service.SendRequestAsync(kim, name, Caller)));
        Assert.Equal("own_account", await w.Code(w.Service.SendRequestAsync(kim, "KIM", Caller)));
        Assert.Equal("own_account", await w.Code(w.Service.BlockAsync(kim, "kim", Caller)));
    }

    [Fact]
    public async Task A_block_is_never_revealed_to_the_blocked_player()
    {
        var w = new World();
        var kim = w.Add("Kim");
        var lee = w.Add("Lee");
        var max = w.Add("Max");
        Assert.Equal("ok", await w.Code(w.Service.BlockAsync(lee, "Kim", Caller)));
        w.Notifier.Calls.Clear();

        // Kim's request to Lee (who blocks him) and to Max (who does not) answer the same.
        var toLee = await w.SendOk(kim, "Lee");
        var toMax = await w.SendOk(kim, "Max");
        Assert.Equal(toMax, toLee);
        Assert.Equal(["Max", "Lee"], (await w.Out(kim)).OrderBy(n => n == "Lee").ToList());
        Assert.Equal(Relations.RequestSent, await w.Service.RelationAsync(kim, lee));
        Assert.Equal(Relations.RequestSent, await w.Service.RelationAsync(kim, max));
        Assert.False(w.Social.Requests[(kim, lee)].Delivered);
        Assert.Equal(["friends 1 3"], w.Notifier.Calls); // the silent request tells nobody

        // Lee sees nothing and cannot answer it; for Lee, Kim is blocked.
        Assert.Empty(await w.In(lee));
        Assert.Equal(Relations.Blocked, await w.Service.RelationAsync(lee, kim));
        Assert.Equal("request_not_found", await w.Code(w.Service.AcceptAsync(lee, "Kim", Caller)));
        Assert.Equal("request_not_found", await w.Code(w.Service.DeclineAsync(lee, "Kim")));
        // Lee's own request to Kim needs the block lifted first (it is Lee's own block, so it may say so).
        Assert.Equal("you_blocked", await w.Code(w.Service.SendRequestAsync(lee, "Kim", Caller)));

        // Kim may cancel his silent request like any other, and send it again.
        Assert.Equal("ok", await w.Code(w.Service.CancelAsync(kim, "Lee")));
        Assert.Equal(SocialService.RequestSentMessage, (await w.SendOk(kim, "Lee")).Message);
        Assert.Equal(SocialService.RequestSentMessage, (await w.SendOk(kim, "Lee")).Message); // a repeat: no change
    }

    [Fact]
    public async Task A_block_ends_the_friendship_and_the_blockers_request_and_silences_the_other_request()
    {
        var w = new World();
        var kim = w.Add("Kim");
        var lee = w.Add("Lee");
        var max = w.Add("Max");
        await w.SendOk(kim, "Lee");
        await w.Service.AcceptAsync(lee, "Kim", Caller);
        await w.SendOk(max, "Lee");   // Max -> Lee pending
        await w.SendOk(kim, "Max");   // Kim -> Max pending
        w.Notifier.Calls.Clear();

        Assert.Equal("Kim", (await w.Service.BlockAsync(lee, "kim", Caller)).Value);
        Assert.False(await w.Social.AreFriendsAsync(kim, lee));
        Assert.Empty(await w.Friends(kim)); // Kim sees it as a removal, nothing more
        Assert.Equal(["blocks 2 1"], w.Notifier.Calls);

        // Lee blocks Max: Lee had no request to Max; Max's request to Lee stays "sent" for Max but leaves Lee's list.
        await w.Service.BlockAsync(lee, "Max", Caller);
        Assert.Equal(["Lee"], await w.Out(max));
        Assert.Empty(await w.In(lee));

        // Max blocks Kim: Kim's request to Max stays "sent" for Kim, undelivered.
        await w.Service.BlockAsync(max, "Kim", Caller);
        Assert.Equal(["Max"], await w.Out(kim));
        Assert.Empty(await w.In(max));

        // The blocker's own request goes: Kim sends to a new player and then blocks him.
        var ann = w.Add("Ann");
        await w.SendOk(kim, "Ann");
        await w.Service.BlockAsync(kim, "Ann", Caller);
        Assert.False(w.Social.Requests.ContainsKey((kim, ann)));
        Assert.Equal("Ann", (await w.Service.BlockAsync(kim, "Ann", Caller)).Value); // repeating changes nothing
        Assert.Single(w.Social.Blocks, b => b.Key == (kim, ann));
        Assert.Equal(["Ann"], (await w.Service.BlocksAsync(kim)).Select(b => b.Username));
    }

    [Fact]
    public async Task Unblocking_delivers_the_formerly_blocked_players_pending_request()
    {
        var w = new World();
        var kim = w.Add("Kim");
        var lee = w.Add("Lee");
        await w.Service.BlockAsync(lee, "Kim", Caller);
        await w.SendOk(kim, "Lee");
        Assert.Empty(await w.In(lee));
        Assert.Equal("Kim", (await w.Service.UnblockAsync(lee, "Kim")).Value);
        Assert.Equal(["Kim"], await w.In(lee));
        Assert.True((await w.Service.AcceptAsync(lee, "Kim", Caller)).Ok);
        Assert.Equal(["Kim"], await w.Friends(lee));
        Assert.Equal("not_blocked", await w.Code(w.Service.UnblockAsync(lee, "Kim")));
    }

    [Fact]
    public async Task Removing_a_friend_is_one_sided_and_untold()
    {
        var w = new World();
        var kim = w.Add("Kim");
        var lee = w.Add("Lee");
        await w.SendOk(kim, "Lee");
        await w.Service.AcceptAsync(lee, "Kim", Caller);
        Assert.Equal("Kim", (await w.Service.RemoveFriendAsync(lee, "kim")).Value);
        Assert.Empty(await w.Friends(kim));
        Assert.Empty(await w.Friends(lee));
        Assert.Equal("not_friends", await w.Code(w.Service.RemoveFriendAsync(kim, "Lee")));
        Assert.Equal("player_not_found", await w.Code(w.Service.RemoveFriendAsync(kim, "Nobody")));
    }

    [Fact]
    public async Task The_incoming_cap_answers_the_same_with_or_without_a_block_and_silent_rows_do_not_count()
    {
        var w = new World();
        Assert.Equal(100, w.Options.MaxPendingIn);
        var lee = w.Add("Lee");
        var blocked = w.Many(5, "blocked");
        foreach (var b in blocked)
        {
            await w.Service.BlockAsync(lee, $"blocked{blocked.IndexOf(b):D3}", Caller);
            await w.SendOk(b, "Lee"); // silent: never counts toward Lee's cap
        }
        var senders = w.Many(100);
        for (var i = 0; i < 99; ++i) await w.SendOk(senders[i], "Lee");
        Assert.Equal(99, (await w.Service.OverviewAsync(lee)).RequestsIn.Count);
        await w.SendOk(senders[99], "Lee"); // the 100th delivered request still fits
        Assert.Equal(100, (await w.Service.OverviewAsync(lee)).RequestsIn.Count);

        var late = w.Add("Late");
        var refused = await w.Service.SendRequestAsync(late, "Lee", Caller);
        Assert.Equal(("requests_full", "Lee can't take more friend requests right now."), (refused.Error!.Code, refused.Error.Message));
        // A sender Lee blocks gets the very same answer (the cap is checked before the block).
        var silent = w.Add("Silent");
        await w.Service.BlockAsync(lee, "Silent", Caller);
        var same = await w.Service.SendRequestAsync(silent, "Lee", Caller);
        Assert.Equal(refused.Error, same.Error);
        Assert.Contains(w.Audit.Rows, r => r.Event == "social_limit" && r.Account == late && r.Detail.Contains("incoming"));

        // Declining one frees a place.
        await w.Service.DeclineAsync(lee, P(0));
        Assert.Equal("ok", await w.Code(w.Service.SendRequestAsync(late, "Lee", Caller)));
    }

    [Fact]
    public async Task The_outgoing_cap_counts_every_request_the_sender_sees_as_sent()
    {
        var w = new World();
        Assert.Equal(50, w.Options.MaxPendingOut);
        var kim = w.Add("Kim");
        var targets = w.Many(52);
        // Two of them block Kim (silent rows), one declines: all still count, as Kim sees them all as sent.
        await w.Service.BlockAsync(targets[0], "Kim", Caller);
        await w.Service.BlockAsync(targets[1], "Kim", Caller);
        for (var i = 0; i < 50; ++i)
        {
            await w.SendOk(kim, P(i));
            if (i % 20 == 19) w.Clock.Advance(TimeSpan.FromHours(1));
        }
        await w.Service.DeclineAsync(targets[2], "Kim");
        Assert.Equal(50, (await w.Service.OverviewAsync(kim)).RequestsOut.Count);
        var refused = await w.Service.SendRequestAsync(kim, P(50), Caller);
        Assert.Equal("outgoing_limit", refused.Error!.Code);
        Assert.Contains("50 open friend requests", refused.Error.Message);
        // A repeat of an open one is no new request: still "Request sent.".
        Assert.Equal("ok", await w.Code(w.Service.SendRequestAsync(kim, P(3), Caller)));

        // Cancelling (the declined one: hidden from Kim, its decline kept) frees a place.
        await w.Service.CancelAsync(kim, P(2));
        Assert.Equal("ok", await w.Code(w.Service.SendRequestAsync(kim, P(50), Caller)));
        Assert.Equal("outgoing_limit", await w.Code(w.Service.SendRequestAsync(kim, P(51), Caller)));
    }

    [Fact]
    public async Task Requests_are_limited_to_20_per_hour_and_100_per_day()
    {
        var w = new World(new FriendsOptions { MaxPendingOut = 1000 }); // the outgoing cap is another test
        Assert.Equal((20, 100), (w.Options.RequestsPerHour, w.Options.RequestsPerDay));
        var kim = w.Add("Kim");
        w.Many(130);
        var n = 0;
        for (; n < 20; ++n) await w.SendOk(kim, P(n));
        var refused = await w.Service.SendRequestAsync(kim, P(n), Caller);
        Assert.Equal("friend_request_rate", refused.Error!.Code);
        Assert.Equal(ErrorKind.TooManyRequests, refused.Error.Kind);
        Assert.Equal(3600, (int)refused.Error.Extra!["retryAfterSeconds"]!);
        Assert.Contains(w.Audit.Rows, r => r.Event == "social_limit" && r.Detail.Contains("requests_per_time"));
        // Repeats of requests already sent do not count and are not refused.
        Assert.Equal("ok", await w.Code(w.Service.SendRequestAsync(kim, P(0), Caller)));

        // Cancel and send again still counts: the limit is on sending, not on open rows.
        await w.Service.CancelAsync(kim, P(0));
        Assert.Equal("friend_request_rate", await w.Code(w.Service.SendRequestAsync(kim, P(0), Caller)));

        // Each hour 20 more, until 100 in the day.
        for (var hour = 1; hour < 5; ++hour)
        {
            w.Clock.Advance(TimeSpan.FromHours(1));
            for (var i = 0; i < 20; ++i, ++n) await w.SendOk(kim, P(n));
        }
        Assert.Equal(100, n);
        w.Clock.Advance(TimeSpan.FromHours(1));
        refused = await w.Service.SendRequestAsync(kim, P(n), Caller);
        Assert.Equal("friend_request_rate", refused.Error!.Code);
        Assert.Equal(19 * 3600, (int)refused.Error.Extra!["retryAfterSeconds"]!); // the first of the day leaves after 24 h
        w.Clock.Advance(TimeSpan.FromHours(19));
        Assert.Equal("ok", await w.Code(w.Service.SendRequestAsync(kim, P(n), Caller)));
        // The limit is per sender: others are not affected.
        Assert.Equal("ok", await w.Code(w.Service.SendRequestAsync(w.Add("Other"), "Kim", Caller)));
    }

    [Fact]
    public async Task Two_hundred_friends_is_the_most_for_the_sender_and_the_receiver()
    {
        var w = new World(new FriendsOptions { RequestsPerHour = 1000, RequestsPerDay = 1000 }); // (the rate is another test)
        Assert.Equal(200, w.Options.MaxFriends);
        var kim = w.Add("Kim");
        var lee = w.Add("Lee");
        var others = w.Many(201);
        for (var i = 0; i < 200; ++i)
        {
            await w.SendOk(kim, P(i));
            Assert.True((await w.Service.AcceptAsync(others[i], "Kim", Caller)).Ok);
        }
        Assert.Equal(200, (await w.Service.OverviewAsync(kim)).Friends.Count);
        var refused = await w.Service.SendRequestAsync(kim, "Lee", Caller);
        Assert.Equal(("friend_limit", "You have 200 friends, the most you can have. Remove one first."), (refused.Error!.Code, refused.Error.Message));

        // Lee's request to a full Kim is sent, but Kim can neither accept it nor answer it with a crossing request.
        await w.SendOk(lee, "Kim");
        Assert.Equal("friend_limit", await w.Code(w.Service.AcceptAsync(kim, "Lee", Caller)));
        Assert.Equal("friend_limit", await w.Code(w.Service.SendRequestAsync(kim, "Lee", Caller)));
        Assert.Equal(["Lee"], await w.In(kim)); // still there to accept later
        await w.Service.RemoveFriendAsync(kim, P(0));
        Assert.Equal(RequestOutcome.Friends, (await w.SendOk(kim, "Lee")).Result);

        // The other side's limit: Lee accepts a request whose sender reached 200 friends meanwhile.
        var full = w.Add("Full");
        await w.SendOk(full, "Lee");
        for (var i = 1; i <= 200; ++i) await w.Social.AddFriendshipAsync(full, others[i], w.Clock.Now.UtcDateTime);
        var accept = await w.Service.AcceptAsync(lee, "Full", Caller);
        Assert.Equal(("other_friend_limit", "Full can't take more friends right now."), (accept.Error!.Code, accept.Error.Message));
        Assert.Contains(w.Audit.Rows, r => r.Event == "social_limit" && r.Account == kim && r.Detail.Contains("friends"));
    }

    [Fact]
    public async Task Five_hundred_blocks_is_the_most()
    {
        var w = new World();
        Assert.Equal(500, w.Options.MaxBlocks);
        var kim = w.Add("Kim");
        w.Many(501);
        for (var i = 0; i < 500; ++i) Assert.Equal("ok", await w.Code(w.Service.BlockAsync(kim, P(i), Caller)));
        Assert.Equal("ok", await w.Code(w.Service.BlockAsync(kim, P(7), Caller))); // already blocked: no new block
        var refused = await w.Service.BlockAsync(kim, P(500), Caller);
        Assert.Equal("block_limit", refused.Error!.Code);
        Assert.Contains(w.Audit.Rows, r => r.Event == "social_limit" && r.Detail.Contains("blocks"));
        await w.Service.UnblockAsync(kim, P(0));
        Assert.Equal("ok", await w.Code(w.Service.BlockAsync(kim, P(500), Caller)));
    }

    [Fact]
    public async Task Relations_cover_every_state_and_never_show_the_other_sides_block()
    {
        var w = new World();
        var kim = w.Add("Kim");
        var lee = w.Add("Lee");
        Assert.Equal(Relations.Self, await w.Service.RelationAsync(kim, kim));
        Assert.Equal(Relations.None, await w.Service.RelationAsync(kim, lee));
        await w.Service.BlockAsync(lee, "Kim", Caller);
        Assert.Equal(Relations.None, await w.Service.RelationAsync(kim, lee)); // Lee's block: not shown to Kim
        Assert.Equal(Relations.Blocked, await w.Service.RelationAsync(lee, kim));
        await w.Service.UnblockAsync(lee, "Kim");
        await w.SendOk(lee, "Kim");
        Assert.Equal(Relations.RequestReceived, await w.Service.RelationAsync(kim, lee));
        Assert.Equal(Relations.RequestSent, await w.Service.RelationAsync(lee, kim));
        await w.Service.AcceptAsync(kim, "Lee", Caller);
        Assert.Equal(Relations.Friends, await w.Service.RelationAsync(kim, lee));
    }

    [Fact]
    public async Task Requests_to_or_from_deleted_accounts_write_nothing()
    {
        var w = new World();
        var kim = w.Add("Kim");
        var gone = w.Add("Gone");
        await w.SendOk(gone, "Kim");
        w.Accounts.Rows[gone] = new AccountRecord { Id = gone, Username = "deleted_2", Status = AccountStatus.Deleted };
        Assert.Equal("player_not_found", await w.Code(w.Service.AcceptAsync(kim, "Gone", Caller)));
        Assert.Equal("player_not_found", await w.Code(w.Service.AcceptAsync(kim, "deleted_2", Caller)));
        Assert.Equal("player_not_found", await w.Code(w.Service.BlockAsync(kim, "deleted_2", Caller)));
    }

    [Fact]
    public void The_defaults_are_the_designs()
    {
        var o = new FriendsOptions();
        Assert.Equal((20, 100, 50, 100, 200, 500, 30),
            (o.RequestsPerHour, o.RequestsPerDay, o.MaxPendingOut, o.MaxPendingIn, o.MaxFriends, o.MaxBlocks, o.RequestDays));
        Assert.IsType<NullWorldNotifier>(NullWorldNotifier.Instance);
        NullWorldNotifier.Instance.FriendsChanged(1, 2); // does nothing, throws nothing
        NullWorldNotifier.Instance.StoryReplaced(1, 1);
    }
}
