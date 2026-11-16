using DW4Online.Relay.Protocol;

namespace DW4Online.Relay.Hub;

/// <summary>
/// One slot's input stream in a sealed instance (SEAMLESS.md 6.4): the pads a member submitted ahead of their ticks,
/// the fill rule (a missing pad is filled at once with the member's newest pad, sealed in time or late, at most six
/// ticks in a row after it came, then neutral), the counters and the delay controller fed by the arrival margins (due
/// time minus arrival).
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
    /// <summary>The fill source: the member's newest pad, sealed in time or late (round 62 review: a late pad is the
    /// newest the relay knows of the member's input; filling with the last pad sealed in time instead made a held button
    /// toggle at 30 Hz through a stretch where seals and late pads alternate, the carried press on, the old fill off).</summary>
    private Pad _lastReal;
    private uint _lastRealTick;
    private bool _hasLastReal;
    /// <summary>Fills since the fill source came (it repeats up to <see cref="RepeatFills"/> of them, then neutral).</summary>
    private int _repeats;
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
    /// <summary>The host's grace (SEAMLESS.md 19.6.7): a missing pad is filled with neutral pads from the first missing tick on (his
    /// Digimon stands still), not "the last pad, then neutral" as for a guest.</summary>
    public bool NeutralFill { get; set; }
    public DelayController Delay { get; }

    public long Sealed { get; private set; }
    public long Filled { get; private set; }
    public long Late { get; private set; }
    public long Duplicates { get; private set; }
    public long TooEarly { get; private set; }
    /// <summary>Consecutive filled ticks (0 after a pad sealed in time; a late pad does not end it: the lag rule counts
    /// the ticks the member's own pads missed).</summary>
    public int FillStreak { get; private set; }
    /// <summary>The server time (timestamp) at which the current fill streak began (or, for a host back from his grace, his return:
    /// <see cref="RestartFillStreakClock"/>).</summary>
    public long FillStreakSince { get; private set; }

    /// <summary>The host back within his grace (SEAMLESS.md 19.6.7, "back in time: play on"): the lag rule (Relay:LagLeaveSeconds)
    /// counts his fill streak from his return, so his resumed client has that long to catch up to the ticks sealed meanwhile. The
    /// streak's length stays: he lags until a pad of his is sealed in time. (Before, the streak counted from the grace's start, and a
    /// host away longer than LagLeaveSeconds was taken out of his stage group by the next housekeeping after his return.) Every
    /// return restarts it; what bounds a link that keeps dropping is the host's absence debt (WorldInstance.HostDebt, fix-flap).</summary>
    public void RestartFillStreakClock(long now)
    {
        if (FillStreak > 0) FillStreakSince = now;
        LagDebt = 0;
    }

    /// <summary>
    /// The lag rule's debt (fix-021, SEAMLESS.md 6.4 and 19.6.7, "the flapping host" left by fix-flap): the sealed ticks this slot was
    /// filled while another connected member's pad came in time (the slot held that member's play up), minus the ticks its own pad came
    /// in time, one for one, never below 0 (<see cref="CountLag"/>; RoomHub.SealOne counts every sealed tick). The fill streak ends at
    /// any pad in time, so a link whose drops each stay below Relay:LagLeaveSeconds, with a short burst of pads in time between them,
    /// never reached the lag rule's limit; the debt reaches it once the time held up exceeds the time played in time by that limit. A
    /// brief hiccup is paid back by as much play in time; a member who plays in time at least as long as it holds the others up never
    /// gets there. Not counted: a host in his grace, a member resyncing, a tick at which no connected member's pad came in time (the
    /// whole group, or this server, stalled). A host's return restarts it with the streak's clock.
    /// </summary>
    public int LagDebt { get; private set; }

    /// <summary>One sealed tick of this slot for <see cref="LagDebt"/>: <paramref name="filled"/> (no pad of its own in time), and whether
    /// that held another member up (<paramref name="heldUp"/>: another connected member's pad came in time, and this slot counts).</summary>
    public void CountLag(bool filled, bool heldUp)
    {
        if (!filled)
        {
            if (LagDebt > 0) --LagDebt;
        }
        else if (heldUp) ++LagDebt;
    }
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
    /// from 3 to 9 (150 ms of input delay for every input) to save a few filled pads per stall. (Eight ticks: a game that
    /// runs only 8-20 passes a second sends its pads in bursts of several, the first after a gap of up to eight ticks; its
    /// lateness is steady and must count. A member whose pads are filled for half a second in a row counts in any case:
    /// it lags, and only a higher d keeps it from being taken out after Relay:LagLeaveSeconds.)
    /// </summary>
    public const double BurstMs = 4.0;
    public const double StallTicks = 8.0;

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
        paced = paced || FillStreak >= LagFills;
        if (!paced && tick >= GraceUntil) ++Unpaced;
        if (tick < nextSeal || tick < FromTick)
        {
            ++Late;
            if (tick >= FromTick)
            {
                _carry |= (ushort)(pad.Buttons & ~_lastSealed.Buttons);
                // The newest pad the relay has of this member fills its next ticks: what it holds stays held, one tick or
                // a few late, instead of the older pad sealed in time (or neutral after six fills).
                if (!_hasLastReal || tick > _lastRealTick) SetSource(pad, tick);
            }
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
            SetSource(pad, tick);
            FillStreak = 0;
            filled = false;
            return Sealing(pad);
        }
        if (FillStreak == 0) FillStreakSince = now;
        ++FillStreak;
        ++Filled;
        filled = true;
        return Sealing(++_repeats <= RepeatFills && !NeutralFill ? _lastReal : Pad.Neutral);
    }

    private void SetSource(Pad pad, uint tick)
    {
        _lastReal = pad;
        _lastRealTick = tick;
        _hasLastReal = true;
        _repeats = 0;
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
        to._lastRealTick = _lastRealTick;
        to._hasLastReal = _hasLastReal;
        to._repeats = _repeats;
        to.Horizon = Horizon;
        to.HasHorizon = HasHorizon;
    }

    public double FillRate => Sealed == 0 ? 0 : (double)Filled / Sealed;
}
/// <summary>
/// The delay rule of SEAMLESS.md 6.4 (round 62): it follows the member's link, not single stalls, and holds still on a
/// game whose pads leave on a vsync grid.
/// <para>Rise, over the last ten seconds of pads (600): by one when late pads came in at least three different seconds of
/// them (one stall of the client, a burst after one lost segment or a load costs that member a few filled pads, not a
/// lasting tick of input delay for the rest of the stage), and at once by up to three when most pads of the last second
/// were late (the starting value too low, a joiner still a tick behind; the stream gives it only paced pads, none of a
/// burst or right after a stall).</para>
/// <para>Fall: when thirty seconds of pads (1800) came since the last change and the 0.5 % quantile of their arrival
/// margin is above 1.15 ticks, by as many ticks as leave 0.15 of a tick of that margin (at most three at once; the client
/// keeps the presses of the samples it skips). Thirty seconds hold more than one beat of a game presenting at 60.000 Hz
/// against the relay's 59.94 Hz (16.7 s): such a game sends its pads at its passes, so their margin is a sawtooth of one
/// pass (16.7 ms) over that beat, and the review of round 62 found the ten seconds the fall looked at before seeing only
/// its high part: d 4 -> 3 -> 5 -> 4 every 33 s at 24 ms of round trip, about 120 inputs late in a row each time. A fall
/// that d had to undo (it rose again from the value it fell to) bars that value for a minute, twice as long after each
/// further failure at it, up to ten minutes: a slower beat (a monitor at 59.95 Hz: 100 s) outlasts any window.</para>
/// Round S1's rule (rise on four late pads in 60 s, fall by one per 30 s) ratcheted every hiccup into lasting delay: two
/// hidden clients at 20 ms of round trip went from d 3 to 8 and 9 within ten seconds of machine load and the guest of a
/// mid-stage join started at 7 (docs/SEAMLESS.md 6.4, round 62). Bounds 1..max.
/// </summary>
public sealed class DelayController(int initial)
{
    /// <summary>Pads the rise looks at (ten seconds).</summary>
    public const int Window = 600;
    /// <summary>One second of pads: late pads count by the seconds they came in.</summary>
    public const int Block = 60;
    /// <summary>Seconds of the window with late pads before d rises.</summary>
    public const int RaiseSeconds = 3;
    /// <summary>Pads the fall looks at (thirty seconds: more than one 60.000 / 59.94 Hz beat of 16.7 s).</summary>
    public const int FallWindow = 1800;
    /// <summary>Pads since the last change before d may fall (the fall window, all of it at the new d).</summary>
    public const int LowerHold = FallWindow;
    /// <summary>The margin quantile d falls by (0.5 %: nine pads of the fall window may be below it).</summary>
    public const double LowerQuantile = 0.005;
    /// <summary>The share of a tick the low margin keeps after d fell (S1 kept a quarter; at 20 ms of round trip a joiner
    /// then stayed at 4 with a margin of 19-20 ms, one tick above the host's 3 at 3-6 ms).</summary>
    public const double LowerReserve = 0.15;
    /// <summary>Ticks d falls or rises at most in one evaluation.</summary>
    public const int MaxStep = 3;
    /// <summary>Pads a value is barred after a fall to it was undone (one minute), the first time.</summary>
    public const int BarHold = 3600;
    /// <summary>The longest bar (ten minutes): it doubles at each further failure at the same value.</summary>
    public const int BarHoldMax = 36000;

