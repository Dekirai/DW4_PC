using System.Collections.Concurrent;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.World;

namespace DW4Online.Tests;

/// <summary>
/// A stand-in for the world relay (round I1a's relay lane implements <see cref="IWorldRelay"/> and
/// <see cref="IWorldNotifier"/>): tests set the snapshot and the play contexts, call the admission the service set, and
/// read what the service asked of the relay. Thread-safe enough for the integration tests' parallel requests.
/// </summary>
public sealed class FakeWorldRelay : IWorldRelay, IWorldNotifier
{
    private int _tickets;
    private readonly ConcurrentDictionary<long, WorldPlayContext> _contexts = new();

    public bool Available { get; set; } = true;
    public int? BoundPort { get; set; } = 52041;
    public Func<WorldAdmission, ValueTask<WorldAdmitted?>>? Admission { get; private set; }
    public WorldSnapshot Current { get; set; } = WorldSnapshot.Empty;

    /// <summary>Tickets issued: account, role, nonce.</summary>
    public ConcurrentQueue<(long Account, string Role, string Nonce)> Issued { get; } = new();
    public ConcurrentQueue<long> Left { get; } = new();
    public ConcurrentQueue<(uint Wid, long Staff)> Ended { get; } = new();
    public ConcurrentQueue<(uint Wid, long Account, long Staff)> Removed { get; } = new();
    /// <summary>Notifier calls as text: "account 5", "story 5/1", "prefs 5", "friends 5 6 True", "blocks 5 6".</summary>
    public ConcurrentQueue<string> Notes { get; } = new();

    public void SetAdmission(Func<WorldAdmission, ValueTask<WorldAdmitted?>> check) => Admission = check;

    public WorldTicket IssueTicket(long accountId, string role)
    {
        if (role is not (WorldRoles.Member or WorldRoles.Staff)) throw new ArgumentException("Unknown role.", nameof(role));
        var nonce = $"n{Interlocked.Increment(ref _tickets)}";
        Issued.Enqueue((accountId, role, nonce));
        return new WorldTicket($"WORLD.{accountId}.{role}.{nonce}", nonce, DateTimeOffset.UtcNow.AddSeconds(60));
    }

    /// <summary>Redeems a ticket as the relay would after its signature check: the service's admission decides.</summary>
    public ValueTask<WorldAdmitted?> AdmitAsync(string ticket)
    {
        var parts = ticket.Split('.');
        return Admission!(new WorldAdmission(long.Parse(parts[1]), parts[2], parts[3]));
    }

    public WorldSnapshot Snapshot() => Current;

    public void SetPlayContext(long accountId, WorldPlayContext? context)
    {
        if (context is null) _contexts.TryRemove(accountId, out _);
        else _contexts[accountId] = context;
    }

    public WorldPlayContext? PlayContext(long accountId) => _contexts.GetValueOrDefault(accountId);

    public bool Leave(long accountId)
    {
        Left.Enqueue(accountId);
        return Current.Online.Any(o => o.AccountId == accountId);
    }

    public bool EndInstance(uint wid, long staffAccountId)
    {
        if (Current.Instances.All(i => i.Wid != wid)) return false;
        Ended.Enqueue((wid, staffAccountId));
        return true;
    }

    public bool RemoveMember(uint wid, long accountId, long staffAccountId)
    {
        if (Current.Instances.FirstOrDefault(i => i.Wid == wid) is not { } row ||
            row.Members.Concat(row.Spectators).All(m => m.AccountId != accountId)) return false;
        Removed.Enqueue((wid, accountId, staffAccountId));
        return true;
    }

    public void FriendsChanged(long account, long other, bool otherSees) => Notes.Enqueue($"friends {account} {other} {otherSees}");
    public void BlocksChanged(long blocker, long blocked) => Notes.Enqueue($"blocks {blocker} {blocked}");
    public void PrefsChanged(long account) => Notes.Enqueue($"prefs {account}");
    public void AccountChanged(long account) => Notes.Enqueue($"account {account}");
    public void StoryReplaced(long account, int box) => Notes.Enqueue($"story {account}/{box}");

    // --- Building snapshots ------------------------------------------------------------------------------------------------

    /// <summary>An instance row with its host first; members as (account, name, stage).</summary>
    public static WorldInstanceRow Instance(uint wid, long host, string hostName, string area, string stage, string visibility = VisibilityNames.Public,
        IEnumerable<(long Account, string Name, string Stage)>? guests = null, DateTime? started = null, IEnumerable<(long Account, string Name)>? spectators = null,
        long? historyId = null, bool randomizer = false, int difficulty = 0)
    {
        var members = new List<WorldMemberRow> { new(host, (uint)host, hostName, InstanceRoles.Host, stage, true, false) };
        foreach (var (account, name, at) in guests ?? [])
            members.Add(new WorldMemberRow(account, (uint)account, name, InstanceRoles.Guest, at, true, false));
        return new WorldInstanceRow(wid, historyId, host, hostName, 1, area, stage, visibility, 1, difficulty,
            "DW4R2-settings/1;protocol=49;data=1;difficulty=0;camera=0;light=1;rand=0,0,0,0;fast=0", randomizer, "", started ?? new DateTime(2026, 11, 1, 11, 0, 0),
            false, false, false, members,
            (spectators ?? []).Select(s => new WorldMemberRow(s.Account, (uint)s.Account, s.Name, InstanceRoles.Spectator, stage, true, false)).ToList());
    }

    /// <summary>Puts the instances in the snapshot and every member online (plus <paramref name="atHome"/>: account, name, stage).</summary>
    public void Set(IEnumerable<WorldInstanceRow> instances, IEnumerable<(long Account, string Name, string Stage)>? atHome = null)
    {
        var list = instances.ToList();
        var online = new List<WorldOnlineRow>();
        foreach (var i in list)
        {
            online.AddRange(i.Members.Select(m => new WorldOnlineRow(m.AccountId, m.MemberId, m.Name, m.Stage, i.Wid, false)));
            online.AddRange(i.Spectators.Select(m => new WorldOnlineRow(m.AccountId, m.MemberId, m.Name, "stagml11", 0, true, Spectating: true)));
        }
        foreach (var (account, name, stage) in atHome ?? [])
            online.Add(new WorldOnlineRow(account, (uint)account, name, stage, 0, false));
        Current = new WorldSnapshot(DateTime.UtcNow, online.Count(o => !o.Spectating), list) { Online = online };
    }
}
