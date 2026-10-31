using System.Buffers.Binary;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Save;
using DW4Online.Domain.Services;

namespace DW4Online.Domain.Story;

/// <summary>A kept revision of an online story as staff see it.</summary>
public sealed record StoryHistoryEntry(long Revision, string Reason, DateTime SavedAt, int Phase, IReadOnlyList<int> Keys, int FlagCount);

/// <summary>
/// The accounts' own online stories (online_story, SEAMLESS.md 8.4): the relay loads the owner's when a room's hub starts
/// and queues every new revision of the room's story (<see cref="Enqueue"/>: the newest revision per account is written,
/// a failed write is tried again with a growing pause, and a load sees a queued revision before it is written); the
/// online character's record is composed from the account's own story (GET) and its uploaded story bytes are ignored
/// (PUT). Every written revision is kept in the history; the online character's reset empties the story, and staff can
/// put a kept revision back (both as a new revision).
/// </summary>
public sealed class OnlineStoryService(IOnlineStoryRepository stories, TimeProvider time, OnlineCharacterOptions? options = null)
{
    private readonly object _lock = new();
    private readonly Dictionary<long, RoomStoryData> _pending = [];
    private bool _draining;
    private TaskCompletionSource _wake = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly CancellationTokenSource _stop = new();

    private int HistoryKeep => Math.Max(1, options?.StoryHistoryRevisions ?? 100);

    /// <summary>Called when a queued write failed (account, revision, error); it is tried again.</summary>
    public Action<long, uint, Exception>? SaveFailed { get; set; }
    /// <summary>The longest pause between two tries of a failed write.</summary>
    public TimeSpan RetryMax { get; init; } = TimeSpan.FromSeconds(30);

    /// <summary>Revisions queued and not written yet.</summary>
    public int PendingCount
    {
        get
        {
            lock (_lock) return _pending.Count;
        }
    }

    /// <summary>The account's story (a queued newer revision first), or an empty one (revision 0) when it has none yet.</summary>
    public async Task<RoomStoryData> LoadAsync(long accountId, CancellationToken ct = default)
    {
        var row = await stories.GetAsync(accountId, ct);
        var stored = row is null ? new RoomStoryData() : RoomStoryData.FromColumns((uint)row.Revision, row.Flags, row.Folder, row.Counters);
        lock (_lock)
            if (_pending.TryGetValue(accountId, out var queued) && queued.Revision > stored.Revision) return queued.Clone();
        return stored;
    }

    /// <summary>Writes a revision now (newer revisions only); false when nothing changed.</summary>
    public Task<bool> SaveAsync(long accountId, RoomStoryData story, CancellationToken ct = default) =>
        stories.SaveAsync(Row(accountId, story, story.Revision), HistoryKeep, ct);

    /// <summary>
    /// Queues a revision to be written (the relay calls it under the room lock: it never waits). Only the newest revision
    /// per account is written; a failed write is tried again after 1, 2, 4 ... <see cref="RetryMax"/> seconds.
    /// </summary>
    public void Enqueue(long accountId, RoomStoryData story)
    {
        lock (_lock)
        {
            if (_pending.TryGetValue(accountId, out var queued) && queued.Revision >= story.Revision) return;
            _pending[accountId] = story;
            if (_draining) return;
            _draining = true;
        }
        _ = Task.Run(DrainAsync);
    }

    /// <summary>Drops the account's queued revision (its story is reset or restored, or the account is deleted).</summary>
    public void Forget(long accountId)
    {
        lock (_lock) _pending.Remove(accountId);
    }

