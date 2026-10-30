using System.Buffers.Binary;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Save;
using DW4Online.Domain.Services;

namespace DW4Online.Domain.Story;

/// <summary>
/// The accounts' own online stories (online_story, SEAMLESS.md 8.4): the relay loads the owner's when a room's hub starts
/// and saves every new revision of the room's story (newer revisions only, so saves may arrive out of order); the online
/// character's record is composed from the account's own story (GET) and its uploaded story bytes are ignored (PUT).
/// </summary>
public sealed class OnlineStoryService(IOnlineStoryRepository stories, TimeProvider time)
{
    /// <summary>The account's story, or an empty one (revision 0) when it has none yet.</summary>
    public async Task<RoomStoryData> LoadAsync(long accountId, CancellationToken ct = default)
    {
        var row = await stories.GetAsync(accountId, ct);
        return row is null ? new RoomStoryData() : RoomStoryData.FromColumns((uint)row.Revision, row.Flags, row.Folder, row.Counters);
    }

    public Task<bool> SaveAsync(long accountId, RoomStoryData story, CancellationToken ct = default) =>
        stories.SaveAsync(new OnlineStoryRow
        {
            AccountId = accountId,
            Revision = story.Revision,
            Flags = (byte[])story.Flags.Clone(),
            Folder = (byte[])story.Folder.Clone(),
            Counters = story.CounterBytes(),
            UpdatedAt = Clock.Now(time),
        }, ct);

    /// <summary>A copy of <paramref name="record"/> whose story bytes are the account's own story (zeros without one).</summary>
    public async Task<byte[]> ComposeAsync(long accountId, byte[] record, CancellationToken ct = default) =>
        StoryRecord.Compose(record, await LoadAsync(accountId, ct));
}

/// <summary>The story bytes of a 3316-byte character record: BASE_FLAG at 792, BASE_FLAGFOLDER at 1816, BASE_COUNTER at 1828 (16 x i32 big endian).</summary>
public static class StoryRecord
{
    public static byte[] Compose(byte[] record, RoomStoryData story)
    {
        if (record.Length != RecordCodec.RecordSize) return record;
        var o = (byte[])record.Clone();
        story.Flags.CopyTo(o, RecordCodec.FlagsAt);
        story.Folder.CopyTo(o, RecordCodec.FlagFolderAt);
        for (var i = 0; i < story.Counters.Length; ++i)
            BinaryPrimitives.WriteInt32BigEndian(o.AsSpan(RecordCodec.CountersAt + 4 * i), story.Counters[i]);
        return o;
    }

    public static RoomStoryData Extract(ReadOnlySpan<byte> record)
    {
        var s = new RoomStoryData();
        if (record.Length != RecordCodec.RecordSize) return s;
        record.Slice(RecordCodec.FlagsAt, StorySets.FlagCount).CopyTo(s.Flags);
        record.Slice(RecordCodec.FlagFolderAt, StorySets.FolderCount).CopyTo(s.Folder);
        for (var i = 0; i < s.Counters.Length; ++i)
            s.Counters[i] = BinaryPrimitives.ReadInt32BigEndian(record.Slice(RecordCodec.CountersAt + 4 * i, 4));
        return s;
    }
}
