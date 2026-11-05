using System.Numerics;
using DW4Online.Domain.Story;

namespace DW4Online.Relay.Protocol;

// The DW4R2 messages (SEAMLESS.md 6.3; the exact layout is the document's appendix "DW4R2 wire layout"). Every record
// parses its payload (Parse) and builds its frame (Frame); both directions live here so that the relay, the fake clients
// and the tests share one definition.

/// <summary>An event inside a tick record: type u8, length u16, payload.</summary>
public sealed record TickEvent(byte Type, byte[] Payload)
{
    public static TickEvent Join(int slot, int entrancePoint, ReadOnlySpan<byte> carry)
    {
        if (2 + carry.Length > MaxPayload) throw new ArgumentException("a JOIN carry above the event limit", nameof(carry));
        var p = new byte[2 + carry.Length];
        p[0] = (byte)slot;
        p[1] = (byte)entrancePoint;
        carry.CopyTo(p.AsSpan(2));
        return new TickEvent(Ev.Join, p);
    }

    public static TickEvent Leave(int slot) => new(Ev.Leave, [(byte)slot]);

    public static TickEvent Intent(int slot, IntentKind kind, int argument)
    {
        var p = new byte[6];
        p[0] = (byte)slot;
        p[1] = (byte)kind;
        System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(p.AsSpan(2), argument);
        return new TickEvent(Ev.Intent, p);
    }

    public static TickEvent LeadFrom(uint tick)
    {
        var p = new byte[4];
        System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(p, tick);
        return new TickEvent(Ev.LeadFrom, p);
    }

    public static TickEvent Delay(int slot, int delay) => new(Ev.Delay, [(byte)slot, (byte)delay]);

    /// <summary>An event's payload length is a u16.</summary>
    public const int MaxPayload = ushort.MaxValue;

    /// <summary>The slot an event concerns (JOIN, LEAVE, INTENT, DELAY), or -1.</summary>
    public int Slot => Type is Ev.Join or Ev.Leave or Ev.Intent or Ev.Delay && Payload.Length > 0 ? Payload[0] : -1;
}

/// <summary>
/// One tick of an instance's stream: present mask u8 (the slots whose pads follow), event count u8, the pads in slot
/// order (6 bytes each), then the events. SEALED, LIVE and STATE_DATA carry these records unchanged.
/// </summary>
public sealed class TickRecord
{
    /// <summary>The event count is a u8.</summary>
    public const int MaxEvents = byte.MaxValue;

    public byte Mask { get; init; }
    public Pad[] Pads { get; } = new Pad[Wire.MaxSlots];
    public List<TickEvent> Events { get; } = [];

    public int EncodedSize => 2 + Pad.Size * BitOperations.PopCount((uint)(Mask & Wire.SlotMask)) + Events.Sum(e => 3 + e.Payload.Length);

    public void Write(WireWriter w)
    {
        // A record past the format's limits would be unreadable for every member (and the log's blocks with it).
        if (Events.Count > MaxEvents) throw new InvalidOperationException("more than 255 events in a tick record");
        foreach (var e in Events)
            if (e.Payload.Length > TickEvent.MaxPayload) throw new InvalidOperationException("an event payload above 64 KiB");
        w.U8((byte)(Mask & Wire.SlotMask)).U8((byte)Events.Count);
        for (var s = 0; s < Wire.MaxSlots; ++s)
            if ((Mask & (1 << s)) != 0) w.Pad(Pads[s]);
        foreach (var e in Events) w.U8(e.Type).U16((ushort)e.Payload.Length).Bytes(e.Payload);
    }

    public byte[] ToBytes()
    {
        var w = new WireWriter(0, EncodedSize);
        Write(w);
        return w.Frame()[Wire.HeaderBytes..];
    }

    public static TickRecord Read(ref WireReader r)
    {
        var mask = r.U8();
        if ((mask & ~Wire.SlotMask) != 0) throw new ProtocolException("tick mask above slot 3");
        var events = r.U8();
        var t = new TickRecord { Mask = mask };
        for (var s = 0; s < Wire.MaxSlots; ++s)
            if ((mask & (1 << s)) != 0) t.Pads[s] = r.Pad();
        for (var i = 0; i < events; ++i)
        {
            var type = r.U8();
            var len = r.U16();
            t.Events.Add(new TickEvent(type, r.Bytes(len).ToArray()));
        }
        return t;
    }

    public static TickRecord Parse(ReadOnlySpan<byte> bytes)
    {
        var r = new WireReader(bytes);
        var t = Read(ref r);
        r.End();
        return t;
    }
}

// Client to server -----------------------------------------------------------------------------------------------------

/// <summary>INPUT: a sealed member's pads for ticks first..first+count-1 (count 1..32).</summary>
public sealed record InputMsg(uint Inst, uint Visit, uint FirstTick, Pad[] Pads)
{
    public const int MaxPads = 32;

    public static InputMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new InputMsg(r.U32(), r.U32(), r.U32(), ReadPads(ref r, r.U8(), MaxPads));
        r.End();
        return m;
    }

    public byte[] Frame()
    {
        var w = new WireWriter(Msg.Input, 13 + Pads.Length * Pad.Size).U32(Inst).U32(Visit).U32(FirstTick).U8((byte)Pads.Length);
        foreach (var pad in Pads) w.Pad(pad);
        return w.Frame();
    }

    internal static Pad[] ReadPads(ref WireReader r, int count, int max)
    {
        if (count < 1 || count > max) throw new ProtocolException("pad count out of range");
        var pads = new Pad[count];
        for (var i = 0; i < count; ++i)
        {
            pads[i] = r.Pad();
            if (!pads[i].Valid) throw new ProtocolException("stick value outside the GameCube range");
        }
        return pads;
    }
}

/// <summary>LOG: a solo-led member's applied pads for ticks first..first+count-1 (count 1..1200), batched every 100 ms.</summary>
public sealed record LogMsg(uint Inst, uint Visit, uint FirstTick, Pad[] Pads)
{
    public const int MaxPads = 1200;

