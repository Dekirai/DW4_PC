using DW4Online.Domain.Rooms;

namespace DW4Online.Domain.Services;

/// <summary>
/// What is left of the room era (round I1, SEAMLESS.md 19.22 step 3): the rooms and their members are read-only for one
/// release, /rooms* answers 410 rooms_retired, and the service start closes the rooms a previous process left open
/// (reason server_restart). A cleanup migration one release later drops the tables, this class and RoomRepository.
/// </summary>
public sealed class RoomService(IRoomRepository repository, TimeProvider time)
{
    /// <summary>At startup: rooms a previous process left open are closed (reason server_restart).</summary>
    public Task<int> CloseLeftoversAsync(CancellationToken ct = default) => repository.CloseAllOpenAsync(Clock.Now(time), ct);
}
