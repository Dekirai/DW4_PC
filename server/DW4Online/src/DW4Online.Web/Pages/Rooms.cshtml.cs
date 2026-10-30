using DW4Online.Domain.Rooms;
using DW4Online.Domain.Services;
using DW4Online.Web.Auth;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace DW4Online.Web.Pages;

/// <summary>
/// The open rooms, read-only, with where their players are: the relay's instances per stage (SEAMLESS.md 3.2, 14). Everybody
/// sees the stages and how many play there together or alone; the names are shown only to signed-in members of that room
/// (the room list names only the room and its host, see the privacy page).
/// </summary>
public sealed class RoomsModel(RoomService rooms, IRelayGateway relay, TimeProvider time) : PageModel
{
    public IReadOnlyList<RoomSummary> Rooms { get; private set; } = [];
    public DateTime Now { get; private set; }
    /// <summary>Per room code: its stages, fields and dungeons first.</summary>
    public IReadOnlyDictionary<string, IReadOnlyList<StageGroup>> Stages { get; private set; } = new Dictionary<string, IReadOnlyList<StageGroup>>();

    /// <summary>One stage of a room: its instances (a hub stage holds one solo instance per member).</summary>
    public sealed record StageGroup(string Stage, string Name, bool Hub, IReadOnlyList<InstanceLine> Instances)
    {
        public int Players => Instances.Sum(i => i.Players);
    }

    /// <summary>
    /// One instance: mode "sealed" (2-4 members in lockstep), "solo" (one member leads) or "hub" (a solo lobby or menu);
    /// <paramref name="Names"/> only for members of the room.
    /// </summary>
    public sealed record InstanceLine(uint Id, string Mode, int Players, bool BossLock, bool Departing, IReadOnlyList<string>? Names);

    public async Task OnGetAsync(CancellationToken ct)
    {
        Now = time.GetUtcNow().UtcDateTime;
        Rooms = await rooms.ListAsync(ct: ct);
        var viewer = HttpContext.WebSession();
        var stages = new Dictionary<string, IReadOnlyList<StageGroup>>();
        foreach (var room in Rooms)
        {
            var instances = relay.Instances(room.Code);
            if (instances.Count == 0) continue;
            var member = viewer is not null && (await rooms.GetAsync(room.Code, viewer.AccountId, ct)).State is not null;
            stages[room.Code] = Group(instances, member);
        }
        Stages = stages;
    }

    /// <summary>The instances by stage: fields and dungeons first (most players first), then the hub stages.</summary>
    public static IReadOnlyList<StageGroup> Group(IReadOnlyList<InstanceGroupView> instances, bool withNames) =>
        instances
            .GroupBy(i => i.Stage.ToLowerInvariant())
            .Select(g => new StageGroup(g.First().Stage, StageNames.Of(g.First().Stage), StageNames.IsHub(g.Key),
                g.OrderByDescending(i => i.Members.Count).ThenBy(i => i.Id)
                    .Select(i => new InstanceLine(i.Id, i.Mode, i.Members.Count, i.BossLock, i.Departing,
                        withNames ? i.Members.OrderBy(m => m.Slot).Select(m => m.Username).ToList() : null))
                    .ToList()))
            .OrderBy(s => s.Hub)
            .ThenByDescending(s => s.Players)
            .ThenBy(s => s.Name, StringComparer.Ordinal)
            .ToList();

    /// <summary>"2 together", "alone", "on their own" (a hub stage).</summary>
    public static string Describe(InstanceLine line) => line.Mode switch
    {
        "sealed" => $"{line.Players} together",
        "hub" => "on their own",
        _ => line.Players == 1 ? "alone" : $"{line.Players} players",
    };
}
