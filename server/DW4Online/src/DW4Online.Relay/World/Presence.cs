using DW4Online.Relay.Hub;

namespace DW4Online.Relay.World;

/// <summary>A member's position in a hub stage as the selection sees it (an immutable copy taken under the hub lock).</summary>
public sealed record PresenceSample(HubMember Member, long Account, string Stage, float X, float Z, IReadOnlySet<long> Friends,
    IReadOnlySet<long> Blocks, IReadOnlySet<long> BlockedBy);

/// <summary>A receiver's chosen figures: every one of them, and the nearest (full rate) among them.</summary>
public sealed class PresenceChoice
{
    public static readonly PresenceChoice Empty = new(new HashSet<HubMember>(), new HashSet<HubMember>());

    public PresenceChoice(IReadOnlySet<HubMember> all, IReadOnlySet<HubMember> near)
    {
        All = all;
        Near = near;
    }

    public IReadOnlySet<HubMember> All { get; }
    public IReadOnlySet<HubMember> Near { get; }
}

/// <summary>
/// The lobby as a meeting place (SEAMLESS.md 19.13): for every receiver in a hub stage, the senders it draws as ghost figures: at
/// most <c>cap</c> (World:HubFigures, 30), friends first, then the nearest; never a pair with a block in either direction. A
/// figure stays chosen until it is 20 % farther than the last candidate that would be chosen instead (no popping). The nearest
/// <c>near</c> (World:NearFigures, 10) get every PRESENCE2, the others every second one. Pure: it runs off the hub lock on an
/// immutable copy of the positions (19.17.4's design change (c)), and the hub swaps the result in under its lock.
/// </summary>
public static class PresenceSelection
{
    public const double Hysteresis = 1.2;

    public static Dictionary<HubMember, PresenceChoice> Compute(IReadOnlyList<PresenceSample> samples,
        IReadOnlyDictionary<HubMember, PresenceChoice> previous, int cap, int near)
    {
        var result = new Dictionary<HubMember, PresenceChoice>(samples.Count);
        foreach (var stage in samples.GroupBy(s => s.Stage, StringComparer.OrdinalIgnoreCase))
        {
            var here = stage.ToList();
            foreach (var r in here)
            {
                var others = new List<(PresenceSample S, double D, bool Friend)>(here.Count);
                foreach (var s in here)
                {
                    if (ReferenceEquals(s, r) || s.Member == r.Member) continue;
                    if (r.Blocks.Contains(s.Account) || r.BlockedBy.Contains(s.Account) || s.Blocks.Contains(r.Account) || s.BlockedBy.Contains(r.Account))
                        continue;
                    var dx = s.X - r.X;
                    var dz = s.Z - r.Z;
                    others.Add((s, Math.Sqrt(dx * dx + dz * dz), r.Friends.Contains(s.Account)));
                }
                if (others.Count == 0)
                {
                    result[r.Member] = PresenceChoice.Empty;
                    continue;
                }
                // Friends first, then by distance; the cap-th candidate's distance is the line a chosen figure keeps its place
                // within (plus 20 %).
                others.Sort((a, b) => a.Friend != b.Friend ? (a.Friend ? -1 : 1) : a.D.CompareTo(b.D));
                var chosen = new HashSet<HubMember>();
                var line = others.Count > cap ? others[cap - 1].D : double.PositiveInfinity;
                var before = previous.GetValueOrDefault(r.Member);
                if (before is not null)
                    foreach (var (s, d, friend) in others)
                        if (before.All.Contains(s.Member) && (friend || d <= line * Hysteresis) && chosen.Count < cap) chosen.Add(s.Member);
                foreach (var (s, _, _) in others)
                {
                    if (chosen.Count >= cap) break;
                    chosen.Add(s.Member);
                }
                var nearest = others.Where(o => chosen.Contains(o.S.Member)).OrderBy(o => o.D).Take(near).Select(o => o.S.Member).ToHashSet();
                result[r.Member] = new PresenceChoice(chosen, nearest);
            }
        }
        return result;
    }
}