    public static LogMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new LogMsg(r.U32(), r.U32(), r.U32(), InputMsg.ReadPads(ref r, r.U16(), MaxPads));
        r.End();
        return m;
    }

    public byte[] Frame()
    {
        var w = new WireWriter(Msg.Log, 14 + Pads.Length * Pad.Size).U32(Inst).U32(Visit).U32(FirstTick).U16((ushort)Pads.Length);
        foreach (var pad in Pads) w.Pad(pad);
        return w.Frame();
    }
}

/// <summary>
/// HEADING: a member confirmed an exit or the gate disc (one to two seconds before its barrier), with the session context
/// hash its engine will arrive with (co-arrivals need the same context).
/// </summary>
public sealed record HeadingMsg(uint Inst, string Stage, byte Point, ulong ContextHash)
{
    public static HeadingMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new HeadingMsg(r.U32(), StageNames.Read(ref r), r.U8(), r.U64());
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.Heading).U32(Inst).Str(Stage).U8(Point).U64(ContextHash).Frame();
}

/// <summary>BARRIER: after the barrier tick (the step that executed NEXTSTAGE); carry = the sender's own player carry.</summary>
public sealed record BarrierMsg(uint Inst, uint Visit, uint Tick, string Stage, byte Point, byte AliveMask, byte PresentMask, ulong ContextHash, byte[] Carry)
{
    /// <summary>A carry becomes a JOIN event (slot, point, carry) whose length is a u16: at most 65,533 bytes.</summary>
    public const int MaxCarry = ushort.MaxValue - 2;

    public static BarrierMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new BarrierMsg(r.U32(), r.U32(), r.U32(), StageNames.Read(ref r), r.U8(), Masks.Read(ref r), Masks.Read(ref r), r.U64(), r.Blob(MaxCarry));
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.Barrier, 40 + Carry.Length).U32(Inst).U32(Visit).U32(Tick).Str(Stage).U8(Point)
        .U8(AliveMask).U8(PresentMask).U64(ContextHash).Blob(Carry).Frame();
}

/// <summary>BARRIER_CHOICE: at a boss-locked target, 0 = wait for them, 1 = go alone.</summary>
public sealed record BarrierChoiceMsg(uint Inst, byte Choice)
{
    public const byte Wait = 0;
    public const byte Alone = 1;

    public static BarrierChoiceMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new BarrierChoiceMsg(r.U32(), r.U8());
        r.End();
        if (m.Choice > 1) throw new ProtocolException("unknown barrier choice");
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.BarrierChoice).U32(Inst).U8(Choice).Frame();
}

/// <summary>SPLIT: after the step of a fork; leaver mask = the side that moves to the child instance.</summary>
public sealed record SplitMsg(uint Inst, uint Visit, uint Tick, byte LeaverMask, SplitKind Kind)
{
    public static SplitMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new SplitMsg(r.U32(), r.U32(), r.U32(), Masks.Read(ref r), (SplitKind)r.U8());
        r.End();
        if (m.Kind is not (SplitKind.Exit or SplitKind.InStage)) throw new ProtocolException("unknown split kind");
        if (m.LeaverMask == 0) throw new ProtocolException("empty leaver mask");
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.Split).U32(Inst).U32(Visit).U32(Tick).U8(LeaverMask).U8((byte)Kind).Frame();
}

/// <summary>SNAPSHOT: a chunk of the visit's load snapshot (the leader uploads it after the load step).</summary>
public sealed record SnapshotMsg(uint Inst, uint Visit, uint Total, uint Offset, byte[] Chunk)
{
    public static SnapshotMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new SnapshotMsg(r.U32(), r.U32(), r.U32(), r.U32(), r.Rest().ToArray());
        if (m.Chunk.Length == 0 && m.Total != 0) throw new ProtocolException("empty snapshot chunk");
        if ((ulong)m.Offset + (ulong)m.Chunk.Length > m.Total) throw new ProtocolException("snapshot chunk beyond its total");
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.Snapshot, 16 + Chunk.Length).U32(Inst).U32(Visit).U32(Total).U32(Offset).Bytes(Chunk).Frame();
}

/// <summary>SNAPHASH: the canonical hash of the visit's load snapshot (every member, the leader included).</summary>
public sealed record SnapHashMsg(uint Inst, uint Visit, ulong Hash)
{
    public static SnapHashMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new SnapHashMsg(r.U32(), r.U32(), r.U64());
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.SnapHash).U32(Inst).U32(Visit).U64(Hash).Frame();
}

/// <summary>READY: a joiner caught up; tick = a multiple of 60 (from a checkpoint in a protocol 48 room: its check tick or a
/// later multiple of 60, <see cref="Wire.ReadyCheckTicks"/>) with its net fingerprint, its replay rate and its carry.</summary>
public sealed record ReadyMsg(uint Inst, uint Visit, uint Tick, ulong NetFp, uint Rate, byte[] Carry, byte Flags = 0)
{
    /// <summary>Round J: the engine was rebuilt from a checkpoint (such a READY never judges a vote, SEAMLESS.md 6.13.4).</summary>
    public const byte FromCheckpoint = 1;

    public static ReadyMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new ReadyMsg(r.U32(), r.U32(), r.U32(), r.U64(), r.U32(), r.Blob(BarrierMsg.MaxCarry));
        if (!r.AtEnd) m = m with { Flags = r.U8() };
        r.End();
        return m;
    }

    public byte[] Frame()
    {
        var w = new WireWriter(Msg.Ready, 33 + Carry.Length).U32(Inst).U32(Visit).U32(Tick).U64(NetFp).U32(Rate).Blob(Carry);
        if (Flags != 0) w.U8(Flags);
        return w.Frame();
    }
}

/// <summary>
/// STATEHASH (round J, SEAMLESS.md 6.13.4): a member's XXH64 of its canonical tick state at a checkpoint tick (the first
/// tick not in the state), with the visit's load end K its engine reached; flags bit 0: it keeps the state's bytes; bit 1
/// (round J's review): a solo leader's report of K alone, hash 0 (nobody to compare with, nothing computed).
/// </summary>
public sealed record StateHashMsg(uint Inst, uint Visit, uint Tick, ulong Hash, uint LoadEnd, byte Flags)
{
    public const byte Kept = 1;
    public const byte LoadEndOnly = 2;

