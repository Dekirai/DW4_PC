using DW4Online.Relay.Protocol;

namespace DW4Online.Relay.Hub;

/// <summary>
/// One slot's input stream in a sealed instance (SEAMLESS.md 6.4): the pads a member submitted ahead of their ticks,
/// the fill rule (a missing pad is filled at once with the member's last real pad, at most six ticks in a row, then
/// neutral), the counters and the delay controller fed by the arrival margins (due time minus arrival).
/// </summary>
public sealed class SlotStream
{
    /// <summary>Pads further ahead than this are refused (abuse limit; the delay is at most 10 ticks).</summary>
    public const int MaxAhead = 128;
    public const int RepeatFills = 6;
    /// <summary>A member whose pads were filled this many ticks in a row is lagging (SEAMLESS.md 6.4).</summary>
    public const int LagFills = 30;

    private readonly Pad?[] _ring = new Pad?[MaxAhead];
    private readonly uint[] _ringTick = new uint[MaxAhead];
    private Pad _lastReal;

    public SlotStream(int slot, uint fromTick, int delay)
    {
        Slot = slot;
        FromTick = fromTick;
        Delay = new DelayController(delay);
    }

    public int Slot { get; }
    /// <summary>The first tick whose record carries this slot's pad (a JOIN's tick, or the visit's tick 0).</summary>
    public uint FromTick { get; set; }
    /// <summary>The first tick without this slot (LEAVE, fork); uint.MaxValue while it stays.</summary>
    public uint UntilTick { get; set; } = uint.MaxValue;
    public DelayController Delay { get; }

    public long Sealed { get; private set; }
    public long Filled { get; private set; }
    public long Late { get; private set; }
    public long Duplicates { get; private set; }
    public long TooEarly { get; private set; }
    /// <summary>Consecutive filled ticks (0 after a real pad).</summary>
    public int FillStreak { get; private set; }
    /// <summary>The server time (timestamp) at which the current fill streak began.</summary>
    public long FillStreakSince { get; private set; }
    /// <summary>The furthest tick this member submitted a pad for (its horizon).</summary>
    public uint Horizon { get; private set; }
    public bool HasHorizon { get; private set; }

    public bool Carries(uint tick) => tick >= FromTick && tick < UntilTick;

    public enum Submit
    {
        Accepted,
        Late,
        Duplicate,
        TooEarly,
    }

    /// <summary>A pad for <paramref name="tick"/>; <paramref name="nextSeal"/> is the next tick the schedule seals.</summary>
    public Submit Offer(uint tick, Pad pad, uint nextSeal, double marginMs)
    {
        if (tick < nextSeal || tick < FromTick)
        {
            ++Late;
            Delay.Record(marginMs, late: true);
            return Submit.Late;
        }
        if (tick - nextSeal >= MaxAhead)
        {
            ++TooEarly;
            return Submit.TooEarly;
        }
        var i = (int)(tick % MaxAhead);
        if (_ring[i] is not null && _ringTick[i] == tick)
        {
            ++Duplicates;
            return Submit.Duplicate;
        }
        _ring[i] = pad;
        _ringTick[i] = tick;
        if (!HasHorizon || tick > Horizon) Horizon = tick;
        HasHorizon = true;
        Delay.Record(marginMs, late: false);
        return Submit.Accepted;
    }

    /// <summary>The pad sealed for <paramref name="tick"/>: the submitted one, or a fill.</summary>
    public Pad Take(uint tick, long now, out bool filled)
    {
        ++Sealed;
        var i = (int)(tick % MaxAhead);
        if (_ring[i] is { } pad && _ringTick[i] == tick)
        {
            _ring[i] = null;
            _lastReal = pad;
            FillStreak = 0;
            filled = false;
            return pad;
        }
        if (FillStreak == 0) FillStreakSince = now;
        ++FillStreak;
        ++Filled;
        filled = true;
        return FillStreak <= RepeatFills ? _lastReal : Pad.Neutral;
    }