    private readonly double[] _margin = new double[FallWindow];
    private readonly bool[] _late = new bool[FallWindow];
    private int _pos;
    private int _count;
    private int _sinceChange;
    private long _records;
    /// <summary>The value the last change fell to (0: the last change was a rise or none), and the one before it.</summary>
    private int _fellTo;
    private int _fellFrom;
    private int _barred;
    private long _barredUntil;
    private int _barHold;

    public int Current { get; private set; } = initial;
    public int Max { get; set; } = 10;
    /// <summary>The share of late pads in the rise window that counts (with <see cref="RaiseSeconds"/> different seconds): 2 %,
    /// twelve of ten seconds. Round 62 had 0.1 % (any one late pad); since a late pad fills its slot's next ticks (its
    /// presses carried, a held button held) it costs that input a tick or two, while every tick of d costs every input
    /// 16.7 ms for at least the fall window. A busy PC's stalls (the first pad after each, up to eight ticks of gap) and a
    /// lost TCP segment's first pad raised d by one per few such seconds and the fall took it back only after 30 s: in the
    /// closed loop of tests/DW4Online.Tests/DelayLoopTests.cs (LoadTable) client stalls of 20-150 ms 0.2 / 0.5 / 1 times
    /// a second at 24 ms and 60 Hz kept d at 6.1 / 8.4 / 9.9 (input 64 / 94 / 112 ms; quiet: 4, 38 ms) with 1.0-1.2 % of
    /// the pads late; at 2 % 5.0 / 5.3 / 7.5 (44 / 43 / 73 ms) with 1.6-3.3 % late. A link too slow for d (most of a
    /// second late) still rises at once, and a game of 8-20 passes a second (the first pad of every burst late) by this.</summary>
    public double LateTarget { get; set; } = 0.02;
    /// <summary>Why the last change came: "late" (late pads in three seconds), "most_late" (most of a second late),
    /// "margin" (the fall), for the relay's log.</summary>
    public string LastReason { get; private set; } = "";
    /// <summary>The value falls stop above while a bar lasts (0: none).</summary>
    public int Barred => _records < _barredUntil ? _barred : 0;

