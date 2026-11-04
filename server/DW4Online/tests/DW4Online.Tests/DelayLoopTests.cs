using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using Xunit.Abstractions;

namespace DW4Online.Tests;

/// <summary>
/// The delay rule in a closed loop with one member (SEAMLESS.md 6.4, round 62 review): the relay seals at 59.94 Hz with
/// the real <see cref="SlotStream"/> and <see cref="DelayController"/> (evaluated every 60 ticks, DELAY two ticks later);
/// the client runs its passes on a display's grid (a vsync at <c>passHz</c>, or unlimited), paces its steps with a port of
/// src/game/online/pacing.cpp, sends the pad for tick + d when it executes a tick and applies DELAY when it executes its
/// tick (a rise sends the extra pads at once, a fall skips samples). Both directions have their one-way time, a uniform
/// jitter and keep their order (TCP).
/// </summary>
public class DelayLoopTests(ITestOutputHelper output)
{
    private const double Tick = 1001.0 / 60.0; // ms

    /// <summary>src/game/online/pacing.cpp (PaceController::due_steps), times in ms.</summary>
    private sealed class Pace
    {
        private double _acc, _level = 1, _excess;
        private bool _primed;

        public int Due(double elapsedMs, int waiting)
        {
            var elapsed = Math.Clamp(elapsedMs, 0, 1000);
            if (!_primed)
            {
                if (waiting < 2) return 0;
                _primed = true;
                _acc = Tick;
            }
            _level += (waiting - _level) * Math.Min(1.0, elapsed / 2000.0);
            var ratio = 1.0 + Math.Clamp((_level - 1.5) * 0.001, -0.001, 0.001);
            if (waiting > 4)
            {
                _acc = 0;
                _level = 1.5;
                return waiting - 1;
            }
            _acc += elapsed * ratio;
            var due = (int)Math.Floor(_acc / Tick);
            if (_level >= 2.25) _excess += elapsed;
            else _excess = 0;
            if (_excess >= 500 && waiting >= Math.Max(due, 0) + 2)
            {
                _excess = 0;
                _level -= 1;
                _acc += Tick;
                ++due;
            }
            if (due <= 0) return 0;
            if (due > waiting)
            {
                due = waiting;
                _acc = Math.Min(_acc - due * Tick, Tick);
                return due;
            }
            _acc -= due * Tick;
            return due;
        }
    }

    /// <summary>Late pads and pads sent (all, and after the settle time), the delay changes (all, after the settle time),
    /// the changes as "second:d" ("+" a rise), the end d, the longest run of late pads after the settle time and d at it.</summary>
    public sealed record Result(int Late, int Pads, int Changes, string Trace, int EndD, int LongestRun, int ChangesAfter, int DAt, int LateAfter,
        int PadsAfter, int StartD);

