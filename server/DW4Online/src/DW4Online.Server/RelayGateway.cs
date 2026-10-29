using DW4Online.Domain.Rooms;
using DW4Online.Domain.Services;
using DW4Online.Relay;

namespace DW4Online.Server;

/// <summary>The relay as the room service sees it: tickets from <see cref="RelayTickets"/>, sockets in <see cref="RelayServer"/>.</summary>
internal sealed class RelayGateway : IRelayGateway
{
    private readonly RelayTickets _tickets;
    private readonly RelayServer? _server;

    public RelayGateway(RelayTickets tickets, IServiceProvider services)
    {
        _tickets = tickets;
        _server = services.GetService<RelayServer>(); // registered only with Relay:Enabled
        if (_server is not null) _server.HostDisconnected += channel => HostLost?.Invoke(channel);
    }

    public bool Available => _server is not null;

    /// <summary>The port the relay listens on, once it started (Relay:Port 0 picks a free one in tests).</summary>
    public int? BoundPort => _server?.Started is { IsCompletedSuccessfully: true } started ? started.Result : null;

    public event Action<string>? HostLost;

    public string IssueTicket(string channel, long accountId, bool host) =>
        _tickets.Issue(channel, accountId, host ? RelayRoles.Host : RelayRoles.Guest);

    public void CloseChannel(string channel) => _server?.CloseRoom(channel);

    public void DropGuest(string channel, long accountId) => _server?.DropGuest(channel, accountId);
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
