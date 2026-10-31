using DW4Online.Domain.Rooms;
using DW4Online.Domain.Services;
using DW4Online.Domain.Story;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;

namespace DW4Online.Server;

/// <summary>The DW4R2 relay as the room service sees it: tickets from <see cref="RelayTickets"/>, connections in <see cref="RelayHub"/>.</summary>
internal sealed class RelayGateway : IRelayGateway
{
    private readonly RelayTickets _tickets;
    private readonly RelayHub? _hub;
    private readonly RelayOptions _options;
    private readonly OnlineStoryService? _stories;
    private readonly ILogger<RelayGateway>? _log;

    public RelayGateway(RelayTickets tickets, RelayOptions options, IServiceProvider services)
    {
        _tickets = tickets;
        _options = options;
        _hub = services.GetService<RelayHub>(); // registered only with Relay:Enabled
        _stories = services.GetService<OnlineStoryService>();
        _log = services.GetService<ILogger<RelayGateway>>();
        // A room story's new revision becomes its owner's online story (SEAMLESS.md 8.4). Under the room lock: only queue
        // it. The queue writes the newest revision per owner, tries a failed write again (a database that is away for a
        // moment, a restart of it) and gives a queued revision to the next room's load before it is written; the service's
        // stop writes what is left (StoryFlushService).
        if (_hub is not null && _stories is not null)
        {
            _hub.StorySaver = (owner, story) => _stories.Enqueue(owner, story);
            _stories.SaveFailed = (owner, revision, e) =>
                _log?.LogWarning(e, "Saving the online story of account {Account} (revision {Revision}) failed; it is tried again", owner, revision);
        }
    }

    public RoomStoryState? Story(string roomCode) => _hub?.Story(roomCode);

    public bool Available => _hub is not null;

    /// <summary>
    /// The port the relay listens on once it started (Relay:Port 0 picks a free one in tests); before that the configured
    /// Relay:Port, or null when that is 0.
    /// </summary>
    public int? BoundPort => _hub?.Started is { IsCompletedSuccessfully: true } started ? started.Result
        : _hub is not null && _options.Port > 0 ? _options.Port : null;

    public RelayTicket IssueTicket(string roomCode, long accountId)
    {
        var issued = _tickets.Create(roomCode, accountId, RelayRoles.Member);
        return new RelayTicket(issued.Ticket, issued.Nonce);
    }

    public void MemberLeft(string roomCode, long accountId, string reason) => _hub?.MemberLeft(roomCode, accountId, reason switch
    {
        LeaveReasons.Kicked => CloseReason.Kicked,
        LeaveReasons.Timeout => CloseReason.Timeout,
        LeaveReasons.RoomClosed => CloseReason.RoomClosed,
        _ => CloseReason.Left,
    }, reason switch
    {
        LeaveReasons.Kicked => "You were removed from the room.",
        LeaveReasons.Timeout => "The room did not hear from you for too long.",
        _ => "You left the room.",
    });

    public void RoomClosed(string roomCode) => _hub?.RoomClosed(roomCode, "The room was closed.");

    public void OwnerPresence(string roomCode, bool present) => _hub?.OwnerPresence(roomCode, present);

    public IReadOnlyList<InstanceGroupView> Instances(string roomCode) => _hub is null
        ? []
        : _hub.Views(roomCode).Select(v => new InstanceGroupView(v.Id, v.Stage, v.Visit, v.Mode,
            v.Members.Select(m => new InstanceGroupMember(m.AccountId, m.Username, m.Slot, m.Connected, m.Lagging)).ToList(),
            v.Departing, v.BossLock, v.Joinable, v.Ticks, v.Parent)).ToList();

    public void SetAdmission(Func<RelayAdmission, ValueTask<RelayAdmitted?>> check)
    {
        if (_hub is null) return;
        _hub.Admission = async a =>
        {
            if (await check(new RelayAdmission(a.Room, a.AccountId, a.Nonce)) is not { } ok) return null;
            // The owner's online story as stored: the room's story starts from it when this connection creates the room's
            // hub (a failed load refuses the connection; the client asks for a new ticket).
            RoomStoryInit? story = null;
            if (_stories is not null && ok.OwnerAccountId > 0)
                story = new RoomStoryInit(ok.OwnerAccountId, await _stories.LoadAsync(ok.OwnerAccountId), ok.Difficulty,
                    StoryPolicies.Parse(ok.StoryPolicy), ok.OwnerPresent);
            return new HubAdmitted(ok.MemberIndex, ok.Username, ok.Owner, new RoomInfo(a.Room, ok.Protocol, ok.DataHash, ok.SettingsHash, ok.MaxPlayers, story));
        };
    }

    public void SetHeartbeat(Func<string, IReadOnlyCollection<long>, ValueTask> beat)
    {
        if (_hub is not null) _hub.Heartbeat = beat;
    }
}

/// <summary>
/// At the service's stop, after the relay stopped (registered before it, so stopped after it): writes the room stories
/// still queued (a database write that failed and waits for its next try), then ends the retries.
/// </summary>
internal sealed class StoryFlushService(OnlineStoryService stories, ILogger<StoryFlushService> log) : IHostedService
{
    public Task StartAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    public async Task StopAsync(CancellationToken cancellationToken)
    {
        try
        {
            if (stories.PendingCount > 0 && !await stories.FlushAsync(TimeSpan.FromSeconds(10), cancellationToken))
                log.LogWarning("{Count} online stories could not be saved before the service stopped", stories.PendingCount);
        }
        catch (OperationCanceledException)
        {
            log.LogWarning("{Count} online stories could not be saved before the service stopped", stories.PendingCount);
        }
        finally
        {
            stories.Stop();
        }
    }
}

/// <summary>Closes rooms a previous process left open (at start), then times out silent members every few seconds.</summary>
internal sealed class RoomSweeper(RoomService rooms, ILogger<RoomSweeper> log) : BackgroundService
{
    public override async Task StartAsync(CancellationToken cancellationToken)
    {
        try
        {
            var closed = await rooms.CloseLeftoversAsync(cancellationToken);
            if (closed > 0) log.LogInformation("Closed {Rooms} rooms left open by the previous run", closed);
        }
        catch (MySqlConnector.MySqlException e)
        {
            log.LogWarning(e, "Could not close the rooms left open by the previous run");
        }
        await base.StartAsync(cancellationToken);
    }

    protected override async Task ExecuteAsync(CancellationToken stop)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(5));
        try
        {
            while (await timer.WaitForNextTickAsync(stop))
            {
                try
                {
                    var removed = await rooms.SweepAsync(stop);
                    if (removed > 0) log.LogInformation("{Members} room members timed out", removed);
                }
                catch (MySqlConnector.MySqlException e)
                {
                    log.LogWarning(e, "Room sweep failed");
                }
            }
        }
        catch (OperationCanceledException)
        {
        }
    }
}
