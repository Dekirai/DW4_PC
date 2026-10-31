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
    private Pad _lastSealed;
    /// <summary>Buttons pressed in pads that came after their tick was sealed and not held in the last sealed pad: they
    /// go with the next sealed pad of this slot (round 62). The delay rule no longer rises for a burst of late pads (a
    /// stall, one lost segment), so a press inside such a burst comes a few ticks late instead of being lost.</summary>
    private ushort _carry;

    /// <summary>Presses carried from late pads into a later sealed one.</summary>
    public long Carried { get; private set; }

    public SlotStream(int slot, uint fromTick, int delay)
    {
        Slot = slot;
        FromTick = fromTick;
        Delay = new DelayController(delay);
        GraceUntil = fromTick + (uint)Math.Max(0, delay) + 2;
    }

    public int Slot { get; }
    /// <summary>The first tick whose record carries this slot's pad (a JOIN's tick, or the visit's tick 0).</summary>
    public uint FromTick { get; set; }
    /// <summary>The first tick without this slot (LEAVE, fork); uint.MaxValue while it stays.</summary>
    public uint UntilTick { get; set; } = uint.MaxValue;
    /// <summary>Pads for ticks before this one do not feed the delay rule when they come late: the member's client sends
    /// them all at once when it learns of the stream (a JOIN it executes at J, a sealed start after its load, a resync's
    /// end), so the first of them are late by design (round 62: every join raised the joiner's d by one).</summary>
    public uint GraceUntil { get; set; }
    /// <summary>Late pads inside the grace (not counted by the delay rule).</summary>
    public long LateInGrace { get; private set; }
    /// <summary>A JOIN's tick runs the joiner's spawn on every machine (its species and weapon packages load in that step):
    /// every member's pads of the next two seconds may come late by that step, and the joiner stands in its entry scene
    /// meanwhile (round 62: a joiner at 40 ms of round trip rose from 5 to 8 in the second after its JOIN).</summary>
    public const uint JoinGraceTicks = 120;

    /// <summary>Late pads for ticks before <paramref name="tick"/> do not feed the delay rule.</summary>
    public void GraceThrough(uint tick) => GraceUntil = Math.Max(GraceUntil, tick);
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

    /// <summary>
    /// A pad that came within this many ms of the member's previous one (a burst: a stall of the client, its downlink or the
    /// relay's sealing thread let its pads out at once, or one lost segment held them back) or more than this many ticks
    /// after it (the first pad after such a stall) says nothing about the link's delay: the delay rule leaves it out.
    /// Round 62: with every late pad counted, a machine busy for a few minutes raised two members at 20 ms of round trip
    /// from 3 to 9 (150 ms of input delay for every input) to save a few filled pads per stall.
    /// </summary>
    public const double BurstMs = 4.0;
    public const double StallTicks = 3.0;

    private double _lastArrivalMs = double.NaN;

    /// <summary>Pads that came in a burst or after a stall (left out of the delay rule).</summary>
    public long Unpaced { get; private set; }

    /// <summary>A pad for <paramref name="tick"/>; <paramref name="nextSeal"/> is the next tick the schedule seals,
    /// <paramref name="arrivalMs"/> the relay's clock when it came (NaN: unknown, the pad counts as paced).</summary>
    public Submit Offer(uint tick, Pad pad, uint nextSeal, double marginMs, double arrivalMs = double.NaN)
    {
        var gap = arrivalMs - _lastArrivalMs;
        var paced = double.IsNaN(gap) || (gap >= BurstMs && gap <= StallTicks * Wire.TickSeconds * 1000.0);
        if (!double.IsNaN(arrivalMs)) _lastArrivalMs = arrivalMs;
        if (!paced && tick >= GraceUntil) ++Unpaced;
        if (tick < nextSeal || tick < FromTick)
        {
            ++Late;
            if (tick >= FromTick) _carry |= (ushort)(pad.Buttons & ~_lastSealed.Buttons);
            if (tick < GraceUntil) ++LateInGrace;
            else if (paced) Delay.Record(marginMs, late: true);
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
        if (tick >= GraceUntil && paced) Delay.Record(marginMs, late: false);
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
            return Sealing(pad);
        }
        if (FillStreak == 0) FillStreakSince = now;
        ++FillStreak;
        ++Filled;
        filled = true;
        return Sealing(FillStreak <= RepeatFills ? _lastReal : Pad.Neutral);
    }

    /// <summary>The pad as it is sealed: with the presses carried from late pads.</summary>
    private Pad Sealing(Pad pad)
    {
        if (_carry != 0)
        {
            var with = (ushort)(pad.Buttons | _carry);
            if (with != pad.Buttons) ++Carried;
            pad = pad with { Buttons = with };
            _carry = 0;
        }
        _lastSealed = pad;
        return pad;
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
/// The delay rule of SEAMLESS.md 6.4 (round 62): it follows the member's link, not single stalls. Over the last ten
/// seconds of pads (600): d rises by one when late pads came in at least three different seconds of them (one stall of
/// the client, a burst after one lost segment or a load costs that member a few filled pads, not a lasting tick of input
/// delay for the rest of the stage), and at once by up to three when most pads of the last second were late (the
/// starting value too low, a joiner still a tick behind; the stream gives it only paced pads, none of a burst or right after
/// a stall); it falls
/// when ten seconds have passed since the last change and the 0.5 % quantile of the arrival margin is above 1.15 ticks,
/// by as many ticks as leave 0.15 of a tick of that margin (at most three at once; the client keeps the presses of the
/// samples it skips). Round S1's rule (rise on four late pads in 60 s, fall by one per 30 s) ratcheted every hiccup into
/// lasting delay: two hidden clients at 20 ms of round trip went from d 3 to 8 and 9 within ten seconds of machine load
/// and the guest of a mid-stage join started at 7 (docs/SEAMLESS.md 6.4, round 62). Bounds 1..max.
/// </summary>
public sealed class DelayController(int initial)
{
    /// <summary>Pads the rules look at (ten seconds).</summary>
    public const int Window = 600;
    /// <summary>One second of pads: late pads count by the seconds they came in.</summary>
    public const int Block = 60;
    /// <summary>Seconds of the window with late pads before d rises.</summary>
    public const int RaiseSeconds = 3;
    /// <summary>Pads since the last change before d may fall (ten seconds).</summary>
    public const int LowerHold = 600;
    /// <summary>The margin quantile d falls by (0.5 %: three pads of the window may be below it).</summary>
    public const double LowerQuantile = 0.005;
    /// <summary>The share of a tick the low margin keeps after d fell (S1 kept a quarter; at 20 ms of round trip a joiner
    /// then stayed at 4 with a margin of 19-20 ms, one tick above the host's 3 at 3-6 ms).</summary>
    public const double LowerReserve = 0.15;
    /// <summary>Ticks d falls or rises at most in one evaluation.</summary>
    public const int MaxStep = 3;

    private readonly double[] _margin = new double[Window];
    private readonly bool[] _late = new bool[Window];
    private int _pos;
    private int _count;
    private int _sinceChange;

    public int Current { get; private set; } = initial;
    public int Max { get; set; } = 10;
    /// <summary>The share of late pads in the window that counts (with <see cref="RaiseSeconds"/> different seconds).</summary>
    public double LateTarget { get; set; } = 0.001;

    public void Record(double marginMs, bool late)
    {
        _margin[_pos] = marginMs;
        _late[_pos] = late;
        _pos = (_pos + 1) % Window;
        _count = Math.Min(Window, _count + 1);
        ++_sinceChange;
    }

    /// <summary>The <paramref name="share"/> quantile of the window's margins (ms), or null with fewer than a second of pads.</summary>
    public double? Quantile(double share)
    {
        if (_count < Block) return null;
        var copy = new double[_count];
        for (var k = 0; k < _count; ++k) copy[k] = _margin[(_pos - _count + k + Window) % Window];
        Array.Sort(copy);
        return copy[Math.Clamp((int)Math.Floor(_count * share), 0, _count - 1)];
    }

    /// <summary>The newest <paramref name="n"/> margins (ms), sorted; fewer while the window holds fewer.</summary>
    private double[] NewestMargins(int n)
    {
        var k = Math.Min(n, _count);
        var copy = new double[k];
        for (var i = 0; i < k; ++i) copy[i] = _margin[(_pos - 1 - i + 2 * Window) % Window];
        Array.Sort(copy);
        return copy;
    }

    /// <summary>The low margin PING reports (the quantile the rule falls by), or null with fewer than a second of pads.</summary>
    public double? LowMargin() => Quantile(LowerQuantile);

    /// <summary>Late pads in the window, and the number of its one-second blocks that hold one.</summary>
    public (int Late, int Seconds) LateInWindow()
    {
        int late = 0, seconds = 0;
        for (var b = 0; b * Block < _count; ++b)
        {
            var any = false;
            for (var k = b * Block; k < Math.Min(_count, (b + 1) * Block); ++k)
            {
                // The newest pads first, by blocks of one second.
                if (!_late[(_pos - 1 - k + 2 * Window) % Window]) continue;
                ++late;
                any = true;
            }
            if (any) ++seconds;
        }
        return (late, seconds);
    }

    /// <summary>A new delay when the rule says so (call every 60 ticks or so), else null.</summary>
    public int? Evaluate()
    {
        const double tick = Wire.TickSeconds * 1000.0;
        var (late, seconds) = LateInWindow();
        // Most of the last second's pads late: d is too low for this link right now (a start value below it, a join
        // whose client still runs a tick behind); every second waited for two more seconds of late pads costs the
        // member most of its inputs, so it rises at once, by what the median margin of that second says.
        var newest = NewestMargins(Block);
        var mostLate = newest.Length == Block && newest[Block / 2] < 0;
        if (Current < Max && (mostLate || (late > _count * LateTarget && seconds >= RaiseSeconds)))
        {
            var step = mostLate ? Math.Clamp((int)Math.Ceiling(-newest[Block / 2] / tick) + 1, 1, MaxStep) : 1;
            Change(Current + step);
            return Current;
        }
        if (_sinceChange >= LowerHold && _count == Window && Current > 1 && LowMargin() is { } low && low > tick * (1 + LowerReserve))
        {
            var step = Math.Clamp((int)Math.Floor((low - tick * LowerReserve) / tick), 1, MaxStep);
            Change(Current - step);
            return Current;
        }
        return null;
    }

    /// <summary>Sets the delay (a DELAY event goes out with it) and starts the window afresh.</summary>
    public void Change(int d)
    {
        Current = Math.Clamp(d, 1, Max);
        Array.Clear(_late);
        _count = 0;
        _pos = 0;
        _sinceChange = 0;
    }

    /// <summary>
    /// The starting delay from round trips (ms): d = ceil((RTT + spread) / tick + 1.5), RTT the median and the spread the
    /// 90th percentile above it, of the newest 30 round trips (the last 30 s of PING). A client answers PING in its host
    /// loop, so a stage load, a snapshot restore or a catch-up pass delays single answers by hundreds of ms; those are no
    /// round trips of the link and are left out (above twice the median plus 50 ms; S1 took the 99.9th percentile of 120
    /// samples, the largest, and started a joiner at 6 at 20 ms of round trip). The arrival margins correct it.
    /// </summary>
    public static int FromRoundTrips(IReadOnlyCollection<double> rttMs, int max)
    {
        if (rttMs.Count == 0) return Math.Min(3, max);
        var (median, spread) = RoundTrip(rttMs);
        var d = (int)Math.Ceiling((median + spread) / (Wire.TickSeconds * 1000.0) + 1.5);
        return Math.Clamp(d, 1, max);
    }

    /// <summary>The median of the newest 30 round trips and the spread of their 90th percentile above it, stalls left out (ms).</summary>
    public static (double Median, double Spread) RoundTrip(IReadOnlyCollection<double> rttMs)
    {
        if (rttMs.Count == 0) return (0, 0);
        var recent = rttMs.Skip(Math.Max(0, rttMs.Count - 30)).OrderBy(x => x).ToArray();
        var median = recent[recent.Length / 2];
        var kept = recent.Where(x => x <= 2 * median + 50).ToArray();
        var p90 = kept[Math.Clamp((int)Math.Ceiling(kept.Length * 0.9) - 1, 0, kept.Length - 1)];
        return (median, Math.Max(0, p90 - median));
    }
}
