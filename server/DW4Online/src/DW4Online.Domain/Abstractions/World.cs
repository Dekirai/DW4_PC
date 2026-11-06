namespace DW4Online.Domain.Abstractions;

// STUB COPY in the relay lane's branch: the web lane owns this file (i1a-contract.md, "web 12:50: tables and repositories").

/// <summary>The instance history (19.17.1), written by the relay's history queue off the hub lock.</summary>
public interface IInstanceHistoryRepository
{
    Task<long> StartAsync(InstanceHistoryStart s, CancellationToken ct);
    Task EndAsync(long id, string endReason, int peakPlayers, DateTime endedAt, CancellationToken ct);
    Task MemberJoinedAsync(long id, long accountId, int box, string role, DateTime joinedAt, CancellationToken ct);
    Task MemberLeftAsync(long id, long accountId, DateTime joinedAt, string leaveReason, DateTime leftAt, bool sharedStory, CancellationToken ct);
    Task<int> CloseOpenAsync(DateTime now, CancellationToken ct);
}

public sealed record InstanceHistoryStart(long HostAccountId, int HostBox, string FirstStage, string Visibility, int Difficulty, int StoryPhase,
    string Settings, DateTime StartedAt);