    /// <summary>
    /// <paramref name="passHz"/> the client's pass grid (0: a pass every 0.5 ms, a game without vsync); stalls are
    /// (start s, length ms) of the client's host loop; <paramref name="settleSeconds"/> where <see cref="Result.ChangesAfter"/>
    /// starts counting and <see cref="Result.DAt"/> is read.
    /// </summary>
    public static Result Run(double passHz, double upMs, double downMs, double jitterMs, int startD, double seconds, int seed,
        (double At, double Ms)[]? stalls = null, double settleSeconds = 60)
    {
        var rng = new Random(seed);
        var pass = passHz > 0 ? 1000.0 / passHz : 0.5;
        if (startD <= 0)
        {
            // The hub's start value: PING / PONG round trips, the client answering in its next pass.
            var rtts = Enumerable.Range(0, 30).Select(_ => upMs + downMs + jitterMs * (rng.NextDouble() + rng.NextDouble()) + pass * rng.NextDouble()).ToList();
            startD = DelayController.FromRoundTrips(rtts, 15);
        }
        var stream = new SlotStream(0, 0, startD) { Delay = { Max = 15 } };
        var relayD = startD;
        var ticks = (int)(seconds * 1000 / Tick);
        var sealedAt = new double[ticks + 400];
        var delayEvents = new Dictionary<uint, int>();
        var pads = new List<(double At, uint Tick)>();
        double lastUp = 0, lastDown = 0, prevPass = 0;
        uint nextSeal = 0, executed = 0, inputNext = 0;
        int d = startD, late = 0, sent = 0, changes = 0, changesAfter = 0, run = 0, longest = 0, dAt = startD, padIndex = 0, lateAfter = 0,
            padsAfter = 0;
        var trace = new List<string>();
        var pace = new Pace();
        for (var k = 0; k < d; ++k) Send(0);
        var stallIndex = 0;
        for (var p = 1; ; ++p)
        {
            var now = p * pass + rng.NextDouble() * 0.3;
            if (stalls is not null && stallIndex < stalls.Length && now >= stalls[stallIndex].At * 1000)
            {
                // The host loop stands still: the next pass comes after the stall, on the grid.
                var resume = stalls[stallIndex].At * 1000 + stalls[stallIndex].Ms;
                ++stallIndex;
                p = (int)Math.Ceiling(resume / pass);
                now = p * pass + rng.NextDouble() * 0.3;
            }
            // The relay up to now: seals and pad arrivals in time order.
            while (true)
            {
                var sealTime = nextSeal * Tick;
                var padTime = padIndex < pads.Count ? pads[padIndex].At : double.MaxValue;
                if (Math.Min(sealTime, padTime) > now) break;
                if (sealTime <= padTime)
                {
                    stream.Take(nextSeal, 0, out _);
                    lastDown = Math.Max(lastDown, sealTime + downMs + rng.NextDouble() * jitterMs);
                    sealedAt[nextSeal] = lastDown;
                    if (nextSeal % 60 == 59 && stream.Delay.Evaluate() is { } nd)
                    {
                        delayEvents[nextSeal + 2] = nd;
                        ++changes;
                        if (nextSeal * Tick >= settleSeconds * 1000) ++changesAfter;
                        trace.Add($"{nextSeal / 60}s:{nd}{(nd > relayD ? "+" : "")}");
                        relayD = nd;
                    }
                    if (Math.Abs(nextSeal * Tick - settleSeconds * 1000) < Tick / 2 + 0.01) dAt = stream.Delay.Current;
                    ++nextSeal;
                    if (nextSeal >= ticks)
                        return new Result(late, sent, changes, string.Join(" ", trace), stream.Delay.Current, longest, changesAfter, dAt, lateAfter, padsAfter,
                            startD);
                }
                else
                {
                    var (at, tk) = pads[padIndex++];
                    var after = at >= settleSeconds * 1000;
                    if (after) ++padsAfter;
                    if (stream.Offer(tk, Pad.Neutral, nextSeal, tk * Tick - at, at) == SlotStream.Submit.Late)
                    {
                        ++late;
                        if (after)
                        {
                            ++lateAfter;
                            longest = Math.Max(longest, ++run);
                        }
                    }
                    else run = 0;
                }
            }
            // The client's pass.
            var waiting = 0;
            while (executed + waiting < nextSeal && sealedAt[executed + waiting] <= now) ++waiting;
            var due = pace.Due(now - prevPass, waiting);
            prevPass = now;
            for (var s = 0; s < due; ++s)
            {
                if (delayEvents.TryGetValue(executed, out var nd))
                {
                    if (nd > d)
                        for (var k = d; k < nd; ++k) Send(now);
                    d = nd;
                }
                while (inputNext <= executed + (uint)d) Send(now);
                ++executed;
            }
        }

        void Send(double at)
        {
            lastUp = Math.Max(lastUp, at + upMs + rng.NextDouble() * jitterMs);
            pads.Add((lastUp, inputNext++));
            ++sent;
        }
    }

    private (double Late, double Changes, int Longest, string Ends, string Trace) Cell(double hz, double oneWay, double jitter, double seconds,
        double settle = 60)
    {
        int late = 0, pads = 0, lateAll = 0, padsAll = 0, changes = 0, longest = 0;
        var ends = new List<string>();
        var first = "";
        foreach (var seed in new[] { 1, 2, 3 })
        {
            var r = Run(hz, oneWay, oneWay, jitter, 0, seconds, seed, settleSeconds: settle);
            late += r.LateAfter;
            pads += r.PadsAfter;
            lateAll += r.Late;
            padsAll += r.Pads;
            changes += r.ChangesAfter;
            longest = Math.Max(longest, r.LongestRun);
            ends.Add($"{r.StartD}>{r.EndD}");
            if (seed == 1) first = r.Trace;
        }
        output.WriteLine($"{hz} Hz, rtt {2 * oneWay} ms, jitter {jitter}: late after {settle} s {100.0 * late / pads:F2} % (all {100.0 * lateAll / padsAll:F2} %), " +
                         $"changes after it {changes / 3.0:F1}, longest late run {longest}, d start>end {string.Join(" ", ends)} | {first}");
        return (100.0 * late / pads, changes / 3.0, longest, string.Join("/", ends), first);
    }

