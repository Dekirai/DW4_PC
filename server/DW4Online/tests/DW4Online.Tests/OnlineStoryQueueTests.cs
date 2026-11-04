using DW4Online.Domain;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Story;

namespace DW4Online.Tests;

/// <summary>
/// The room stories' save queue (round 56 review: a revision saved while the database was away for two seconds was lost,
/// and the owner's next room started from the older one). The newest revision per account is written, a failed write is
/// tried again, a load sees a queued revision before it is written, and a flush writes what is left.
/// </summary>
public class OnlineStoryQueueTests
{
    private sealed class FlakyStories : IOnlineStoryRepository
    {
        private readonly Lock _gate = new();
        public int FailuresLeft;
        public readonly Dictionary<long, OnlineStoryRow> Rows = [];
        public int Writes;

        public Task<OnlineStoryRow?> GetAsync(long accountId, CancellationToken ct = default)
        {
            lock (_gate) return Task.FromResult(Rows.GetValueOrDefault(accountId));
        }

        public Task<bool> SaveAsync(OnlineStoryRow row, StoryHistoryPolicy history, CancellationToken ct = default)
        {
            lock (_gate)
            {
                if (FailuresLeft > 0)
                {
                    --FailuresLeft;
                    throw new InvalidOperationException("the database is away");
                }
                ++Writes;
                if (Rows.TryGetValue(row.AccountId, out var old) && old.Revision >= row.Revision) return Task.FromResult(false);
                Rows[row.AccountId] = row;
                return Task.FromResult(true);
            }
        }

        public Task<long> ReplaceAsync(OnlineStoryRow row, string reason, StoryHistoryPolicy history, CancellationToken ct = default)
        {
            lock (_gate)
            {
                var revision = (Rows.GetValueOrDefault(row.AccountId)?.Revision ?? 0) + 1;
                Rows[row.AccountId] = new OnlineStoryRow { AccountId = row.AccountId, Revision = revision, Flags = row.Flags, Folder = row.Folder, Counters = row.Counters };
                return Task.FromResult(revision);
            }
        }

        public Task KeepAsync(OnlineStoryRow row, string reason, StoryHistoryPolicy history, CancellationToken ct = default) => Task.CompletedTask;
        public Task<IReadOnlyList<OnlineStoryHistoryRow>> ListHistoryAsync(long accountId, CancellationToken ct = default) =>
            Task.FromResult<IReadOnlyList<OnlineStoryHistoryRow>>([]);
    }

    private static RoomStoryData Story(uint revision, int flag)
    {
        var s = new RoomStoryData { Revision = revision };
        s.Flags[flag] = 1;
        return s;
    }

    [Fact]
    public async Task A_failed_write_is_tried_again_and_a_load_sees_the_queued_revision_first()
    {
        var repo = new FlakyStories { FailuresLeft = 2 };
        var failures = 0;
        var stories = new OnlineStoryService(repo, TimeProvider.System) { RetryMax = TimeSpan.FromMilliseconds(200), SaveFailed = (_, _, _) => ++failures };
        repo.Rows[7] = new OnlineStoryRow { AccountId = 7, Revision = 1, Flags = new byte[1024], Folder = new byte[12], Counters = new byte[64] };
        stories.Enqueue(7, Story(2, 505));
        stories.Enqueue(7, Story(3, 506));
        stories.Enqueue(7, Story(2, 505)); // (an older one after a newer: ignored)
        // While the writes fail, the owner's next room starts from the queued revision, not the stored one.
        var loaded = await stories.LoadAsync(7);
        Assert.Equal(3u, loaded.Revision);
        Assert.Equal(1, loaded.Flags[506]);
        Assert.True(await stories.FlushAsync(TimeSpan.FromSeconds(10)));
        Assert.Equal(3, repo.Rows[7].Revision);
        Assert.Equal(1, repo.Rows[7].Flags[506]);
        Assert.Equal(2, failures);
        Assert.Equal(0, stories.PendingCount);
        stories.Stop();
    }

    [Fact]
    public async Task Forget_drops_a_queued_revision()
    {
        var repo = new FlakyStories { FailuresLeft = 1_000 };
        var stories = new OnlineStoryService(repo, TimeProvider.System) { RetryMax = TimeSpan.FromMilliseconds(100) };
        stories.Enqueue(9, Story(4, 505));
        Assert.Equal(1, stories.PendingCount);
        stories.Forget(9);
        Assert.Equal(0, stories.PendingCount);
        Assert.True(await stories.FlushAsync(TimeSpan.FromSeconds(1)));
        Assert.Equal(0u, (await stories.LoadAsync(9)).Revision);
        stories.Stop();
    }