    public static StateHashMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new StateHashMsg(r.U32(), r.U32(), r.U32(), r.U64(), r.U32(), r.U8());
        r.End();
        if (m.LoadEnd > m.Tick) throw new ProtocolException("load end beyond the checkpoint");
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.StateHash).U32(Inst).U32(Visit).U32(Tick).U64(Hash).U32(LoadEnd).U8(Flags).Frame();
}

/// <summary>CHECKPOINT (round J): a chunk of the donor's tick state at tick T (codec byte + data), its hash and load end.</summary>
public sealed record CheckpointMsg(uint Inst, uint Visit, uint Tick, ulong Hash, uint LoadEnd, uint Total, uint Offset, byte[] Chunk)
{
    public const int MaxBytes = 8 * 1024 * 1024;

    public static CheckpointMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new CheckpointMsg(r.U32(), r.U32(), r.U32(), r.U64(), r.U32(), r.U32(), r.U32(), r.Rest().ToArray());
        if (m.Total == 0 || m.Total > MaxBytes) throw new ProtocolException("checkpoint size");
        if (m.Chunk.Length == 0 || m.Chunk.Length > Wire.MaxChunkBytes) throw new ProtocolException("checkpoint chunk size");
        if ((ulong)m.Offset + (ulong)m.Chunk.Length > m.Total) throw new ProtocolException("checkpoint chunk beyond its total");
        if (m.LoadEnd > m.Tick) throw new ProtocolException("load end beyond the checkpoint");
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.Checkpoint, 36 + Chunk.Length).U32(Inst).U32(Visit).U32(Tick).U64(Hash).U32(LoadEnd).U32(Total)
        .U32(Offset).Bytes(Chunk).Frame();
}

/// <summary>Why a rebuild from a checkpoint failed (CHECKPOINT_BAD).</summary>
public enum CheckpointBadReason : byte
{
    Version = 1,
    Decode = 2,
    LoadEnd = 3,
    Topology = 4,
    Hash = 5,
}

/// <summary>CHECKPOINT_BAD (round J): a joiner's rebuild failed at the section named; the relay falls back.</summary>
public sealed record CheckpointBadMsg(uint Inst, uint Visit, uint Tick, CheckpointBadReason Reason, string Section)
{
    public static CheckpointBadMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new CheckpointBadMsg(r.U32(), r.U32(), r.U32(), (CheckpointBadReason)r.U8(), r.Str(64));
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.CheckpointBad).U32(Inst).U32(Visit).U32(Tick).U8((byte)Reason).Str(Section).Frame();
}

/// <summary>CHECKPOINT_ASK (round J): the donor uploads its tick state (tick 0: at its next state-safe tick).</summary>
public sealed record CheckpointAskMsg(uint Inst, uint Visit, uint Tick, byte Flags)
{
    public const byte Periodic = 1;
    public const byte Diagnosis = 2;

    public static CheckpointAskMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new CheckpointAskMsg(r.U32(), r.U32(), r.U32(), r.U8());
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.CheckpointAsk).U32(Inst).U32(Visit).U32(Tick).U8(Flags).Frame();
}

/// <summary>JOIN_AT: the solo leader's answer to JOIN_REQ: from tick J the instance is sealed and the JOIN applies.</summary>
public sealed record JoinAtMsg(uint Inst, uint Visit, uint Tick)
{
    public static JoinAtMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new JoinAtMsg(r.U32(), r.U32(), r.U32());
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.JoinAt).U32(Inst).U32(Visit).U32(Tick).Frame();
}

/// <summary>NETFP: the net fingerprint after tick (every 60 ticks, every member; protocol 48: also the check tick of the
/// member's own checkpoint upload, <see cref="Wire.ReadyCheckTicks"/>).</summary>
public sealed record NetFpMsg(uint Inst, uint Visit, uint Tick, ulong Hash)
{
    public static NetFpMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new NetFpMsg(r.U32(), r.U32(), r.U32(), r.U64());
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.NetFp).U32(Inst).U32(Visit).U32(Tick).U64(Hash).Frame();
}

/// <summary>STATE: the instance's joinable / membership-safe / boss lock / load end flags and the session context hash, on
/// change.</summary>
public sealed record StateMsg(uint Inst, uint Visit, uint Tick, byte Flags, ulong ContextHash, StateReason Reason)
{
    public const byte Joinable = 1;
    public const byte MembershipSafe = 2;
    public const byte BossLock = 4;
    /// <summary>Protocol 47 (round J's review): the visit reached its load end K; the first such STATE's context is the
    /// visit's entry context (SEAMLESS.md 3.7), joinable or not.</summary>
    public const byte LoadEnded = 8;

    public static StateMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new StateMsg(r.U32(), r.U32(), r.U32(), r.U8(), r.U64(), (StateReason)r.U8());
        r.End();
        if (m.Flags > 15) throw new ProtocolException("unknown state flags");
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.State).U32(Inst).U32(Visit).U32(Tick).U8(Flags).U64(ContextHash).U8((byte)Reason).Frame();
}

/// <summary>INTENT: a member's port choice (go without them, give up).</summary>
public sealed record IntentMsg(uint Inst, uint Visit, IntentKind Kind, int Argument)
{
    public static IntentMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new IntentMsg(r.U32(), r.U32(), (IntentKind)r.U8(), r.I32());
        r.End();
        if (m.Kind is not (IntentKind.GoWithout or IntentKind.GiveUp)) throw new ProtocolException("unknown intent");
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.Intent).U32(Inst).U32(Visit).U8((byte)Kind).I32(Argument).Frame();
}