    public void Record(double marginMs, bool late)
    {
        _margin[_pos] = marginMs;
        _late[_pos] = late;
        _pos = (_pos + 1) % FallWindow;
        _count = Math.Min(FallWindow, _count + 1);
        ++_sinceChange;
        ++_records;
    }

    /// <summary>The <paramref name="share"/> quantile of the margins since the last change, at most the fall window's
    /// (ms), or null with fewer than a second of pads.</summary>
    public double? Quantile(double share)
    {
        if (_count < Block) return null;
        var copy = _scratch ??= new double[FallWindow];
        for (var k = 0; k < _count; ++k) copy[k] = _margin[(_pos - _count + k + FallWindow) % FallWindow];
        // The k-th smallest by selection (the same value a sort gives, in linear time: SEAMLESS.md 19.17.4's ping pass sorted the
        // whole fall window for every sealed member once a second).
        return Select(copy, _count, Math.Clamp((int)Math.Floor(_count * share), 0, _count - 1));
    }

    private double[]? _scratch;

    /// <summary>The <paramref name="k"/>-th smallest of the first <paramref name="n"/> values (Hoare's selection; reorders them).</summary>
    public static double Select(double[] a, int n, int k)
    {
        int lo = 0, hi = n - 1;
        while (lo < hi)
        {
            var pivot = a[lo + (hi - lo) / 2];
            int i = lo, j = hi;
            while (i <= j)
            {
                while (a[i] < pivot) ++i;
                while (a[j] > pivot) --j;
                if (i > j) break;
                (a[i], a[j]) = (a[j], a[i]);
                ++i;
                --j;
            }
            if (k <= j) hi = j;
            else if (k >= i) lo = i;
            else return a[k];
        }
        return a[k];
    }

    private double? _pingMargin;
    private long _pingMarginAt = long.MinValue;

    /// <summary>Pads a PING's margin may be old (five seconds): it is a display value of the client's network line, not a rule.</summary>
    public const int PingMarginRecords = 300;

    /// <summary>
    /// The low margin PING and NETSTAT report (M4 (a), SEAMLESS.md 19.17.4): <see cref="LowMargin"/>, cached per stream for
    /// <see cref="PingMarginRecords"/> pads and dropped at every change of d, so the housekeeping's ping pass computes it at most
    /// once in five seconds per stream (it held the hub lock 7.5-21 ms once a second with 120 sealed members).
    /// </summary>
    public double? PingMargin()
    {
        if (_pingMarginAt != long.MinValue && _records - _pingMarginAt < PingMarginRecords) return _pingMargin;
        _pingMargin = LowMargin();
        _pingMarginAt = _records;
        return _pingMargin;
    }

