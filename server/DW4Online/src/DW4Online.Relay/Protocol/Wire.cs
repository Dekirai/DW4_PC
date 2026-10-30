using System.Buffers.Binary;
using System.Text;

namespace DW4Online.Relay.Protocol;

/// <summary>
/// DW4R2 framing and field encoding (SEAMLESS.md 6.2, 6.3 and its appendix). After the handshake line every frame is
/// type (u8) + payload length (u32 LE) + payload; all integers are little endian; strings are u8 length + ASCII.
/// </summary>
public static class Wire
{
    public const string Magic = "DW4R2";
    public const int HeaderBytes = 5;
    /// <summary>Frames above this are refused (state transfers come in chunks).</summary>
    public const int MaxFrameBytes = 1024 * 1024;
    /// <summary>The largest chunk of SNAPSHOT / STATE_DATA (the sender splits longer blobs).</summary>
    public const int MaxChunkBytes = 60 * 1024;
    /// <summary>59.94 Hz: one tick of the instance schedule (kFieldSeconds, engine.hpp).</summary>
    public const double TickSeconds = 1001.0 / 60000.0;
    public const int MaxSlots = 4;
    public const byte SlotMask = 0x0F;
    public const int MaxStageName = 24;
}

/// <summary>Frame types. Client to server 0x01-0x3F, server to client 0x80-0xBF.</summary>
public static class Msg
{
    // client -> server
    public const byte Input = 0x01;
    public const byte Log = 0x02;
    public const byte Heading = 0x03;
    public const byte Barrier = 0x04;
    public const byte BarrierChoice = 0x05;
    public const byte Split = 0x06;
    public const byte Snapshot = 0x07;
    public const byte SnapHash = 0x08;
    public const byte Ready = 0x09;
    public const byte JoinAt = 0x0A;
    public const byte NetFp = 0x0B;
    public const byte State = 0x0C;
    public const byte Intent = 0x0D;
    public const byte Story = 0x0E;
    public const byte Presence = 0x0F;
    public const byte Resume = 0x10;
    public const byte Rate = 0x11;
    public const byte Pong = 0x12;
    public const byte Bye = 0x13;

    // server -> client
    public const byte Welcome = 0x80;
    public const byte Sealed = 0x81;
    public const byte Live = 0x82;
    public const byte JoinReq = 0x83;
    public const byte BarrierOk = 0x84;
    public const byte BarrierWait = 0x85;
    public const byte StateData = 0x86;
    public const byte PresenceOut = 0x87;
    public const byte Resync = 0x88;
    public const byte StoryAck = 0x89;
    public const byte Room = 0x8A;
    public const byte Ping = 0x8B;
    public const byte Close = 0x8C;
    public const byte Roster = 0x8D;
    public const byte Reassign = 0x8E;

    public static string Name(byte type) => type switch
    {
        Input => "INPUT", Log => "LOG", Heading => "HEADING", Barrier => "BARRIER", BarrierChoice => "BARRIER_CHOICE",
        Split => "SPLIT", Snapshot => "SNAPSHOT", SnapHash => "SNAPHASH", Ready => "READY", JoinAt => "JOIN_AT",
        NetFp => "NETFP", State => "STATE", Intent => "INTENT", Story => "STORY", Presence => "PRESENCE",
        Resume => "RESUME", Rate => "RATE", Pong => "PONG", Bye => "BYE",
        Welcome => "WELCOME", Sealed => "SEALED", Live => "LIVE", JoinReq => "JOIN_REQ", BarrierOk => "BARRIER_OK",
        BarrierWait => "BARRIER_WAIT", StateData => "STATE_DATA", PresenceOut => "PRESENCE", Resync => "RESYNC",
        StoryAck => "STORY_ACK", Room => "ROOM", Ping => "PING", Close => "CLOSE", Roster => "ROSTER", Reassign => "REASSIGN",
        _ => "0x" + type.ToString("X2"),
    };
}

/// <summary>Events inside a tick record (SEALED, LIVE, STATE_DATA): type u8 + length u16 + payload.</summary>
public static class Ev
{
    /// <summary>slot u8, entrance point u8, carry (the rest).</summary>
    public const byte Join = 1;
    /// <summary>slot u8.</summary>
    public const byte Leave = 2;
    /// <summary>slot u8, kind u8, argument i32.</summary>
    public const byte Intent = 3;
    /// <summary>tick u32: the remaining member leads alone from this tick (the record is the last sealed tick).</summary>
    public const byte LeadFrom = 4;
    /// <summary>slot u8, delay u8: that member samples its pads this many ticks ahead from now on.</summary>
    public const byte Delay = 5;
}