    private sealed class GatedStories : IOnlineStoryRepository
    {
        private readonly Lock _gate = new();
        public readonly Dictionary<long, OnlineStoryRow> Rows = [];
        public TaskCompletionSource SaveGate { get; set; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public Func<Task>? OnGet { get; set; }
        public int Writes;

        public async Task<OnlineStoryRow?> GetAsync(long accountId, CancellationToken ct = default)
        {
            OnlineStoryRow? row;
            lock (_gate) row = Rows.GetValueOrDefault(accountId);
            if (OnGet is { } hook) await hook();
            return row;
        }

        public async Task<bool> SaveAsync(OnlineStoryRow row, StoryHistoryPolicy history, CancellationToken ct = default)
        {
            await SaveGate.Task;
            lock (_gate)
            {
                ++Writes;
                if (Rows.TryGetValue(row.AccountId, out var old) && old.Revision >= row.Revision) return false;
                Rows[row.AccountId] = row;
                return true;
            }
        }

        public Task<long> ReplaceAsync(OnlineStoryRow row, string reason, StoryHistoryPolicy history, CancellationToken ct = default)
        {
            lock (_gate)
            {
                var revision = (Rows.GetValueOrDefault(row.AccountId)?.Revision ?? 0) + 1;
                Rows[row.AccountId] = new OnlineStoryRow { AccountId = row.AccountId, Revision = revision, Flags = row.Flags, Folder = row.Folder, Counters = row.Counters };
                return Task.FromResult(revision);
            }
        }

        public Task KeepAsync(OnlineStoryRow row, string reason, StoryHistoryPolicy history, CancellationToken ct = default) => Task.CompletedTask;
        public Task<IReadOnlyList<OnlineStoryHistoryRow>> ListHistoryAsync(long accountId, CancellationToken ct = default) =>
            Task.FromResult<IReadOnlyList<OnlineStoryHistoryRow>>([]);
    }

    private static OnlineStoryRow Row(long account, long revision) =>
        new() { AccountId = account, Revision = revision, Flags = new byte[1024], Folder = new byte[12], Counters = new byte[64] };

    /// <summary>A revision that changed only counters waits until a minute after the account's last write; a newer one replaces it.</summary>
    [Fact]
    public async Task Counter_only_revisions_are_written_at_most_once_per_interval()
    {
        var repo = new GatedStories();
        repo.SaveGate.SetResult();
        var stories = new OnlineStoryService(repo, TimeProvider.System, new OnlineCharacterOptions { StoryCounterSaveSeconds = 2 });
        stories.Enqueue(3, Story(1, 505));
        await WaitFor(() => repo.Writes == 1);
        await Task.Delay(100); // (its write time is noted after the write)
        stories.Enqueue(3, Story(2, 505), countersOnly: true);
        stories.Enqueue(3, Story(3, 505), countersOnly: true);
        await Task.Delay(700);
        Assert.Equal(1, repo.Writes);
        Assert.Equal(3u, (await stories.LoadAsync(3)).Revision); // (a room's load sees it meanwhile)
        await WaitFor(() => repo.Writes == 2);
        Assert.Equal(3, repo.Rows[3].Revision);
        // A flag change does not wait.
        stories.Enqueue(3, Story(4, 506));
        await WaitFor(() => repo.Writes == 3);
        stories.Stop();
    }

    /// <summary>
    /// Round 59 review (from the code): a reset or restore while the queue still held (or was writing) an older revision of
    /// the room could see that revision land after the reset. The reset waits for a write in flight and raises the account's
    /// generation; nothing queued before it is written after it.
    /// </summary>
    [Fact]
    public async Task Nothing_queued_before_a_reset_lands_after_it()
    {
        var repo = new GatedStories();
        repo.Rows[9] = Row(9, 4);
        var stories = new OnlineStoryService(repo, TimeProvider.System);
        stories.Enqueue(9, Story(5, 505)); // the drain takes it and waits in the write
        await Task.Delay(100);
        stories.Enqueue(9, Story(7, 506)); // queued behind it
        var reset = stories.ResetAsync(9);
        await Task.Delay(100);
        Assert.False(reset.IsCompleted); // (it waits for the write in flight)
        repo.SaveGate.SetResult();
        var revision = await reset;
        Assert.True(await stories.FlushAsync(TimeSpan.FromSeconds(5)));
        await Task.Delay(200);
        Assert.Equal(revision, repo.Rows[9].Revision);
        Assert.All(repo.Rows[9].Flags, f => Assert.Equal(0, f));
        stories.Stop();
    }

    /// <summary>
    /// Round 59 review (from the code): a room's load read the database row first and the queue after it; a write that
    /// landed in between (and left the queue) gave the new hub the older row. The queue is read first now.
    /// </summary>
    [Fact]
    public async Task A_load_sees_a_revision_written_while_it_reads()
    {
        var repo = new GatedStories();
        repo.Rows[5] = Row(5, 1);
        var stories = new OnlineStoryService(repo, TimeProvider.System);
        stories.Enqueue(5, Story(2, 505)); // waits in the write
        await Task.Delay(100);
        // The load reads the old row; meanwhile the write goes through and leaves the queue.
        repo.OnGet = async () =>
        {
            repo.SaveGate.TrySetResult();
            await WaitFor(() => stories.PendingCount == 0);
        };
        var loaded = await stories.LoadAsync(5);
        Assert.Equal(2u, loaded.Revision);
        Assert.Equal(1, loaded.Flags[505]);
        stories.Stop();
    }

    private static async Task WaitFor(Func<bool> condition)
    {
        for (var i = 0; i < 200 && !condition(); ++i) await Task.Delay(25);
        Assert.True(condition());
    }
}
