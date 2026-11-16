using DW4Online.Domain.Rooms;

namespace DW4Online.Relay.Protocol;

// Round I1's messages (protocol 49, SEAMLESS.md 19.18; the layouts agreed in i1a-contract.md, "client 12:40" and "relay 13:05").

/// <summary>
/// The 11-byte settings block of BARRIER, BARRIER_OK and INSTANCE_INFO: difficulty u8 (0-2) | camera u8 (0-1) | light u8 (0-1) |
/// fast loading u8 (0/1) | randomizer seed u32 | drops u8 (0/1) | enemies u8 (0-2) | zones u8 (0-2). Its hash is
/// <see cref="RoomSettings.Hash"/> with protocol 49 and the instance's data hash.
/// </summary>
public static class SettingsBlock
{
    public static RoomSettings Read(ref WireReader r)
    {
        var difficulty = r.U8();
        var camera = r.U8();
        var light = r.U8();
        var fast = r.U8();
        var seed = r.U32();
        var drops = r.U8();
        var enemies = r.U8();
        var zones = r.U8();
        if (difficulty > 2 || camera > 1 || light > 1 || fast > 1 || drops > 1 || enemies > 2 || zones > 2)
            throw new ProtocolException("settings out of range");
        return new RoomSettings(difficulty, camera, light, seed, drops != 0, enemies, zones, fast != 0);
    }

    public static void Write(WireWriter w, RoomSettings s) =>
        w.U8((byte)Math.Clamp(s.Difficulty, 0, 2)).U8((byte)Math.Clamp(s.CameraMode, 0, 1)).U8((byte)Math.Clamp(s.LightMode, 0, 1))
            .U8(s.FastLoading ? (byte)1 : (byte)0).U32(s.RandomizerSeed).U8(s.RandomizerDrops ? (byte)1 : (byte)0)
            .U8((byte)Math.Clamp(s.RandomizerEnemies, 0, 2)).U8((byte)Math.Clamp(s.RandomizerZones, 0, 2));
}

/// <summary>The relation flags of PRESENCE / PRESENCE2 out and MEMBER_INFO (protocol 49).</summary>
public static class Relation
{
    public const byte Friend = 1;
    /// <summary>Round I4; never set in I1a.</summary>
    public const byte Party = 2;
    /// <summary>A playing staff member (World:StaffTagWhenPlaying).</summary>
    public const byte Staff = 4;
    /// <summary>A member of the receiver's own instance.</summary>
    public const byte SameInstance = 8;
}

/// <summary>INSTANCE_OP (client, protocol 49): op u8 (1 visibility, 2 remove member, 3 end) | argument u32 (visibility 0-2, member id, 0).
/// It acts on the sender's own instance; staff on other instances act through the admin REST endpoints.</summary>
public sealed record InstanceOpMsg(InstanceOpKind Op, uint Argument)
{
    public static InstanceOpMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new InstanceOpMsg((InstanceOpKind)r.U8(), r.U32());
        r.End();
        if (m.Op is not (InstanceOpKind.Visibility or InstanceOpKind.Remove or InstanceOpKind.End)) throw new ProtocolException("unknown instance op");
        if (m.Op == InstanceOpKind.Visibility && m.Argument > 2) throw new ProtocolException("unknown visibility");
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.InstanceOp).U8((byte)Op).U32(Argument).Frame();
}

/// <summary>
/// REPORT (client, protocol 50, round I1b): member u32 (the reported member) | reason u8 (1 griefing, 2 cheating, 3 name, 4 other)
/// | wid u32 (the instance the reporter shares with him; 0: none). The relay answers with WORLD_NOTICE 14 (<see cref="ReportResult"/>).
/// </summary>
public sealed record ReportMsg(uint Member, byte Reason, uint Wid)
{
    public static ReportMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new ReportMsg(r.U32(), r.U8(), r.U32());
        r.End();
        if (m.Reason is < 1 or > 4) throw new ProtocolException("unknown report reason");
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.Report).U32(Member).U8(Reason).U32(Wid).Frame();
}

/// <summary>A member row of INSTANCE_INFO: member u32 | stage str | level u16 | flags u8.</summary>
public sealed record InstanceInfoMember(uint Member, string Stage, ushort Level, byte Flags)
{
    public const byte Connected = 1;
    public const byte Lagging = 2;
    public const byte Staff = 4;
    public const byte Host = 8;
    /// <summary>In the receiver's own stage group.</summary>
    public const byte SameGroup = 16;
}