/// <summary>Instance modes (WELCOME, BARRIER_OK).</summary>
public enum InstanceMode : byte
{
    /// <summary>A hub or UI stage (or the title): solo, not logged, never joined.</summary>
    Hub = 0,
    /// <summary>One member in a field, dungeon or boss stage: it runs its own clock and streams LOG.</summary>
    SoloLed = 1,
    /// <summary>Two to four members: the server seals every tick on its 59.94 Hz schedule.</summary>
    Sealed = 2,
}

public enum BarrierDecision : byte
{
    New = 1,
    Join = 2,
    Dissolve = 3,
    CoArrive = 4,
}

public enum WaitReason : byte
{
    CoArrival = 1,
    NotJoinable = 2,
    BossLock = 3,
    Snapshot = 4,
}

public enum IntentKind : byte
{
    GoWithout = 1,
    GiveUp = 2,
}

public enum SplitKind : byte
{
    /// <summary>An exit, the gate disc or give up: the stayers keep the instance; the leavers become a departing child.</summary>
    Exit = 0,
    /// <summary>A boss or story mat with Go without them, a story trigger: the participants keep the instance.</summary>
    InStage = 1,
}

/// <summary>STATE reasons (why an instance is not joinable).</summary>
public enum StateReason : byte
{
    None = 0,
    EntryScene = 1,
    Scene = 2,
    Menu = 3,
    Loading = 4,
    Boss = 5,
    ForkPending = 6,
    Hub = 7,
}

/// <summary>Why a barrier ended in NEW next to an instance of the same stage (the loading screen notice).</summary>
public enum BarrierNotice : byte
{
    None = 0,
    OtherContext = 1,
    Full = 2,
    TooLongToCatchUp = 3,
    HoldExpired = 4,
    WentAlone = 5,
    JoinFailed = 6,
}

/// <summary>STATE_DATA purposes.</summary>
public enum StatePurpose : byte
{
    Join = 1,
    CoArrival = 2,
    Resync = 3,
    Resume = 4,
}

/// <summary>ROOM notices.</summary>
public enum RoomNotice : byte
{
    MemberJoined = 1,
    MemberLeft = 2,
    OwnerPresent = 3,
    OwnerAway = 4,
    Lagging = 5,
    LagOk = 6,
    Connected = 7,
    Disconnected = 8,
}

/// <summary>CLOSE reasons (server to client before it closes).</summary>
public enum CloseReason : byte
{
    Left = 1,
    Kicked = 2,
    RoomClosed = 3,
    Replaced = 4,
    Protocol = 5,
    Abuse = 6,
    Timeout = 7,
    ServerStopping = 8,
}

/// <summary>
/// One controller sample on the wire (SEAMLESS.md 6.4): buttons u16 LE, then the main stick x, y and the C-stick x, y as
/// int8 = round(v * 74) (the GameCube's steps). 6 bytes (connected comes from the present mask). The relay never interprets it.
/// </summary>
public readonly record struct Pad(ushort Buttons, sbyte Lx, sbyte Ly, sbyte Rx, sbyte Ry)
{
    public const int Size = 6;
    public static readonly Pad Neutral = default;

    public static Pad Read(ReadOnlySpan<byte> s) => new(BinaryPrimitives.ReadUInt16LittleEndian(s), (sbyte)s[2], (sbyte)s[3], (sbyte)s[4], (sbyte)s[5]);

    public void Write(Span<byte> d)
    {
        BinaryPrimitives.WriteUInt16LittleEndian(d, Buttons);
        d[2] = (byte)Lx;
        d[3] = (byte)Ly;
        d[4] = (byte)Rx;
        d[5] = (byte)Ry;
    }

    /// <summary>Sticks outside -74..74 cannot come from a GameCube controller (a malformed or forged pad).</summary>
    public bool Valid => Lx is >= -74 and <= 74 && Ly is >= -74 and <= 74 && Rx is >= -74 and <= 74 && Ry is >= -74 and <= 74;
}

/// <summary>A malformed frame: the connection is closed (abuse limits, SEAMLESS.md 14).</summary>
public class ProtocolException(string message) : Exception(message);