    /// <summary>Moves the pads submitted for ticks from <paramref name="tick"/> on into another stream (a fork's child).</summary>
    public void MoveAhead(SlotStream to, uint tick)
    {
        for (var i = 0; i < MaxAhead; ++i)
            if (_ring[i] is { } pad && _ringTick[i] >= tick)
            {
                to._ring[i] = pad;
                to._ringTick[i] = _ringTick[i];
                _ring[i] = null;
            }
        to._lastReal = _lastReal;
        to.Horizon = Horizon;
        to.HasHorizon = HasHorizon;
    }

    public double FillRate => Sealed == 0 ? 0 : (double)Filled / Sealed;
}

/// <summary>
/// The delay rule of SEAMLESS.md 6.4: raise d by one when more than 0.1 % of the last 3,600 pads arrived late; lower it
/// by one when the 0.1 % quantile of the arrival margin stayed above one tick for 30 s. Bounds 1..max.
/// </summary>
public sealed class DelayController(int initial)
{
    public const int Window = 3600;
    public const int LowerWindow = 1800; // 30 s of pads
    private readonly bool[] _late = new bool[Window];
    private readonly double[] _margin = new double[LowerWindow];
    private int _pos;
    private int _mpos;
    private int _count;
    private int _mcount;
    private int _lateInWindow;
    private int _sinceChange;

    public int Current { get; private set; } = initial;
    public int Max { get; set; } = 10;
    public double LateTarget { get; set; } = 0.001;

    public void Record(double marginMs, bool late)
    {
        if (_count == Window && _late[_pos]) --_lateInWindow;
        _late[_pos] = late;
        if (late) ++_lateInWindow;
        _pos = (_pos + 1) % Window;
        _count = Math.Min(Window, _count + 1);
        _margin[_mpos] = marginMs;
        _mpos = (_mpos + 1) % LowerWindow;
        _mcount = Math.Min(LowerWindow, _mcount + 1);
        ++_sinceChange;
    }

    /// <summary>The 0.1 % quantile of the margins of the last 30 s (ms), or null with fewer samples.</summary>
    public double? LowMargin()
    {
        if (_mcount < 60) return null;
        var copy = _margin.AsSpan(0, _mcount).ToArray();
        Array.Sort(copy);
        return copy[(int)Math.Floor(_mcount * LateTarget)];
    }

    /// <summary>A new delay when the rule says so (call every 60 ticks or so), else null.</summary>
    public int? Evaluate()
    {
        var lateLimit = Math.Max(1, (int)Math.Floor(Window * LateTarget));
        if (_lateInWindow > lateLimit && Current < Max)
        {
            Change(Current + 1);
            return Current;
        }
        if (_sinceChange >= LowerWindow && _mcount == LowerWindow && Current > 1 && LowMargin() is { } low && low > Wire.TickSeconds * 1000.0 * 1.25)
        {
            Change(Current - 1);
            return Current;
        }
        return null;
    }

    /// <summary>Sets the delay (a DELAY event goes out with it) and starts the windows afresh.</summary>
    public void Change(int d)
    {
        Current = Math.Clamp(d, 1, Max);
        Array.Clear(_late);
        _lateInWindow = 0;
        _count = 0;
        _pos = 0;
        _mcount = 0;
        _mpos = 0;
        _sinceChange = 0;
    }

    /// <summary>
    /// The starting delay from round trips (ms): d = ceil((RTT + spread) / tick) + 1, the +1 for the client's playout
    /// buffer of one tick; the arrival margins correct it within seconds.
    /// </summary>
    public static int FromRoundTrips(IReadOnlyCollection<double> rttMs, int max)
    {
        if (rttMs.Count == 0) return Math.Min(3, max);
        var sorted = rttMs.OrderBy(x => x).ToArray();
        var median = sorted[sorted.Length / 2];
        var top = sorted[Math.Min(sorted.Length - 1, (int)Math.Ceiling(sorted.Length * 0.999) - 1)];
        var spread = Math.Max(0, top - median);
        var d = (int)Math.Ceiling((median + spread + 1.0) / (Wire.TickSeconds * 1000.0)) + 1;
        return Math.Clamp(d, 1, max);
    }
}