/// <summary>PRESENCE (both directions; the server adds the member index in front).</summary>
public sealed record PresenceMsg(string Stage, uint Inst, float X, float Y, float Z, float Yaw, byte Species, ushort Level, ushort Hp, ushort HpMax,
    ushort Mp, ushort MpMax)
{
    public static PresenceMsg Read(ref WireReader r) =>
        new(StageNames.Read(ref r, allowEmpty: true), r.U32(), r.F32(), r.F32(), r.F32(), r.F32(), r.U8(), r.U16(), r.U16(), r.U16(), r.U16(), r.U16());

    public static PresenceMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = Read(ref r);
        r.End();
        if (!float.IsFinite(m.X) || !float.IsFinite(m.Y) || !float.IsFinite(m.Z) || !float.IsFinite(m.Yaw)) throw new ProtocolException("presence position is not finite");
        return m;
    }

    public void Write(WireWriter w) => w.Str(Stage).U32(Inst).F32(X).F32(Y).F32(Z).F32(Yaw).U8(Species).U16(Level).U16(Hp).U16(HpMax).U16(Mp).U16(MpMax);

    public byte[] Frame()
    {
        var w = new WireWriter(Msg.Presence);
        Write(w);
        return w.Frame();
    }

    /// <summary>Plausible bounds of the hub (SEAMLESS.md 12): coordinates within this of the stage origin.</summary>
    public const float MaxCoordinate = 100000f;
    /// <summary>Two turns either way (the client sends atan2's -pi..pi).</summary>
    public const float MaxYaw = 4f * MathF.PI;
    /// <summary>The player forms of the game ("shinka" rows 0-15).</summary>
    public const byte MaxSpecies = 15;
    public const ushort MaxLevel = 999;

    /// <summary>
    /// Null when the hub forwards it (PRESENCE and PRESENCE2 alike, round 60: a PRESENCE drives a ghost figure too); else
    /// why not (the frame is dropped and counted, the connection stays). HP and MP above their maximum are no reason: a
    /// script may raise them above it (MDLaddHp), <see cref="Clamped"/> forwards them as the maximum.
    /// </summary>
    public string? Implausible()
    {
        if (Species > MaxSpecies) return "species";
        if (MathF.Abs(X) > MaxCoordinate || MathF.Abs(Y) > MaxCoordinate || MathF.Abs(Z) > MaxCoordinate) return "position";
        if (MathF.Abs(Yaw) > MaxYaw) return "yaw";
        if (Level > MaxLevel) return "stats";
        return null;
    }

    /// <summary>HP and MP above a known maximum (not 0) as that maximum.</summary>
    public PresenceMsg Clamped() =>
        (HpMax > 0 && Hp > HpMax) || (MpMax > 0 && Mp > MpMax)
            ? this with { Hp = HpMax > 0 ? Math.Min(Hp, HpMax) : Hp, Mp = MpMax > 0 ? Math.Min(Mp, MpMax) : Mp }
            : this;
}

/// <summary>
/// PRESENCE2 (round 60: the ghost figures of the hub stages, SEAMLESS.md 12 and the DW4R2 appendix): u8 version | the
/// PRESENCE fields | str clip (0-16 of A-Z a-z 0-9 _) | u8 flags (1 the clip loops, 2 busy) | u8 warp | u32 the sender's
/// clock in ms | str weapon (0-24, the same characters). Only from members whose WELCOME carried
/// <see cref="WelcomeMsg.PresenceV2"/>; the hub forwards it as PRESENCE2 to the members that send it and as PRESENCE to
/// the others. A later version appends to this layout and stays within <see cref="MaxBytes"/>: the hub reads and checks
/// its version 1 part and forwards only that, written at version 1 with the version 1 flags (counted), so an older hub
/// never forwards what it cannot check; a larger frame is dropped unread (counted).
/// </summary>
public sealed record Presence2Msg(byte Version, PresenceMsg Presence, string Clip, byte Flags, byte Warp, uint SentMs, string Weapon)
{
    public const byte CurrentVersion = 1;
    public const int MaxClip = 16;
    public const int MaxWeapon = 24;
    public const byte Loop = 1;
    public const byte Busy = 2;
    public const byte KnownFlags = Loop | Busy;
    /// <summary>The largest PRESENCE2 payload of any version (version 1 takes at most 105 bytes).</summary>
    public const int MaxBytes = 256;

    public static Presence2Msg Read(ref WireReader r)
    {
        var version = r.U8();
        if (version == 0) throw new ProtocolException("presence version 0");
        var presence = PresenceMsg.Read(ref r);
        var clip = Name(ref r, MaxClip, "clip");
        var flags = r.U8();
        var warp = r.U8();
        var sent = r.U32();
        var weapon = Name(ref r, MaxWeapon, "weapon");
        return new Presence2Msg(version, presence, clip, flags, warp, sent, weapon);
    }

    /// <summary>A malformed frame throws (the connection closes); a later version's appended bytes are skipped.</summary>
    public static Presence2Msg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = Read(ref r);
        if (m.Version == CurrentVersion) r.End();
        var q = m.Presence;
        if (!float.IsFinite(q.X) || !float.IsFinite(q.Y) || !float.IsFinite(q.Z) || !float.IsFinite(q.Yaw)) throw new ProtocolException("presence position is not finite");
        return m;
    }

    /// <summary>Null when the hub forwards it; else why not (the frame is dropped and counted, the connection stays).</summary>
    public string? Implausible() => Presence.Implausible() ?? ((Flags & ~KnownFlags) != 0 ? "flags" : null);

    public void Write(WireWriter w)
    {
        w.U8(CurrentVersion);
        Presence.Write(w);
        w.Str(Clip).U8(Flags).U8(Warp).U32(SentMs).Str(Weapon);
    }

    public byte[] Frame()
    {
        var w = new WireWriter(Msg.Presence2);
        Write(w);
        return w.Frame();
    }

    private static string Name(ref WireReader r, int max, string what)
    {
        var s = r.Str(max);
        if (!s.All(c => char.IsAsciiLetterOrDigit(c) || c == '_')) throw new ProtocolException("bad presence " + what);
        return s;
    }
}

/// <summary>RESUME: after a reconnect: the instance, slot and the last tick this machine executed.</summary>
public sealed record ResumeMsg(uint Inst, uint Visit, byte Slot, uint LastTick)
{
    public static ResumeMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new ResumeMsg(r.U32(), r.U32(), r.U8(), r.U32());
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.Resume).U32(Inst).U32(Visit).U8(Slot).U32(LastTick).Frame();
}

/// <summary>RATE: the replay rate in steps per second (calibration at the room join and after every replay).</summary>
public sealed record RateMsg(uint StepsPerSecond)
{
    public static RateMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new RateMsg(r.U32());
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.Rate).U32(StepsPerSecond).Frame();
}

public sealed record PongMsg(uint Nonce, ulong ClientTimeMs)
{
    public static PongMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new PongMsg(r.U32(), r.U64());
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.Pong).U32(Nonce).U64(ClientTimeMs).Frame();
}

