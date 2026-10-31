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

        public Task<bool> SaveAsync(OnlineStoryRow row, int historyKeep, CancellationToken ct = default)
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

        public Task<long> ReplaceAsync(OnlineStoryRow row, string reason, int historyKeep, CancellationToken ct = default) => throw new NotSupportedException();
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
}
