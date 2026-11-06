using DW4Online.Domain.Rooms;
using DW4Online.Domain.Services;
using DW4Online.Domain.Story;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;

namespace DW4Online.Server;

/// <summary>
/// The room service's view of the relay after round I1 (SEAMLESS.md 19.22): rooms are retired, the relay is the world's
/// (WorldGateway) and refuses room tickets (its one channel is WORLD). RoomService keeps only CloseLeftoversAsync for one release
/// (the web lane strips the rest); what it still calls here does nothing.
/// </summary>
internal sealed class RelayGateway : IRelayGateway
{
    private readonly RelayTickets _tickets;
    private readonly RelayHub? _hub;
    private readonly RelayOptions _options;

    public RelayGateway(RelayTickets tickets, RelayOptions options, IServiceProvider services)
    {
        _tickets = tickets;
        _options = options;
        _hub = services.GetService<RelayHub>(); // registered only with Relay:Enabled
    }

    public RoomStoryState? Story(string roomCode) => null;

    public int? RecentInstancePlayers(string roomCode, long accountId) => null;

    public bool Available => _hub is not null;

    public int? BoundPort => _hub?.Started is { IsCompletedSuccessfully: true } started ? started.Result
        : _hub is not null && _options.Port > 0 ? _options.Port : null;

    /// <summary>A room ticket (the relay refuses it at the handshake: rooms are retired, ERR ticket).</summary>
    public RelayTicket IssueTicket(string roomCode, long accountId)
    {
        var issued = _tickets.Create(roomCode, accountId, RelayRoles.Member);
        return new RelayTicket(issued.Ticket, issued.Nonce);
    }

    public void MemberLeft(string roomCode, long accountId, string reason)
    {
    }

    public void RoomClosed(string roomCode)
    {
    }

    public void OwnerPresence(string roomCode, bool present)
    {
    }

    public IReadOnlyList<InstanceGroupView> Instances(string roomCode) => [];

    public void SetAdmission(Func<RelayAdmission, ValueTask<RelayAdmitted?>> check)
    {
    }

    public void SetHeartbeat(Func<string, IReadOnlyCollection<long>, ValueTask> beat)
    {
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