/// <summary>Reads a payload field by field; any read past the end is a <see cref="ProtocolException"/>.</summary>
public ref struct WireReader(ReadOnlySpan<byte> data)
{
    private readonly ReadOnlySpan<byte> _data = data;
    private int _pos;

    public readonly int Remaining => _data.Length - _pos;
    public readonly bool AtEnd => _pos == _data.Length;

    private ReadOnlySpan<byte> Take(int n)
    {
        if (n < 0 || _pos + n > _data.Length) throw new ProtocolException("payload too short");
        var s = _data.Slice(_pos, n);
        _pos += n;
        return s;
    }

    public byte U8() => Take(1)[0];
    public sbyte I8() => (sbyte)Take(1)[0];
    public ushort U16() => BinaryPrimitives.ReadUInt16LittleEndian(Take(2));
    public short I16() => BinaryPrimitives.ReadInt16LittleEndian(Take(2));
    public uint U32() => BinaryPrimitives.ReadUInt32LittleEndian(Take(4));
    public int I32() => BinaryPrimitives.ReadInt32LittleEndian(Take(4));
    public ulong U64() => BinaryPrimitives.ReadUInt64LittleEndian(Take(8));
    public float F32() => BinaryPrimitives.ReadSingleLittleEndian(Take(4));
    public Pad Pad() => Protocol.Pad.Read(Take(Protocol.Pad.Size));
    public ReadOnlySpan<byte> Bytes(int n) => Take(n);
    public ReadOnlySpan<byte> Rest() => Take(Remaining);

    /// <summary>u8 length + printable ASCII.</summary>
    public string Str(int max = 255)
    {
        var n = U8();
        if (n > max) throw new ProtocolException("string too long");
        var b = Take(n);
        foreach (var c in b)
            if (c is < 0x20 or > 0x7E) throw new ProtocolException("string is not printable ASCII");
        return Encoding.ASCII.GetString(b);
    }

    /// <summary>u32 length + bytes, at most <paramref name="max"/>.</summary>
    public byte[] Blob(int max)
    {
        var n = U32();
        if (n > (uint)max) throw new ProtocolException("blob too long");
        return Take((int)n).ToArray();
    }

    public readonly void End()
    {
        if (!AtEnd) throw new ProtocolException("trailing bytes");
    }
}

/// <summary>Builds a frame (header included) into a growing buffer.</summary>
public sealed class WireWriter
{
    private byte[] _buf;
    private int _len;

    public WireWriter(byte type, int capacity = 64)
    {
        _buf = new byte[Math.Max(capacity, 16) + Wire.HeaderBytes];
        _buf[0] = type;
        _len = Wire.HeaderBytes;
    }

    public int PayloadLength => _len - Wire.HeaderBytes;

    private Span<byte> Grow(int n)
    {
        if (_len + n > _buf.Length) Array.Resize(ref _buf, Math.Max(_buf.Length * 2, _len + n));
        var s = _buf.AsSpan(_len, n);
        _len += n;
        return s;
    }

    public WireWriter U8(byte v)
    {
        Grow(1)[0] = v;
        return this;
    }

    public WireWriter I8(sbyte v) => U8((byte)v);

    public WireWriter U16(ushort v)
    {
        BinaryPrimitives.WriteUInt16LittleEndian(Grow(2), v);
        return this;
    }

    public WireWriter I16(short v)
    {
        BinaryPrimitives.WriteInt16LittleEndian(Grow(2), v);
        return this;
    }

    public WireWriter U32(uint v)
    {
        BinaryPrimitives.WriteUInt32LittleEndian(Grow(4), v);
        return this;
    }

    public WireWriter I32(int v)
    {
        BinaryPrimitives.WriteInt32LittleEndian(Grow(4), v);
        return this;
    }

    public WireWriter U64(ulong v)
    {
        BinaryPrimitives.WriteUInt64LittleEndian(Grow(8), v);
        return this;
    }

    public WireWriter F32(float v)
    {
        BinaryPrimitives.WriteSingleLittleEndian(Grow(4), v);
        return this;
    }

    public WireWriter Pad(Pad p)
    {
        p.Write(Grow(Protocol.Pad.Size));
        return this;
    }

    public WireWriter Bytes(ReadOnlySpan<byte> b)
    {
        b.CopyTo(Grow(b.Length));
        return this;
    }

    public WireWriter Str(string s)
    {
        var b = Encoding.ASCII.GetBytes(s);
        if (b.Length > 255) throw new ArgumentException("string too long", nameof(s));
        U8((byte)b.Length);
        return Bytes(b);
    }

    public WireWriter Blob(ReadOnlySpan<byte> b)
    {
        U32((uint)b.Length);
        return Bytes(b);
    }

    /// <summary>The finished frame with its header.</summary>
    public byte[] Frame()
    {
        BinaryPrimitives.WriteUInt32LittleEndian(_buf.AsSpan(1, 4), (uint)(_len - Wire.HeaderBytes));
        return _buf.AsSpan(0, _len).ToArray();
    }
}