public sealed record ByeMsg(byte Reason)
{
    public static ByeMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new ByeMsg(r.U8());
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.Bye).U8(Reason).Frame();
}

// Server to client -----------------------------------------------------------------------------------------------------

/// <summary>
/// WELCOME: the first frame after "OK": the member's room index and the instance the server has it in (a fresh solo hub
/// instance, or after a reconnect within the window the instance and slot it still holds, with the last sealed tick).
/// </summary>
public sealed record WelcomeMsg(byte Member, uint Inst, uint Visit, byte Slot, InstanceMode Mode, string Stage, uint LastTick, byte Delay,
    ushort MaxCatchUpSeconds, byte Flags)
{
    public const byte Resumable = 1;
    /// <summary>This hub takes PRESENCE2 (round 60). A client sends it only then: an older hub closes on an unknown frame.</summary>
    public const byte PresenceV2 = 2;
    /// <summary>This hub takes STATEHASH, CHECKPOINT and CHECKPOINT_BAD (round J). A client sends them only then: an older hub
    /// closes on an unknown frame.</summary>
    public const byte Checkpoints = 4;
    public const uint NoTick = uint.MaxValue;

    public static WelcomeMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new WelcomeMsg(r.U8(), r.U32(), r.U32(), r.U8(), (InstanceMode)r.U8(), StageNames.Read(ref r, allowEmpty: true), r.U32(), r.U8(), r.U16(), r.U8());
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.Welcome).U8(Member).U32(Inst).U32(Visit).U8(Slot).U8((byte)Mode).Str(Stage).U32(LastTick)
        .U8(Delay).U16(MaxCatchUpSeconds).U8(Flags).Frame();
}

/// <summary>SEALED / LIVE: ticks first..first+count-1 of an instance's stream (batched when behind).</summary>
public sealed record TicksMsg(byte Type, uint Inst, uint Visit, uint FirstTick, IReadOnlyList<TickRecord> Ticks)
{
    public static TicksMsg Parse(byte type, ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var inst = r.U32();
        var visit = r.U32();
        var first = r.U32();
        var count = r.U16();
        var ticks = new List<TickRecord>(count);
        for (var i = 0; i < count; ++i) ticks.Add(TickRecord.Read(ref r));
        r.End();
        return new TicksMsg(type, inst, visit, first, ticks);
    }

    /// <summary>A SEALED or LIVE frame from already encoded tick records.</summary>
    public static byte[] Frame(byte type, uint inst, uint visit, uint first, IReadOnlyList<byte[]> records)
    {
        var w = new WireWriter(type, 14 + records.Sum(x => x.Length)).U32(inst).U32(visit).U32(first).U16((ushort)records.Count);
        foreach (var rec in records) w.Bytes(rec);
        return w.Frame();
    }
}

/// <summary>JOIN_REQ: to the leader of a solo-led instance: choose the tick J from which the instance is sealed.</summary>
public sealed record JoinReqMsg(uint Inst, uint Visit, byte Slot, byte Delay)
{
    public static JoinReqMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new JoinReqMsg(r.U32(), r.U32(), r.U8(), r.U8());
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.JoinReq).U32(Inst).U32(Visit).U8(Slot).U8(Delay).Frame();
}

/// <summary>
/// STORY (SEAMLESS.md 8.3): a member's pending set against the confirmed room story, at every barrier (before BARRIER) and
/// while it is not empty. inst, visit (the member's instance now), the confirmed revision, phase u8 (0: no claim), u16 n +
/// n x u16 flags, u8 n + n x u8 keys, u8 n + n x (u8 counter, u32 line inst, u32 line visit, i32 growth).
/// </summary>
public sealed record StoryMsg(uint Inst, uint Visit, uint Revision, StoryDelta Delta)
{
    public const int MaxLines = 32;

    public static StoryMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var inst = r.U32();
        var visit = r.U32();
        var revision = r.U32();
        var d = new StoryDelta { Phase = r.U8() };
        var nf = r.U16();
        if (nf > StorySets.FlagCount) throw new ProtocolException("story: too many flags");
        for (var i = 0; i < nf; ++i)
        {
            var f = r.U16();
            if (f >= StorySets.FlagCount) throw new ProtocolException("story: flag out of range");
            d.Flags.Add(f);
        }
        var nk = r.U8();
        if (nk > StorySets.FolderCount) throw new ProtocolException("story: too many keys");
        for (var i = 0; i < nk; ++i)
        {
            var k = r.U8();
            if (k >= StorySets.FolderCount) throw new ProtocolException("story: key out of range");
            d.Folder.Add(k);
        }
        var nc = r.U8();
        if (nc > MaxLines) throw new ProtocolException("story: too many counter lines");
        for (var i = 0; i < nc; ++i)
        {
            var counter = r.U8();
            if (counter >= StorySets.CounterCount) throw new ProtocolException("story: counter out of range");
            d.Counters.Add(new CounterGrowth(counter, r.U32(), r.U32(), r.I32()));
        }
        r.End();
        return new StoryMsg(inst, visit, revision, d);
    }

    public byte[] Frame()
    {
        var w = new WireWriter(Msg.Story, 16 + 2 * Delta.Flags.Count + Delta.Folder.Count + 13 * Delta.Counters.Count)
            .U32(Inst).U32(Visit).U32(Revision).U8(Delta.Phase).U16((ushort)Delta.Flags.Count);
        foreach (var f in Delta.Flags) w.U16((ushort)f);
        w.U8((byte)Delta.Folder.Count);
        foreach (var k in Delta.Folder) w.U8((byte)k);
        w.U8((byte)Delta.Counters.Count);
        foreach (var g in Delta.Counters) w.U8((byte)g.Counter).U32(g.Inst).U32(g.Visit).I32(g.Growth);
        return w.Frame();
    }
}

/// <summary>
/// STORY_ACK: the room story's revision and blob (1,100 bytes; empty: none), flags (1: the answer to this member's STORY)
/// and the refused keys (kind u8, index u16, reason u8). After WELCOME, to every member on each new revision, and to a
/// STORY's sender.
/// </summary>
public sealed record StoryAckMsg(uint Revision, byte[] Story, byte Flags, IReadOnlyList<StoryRefused> Refused)
{
    public const byte Answer = 1;