    /// <summary>The newest <paramref name="n"/> margins (ms), sorted; fewer while the window holds fewer.</summary>
    private double[] NewestMargins(int n)
    {
        var k = Math.Min(n, _count);
        var copy = new double[k];
        for (var i = 0; i < k; ++i) copy[i] = _margin[(_pos - 1 - i + 2 * FallWindow) % FallWindow];
        Array.Sort(copy);
        return copy;
    }

    /// <summary>The low margin PING reports (the quantile the rule falls by), or null with fewer than a second of pads.</summary>
    public double? LowMargin() => Quantile(LowerQuantile);

    /// <summary>Late pads in the rise window (the newest ten seconds), and the number of its one-second blocks that hold one.</summary>
    public (int Late, int Seconds) LateInWindow()
    {
        int late = 0, seconds = 0;
        var n = Math.Min(_count, Window);
        for (var b = 0; b * Block < n; ++b)
        {
            var any = false;
            for (var k = b * Block; k < Math.Min(n, (b + 1) * Block); ++k)
            {
                // The newest pads first, by blocks of one second.
                if (!_late[(_pos - 1 - k + 2 * FallWindow) % FallWindow]) continue;
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
        // member most of its inputs, so it rises at once, by what the median margin of that second says and half a tick
        // (review of round 62: "+ 1 tick" took a joiner 2 ms late at 20 ms of round trip from 3 to 5, and the fall needs
        // thirty seconds per step since then).
        var newest = NewestMargins(Block);
        var mostLate = newest.Length == Block && newest[Block / 2] < 0;
        if (Current < Max && (mostLate || (late > Math.Min(_count, Window) * LateTarget && seconds >= RaiseSeconds)))
        {
            var step = mostLate ? Math.Clamp((int)Math.Ceiling((-newest[Block / 2] + tick / 2) / tick), 1, MaxStep) : 1;
            if (_fellTo == Current)
            {
                // The value the last change fell to was too low after all: barred for a while, and d goes back to the
                // value it fell from (it held for the whole fall window), unless the lateness asks for more.
                Bar(Current);
                if (mostLate) step = Math.Clamp(Math.Max(_fellFrom - Current, (int)Math.Ceiling(-newest[Block / 2] / tick)), 1, MaxStep);
            }
            LastReason = mostLate ? "most_late" : "late";
            Change(Current + step, fell: false);
            return Current;
        }
        if (_sinceChange >= LowerHold && _count == FallWindow && Current > 1 && LowMargin() is { } low && low > tick * (1 + LowerReserve))
        {
            var step = Math.Clamp((int)Math.Floor((low - tick * LowerReserve) / tick), 1, MaxStep);
            var to = Current - step;
            if (_records < _barredUntil) to = Math.Max(to, _barred + 1);
            if (to < Current)
            {
                LastReason = "margin";
                Change(to, fell: true);
                return Current;
            }
        }
        return null;
    }

    /// <summary>Takes over the bar of the member's previous stream (another stage, a fork's child, a rejoin) with the pads
    /// it had left, and the length of the next one: a value too low for the member's link and game stays barred, and a
    /// display beat that outlasts a stage visit does not start its tries afresh at every stage.</summary>
    public void Inherit(DelayController? previous)
    {
        if (previous is null || previous == this) return;
        _barred = previous._barred;
        _barHold = previous._barHold;
        _barredUntil = _records + Math.Max(0, previous._barredUntil - previous._records);
        // (Long over: the next failure starts with the first bar again, as on the previous stream.)
        if (previous._records - previous._barredUntil >= BarHoldMax) _barHold = 0;
    }

    private void Bar(int d)
    {
        // The same value failing again before its last bar was long over: twice as long (1, 2, 4, 8, 10 minutes).
        var again = d == _barred && _barHold > 0 && _records - _barredUntil < BarHoldMax;
        // (An inherited bar that had run out counts as ending at this stream's first pad.)
        _barHold = again ? Math.Min(2 * _barHold, BarHoldMax) : BarHold;
        _barred = d;
        _barredUntil = _records + _barHold;
    }

    /// <summary>Sets the delay (a DELAY event goes out with it) and starts the windows afresh.</summary>
    private void Change(int d, bool fell)
    {
        _fellFrom = Current;
        Current = Math.Clamp(d, 1, Max);
        _fellTo = fell ? Current : 0;
        Array.Clear(_late);
        _count = 0;
        _pos = 0;
        _sinceChange = 0;
        _pingMarginAt = long.MinValue;
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