    [Theory]
    // A game presenting with vsync at 60.000 Hz: its pads' margin is a sawtooth of one pass over the 16.7 s beat
    // against the relay's 59.94 Hz. The round 62 rule fell by its high part every 33 s (24 ms: 4 -> 3 -> 5 -> 4, 0.6-3.4 %
    // of the pads late in runs of up to 120).
    [InlineData(60.0, 10, 1)]
    [InlineData(60.0, 12, 1)]
    [InlineData(60.0, 13, 1)]
    [InlineData(60.0, 15, 1)]
    [InlineData(60.0, 20, 2)]
    [InlineData(60.0, 40, 3)]
    // Presenting at the relay's own rate, at 144 Hz and without vsync (the hidden test clients).
    [InlineData(59.94, 12, 1)]
    [InlineData(144.0, 12, 1)]
    [InlineData(144.0, 20, 2)]
    [InlineData(0.0, 12, 1)]
    [InlineData(0.0, 20, 2)]
    public void A_vsync_grid_does_not_make_the_delay_swing(double hz, double oneWay, double jitter)
    {
        var (late, changes, longest, _, _) = Cell(hz, oneWay, jitter, 600);
        Assert.True(late < 0.1, $"{late:F2} % of the pads late");
        Assert.True(changes <= 1.0, $"{changes:F1} delay changes after the first minute (average of three runs)");
        Assert.True(longest <= 60, $"a run of {longest} late pads");
    }

    [Theory]
    // A slow beat (a monitor at 59.95 Hz: the margin falls by one pass once in 100 s) outlasts any fall window: a fall in
    // its high part fails in the next drop. The bar makes each further try wait twice as long (1, 2, 4, 8, 10 minutes), and
    // d goes back at once to the value it fell from; with the newest pad as the fill source the late pads of a failure come
    // one tick late instead of being lost.
    [InlineData(10, 1)]
    [InlineData(12, 1)]
    [InlineData(13, 1)]
    [InlineData(20, 2)]
    public void A_slow_beat_fails_ever_more_rarely(double oneWay, double jitter)
    {
        foreach (var seed in new[] { 1, 2, 3 })
        {
            var r = Run(59.95, oneWay, oneWay, jitter, 0, 2400, seed, settleSeconds: 900);
            var rises = r.Trace.Split(' ', StringSplitOptions.RemoveEmptyEntries).Where(x => x.EndsWith('+')).ToList();
            output.WriteLine($"seed {seed}: late {100.0 * r.Late / r.Pads:F2} % (after 15 min {100.0 * r.LateAfter / r.PadsAfter:F2} %), " +
                             $"changes {r.Changes} (after 15 min {r.ChangesAfter}), longest late run after 15 min {r.LongestRun}, d {r.StartD}>{r.EndD} | {r.Trace}");
            // (Tries at about 0.5, 2.5, 5.5, 10.5, 20 and 32 minutes; the round 62 rule failed every 100 s.)
            Assert.True(rises.Count <= 7, $"{rises.Count} rises in 40 minutes");
            Assert.True(r.ChangesAfter <= 4, $"{r.ChangesAfter} changes in the last 25 minutes");
            Assert.True(100.0 * r.Late / r.Pads < 0.35, $"{100.0 * r.Late / r.Pads:F2} % late");
            Assert.True(r.LongestRun <= 90, $"a run of {r.LongestRun} late pads");
        }
    }

    [Fact]
    public void After_a_stretch_of_stalls_the_delay_comes_back()
    {
        // A busy PC: the client's host loop stalls 150 ms every 3 s for 30 s, then runs quietly (60 Hz vsync, 24 ms).
        var stalls = Enumerable.Range(0, 10).Select(k => (60.0 + 3 * k, 150.0)).ToArray();
        var quiet = Run(60, 12, 12, 1, 0, 240, 1, settleSeconds: 55);
        var busy = Run(60, 12, 12, 1, 0, 240, 1, stalls, settleSeconds: 200);
        output.WriteLine($"quiet: d at 55 s {quiet.DAt}, end {quiet.EndD} | {quiet.Trace}");
        output.WriteLine($"stalls 60-90 s: d at 200 s {busy.DAt}, end {busy.EndD}, late {busy.Late} | {busy.Trace}");
        Assert.Equal(quiet.DAt, busy.EndD);
        Assert.True(busy.DAt <= quiet.DAt + 1, $"d {busy.DAt} 110 s after the last stall");
    }

    /// <summary>The table of SEAMLESS.md 6.4: DW4ONLINE_DELAY_TABLE=1 dotnet test --filter DelayLoopTests.Table --logger "console;verbosity=detailed".</summary>
    [Fact]
    public void Table()
    {
        if (Environment.GetEnvironmentVariable("DW4ONLINE_DELAY_TABLE") is null) return;
        foreach (var hz in new[] { 60.0, 59.95, 59.94, 144.0, 30.0, 0.0 })
            foreach (var (ow, j) in new[] { (10.0, 1.0), (12.0, 1.0), (13.0, 1.0), (15.0, 1.0), (20.0, 2.0), (25.0, 2.0), (40.0, 3.0), (75.0, 5.0) })
                Cell(hz, ow, j, 600);
    }
}