    public static StoryAckMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var revision = r.U32();
        var story = r.Blob(RoomStoryData.BlobBytes);
        if (story.Length != 0 && story.Length != RoomStoryData.BlobBytes) throw new ProtocolException("story_ack: story blob size");
        var flags = r.U8();
        var n = r.U16();
        var refused = new List<StoryRefused>(n);
        for (var i = 0; i < n; ++i)
        {
            var kind = r.U8();
            if (kind > 3) throw new ProtocolException("story_ack: unknown key kind");
            refused.Add(new StoryRefused((StoryKeyKind)kind, r.U16(), (StoryRefusal)r.U8()));
        }
        r.End();
        return new StoryAckMsg(revision, story, flags, refused);
    }

    public byte[] Frame()
    {
        var w = new WireWriter(Msg.StoryAck, 16 + Story.Length + 4 * Refused.Count).U32(Revision).Blob(Story).U8(Flags).U16((ushort)Refused.Count);
        foreach (var x in Refused) w.U8((byte)x.Kind).U16((ushort)x.Index).U8((byte)x.Reason);
        return w.Frame();
    }
}

/// <summary>A co-arrival in the base's BARRIER_OK: its slot in the new instance, its room member index and its carry.</summary>
public sealed record CoArrival(byte Slot, byte Member, byte[] Carry);

/// <summary>
/// BARRIER_OK: the server's decision for a parked member. SlotMap[old slot] = the new slot of that engine player or -1
/// (NEW: the group's engine continues with these slots; DISSOLVE: only the own player stays, at slot 0). CoArrivals: the
/// players the base engine spawns in its load step. Story: the room story's 1,100-byte blob (the same bytes for every member
/// of the group; empty: none) and its revision; Dropped: the room's dropped set (StoryKeys blob, SEAMLESS.md 8.6). Notice and
/// names say why a NEW opened next to an instance of the same stage.
/// </summary>
public sealed record BarrierOkMsg(uint Inst, uint Visit, BarrierDecision Decision, uint NewInst, uint NewVisit, string Stage, byte Point, byte OwnSlot,
    sbyte[] SlotMap, InstanceMode Mode, byte Delay, IReadOnlyList<CoArrival> CoArrivals, uint StoryRevision, byte[] Story, byte[] Dropped,
    BarrierNotice Notice = BarrierNotice.None, IReadOnlyList<string>? NoticeNames = null)
{
    public static BarrierOkMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var inst = r.U32();
        var visit = r.U32();
        var decision = (BarrierDecision)r.U8();
        var newInst = r.U32();
        var newVisit = r.U32();
        var stage = StageNames.Read(ref r, allowEmpty: true);
        var point = r.U8();
        var own = r.U8();
        var map = new sbyte[Wire.MaxSlots];
        for (var i = 0; i < map.Length; ++i) map[i] = r.I8();
        var mode = (InstanceMode)r.U8();
        var delay = r.U8();
        var n = r.U8();
        var co = new List<CoArrival>(n);
        for (var i = 0; i < n; ++i) co.Add(new CoArrival(r.U8(), r.U8(), r.Blob(BarrierMsg.MaxCarry)));
        var revision = r.U32();
        var story = r.Blob(1 << 16);
        var dropped = r.Blob(1 << 16);
        var notice = (BarrierNotice)r.U8();
        var nn = r.U8();
        var names = new List<string>(nn);
        for (var i = 0; i < nn; ++i) names.Add(r.Str(64));
        r.End();
        return new BarrierOkMsg(inst, visit, decision, newInst, newVisit, stage, point, own, map, mode, delay, co, revision, story, dropped, notice, names);
    }

    public byte[] Frame()
    {
        var w = new WireWriter(Msg.BarrierOk, 64 + CoArrivals.Sum(c => c.Carry.Length + 6) + Story.Length + Dropped.Length)
            .U32(Inst).U32(Visit).U8((byte)Decision).U32(NewInst).U32(NewVisit).Str(Stage).U8(Point).U8(OwnSlot);
        foreach (var s in SlotMap) w.I8(s);
        w.U8((byte)Mode).U8(Delay).U8((byte)CoArrivals.Count);
        foreach (var c in CoArrivals) w.U8(c.Slot).U8(c.Member).Blob(c.Carry);
        w.U32(StoryRevision).Blob(Story).Blob(Dropped).U8((byte)Notice);
        var names = NoticeNames ?? [];
        w.U8((byte)names.Count);
        foreach (var n in names) w.Str(n);
        return w.Frame();
    }
}

/// <summary>
/// REASSIGN: after a fork the member's copy continues as another instance (the side that does not keep the id): from
/// tick FromTick its stream comes with NewInst. Flags bit 0 = departing (an exit fork's child), bit 1 = joinable.
/// </summary>
public sealed record ReassignMsg(uint OldInst, uint Visit, uint NewInst, uint FromTick, InstanceMode Mode, byte Flags)
{
    public const byte Departing = 1;

    public static ReassignMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new ReassignMsg(r.U32(), r.U32(), r.U32(), r.U32(), (InstanceMode)r.U8(), r.U8());
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.Reassign).U32(OldInst).U32(Visit).U32(NewInst).U32(FromTick).U8((byte)Mode).U8(Flags).Frame();
}

/// <summary>BARRIER_WAIT: a held barrier (loading screen text); flag 1 = the member must choose wait or alone (boss lock).</summary>
public sealed record BarrierWaitMsg(uint Inst, WaitReason Reason, ushort SecondsLeft, IReadOnlyList<string> Names, byte Flags)
{
    public const byte ChoiceNeeded = 1;

    public static BarrierWaitMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var inst = r.U32();
        var reason = (WaitReason)r.U8();
        var seconds = r.U16();
        var n = r.U8();
        var names = new List<string>(n);
        for (var i = 0; i < n; ++i) names.Add(r.Str(64));
        var m = new BarrierWaitMsg(inst, reason, seconds, names, r.U8());
        r.End();
        return m;
    }

    public byte[] Frame()
    {
        var w = new WireWriter(Msg.BarrierWait).U32(Inst).U8((byte)Reason).U16(SecondsLeft).U8((byte)Names.Count);
        foreach (var n in Names) w.Str(n);
        return w.U8(Flags).Frame();
    }
}

