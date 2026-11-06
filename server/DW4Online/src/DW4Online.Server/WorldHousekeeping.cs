using DW4Online.Domain;
using DW4Online.Domain.Services;

namespace DW4Online.Server;

/// <summary>
/// The world's housekeeping (round I1a): at the start, rooms (read-only since round I1) and instances a previous process
/// left open are closed (server_restart, server_stop); then once a minute every member in the world counts as seen
/// (accounts.last_seen_at, "last online" for friends), and once an hour the instance history older than
/// World:HistoryDays (90) goes with its members.
/// </summary>
internal sealed class WorldHousekeeping(WorldService world, RoomService rooms, WorldWebOptions options, TimeProvider time, ILogger<WorldHousekeeping> log)
    : BackgroundService
{
    public override async Task StartAsync(CancellationToken cancellationToken)
    {
        try
        {
            var closed = await rooms.CloseLeftoversAsync(cancellationToken);
            if (closed > 0) log.LogInformation("Closed {Rooms} rooms left open by an earlier release", closed);
            var ended = await world.CloseLeftoversAsync(cancellationToken);
            if (ended > 0) log.LogInformation("Ended {Instances} instances the previous run left open (server_stop)", ended);
        }
        catch (MySqlConnector.MySqlException e)
        {
            log.LogWarning(e, "Could not close the rooms and instances left open by the previous run");
        }
        await base.StartAsync(cancellationToken);
    }

    protected override async Task ExecuteAsync(CancellationToken stop)
    {
        var nextPrune = DateTimeOffset.MinValue;
        try
        {
            while (!stop.IsCancellationRequested)
            {
                await Task.Delay(TimeSpan.FromMinutes(1), time, stop);
                try
                {
                    await world.TouchOnlineAsync(stop);
                    if (time.GetUtcNow() >= nextPrune)
                    {
                        nextPrune = time.GetUtcNow().AddHours(1);
                        var pruned = await world.PruneHistoryAsync(stop);
                        if (pruned > 0) log.LogInformation("Pruned {Instances} instances older than {Days} days from the history", pruned, options.HistoryDays);
                    }
                }
                catch (MySqlConnector.MySqlException e)
                {
                    log.LogWarning(e, "World housekeeping failed");
                }
            }
        }
        catch (OperationCanceledException)
        {
        }
    }
}
