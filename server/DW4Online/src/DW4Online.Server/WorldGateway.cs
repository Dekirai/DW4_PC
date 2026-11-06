using System.Diagnostics;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Story;
using DW4Online.Domain.World;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Server;

/// <summary>
/// The world relay as the domain sees it (SEAMLESS.md 19.17.2; i1a-contract.md "web 12:50" and "relay 13:05"): IWorldRelay for
/// POST /world/*, the snapshot, the play context and the admin actions; IWorldNotifier for the domain's in-process changes (the
/// live re-check of roles, prefs, friends and blocks without a database read under the hub lock; StoryReplaced). The admission
/// is web's WorldService check (SetAdmission) plus the account's story, loaded here off the lock; until web sets it, a check of
/// its own from the account, social and story repositories (relay-only tests and the relay lane's branch).
/// </summary>
internal sealed class WorldGateway : IWorldRelay, IWorldNotifier, IWorldVisibilitySink, IAsyncDisposable
{
    private readonly RelayTickets _tickets;
    private readonly RelayHub? _hub;
    private readonly RelayOptions _options;
    private readonly OnlineStoryService? _stories;
    private readonly IAccountRepository? _accounts;
    private readonly ISocialRepository? _social;
    private readonly ILogger<WorldGateway>? _log;
    private readonly WorldHistoryQueue _queue;
    private Func<WorldAdmission, ValueTask<WorldAdmitted?>>? _check;

    public WorldGateway(RelayTickets tickets, RelayOptions options, IServiceProvider services)
    {
        _tickets = tickets;
        _options = options;
        _hub = services.GetService<RelayHub>(); // registered only with Relay:Enabled
        _stories = services.GetService<OnlineStoryService>();
        _accounts = services.GetService<IAccountRepository>();
        _social = services.GetService<ISocialRepository>();
        _log = services.GetService<ILogger<WorldGateway>>();
        _queue = new WorldHistoryQueue(services.GetService<IInstanceHistoryRepository>(), services.GetService<IAuditLog>(), _log);
        if (_hub is null) return;
        _hub.Admission = AdmitAsync;
        lock (_hub.World.Sync)
        {
            _hub.World.History = _queue.Enqueue;
            _hub.World.Audit = _queue.Audit;
            _hub.World.RememberVisibility = (account, visibility) =>
            {
                if (RememberVisibility is { } remember) _queue.Post(_ => remember(account, Visibilities.Text(visibility)));
            };
        }
        // A keeper's new revision becomes its owner's online story (SEAMLESS.md 8.4). Under the hub lock: only queue it.
        if (_stories is not null)
        {
            _hub.StorySaver = (owner, story, countersOnly) => _stories.Enqueue(owner, story, countersOnly);
            _stories.SaveFailed = (owner, revision, e) =>
                _log?.LogWarning(e, "Saving the online story of account {Account} (revision {Revision}) failed; it is tried again", owner, revision);
        }
    }

    /// <summary>The queue of history and audit writes (tests wait for it).</summary>
    public WorldHistoryQueue Queue => _queue;

    public bool Available => _hub is not null;

    public int? BoundPort => _hub?.Started is { IsCompletedSuccessfully: true } started ? started.Result
        : _hub is not null && _options.Port > 0 ? _options.Port : null;

    public Func<long, string, Task>? RememberVisibility { get; set; }

    public void SetAdmission(Func<WorldAdmission, ValueTask<WorldAdmitted?>> check) => _check = check;

    public WorldTicket IssueTicket(long accountId, string role)
    {
        var issued = _tickets.Create(Wire.WorldChannel, accountId, role == RelayRoles.Staff ? RelayRoles.Staff : RelayRoles.Member);
        return new WorldTicket(issued.Ticket, issued.Nonce, issued.Expires);
    }

    public WorldSnapshot Snapshot() => _hub?.World.CachedSnapshot ?? WorldSnapshot.Empty;

    public WorldPlayContext? PlayContext(long accountId)
    {
        if (_hub is null) return null;
        var hub = _hub.World;
        if (!Monitor.TryEnter(hub.Sync, 50)) return null;
        try
        {
            return hub.PlayContext(accountId, Stopwatch.GetTimestamp());
        }
        finally
        {
            Monitor.Exit(hub.Sync);
        }
    }

    public bool Leave(long accountId) => _hub?.Locked((hub, now) => hub.Leave(accountId, now)) ?? false;