/// <summary>STATE_DATA: a chunk of a state blob (<see cref="StateBlob"/>) for a join, co-arrival, resync or resume.</summary>
public sealed record StateDataMsg(uint Inst, uint Visit, uint Total, uint Offset, byte[] Chunk)
{
    public static StateDataMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new StateDataMsg(r.U32(), r.U32(), r.U32(), r.U32(), r.Rest().ToArray());
        if ((ulong)m.Offset + (ulong)m.Chunk.Length > m.Total) throw new ProtocolException("state chunk beyond its total");
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.StateData, 16 + Chunk.Length).U32(Inst).U32(Visit).U32(Total).U32(Offset).Bytes(Chunk).Frame();
}

/// <summary>A fork in an instance's history since its snapshot: the tick, the leaver mask, the kind and the side (0 stayers, 1 leavers) that leads to the instance.</summary>
public sealed record ForkSide(uint Tick, byte LeaverMask, SplitKind Kind, byte Side)
{
    public const byte Stay = 0;
    public const byte Leave = 1;
}

/// <summary>
/// The reassembled STATE_DATA: purpose u8, slot u8 (the slot the receiver gets or holds), codec u8 (0 = raw tick records,
/// 2 = with a checkpoint), snapshot (u32 length + the bytes the leader uploaded), fork count u8 + forks (tick u32, leaver
/// mask u8, kind u8, side u8); codec 2 then: the load end K u32 + the records [0, K), the checkpoint tick T u32 (the first
/// tick not in the state), its hash u64, flags u8 (1 agreed), its bytes (u32 length + the donor's upload); then first tick
/// u32, tick count u32 and the tick records. LIVE frames continue the stream after the last tick.
/// </summary>
public sealed record StateBlob(StatePurpose Purpose, byte Slot, byte[] Snapshot, IReadOnlyList<ForkSide> Forks, uint FirstTick, IReadOnlyList<TickRecord> Ticks)
{
    public const int MaxSnapshot = 4 * 1024 * 1024;
    public const byte CheckpointAgreed = 1;
    public IReadOnlyList<TickRecord> LoadTicks { get; init; } = [];
    public uint CheckpointTick { get; init; }
    public ulong CheckpointHash { get; init; }
    public byte CheckpointFlags { get; init; }
    public byte[]? Checkpoint { get; init; }

    /// <summary>Codec 2: the load ticks [0, K), the checkpoint of tick T and the records from T on.</summary>
    public static byte[] BuildWithCheckpoint(StatePurpose purpose, byte slot, ReadOnlySpan<byte> snapshot, IReadOnlyList<ForkSide> forks,
        IReadOnlyList<byte[]> loadRecords, uint checkpointTick, ulong checkpointHash, byte checkpointFlags, ReadOnlySpan<byte> checkpoint,
        IReadOnlyList<byte[]> records)
    {
        var w = new WireWriter(0, 40 + snapshot.Length + checkpoint.Length + forks.Count * 7 + loadRecords.Sum(x => x.Length) + records.Sum(x => x.Length))
            .U8((byte)purpose).U8(slot).U8(2).Blob(snapshot).U8((byte)forks.Count);
        foreach (var f in forks) w.U32(f.Tick).U8(f.LeaverMask).U8((byte)f.Kind).U8(f.Side);
        w.U32((uint)loadRecords.Count);
        foreach (var rec in loadRecords) w.Bytes(rec);
        w.U32(checkpointTick).U64(checkpointHash).U8(checkpointFlags).Blob(checkpoint);
        w.U32(checkpointTick).U32((uint)records.Count);
        foreach (var rec in records) w.Bytes(rec);
        return w.Frame()[Wire.HeaderBytes..];
    }

    public static byte[] Build(StatePurpose purpose, byte slot, ReadOnlySpan<byte> snapshot, IReadOnlyList<ForkSide> forks, uint firstTick, IReadOnlyList<byte[]> records)
    {
        var w = new WireWriter(0, 16 + snapshot.Length + forks.Count * 7 + records.Sum(x => x.Length))
            .U8((byte)purpose).U8(slot).U8(0).Blob(snapshot).U8((byte)forks.Count);
        foreach (var f in forks) w.U32(f.Tick).U8(f.LeaverMask).U8((byte)f.Kind).U8(f.Side);
        w.U32(firstTick).U32((uint)records.Count);
        foreach (var rec in records) w.Bytes(rec);
        return w.Frame()[Wire.HeaderBytes..];
    }

    public static StateBlob Parse(ReadOnlySpan<byte> blob)
    {
        var r = new WireReader(blob);
        var purpose = (StatePurpose)r.U8();
        var slot = r.U8();
        var codec = r.U8();
        if (codec != 0 && codec != 2) throw new ProtocolException("unknown state codec");
        var snapshot = r.Blob(MaxSnapshot);
        var nf = r.U8();
        var forks = new List<ForkSide>(nf);
        for (var i = 0; i < nf; ++i) forks.Add(new ForkSide(r.U32(), r.U8(), (SplitKind)r.U8(), r.U8()));
        var load = new List<TickRecord>();
        uint cpTick = 0;
        ulong cpHash = 0;
        byte cpFlags = 0;
        byte[]? cp = null;
        if (codec == 2)
        {
            var k = r.U32();
            for (var i = 0u; i < k; ++i) load.Add(TickRecord.Read(ref r));
            cpTick = r.U32();
            cpHash = r.U64();
            cpFlags = r.U8();
            cp = r.Blob(CheckpointMsg.MaxBytes);
        }
        var first = r.U32();
        var count = r.U32();
        var ticks = new List<TickRecord>((int)Math.Min(count, 1_000_000));
        for (var i = 0u; i < count; ++i) ticks.Add(TickRecord.Read(ref r));
        r.End();
        return new StateBlob(purpose, slot, snapshot, forks, first, ticks)
        {
            LoadTicks = load, CheckpointTick = cpTick, CheckpointHash = cpHash, CheckpointFlags = cpFlags, Checkpoint = cp,
        };
    }
}

