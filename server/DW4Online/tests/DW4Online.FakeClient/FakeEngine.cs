using System.Buffers.Binary;
using System.Text;
using DW4Online.Relay.Protocol;

namespace DW4Online.FakeClient;

/// <summary>
/// A deterministic stand-in for the game engine, just rich enough to exercise every relay path (SEAMLESS.md 16): it
/// executes tick records, applies JOIN and LEAVE at the first joinable / membership-safe step, hashes every pad of the
/// present slots, and turns special pad bits into the stage flow: an EXIT bit (stage index and point in the main stick)
/// pressed by every present member is the original group move; pressed by some, it forks the instance at that step
/// (the copy despawns the other side). Departures reach their NEXTSTAGE (the barrier tick) after <see cref="DepartTicks"/>.
/// The first <see cref="EntryTicks"/> ticks of a visit are its entry scene (not joinable, not membership-safe). A
/// snapshot holds the state plus a presentation field (an audio position) that the canonical form zeroes.
/// </summary>
public sealed class FakeEngine
{
    /// <summary>The entry scene's length (simulation: every member of a run uses the same value).</summary>
    public int EntryTicks { get; set; } = 120;
    /// <summary>From the exit to its NEXTSTAGE (the real game sends HEADING one to two seconds before the barrier).</summary>
    public int DepartTicks { get; set; } = 120;
    /// <summary>"I take this exit" (main stick x = stage index, y = point): all present members = the group move, some = a fork.</summary>
    public const ushort ExitBit = 0x0001;
    /// <summary>"Everyone on the mat said yes": the whole instance moves (the original group move from one pad).</summary>
    public const ushort GroupExitBit = 0x0004;
    /// <summary>The stages a fake exit can lead to (main stick x = index). Hub stages dissolve the group.</summary>
    public static readonly string[] Stages = ["stagML11", "stag0101", "stag0102", "stag0201", "dng101b1", "stag0401", "stagBS01", "stagIS12"];

    public string Stage { get; private set; } = "";
    public uint Tick { get; private set; }
    public byte Present { get; private set; } = 1;
    public byte Leaving { get; private set; }
    public byte PendingJoin { get; private set; }
    public byte Quest { get; set; }
    public ulong Hash { get; private set; } = 0x9E3779B97F4A7C15UL;
    public uint AudioPosition { get; set; }

    public (string Stage, byte Point)? Departure { get; private set; }
    public int DepartLeft { get; private set; }
    /// <summary>NEXTSTAGE executed at this tick (the engine parks after it).</summary>
    public (uint Tick, string Stage, byte Point)? Barrier { get; private set; }
    /// <summary>The fork of the last step (tick, leaver mask), for SPLIT.</summary>
    public (uint Tick, byte Leavers)? LastFork { get; private set; }
    /// <summary>Joins and leaves applied by the last step (slot masks), for the membership log.</summary>
    public (byte Joined, byte Left) LastMembership { get; private set; }

    /// <summary>The local slot (live play: which side of a fork this copy takes).</summary>
    public int LocalSlot { get; set; }
    /// <summary>The slots on this copy's side since its last fork: events for other slots are ignored (SEAMLESS.md 6.7).</summary>
    public byte SideMask { get; private set; } = 0x0F;
    /// <summary>Replays: the side to take at each fork tick (STATE_DATA fork list).</summary>
    public Dictionary<uint, byte> ForkSides { get; } = [];

    public bool Joinable => Tick >= EntryTicks && Departure is null;
    public bool MembershipSafe => Tick >= EntryTicks;
    /// <summary>The session context hash this stand-in reports (HEADING, BARRIER, STATE): its quest's, or a real game's when
    /// set (W29's fake guests arrive in a real host's instance, whose candidates need its context, 19.7.2).</summary>
    public ulong? ContextOverride { get; set; }
    public ulong ContextHash => ContextOverride ?? FnvMix(0xC0FFEE, Quest);
    public ulong NetFingerprint => FnvMix(FnvMix(Hash, Tick), (ulong)Present | ((ulong)Leaving << 8));

    public static ulong FnvMix(ulong h, ulong v)
    {
        for (var i = 0; i < 8; ++i)
        {
            h ^= (byte)(v >> (8 * i));
            h *= 1099511628211UL;
        }
        return h;
    }

    private static ulong FnvMix(ulong h, string s)
    {
        foreach (var c in Encoding.ASCII.GetBytes(s))
        {
            h ^= c;
            h *= 1099511628211UL;
        }
        return h;
    }

