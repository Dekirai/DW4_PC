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
    private readonly DW4Online.Domain.Services.ReportService? _reports;
    private readonly WorldHistoryQueue _queue;
    private Func<WorldAdmission, ValueTask<WorldAdmitted?>>? _check;
    /// <summary>The newest ticket nonce issued to each account in this process (web's admission answers null for an older one).</summary>
    private readonly System.Collections.Concurrent.ConcurrentDictionary<long, string> _newestNonce = new();

    public WorldGateway(RelayTickets tickets, RelayOptions options, IServiceProvider services)
    {
        _tickets = tickets;
        _options = options;
        _hub = services.GetService<RelayHub>(); // registered only with Relay:Enabled
        _stories = services.GetService<OnlineStoryService>();
        _accounts = services.GetService<IAccountRepository>();
        _social = services.GetService<ISocialRepository>();
        _log = services.GetService<ILogger<WorldGateway>>();
        _reports = services.GetService<DW4Online.Domain.Services.ReportService>();
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
            if (_reports is not null) _hub.World.Reports = Report;
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
        _newestNonce[accountId] = issued.Nonce;
        return new WorldTicket(issued.Ticket, issued.Nonce, issued.Expires);
    }

    /// <summary>The admission's nonce is no longer the account's newest: a newer ticket was issued (a reconnect through /world/ticket, a
    /// Play on another PC), so a null answer for it says nothing about the account (i1a review).</summary>
    internal bool Superseded(HubAdmission a) => _newestNonce.TryGetValue(a.AccountId, out var newest) && newest != a.Nonce;

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

    /// <summary>
    /// Runs the account's admission again (off the lock) and gives the result to the hub (19.14's live re-check) with the handle and
    /// the number it ran with: the hub drops a result for an admission the member no longer has and one a newer re-check overtook. A
    /// refusal of a nonce that is no longer the account's newest is no refusal of the account (its client fetched a new ticket for a
    /// reconnect; that handshake is checked on its own): it is dropped here.
    /// </summary>
    private void Recheck(long account)
    {
        if (_hub is null) return;
        var handles = _hub.Locked((hub, _) => hub.AdmissionHandles(account));
        foreach (var (a, handle, seq) in handles)
        {
            if (handle is not HubAdmission admission) continue;
            _ = Task.Run(async () =>
            {
                try
                {
                    var admit = await AdmitAsync(admission);
                    if (admit is null && _newestNonce.TryGetValue(a, out var newest) && newest != admission.Nonce)
                    {
                        // (Asked again with the newest nonce: refused there too, the account itself may not play: a client that
                        // fetched a newer ticket and kept its old connection does not escape a lock.)
                        var newer = admission with { Nonce = newest };
                        if (await CheckAsync(newer) is not null || Superseded(newer))
                        {
                            _log?.LogInformation("The world's re-check of account {Account} ran with an older ticket; dropped", a);
                            return;
                        }
                    }
                    lock (_hub.World.Sync) _hub.World.Readmitted(a, admit, Stopwatch.GetTimestamp(), admission, seq);
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
        var ok = await CheckAsync(a);
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

    /// <summary>Web's admission check (or this gateway's own until web sets it), without the story.</summary>
    private ValueTask<WorldAdmitted?> CheckAsync(HubAdmission a)
    {
        var request = new WorldAdmission(a.AccountId, a.Role, a.Nonce);
        return _check is { } check ? check(request) : DefaultCheckAsync(request);
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

    /// <summary>
    /// A player's REPORT (round I1b, 19.16): the report service stores it off the hub lock (its rules: the daily limit, a repeat about
    /// the same player adds nothing) and the reporter gets WORLD_NOTICE 14 with the result.
    /// </summary>
    private void Report(ReportRequest r)
    {
        var reports = _reports!;
        _ = Task.Run(async () =>
        {
            ReportResult result;
            try
            {
                var outcome = await reports.ReportAsync(r.ReporterAccount, r.ReportedAccount, r.Reason, r.HistoryId,
                    DW4Online.Domain.Abstractions.ReportSources.Game);
                result = outcome.Ok ? ReportResult.Sent
                    : outcome.Error!.Kind == DW4Online.Domain.ErrorKind.TooManyRequests ? ReportResult.Limit
                    : outcome.Error.Kind is DW4Online.Domain.ErrorKind.NotFound or DW4Online.Domain.ErrorKind.Invalid ? ReportResult.NotAvailable
                    : ReportResult.Failed;
            }
            catch (Exception e)
            {
                _log?.LogWarning(e, "Storing the report of account {Reporter} about {Reported} failed", r.ReporterAccount, r.ReportedAccount);
                result = ReportResult.Failed;
            }
            _hub?.Locked((hub, _) =>
            {
                hub.ReportAnswered(r.ReporterAccount, r.ReportedMember, r.ReportedName, result);
                return true;
            });
        });
    }

    public ValueTask DisposeAsync() => _queue.DisposeAsync();
}