    /// <summary>Writes the queued revisions now (a pause between tries ends); true when none is left within <paramref name="timeout"/>.</summary>
    public async Task<bool> FlushAsync(TimeSpan timeout, CancellationToken ct = default)
    {
        var until = time.GetUtcNow() + timeout;
        while (true)
        {
            lock (_lock)
            {
                if (_pending.Count == 0) return true;
                Interlocked.Exchange(ref _wake, new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously)).TrySetResult();
            }
            if (time.GetUtcNow() >= until) return false;
            await Task.Delay(50, ct);
        }
    }

    /// <summary>Stops the retries (the service stops; call <see cref="FlushAsync"/> first).</summary>
    public void Stop() => _stop.Cancel();

    private async Task DrainAsync()
    {
        var pause = TimeSpan.FromSeconds(1);
        while (!_stop.IsCancellationRequested)
        {
            List<(long Account, RoomStoryData Story)> batch;
            lock (_lock)
            {
                if (_pending.Count == 0)
                {
                    _draining = false;
                    return;
                }
                batch = _pending.Select(kv => (kv.Key, kv.Value)).ToList();
            }
            var failed = false;
            foreach (var (account, story) in batch)
            {
                try
                {
                    await SaveAsync(account, story, _stop.Token);
                    lock (_lock)
                        if (_pending.TryGetValue(account, out var queued) && ReferenceEquals(queued, story)) _pending.Remove(account);
                }
                catch (OperationCanceledException) when (_stop.IsCancellationRequested)
                {
                    break;
                }
                catch (Exception e)
                {
                    failed = true;
                    SaveFailed?.Invoke(account, story.Revision, e);
                }
            }
            if (!failed)
            {
                pause = TimeSpan.FromSeconds(1);
                continue;
            }
            Task wake;
            lock (_lock) wake = _wake.Task;
            try
            {
                await Task.WhenAny(Task.Delay(pause, _stop.Token), wake);
            }
            catch (OperationCanceledException)
            {
            }
            pause = TimeSpan.FromTicks(Math.Min(RetryMax.Ticks, pause.Ticks * 2));
        }
        lock (_lock) _draining = false;
    }

    /// <summary>
    /// Empties the account's story (the online character's reset: a new character starts from a new game's story) as its
    /// next revision; the old one stays in the history. Returns the new revision (0: the account had no story).
    /// </summary>
    public async Task<long> ResetAsync(long accountId, CancellationToken ct = default)
    {
        Forget(accountId);
        if (await stories.GetAsync(accountId, ct) is null) return 0;
        return await stories.ReplaceAsync(Row(accountId, new RoomStoryData(), 0), StoryWriteReasons.Reset, HistoryKeep, ct);
    }

    /// <summary>Staff: makes a kept revision the story again (as the next revision). Null: no such kept revision.</summary>
    public async Task<long?> RestoreAsync(long accountId, long revision, CancellationToken ct = default)
    {
        var kept = (await stories.ListHistoryAsync(accountId, ct)).FirstOrDefault(h => h.Revision == revision);
        if (kept is null) return null;
        Forget(accountId);
        var story = RoomStoryData.FromColumns((uint)kept.Revision, kept.Flags, kept.Folder, kept.Counters);
        return await stories.ReplaceAsync(Row(accountId, story, 0), StoryWriteReasons.AdminRestore, HistoryKeep, ct);
    }

    /// <summary>The kept revisions, newest first, with their phase, key items and number of flags.</summary>
    public async Task<IReadOnlyList<StoryHistoryEntry>> HistoryAsync(long accountId, CancellationToken ct = default) =>
        (await stories.ListHistoryAsync(accountId, ct)).Select(h =>
        {
            var s = RoomStoryData.FromColumns((uint)h.Revision, h.Flags, h.Folder, h.Counters);
            return new StoryHistoryEntry(h.Revision, h.Reason, h.SavedAt, StoryMerge.Phase(s),
                Enumerable.Range(0, StorySets.FolderCount).Where(k => s.Folder[k] != 0).ToList(), s.Flags.Count(f => f != 0));
        }).ToList();

    /// <summary>A copy of <paramref name="record"/> whose story bytes are the account's own story (zeros without one).</summary>
    public async Task<byte[]> ComposeAsync(long accountId, byte[] record, CancellationToken ct = default) =>
        StoryRecord.Compose(record, await LoadAsync(accountId, ct));

    private OnlineStoryRow Row(long accountId, RoomStoryData story, uint revision) => new()
    {
        AccountId = accountId,
        Revision = revision,
        Flags = (byte[])story.Flags.Clone(),
        Folder = (byte[])story.Folder.Clone(),
        Counters = story.CounterBytes(),
        UpdatedAt = Clock.Now(time),
    };
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
