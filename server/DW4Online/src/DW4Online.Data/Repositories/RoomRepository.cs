using Dapper;
using DW4Online.Domain.Rooms;
using MySqlConnector;

namespace DW4Online.Data.Repositories;

/// <summary>The durable room record written by the room service (the live state lives in memory).</summary>
public sealed class RoomRepository(Database db) : IRoomRepository
{
    public async Task<long> CreateAsync(NewRoom room, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        long id;
        try
        {
            id = await c.ExecuteScalarAsync<long>(new CommandDefinition(
                "INSERT INTO rooms (public_code, name, host_account_id, has_password, max_players, protocol, data_hash, client_version, " +
                "settings, ranked, randomizer, status, created_at) VALUES (@code, @name, @host, @pw, @max, @protocol, @hash, @version, " +
                "@settings, @ranked, @randomizer, 'open', @now); SELECT LAST_INSERT_ID();",
                new
                {
                    code = room.Code,
                    name = room.Name,
                    host = room.HostAccountId,
                    pw = room.HasPassword,
                    max = room.MaxPlayers,
                    protocol = room.Protocol,
                    hash = room.DataHash,
                    version = room.ClientVersion,
                    settings = room.Settings,
                    ranked = room.Ranked,
                    randomizer = room.Randomizer,
                    now = room.Now,
                }, tx, cancellationToken: ct));
        }
        catch (MySqlException e) when (MySqlErrors.IsDuplicate(e))
        {
            await tx.RollbackAsync(ct);
            throw new DuplicateRoomCodeException();
        }
        await c.ExecuteAsync(new CommandDefinition(
            "INSERT INTO room_members (room_id, account_id, slot, joined_at) VALUES (@id, @a, 0, @now)",
            new { id, a = room.HostAccountId, now = room.Now }, tx, cancellationToken: ct));
        await tx.CommitAsync(ct);
        return id;
    }

    // A leave and a re-join in the same millisecond reopen the row instead of colliding with the primary key.
    public async Task AddMemberAsync(long roomId, long accountId, int slot, DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await c.ExecuteAsync(new CommandDefinition(
            "INSERT INTO room_members (room_id, account_id, slot, joined_at) VALUES (@roomId, @accountId, @slot, @now) " +
            "ON DUPLICATE KEY UPDATE slot = VALUES(slot), left_at = NULL, leave_reason = NULL",
            new { roomId, accountId, slot, now }, cancellationToken: ct));
    }

    public async Task RemoveMemberAsync(long roomId, long accountId, string reason, DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE room_members SET left_at = @now, leave_reason = @reason WHERE room_id = @roomId AND account_id = @accountId AND left_at IS NULL",
            new { roomId, accountId, reason, now }, cancellationToken: ct));
    }

    public async Task SetStoryFrozenAsync(long roomId, DateTime? since, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE rooms SET story_frozen_at = @since WHERE id = @roomId", new { roomId, since }, cancellationToken: ct));
    }

    public async Task MarkPlayingAsync(long roomId, DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE rooms SET status = 'playing', started_at = COALESCE(started_at, @now) WHERE id = @roomId AND closed_at IS NULL",
            new { roomId, now }, cancellationToken: ct));
    }

    public async Task CloseAsync(long roomId, string reason, DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE room_members SET left_at = @now, leave_reason = 'room_closed' WHERE room_id = @roomId AND left_at IS NULL",
            new { roomId, now }, tx, cancellationToken: ct));
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE rooms SET status = 'closed', close_reason = @reason, closed_at = @now WHERE id = @roomId AND closed_at IS NULL",
            new { roomId, reason, now }, tx, cancellationToken: ct));
        await tx.CommitAsync(ct);
    }

    public async Task<int> CloseAllOpenAsync(DateTime now, CancellationToken ct = default)
    {
        await using var c = await db.OpenAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        await c.ExecuteAsync(new CommandDefinition(
            "UPDATE room_members m JOIN rooms r ON r.id = m.room_id SET m.left_at = @now, m.leave_reason = 'room_closed' " +
            "WHERE r.closed_at IS NULL AND m.left_at IS NULL", new { now }, tx, cancellationToken: ct));
        var closed = await c.ExecuteAsync(new CommandDefinition(
            "UPDATE rooms SET status = 'closed', close_reason = 'server_restart', closed_at = @now WHERE closed_at IS NULL",
            new { now }, tx, cancellationToken: ct));
        await tx.CommitAsync(ct);
        return closed;
    }
}