/// <summary>
/// INSTANCE_INFO (server, protocol 49): wid u32 (0: the receiver is home; the rest then empty) | host member u32 | visibility u8 |
/// settings (11 B) | area str | story label str | count u8 + members | seats taken u8 | held u8 | reserved u8 | grace seconds left
/// u16 (0: none) | own role u8 (0 host, 1 guest, 2 staff playing, 3 spectator). On every change of the receiver's instance.
/// </summary>
public sealed record InstanceInfoMsg(uint Wid, uint HostMember, Visibility Visibility, RoomSettings Settings, string Area, string StoryLabel,
    IReadOnlyList<InstanceInfoMember> Members, byte SeatsTaken, byte SeatsHeld, byte SeatsReserved, ushort GraceSeconds, InstanceRole Role)
{
    public static readonly InstanceInfoMsg Home = new(0, 0, Visibility.Public, RoomSettings.Default, "", "", [], 0, 0, 0, 0, InstanceRole.Host);

    public static InstanceInfoMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var wid = r.U32();
        var host = r.U32();
        var visibility = (Visibility)r.U8();
        var settings = SettingsBlock.Read(ref r);
        var area = r.Str(32);
        var label = r.Str(64);
        var n = r.U8();
        var members = new List<InstanceInfoMember>(n);
        for (var i = 0; i < n; ++i) members.Add(new InstanceInfoMember(r.U32(), StageNames.Read(ref r, allowEmpty: true), r.U16(), r.U8()));
        var m = new InstanceInfoMsg(wid, host, visibility, settings, area, label, members, r.U8(), r.U8(), r.U8(), r.U16(), (InstanceRole)r.U8());
        r.End();
        return m;
    }

    public byte[] Frame()
    {
        var w = new WireWriter(Msg.InstanceInfo, 64 + Members.Count * 24).U32(Wid).U32(HostMember).U8((byte)Visibility);
        SettingsBlock.Write(w, Settings);
        w.Str(Area).Str(StoryLabel).U8((byte)Members.Count);
        foreach (var x in Members) w.U32(x.Member).Str(x.Stage).U16(x.Level).U8(x.Flags);
        return w.U8(SeatsTaken).U8(SeatsHeld).U8(SeatsReserved).U16(GraceSeconds).U8((byte)Role).Frame();
    }
}

/// <summary>WORLD_NOTICE (server, protocol 49): kind u8 | member u32 | name str | argument u32 | text str (the client writes the English
/// text of 19.5.6 from them).</summary>
public sealed record WorldNoticeMsg(WorldNoticeKind Kind, uint Member, string Name, uint Argument, string Text)
{
    public static WorldNoticeMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new WorldNoticeMsg((WorldNoticeKind)r.U8(), r.U32(), r.Str(64), r.U32(), r.Str(64));
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.WorldNotice).U8((byte)Kind).U32(Member).Str(Name).U32(Argument).Str(Text).Frame();
}

/// <summary>
/// OFFER (server, protocol 50, round I1b, SEAMLESS.md 19.30.5): offer id u16 | op u8 | flags u8 | entrant count u8 + member u32 each (the
/// first entrant first) | stage str (where the first entrant still in the dungeon is) | notice seconds u8 | close reason u8.
/// </summary>
public sealed record OfferMsg(ushort Id, OfferOp Op, byte Flags, IReadOnlyList<uint> Entrants, string Stage, byte NoticeSeconds, OfferClose Close)
{
    /// <summary>Show the notice now.</summary>
    public const byte Notice = 1;
    /// <summary>A late notice: it names where the entrants are now.</summary>
    public const byte Late = 2;
    /// <summary>Reserved for round I2's blocks (no notice naming a blocked entrant).</summary>
    public const byte Unnamed = 4;
    /// <summary>The receiver's own request waits.</summary>
    public const byte Waiting = 8;
    /// <summary>The receiver's GO is stamped.</summary>
    public const byte Stamped = 16;
    /// <summary>The target is boss-locked.</summary>
    public const byte Boss = 32;
    /// <summary>The receiver closed it with B (kept by the client).</summary>
    public const byte ClosedByB = 64;
    /// <summary>A silent reopen (a re-entry within World:OfferReopenSeconds).</summary>
    public const byte Reopened = 128;

    public static OfferMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var id = r.U16();
        var op = (OfferOp)r.U8();
        var flags = r.U8();
        var n = r.U8();
        if (id == 0) throw new ProtocolException("offer id 0");
        if (op is not (OfferOp.Open or OfferOp.Changed or OfferOp.Closed)) throw new ProtocolException("unknown offer op");
        if (n > Wire.MaxSlots) throw new ProtocolException("more than four entrants");
        var entrants = new List<uint>(n);
        for (var i = 0; i < n; ++i) entrants.Add(r.U32());
        var stage = StageNames.Read(ref r, allowEmpty: true);
        var seconds = r.U8();
        var close = (OfferClose)r.U8();
        r.End();
        if (close > OfferClose.Gone) throw new ProtocolException("unknown offer close reason");
        return new OfferMsg(id, op, flags, entrants, stage, seconds, close);
    }

    public byte[] Frame()
    {
        if (Entrants.Count > Wire.MaxSlots) throw new ArgumentException("more than four entrants");
        var w = new WireWriter(Msg.Offer, 16 + Entrants.Count * 4).U16(Id).U8((byte)Op).U8(Flags).U8((byte)Entrants.Count);
        foreach (var e in Entrants) w.U32(e);
        return w.Str(Stage).U8(NoticeSeconds).U8((byte)Close).Frame();
    }
}

/// <summary>MEMBER_INFO (server, protocol 49): member u32 | name str | level u16 | flags u8 (<see cref="Relation"/>): before a member's
/// first mention to a receiver, and again when its flags change.</summary>
public sealed record MemberInfoMsg(uint Member, string Name, ushort Level, byte Flags)
{
    public static MemberInfoMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new MemberInfoMsg(r.U32(), r.Str(64), r.U16(), r.U8());
        r.End();
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.MemberInfo).U32(Member).Str(Name).U16(Level).U8(Flags).Frame();
}