    public bool EndInstance(uint wid, long staffAccountId) => _hub?.Locked((hub, now) => hub.StaffEnd(wid, staffAccountId, now)) ?? false;

    public bool RemoveMember(uint wid, long accountId, long staffAccountId) =>
        _hub?.Locked((hub, now) => hub.StaffRemove(wid, accountId, staffAccountId, now)) ?? false;

    // IWorldNotifier: every call returns at once; the work runs on the thread pool, off the hub lock.

    public void FriendsChanged(long account, long other, bool otherSees)
    {
        Recheck(account);
        if (otherSees) Recheck(other);
    }

    public void BlocksChanged(long blocker, long blocked)
    {
        // (The blocked account's cache holds who blocks it; the relay never tells him, 19.11.)
        Recheck(blocker);
        Recheck(blocked);
    }

    public void PrefsChanged(long account) => Recheck(account);

    public void AccountChanged(long account) => Recheck(account);

    public void StoryReplaced(long account, int box)
    {
        if (_hub is null) return;
        var known = _hub.Locked((hub, now) => hub.StoryReplacing(account, box, now));
        if (!known || _stories is null) return;
        _ = Task.Run(async () =>
        {
            try
            {
                // (A revision the keeper queued after the commit and before this call must not land: Forget raises its generation.)
                _stories.Forget(account);
                var story = await _stories.LoadAsync(account);
                lock (_hub.World.Sync) _hub.World.StoryReloaded(account, box, story);
            }
            catch (Exception e)
            {
                _log?.LogWarning(e, "Reloading the story of account {Account} after its replacement failed", account);
            }
        });
    }

    /// <summary>Runs the account's admission again (off the lock) and gives the result to the hub (19.14's live re-check).</summary>
    private void Recheck(long account)
    {
        if (_hub is null) return;
        var handles = _hub.Locked((hub, _) => hub.AdmissionHandles(account));
        foreach (var (a, handle) in handles)
        {
            if (handle is not HubAdmission admission) continue;
            _ = Task.Run(async () =>
            {
                try
                {
                    var admit = await AdmitAsync(admission);
                    lock (_hub.World.Sync) _hub.World.Readmitted(a, admit, Stopwatch.GetTimestamp());
                }
                catch (Exception e)
                {
                    _log?.LogWarning(e, "The world's re-check of account {Account} failed", a);
                }
            });
        }
    }

    /// <summary>The admission (off the hub lock): web's check, or this gateway's own; then the account's story.</summary>
    private async ValueTask<HubAdmit?> AdmitAsync(HubAdmission a)
    {
        var request = new WorldAdmission(a.AccountId, a.Role, a.Nonce);
        var ok = _check is { } check ? await check(request) : await DefaultCheckAsync(request);
        if (ok is null) return null;
        var story = _stories is not null ? await _stories.LoadAsync(a.AccountId) : new RoomStoryData();
        return new HubAdmit(a.AccountId, ok.Username)
        {
            Staff = ok.Role == RelayRoles.Staff,
            Box = ok.Box,
            Story = story,
            DefaultVisibility = Visibilities.Parse(ok.Prefs.DefaultVisibility),
            Friends = ok.Friends.ToList(),
            Blocks = ok.Blocks.ToList(),
            BlockedBy = ok.BlockedBy.ToList(),
            NewSession = ok.NewSession,
            Handle = a,
        };
    }

    /// <summary>The admission before web's WorldService sets its own: the account active, its live role, its friends and blocks.</summary>
    private async ValueTask<WorldAdmitted?> DefaultCheckAsync(WorldAdmission a)
    {
        if (_accounts is null) return new WorldAdmitted(a.AccountId, "member" + a.AccountId, a.Role, 1, OnlinePrefs.Default);
        var account = await _accounts.GetAsync(a.AccountId);
        if (account is null || account.Status != AccountStatus.Active) return null;
        var role = account.Role is Roles.Moderator or Roles.Admin ? RelayRoles.Staff : RelayRoles.Member;
        IReadOnlyList<long> friends = [], blocks = [];
        if (_social is not null)
        {
            friends = (await _social.ListFriendsAsync(a.AccountId)).Select(r => r.AccountId).ToList();
            blocks = (await _social.ListBlocksAsync(a.AccountId)).Select(r => r.AccountId).ToList();
        }
        return new WorldAdmitted(a.AccountId, account.Username, role, 1, OnlinePrefs.Default) { Friends = friends, Blocks = blocks };
    }

    public ValueTask DisposeAsync() => _queue.DisposeAsync();
}
