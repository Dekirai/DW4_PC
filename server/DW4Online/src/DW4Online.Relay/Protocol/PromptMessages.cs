namespace DW4Online.Relay.Protocol;

// Round I1b, the arrival prompt (protocol 50, SEAMLESS.md 19.7, 19.18; the layouts agreed in i1b-contract.md, "prompt 16:30").

/// <summary>PLAN's purpose: the hub departure the gate form holds (the census of 19.7.4).</summary>
public enum PlanPurpose : byte
{
    /// <summary>stagML11 mate00, the lobby gate's destination select (quest starts included).</summary>
    Gate = 1,
    /// <summary>stagML11 mate02, the return mat to the Security Gate stage.</summary>
    ReturnMat = 2,
    Other = 3,
}

/// <summary>
/// PLAN (client, 19.7.4): plan u16 (1..0xFFFE) | purpose u8 | stage str | point u8 | context u64 | group u8 (1 until round I4).
/// Sent while the gate form holds the confirming press; at most one a second per member.
/// </summary>
public sealed record PlanMsg(ushort Plan, PlanPurpose Purpose, string Stage, byte Point, ulong Context, byte Group)
{
    public static PlanMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new PlanMsg(r.U16(), (PlanPurpose)r.U8(), StageNames.Read(ref r), r.U8(), r.U64(), r.U8());
        r.End();
        if (m.Plan is 0 or 0xFFFF) throw new ProtocolException("plan id out of range");
        if (m.Purpose is not (PlanPurpose.Gate or PlanPurpose.ReturnMat or PlanPurpose.Other)) throw new ProtocolException("unknown plan purpose");
        if (m.Group is < 1 or > 4) throw new ProtocolException("plan group out of range");
        return m;
    }

    public byte[] Frame() => new WireWriter(Msg.Plan).U16(Plan).U8((byte)Purpose).Str(Stage).U8(Point).U64(Context).U8(Group).Frame();
}

/// <summary>A row's state (19.7.2): choosable (joinable now, or waiting) or greyed.</summary>
public enum OptionState : byte
{
    /// <summary>A JOIN at its stage group at the arrival stage, or a guest's NEW where it has none (here tells).</summary>
    Joinable = 0,
    BossFight = 1,
    /// <summary>A stage group at the arrival stage that is not joinable yet (loading, a scene, its entry): the join waits.</summary>
    Loading = 2,
    HostAway = 3,
    Full = 4,
    ArrivingNow = 5,
    OtherQuest = 6,
    StoryNotHere = 7,
    TooLong = 8,
}

public static class OptionStates
{
    public static bool Choosable(OptionState s) => s is OptionState.Joinable or OptionState.BossFight or OptionState.Loading or OptionState.ArrivingNow;
}

/// <summary>A member of an OPTIONS row (X: Details): name str | level u16 | species u8.</summary>
public sealed record OptionMember(string Name, ushort Level, byte Species);

/// <summary>
/// An OPTIONS row: wid u32 | host member u32 | host name str | relation u8 (1 party, 2 the host a friend, 4 friends inside) | friends
/// inside u8 | host stage str | players u8 | here u8 | seats taken u8 | held u8 | reserved u8 | story label str | same story point u8 |
/// difficulty u8 | settings u8 (1 randomizer, 2 other settings) | state u8 | visibility u8 (0xFF unless staff) | ping u16 | members.
/// </summary>
public sealed record OptionRow(uint Wid, uint Host, string HostName, byte Relation, byte FriendsInside, string HostStage, byte Players, byte Here,
    byte Taken, byte Held, byte Reserved, string StoryLabel, byte SameStoryPoint, byte Difficulty, byte Settings, OptionState State, byte Visibility,
    ushort Ping, IReadOnlyList<OptionMember> Members)
{
    public const byte Party = 1;
    public const byte FriendHost = 2;
    public const byte FriendsIn = 4;
    public const byte Randomizer = 1;
    public const byte OtherSettings = 2;
    public const byte Hidden = 0xFF;
}