    /// <summary>Executes one tick record (the tick is <see cref="Tick"/>).</summary>
    public void Step(TickRecord rec)
    {
        var t = Tick;
        LastFork = null;
        byte joined = 0, left = 0;
        foreach (var e in rec.Events)
        {
            if (e.Slot >= 0 && (SideMask & (1 << e.Slot)) == 0) continue; // another side's event
            switch (e.Type)
            {
                case Ev.Join: PendingJoin |= (byte)(1 << e.Payload[0]); break;
                case Ev.Leave:
                    if ((Present & (1 << e.Payload[0])) != 0) Leaving |= (byte)(1 << e.Payload[0]);
                    break;
                case Ev.Intent: Hash = FnvMix(Hash, 0x1A00UL | e.Payload[0]); break;
            }
        }
        if (Joinable && PendingJoin != 0)
        {
            joined = (byte)(PendingJoin & ~Present);
            Present |= PendingJoin;
            PendingJoin = 0;
        }
        if (MembershipSafe && Leaving != 0)
        {
            left = Leaving;
            Present &= (byte)~Leaving;
            Leaving = 0;
        }
        var active = (byte)(Present & ~Leaving);
        Hash = FnvMix(Hash, t);
        Hash = FnvMix(Hash, (ulong)Present | ((ulong)Leaving << 8));
        byte exiters = 0;
        byte group = 0;
        Pad first = default;
        for (var s = 0; s < Wire.MaxSlots; ++s)
        {
            var p = (active & (1 << s)) != 0 && (rec.Mask & (1 << s)) != 0 ? rec.Pads[s] : Pad.Neutral;
            Hash = FnvMix(Hash, p.Buttons | ((ulong)(byte)p.Lx << 16) | ((ulong)(byte)p.Ly << 24) | ((ulong)(byte)p.Rx << 32) | ((ulong)(byte)p.Ry << 40));
            if ((active & (1 << s)) != 0 && (p.Buttons & (ExitBit | GroupExitBit)) != 0)
            {
                if (exiters == 0 && group == 0) first = p;
                if ((p.Buttons & GroupExitBit) != 0) group |= (byte)(1 << s);
                else exiters |= (byte)(1 << s);
            }
        }
        if (Departure is null && (exiters != 0 || group != 0))
        {
            var target = Stages[Math.Abs((int)first.Lx) % Stages.Length];
            var point = (byte)Math.Abs((int)first.Ly);
            if (group != 0 || exiters == active) StartDeparture(target, point);
            else
            {
                // A fork at this step: the leavers' copy despawns the stayers and walks to its exit.
                var side = ForkSides.TryGetValue(t, out var fs) ? fs : (exiters & (1 << LocalSlot)) != 0 ? ForkSide.Leave : ForkSide.Stay;
                LastFork = (t, exiters);
                Hash = FnvMix(Hash, 0xF0F0UL | exiters);
                if (side == ForkSide.Leave)
                {
                    Present = (byte)(Present & exiters);
                    SideMask = exiters;
                    StartDeparture(target, point);
                }
                else
                {
                    Present = (byte)(Present & ~exiters);
                    SideMask = (byte)(SideMask & ~exiters & 0x0F);
                }
                PendingJoin = 0;
            }
        }
        else if (Departure is { } d && --DepartLeft <= 0) Barrier = (t, d.Stage, d.Point);
        LastMembership = (joined, left);
        Tick = t + 1;
    }

    private void StartDeparture(string stage, byte point)
    {
        Departure = (stage, point);
        DepartLeft = DepartTicks;
    }

    /// <summary>
    /// The load step of a new visit: the slot map (old slot -> new slot or -1) and the co-arrivals' slots. Every member of
    /// the group applies the same BARRIER_OK, so every engine ends in the same state.
    /// </summary>
    public void Load(string stage, byte point, sbyte[] slotMap, IEnumerable<int> coArrivalSlots)
    {
        byte present = 0;
        for (var s = 0; s < Wire.MaxSlots; ++s)
            if ((Present & (1 << s)) != 0 && slotMap[s] >= 0) present |= (byte)(1 << slotMap[s]);
        foreach (var c in coArrivalSlots) present |= (byte)(1 << c);
        Stage = stage;
        Tick = 0;
        Present = present;
        Leaving = 0;
        PendingJoin = 0;
        Departure = null;
        Barrier = null;
        LastFork = null;
        SideMask = 0x0F;
        ForkSides.Clear();
        Hash = FnvMix(FnvMix(FnvMix(FnvMix(0xD4D4D4D4UL, stage), point), present), Quest);
    }

    /// <summary>A hub stage (DISSOLVE): this player alone in slot 0.</summary>
    public void LoadSolo(string stage)
    {
        Stage = stage;
        Tick = 0;
        Present = 1;
        Leaving = 0;
        PendingJoin = 0;
        Departure = null;
        Barrier = null;
        SideMask = 0x0F;
        ForkSides.Clear();
        Hash = FnvMix(FnvMix(0xA5A5UL, stage), Quest);
    }

    /// <summary>The snapshot after a load step: the state, the presentation field, 16 KB of filler (like the 17 KB real one).</summary>
    public byte[] Snapshot(bool canonical)
    {
        var w = new WireWriter(0, 17_000).Str(Stage).U32(Tick).U8(Present).U8(Leaving).U8(PendingJoin).U8(Quest).U8(SideMask).U64(Hash)
            .U32(canonical ? 0 : AudioPosition);
        var filler = new byte[16 * 1024];
        var x = Hash | 1;
        for (var i = 0; i < filler.Length; i += 8)
        {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            BinaryPrimitives.WriteUInt64LittleEndian(filler.AsSpan(i), x);
        }
        w.Bytes(filler);
        return w.Frame()[Wire.HeaderBytes..];
    }

    public ulong CanonicalHash()
    {
        var h = 14695981039346656037UL;
        foreach (var b in Snapshot(canonical: true))
        {
            h ^= b;
            h *= 1099511628211UL;
        }
        return h;
    }

    public void Restore(byte[] snapshot)
    {
        var r = new WireReader(snapshot);
        Stage = r.Str();
        Tick = r.U32();
        Present = r.U8();
        Leaving = r.U8();
        PendingJoin = r.U8();
        Quest = r.U8();
        SideMask = r.U8();
        Hash = r.U64();
        r.U32(); // the leader's audio position: presentation only (6.6); this machine keeps its own
        Departure = null;
        Barrier = null;
        ForkSides.Clear();
    }

    /// <summary>A player carry (the real one is PlayerSlot + BitChip + the 3,316-byte record + personal values).</summary>
    public static byte[] Carry(string name)
    {
        var c = new byte[3400];
        Encoding.ASCII.GetBytes(name.Length > 32 ? name[..32] : name).CopyTo(c, 0);
        return c;
    }
}