public sealed record PresenceOutMsg(byte Member, PresenceMsg Presence)
{
    public static PresenceOutMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var member = r.U8();
        var m = new PresenceOutMsg(member, PresenceMsg.Read(ref r));
        r.End();
        return m;
    }

    public byte[] Frame()
    {
        var w = new WireWriter(Msg.PresenceOut).U8(Member);
        Presence.Write(w);
        return w.Frame();
    }
}

/// <summary>PRESENCE2 to a member that sends PRESENCE2 itself: the member index, then the sender's PRESENCE2 at version 1.</summary>
public sealed record Presence2OutMsg(byte Member, Presence2Msg Presence)
{
    public static Presence2OutMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var member = r.U8();
        var m = new Presence2OutMsg(member, Presence2Msg.Read(ref r));
        if (m.Presence.Version == Presence2Msg.CurrentVersion) r.End();
        return m;
    }

    public byte[] Frame()
    {
        var w = new WireWriter(Msg.Presence2Out).U8(Member);
        Presence.Write(w);
        return w.Frame();
    }
}

public sealed record ResyncMsg(uint Inst, uint Visit)
{
    public static ResyncMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new ResyncMsg(r.U32(), r.U32());
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.Resync).U32(Inst).U32(Visit).Frame();
}

/// <summary>ROOM: an in-band room change (flags bit 0 = the member is the room owner).</summary>
public sealed record RoomMsg(RoomNotice Kind, byte Member, ulong AccountId, string Name, byte Flags)
{
    public const byte Owner = 1;

    public static RoomMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new RoomMsg((RoomNotice)r.U8(), r.U8(), r.U64(), r.Str(64), r.U8());
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.Room).U8((byte)Kind).U8(Member).U64(AccountId).Str(Name).U8(Flags).Frame();
}

/// <summary>PING: once per second; margin = the member's recent 0.1 % arrival margin in ms (sealed), delay = the suggested input delay.</summary>
public sealed record PingMsg(uint Nonce, ulong ServerTimeMs, short MarginMs, byte Delay)
{
    public static PingMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new PingMsg(r.U32(), r.U64(), r.I16(), r.U8());
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.Ping).U32(Nonce).U64(ServerTimeMs).I16(MarginMs).U8(Delay).Frame();
}

/// <summary>
/// NETSTAT (round 62), once per second after PING: the member's link as the relay sees it, for its network indicator and
/// online.log. Round trip median and spread (p90 above it, host-loop stalls left out, ms), the input delay, the arrival
/// margin (the delay rule's low quantile, ms), and the pads of its current sealed stream: sealed, filled (its pad was not
/// there in time) and late (came after its tick was sealed). Flags: 1 = in a sealed stream. Clients before round 62 skip
/// it (unknown server frames are skipped), so no protocol change.
/// </summary>
public sealed record NetStatMsg(ushort RttMs, ushort SpreadMs, byte Delay, short MarginMs, uint Sealed, uint Filled, uint Late, byte Flags)
{
    public const byte InStream = 1;

    public static NetStatMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new NetStatMsg(r.U16(), r.U16(), r.U8(), r.I16(), r.U32(), r.U32(), r.U32(), r.U8());
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.NetStat).U16(RttMs).U16(SpreadMs).U8(Delay).I16(MarginMs).U32(Sealed).U32(Filled).U32(Late).U8(Flags).Frame();
}

public sealed record CloseMsg(CloseReason Reason, string Text)
{
    public static CloseMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new CloseMsg((CloseReason)r.U8(), r.Str());
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.Close).U8((byte)Reason).Str(Text.Length > 200 ? Text[..200] : Text).Frame();
}

/// <summary>A roster line: member, stage, instance, level, HP, max HP, flags (1 connected, 2 lagging, 4 owner).</summary>
public sealed record RosterEntry(byte Member, string Stage, uint Inst, ushort Level, ushort Hp, ushort HpMax, byte Flags)
{
    public const byte Connected = 1;
    public const byte Lagging = 2;
    public const byte Owner = 4;
}

/// <summary>ROSTER: once per second, every member of the room (the room UI).</summary>
public sealed record RosterMsg(IReadOnlyList<RosterEntry> Entries)
{
    public static RosterMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var n = r.U8();
        var list = new List<RosterEntry>(n);
        for (var i = 0; i < n; ++i) list.Add(new RosterEntry(r.U8(), StageNames.Read(ref r, allowEmpty: true), r.U32(), r.U16(), r.U16(), r.U16(), r.U8()));
        r.End();
        return new RosterMsg(list);
    }

    public byte[] Frame()
    {
        var w = new WireWriter(Msg.Roster).U8((byte)Entries.Count);
        foreach (var e in Entries) w.U8(e.Member).Str(e.Stage).U32(e.Inst).U16(e.Level).U16(e.Hp).U16(e.HpMax).U8(e.Flags);
        return w.Frame();
    }
}

/// <summary>Stage names: 1-24 of A-Z, a-z, 0-9, '_' (empty only where the title / no stage is allowed).</summary>
public static class StageNames
{
    public static bool Valid(string s, bool allowEmpty = false) =>
        (s.Length == 0 ? allowEmpty : s.Length <= Wire.MaxStageName) && s.All(c => char.IsAsciiLetterOrDigit(c) || c == '_');

    public static string Read(ref WireReader r, bool allowEmpty = false)
    {
        var s = r.Str(Wire.MaxStageName);
        if (!Valid(s, allowEmpty)) throw new ProtocolException("bad stage name");
        return s;
    }

    /// <summary>
    /// Hub and UI stages (SEAMLESS.md 2): stagML02, stagML11-14 and every stagIS* (title, character select, save room,
    /// call shop, staff roll, the menu stages); the title before the first stage ("") counts as one.
    /// </summary>
    public static bool IsHub(string stage) =>
        stage.Length == 0 ||
        stage.StartsWith("stagIS", StringComparison.OrdinalIgnoreCase) ||
        string.Equals(stage, "stagML02", StringComparison.OrdinalIgnoreCase) ||
        (stage.Length == 8 && stage.StartsWith("stagML1", StringComparison.OrdinalIgnoreCase) && stage[7] is >= '1' and <= '4');
}

internal static class Masks
{
    public static byte Read(ref WireReader r)
    {
        var m = r.U8();
        if ((m & ~Wire.SlotMask) != 0) throw new ProtocolException("slot mask above slot 3");
        return m;
    }
}