/// <summary>
/// OPTIONS (server, 19.7.3): plan u16 (0: a held barrier's) | inst u32 | visit u32 (the held barrier's; 0 0 for a PLAN's answer) | stage
/// str | flags u8 | visibility u8 (the remembered default) | preselected u8 (row index, 0xFF New instance) | rtt u16 (the receiver's
/// own) | notice u8 (1 full now, 2 ended, 3 no longer open) | notice name str | count u8 (at most 8) + rows.
/// </summary>
public sealed record OptionsMsg(ushort Plan, uint Inst, uint Visit, string Stage, byte Flags, Visibility Visibility, byte Preselected, ushort Rtt,
    byte Notice, string NoticeName, IReadOnlyList<OptionRow> Rows)
{
    public const byte NewInstance = 0xFF;
    /// <summary>Sent again on a change (the client keeps its cursor on the same wid or on New instance).</summary>
    public const byte Refresh = 1;
    /// <summary>An automatic co-arrival into a starting instance takes the arrival (no rows, no overlay).</summary>
    public const byte Starting = 2;
    /// <summary>No candidate: the press goes out, NEW with the remembered visibility.</summary>
    public const byte Nothing = 4;
    public const int MaxRows = 8;
    public const byte NoticeFull = 1;
    public const byte NoticeEnded = 2;
    public const byte NoticeClosed = 3;

    public static OptionsMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var plan = r.U16();
        var inst = r.U32();
        var visit = r.U32();
        var stage = StageNames.Read(ref r);
        var flags = r.U8();
        var visibility = r.U8();
        if (visibility > 2) throw new ProtocolException("unknown visibility");
        var preselected = r.U8();
        var rtt = r.U16();
        var notice = r.U8();
        var noticeName = r.Str(64);
        var n = r.U8();
        if (n > MaxRows) throw new ProtocolException("too many OPTIONS rows");
        var rows = new List<OptionRow>(n);
        for (var i = 0; i < n; ++i)
        {
            var wid = r.U32();
            var host = r.U32();
            var hostName = r.Str(64);
            var relation = r.U8();
            var friends = r.U8();
            var hostStage = StageNames.Read(ref r, allowEmpty: true);
            var players = r.U8();
            var here = r.U8();
            var taken = r.U8();
            var held = r.U8();
            var reserved = r.U8();
            var label = r.Str(64);
            var same = r.U8();
            var difficulty = r.U8();
            var settings = r.U8();
            var state = r.U8();
            if (state > (byte)OptionState.TooLong) throw new ProtocolException("unknown row state");
            var vis = r.U8();
            var ping = r.U16();
            var k = r.U8();
            if (k > 4) throw new ProtocolException("too many OPTIONS members");
            var members = new List<OptionMember>(k);
            for (var j = 0; j < k; ++j) members.Add(new OptionMember(r.Str(64), r.U16(), r.U8()));
            rows.Add(new OptionRow(wid, host, hostName, relation, friends, hostStage, players, here, taken, held, reserved, label, same, difficulty,
                settings, (OptionState)state, vis, ping, members));
        }
        r.End();
        if (preselected != NewInstance && preselected >= rows.Count) throw new ProtocolException("OPTIONS preselection out of range");
        return new OptionsMsg(plan, inst, visit, stage, flags, (Visibility)visibility, preselected, rtt, notice, noticeName, rows);
    }

    public byte[] Frame()
    {
        if (Rows.Count > MaxRows) throw new InvalidOperationException("too many OPTIONS rows");
        var w = new WireWriter(Msg.Options, 64 + Rows.Count * 96).U16(Plan).U32(Inst).U32(Visit).Str(Stage).U8(Flags).U8((byte)Visibility)
            .U8(Preselected).U16(Rtt).U8(Notice).Str(NoticeName).U8((byte)Rows.Count);
        foreach (var x in Rows)
        {
            w.U32(x.Wid).U32(x.Host).Str(x.HostName).U8(x.Relation).U8(x.FriendsInside).Str(x.HostStage).U8(x.Players).U8(x.Here).U8(x.Taken)
                .U8(x.Held).U8(x.Reserved).Str(x.StoryLabel).U8(x.SameStoryPoint).U8(x.Difficulty).U8(x.Settings).U8((byte)x.State).U8(x.Visibility)
                .U16(x.Ping).U8((byte)Math.Min(4, x.Members.Count));
            foreach (var y in x.Members.Take(4)) w.Str(y.Name).U16(y.Level).U8(y.Species);
        }
        return w.Frame();
    }
}

/// <summary>CHOICE's answer (19.18): 1 join, 2 new, 3 back (at a held barrier from home), 4 travel row (I3 / I4), 5 return (I3).</summary>
public enum ChoiceKind : byte
{
    Join = 1,
    New = 2,
    Back = 3,
    Travel = 4,
    Return = 5,
}

/// <summary>
/// CHOICE (client, 19.7.4 / 19.7.5): plan u16 | inst u32 | visit u32 (the held barrier's; 0 0 with a plan) | choice u8 | wid u32 | visibility
/// u8 | flags u8 | back point u8 (choice back: the hub point to return to, 0xFF the default stagML11 point 3).
/// </summary>
public sealed record ChoiceMsg(ushort Plan, uint Inst, uint Visit, ChoiceKind Choice, uint Wid, Visibility Visibility, byte Flags, byte BackPoint)
{
    public const byte DefaultBackPoint = 0xFF;

    public static ChoiceMsg Parse(ReadOnlySpan<byte> p)
    {
        var r = new WireReader(p);
        var m = new ChoiceMsg(r.U16(), r.U32(), r.U32(), (ChoiceKind)r.U8(), r.U32(), (Visibility)r.U8(), r.U8(), r.U8());
        r.End();
        if (m.Choice is < ChoiceKind.Join or > ChoiceKind.Return) throw new ProtocolException("unknown choice");
        if ((byte)m.Visibility > 2) throw new ProtocolException("unknown visibility");
        return m;
    }

    public byte[] Frame() =>
        new WireWriter(Msg.Choice).U16(Plan).U32(Inst).U32(Visit).U8((byte)Choice).U32(Wid).U8((byte)Visibility).U8(Flags).U8(BackPoint).Frame();
}
