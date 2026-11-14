using System.Diagnostics;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// A link that keeps dropping ("flapping", SEAMLESS.md 19.6.7, fix-flap). Two rules: a drop less than Relay:LagLeaveSeconds after the
/// host's last return belongs to the same absence episode and keeps its grace's deadline (the countdown runs on through brief
/// returns), and the lag clock restarts only at his first return of a fill streak (later returns skip only their absence). So a host
/// whose link keeps dropping is out of his guests' stage group (lag_leave) or his instance has ended (host_lost) within
/// Relay:LagLeaveSeconds + World:HostGraceSeconds + Relay:LagLeaveSeconds of the start, the worst case of one absence, also when the
/// pads of his last ticks before each return come in time (as the real client's do, W7g); a host back once after up to the whole
/// grace plays on (2b009cee), a return of LagLeaveSeconds and a pad in time renew both; a guest's lag clock never restarts.
/// </summary>
public partial class HubLogicTests
{
    /// <summary>A sealed pair (a hosts, b is his guest) whose members' pads the test sets per phase: 0 none (silent or away), 1 late
    /// ones (a resumed client catching up), 2 in time. Housekeeping every 15 steps of 17 ms (255 ms, as the relay's 250 ms).</summary>
    private sealed class FlapRig
    {
        public Harness H { get; }
        public WorldInstance W { get; }
        public HubInstance I { get; }
        public Dictionary<string, int> Mode { get; } = new() { ["a"] = 2, ["b"] = 2 };
        private int _step;

        public FlapRig(int lagLeaveSeconds = 10)
        {
            H = SealedPair(out _, relay: new RelayOptions { LagLeaveSeconds = lagLeaveSeconds });
            W = H.Mem("a").World!;
            I = H.I("a");
        }

        /// <summary>The relay's clock in seconds (for elapsed times).</summary>
        public double Seconds => H.Now / (double)Stopwatch.Frequency;

        public bool LagLeft => H.Hub.Counters.GetValueOrDefault("lag_leave") > 0;
        public bool Out(string name) => LagLeft || W.Ending || H.Mem(name).Instance != I;

        /// <summary>Plays <paramref name="ms"/>; returns early (true) once <paramref name="stop"/> holds after a housekeeping pass.</summary>
        public bool Play(double ms, Func<bool>? stop = null)
        {
            var end = H.Now + (long)(ms * Stopwatch.Frequency / 1000);
            while (H.Now < end)
            {
                foreach (var (name, mode) in Mode)
                {
                    var m = H.Mem(name);
                    if (mode == 0 || !m.Connected || m.Instance != I) continue;
                    if (mode == 1) H.Send(name, new InputMsg(I.Id, I.Visit, I.NextSeal - 60, [new Pad(9, 0, 0, 0, 0)]).Frame());
                    else H.Send(name, new InputMsg(I.Id, I.Visit, I.NextSeal + 3, [new Pad(1, 0, 0, 0, 0), new Pad(1, 0, 0, 0, 0)]).Frame());
                }
                H.Seal(17);
                if (++_step % 15 == 0)
                {
                    H.Housekeep();
                    if (stop?.Invoke() == true) return true;
                }
            }
            return false;
        }

        public void Drop(string name) => H.Hub.Disconnect(H.Mem(name), H.P(name), H.Now);
        public void Back(string name) => H.Connect(name, name == "a" ? 0 : 1);

        /// <summary>The HostAway notices b got (their argument: the grace's seconds left).</summary>
        public List<WorldNoticeMsg> AwayNotices() =>
            H.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse).Where(n => n.Kind == WorldNoticeKind.HostAway).ToList();
    }

    /// <summary>The worst case of one absence (2b009cee): lagging up to Relay:LagLeaveSeconds before it, the whole grace away, then
    /// LagLeaveSeconds to catch up; each of the three ends noticed by a housekeeping pass (the rig's 255 ms) on top.</summary>
    private const double FlapBoundSeconds = 10 + 60 + 10 + 3 * 0.255;

    /// <summary>
    /// A host whose link drops for 2 s every 5 s without a pad in time (the closes of a flapping line) is taken out of his stage group by
    /// the lag rule once he has been connected Relay:LagLeaveSeconds since his first return (the absences left out): 16 s after his
    /// first return, at the fourth return's connected phase; his drops form one absence episode whose grace's countdown runs on (60,
    /// 55, 50, 45 s on the guest's notices). (Before, every return restarted the lag clock and every drop a fresh 60 s grace: he stayed
    /// in the stage group for good, the guest playing against his standing Digimon.) He stays the instance's host and plays apart.
    /// </summary>
    [Fact]
    public void A_host_whose_link_keeps_closing_without_a_pad_in_time_is_taken_out_by_the_lag_rule_after_his_connected_allowance()
    {
        var r = new FlapRig();
        r.Play(1000);
        var firstDrop = r.Seconds;
        double firstBack = 0;
        var drops = 0;
        while (!r.Out("a") && r.Seconds - firstDrop < 200)
        {
            r.Drop("a");
            ++drops;
            Assert.True(r.W.HostAway);
            r.Mode["a"] = 0;
            if (r.Play(2000, () => r.Out("a"))) break;
            r.Back("a");
            Assert.False(r.W.HostAway);
            if (firstBack == 0) firstBack = r.Seconds;
            r.Mode["a"] = 1;
            r.Play(3000, () => r.Out("a"));
        }
        Assert.True(r.LagLeft, "the flapping host is still in his stage group after 200 s");
        // Connected 3 + 3 + 3 + 1 s since his first return, three absences of 2 s between: 16 s (one pass of slack each way).
        Assert.InRange(r.Seconds - firstBack, 16 - 0.3, 16 + 0.6);
        Assert.Equal(4, drops);
        Assert.Equal(3, r.H.Hub.Counters.GetValueOrDefault("host_grace_continued"));
        Assert.Equal(4, r.H.Hub.Counters.GetValueOrDefault("host_grace"));
        Assert.False(r.W.Ending);
        Assert.Same(r.H.Mem("a"), r.W.Host);
        Assert.Contains(r.H.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse), n => n.Kind == WorldNoticeKind.Apart);
        Assert.Equal(new uint[] { 60, 55, 50, 45 }, r.AwayNotices().Select(n => n.Argument).Take(4).ToArray());
    }

    /// <summary>
    /// The same over a half-open link: 4 s of silence start each grace (19.6.7), his held-back late pads end it. The silence before each
    /// grace counts as connected time, so the lag rule takes him out 10 s of connected time after his first return, during his third
    /// silence. (Before, each return restarted the clock and the next grace came 4 s later, before LagLeaveSeconds: for good.)
    /// </summary>
    [Fact]
    public void A_host_whose_link_keeps_falling_silent_without_a_pad_in_time_is_taken_out_by_the_lag_rule_after_his_connected_allowance()
    {
        var r = new FlapRig();
        r.Play(1000);
        var silentFrom = r.Seconds;
        double firstBack = 0;
        while (!r.Out("a") && r.Seconds - silentFrom < 200)
        {
            r.Mode["a"] = 0;
            if (r.Play(5000, () => r.Out("a"))) break;
            Assert.True(r.W.HostAway);
            r.Mode["a"] = 1; // the held-back pads arrive late: INPUT, he is back
            if (r.Play(2000, () => r.Out("a"))) break;
            if (firstBack == 0) firstBack = r.Seconds - 2;
            Assert.False(r.W.HostAway);
        }
        Assert.True(r.LagLeft, "the flapping host is still in his stage group after 200 s");
        // Returns at +5 s (late pads 2 s, silence 4 s), +12 s (2 s, then 2 s of silence): 10 s connected at about +16 s.
        Assert.InRange(r.Seconds - silentFrom, 16 - 0.3, 16 + 0.6);
        Assert.InRange(firstBack - silentFrom, 5, 5.3);
        Assert.Equal(1, r.H.Hub.Counters.GetValueOrDefault("host_grace_continued"));
        Assert.False(r.W.Ending);
    }

    /// <summary>
    /// A drop less than Relay:LagLeaveSeconds after his return keeps the grace's deadline: a host away 50 s, back for 3 s of late pads,
    /// then gone again has the 7 s the first grace had left, not a fresh 60 s; the guest sees 7 s, and the instance ends (host_lost) when
    /// they are over. A return just after the deadline, before the housekeeping's pass, does not save it. (Before, the second drop
    /// started a fresh 60 s grace: a link that drops just before each grace's end kept the guests waiting for good.)
    /// </summary>
    [Fact]
    public void A_drop_soon_after_a_return_keeps_the_graces_deadline_and_its_end_ends_the_instance()
    {
        var r = new FlapRig();
        r.Play(1000);
        var firstDrop = r.Seconds;
        r.Drop("a");
        r.Mode["a"] = 0;
        r.Play(50_000);
        Assert.False(r.W.Ending);
        r.Back("a");
        r.Mode["a"] = 1;
        r.Play(3000);
        Assert.False(r.LagLeft);
        r.Drop("a");
        var secondDrop = r.Seconds;
        r.Mode["a"] = 0;
        Assert.Equal(7u, r.AwayNotices().Last().Argument);
        // Just past the deadline, between two housekeeping passes: a reconnect is not in time.
        r.H.Advance((firstDrop + 60.05 - r.Seconds) * 1000);
        r.Back("a");
        Assert.True(r.W.HostAway);
        Assert.True(r.Play(1000, () => r.W.Ending));
        Assert.Equal(EndReason.HostLost, r.W.EndReason);
        Assert.InRange(r.Seconds - firstDrop, 60, 60 + 0.6);
        Assert.InRange(r.Seconds - secondDrop, 7 - 0.3, 7 + 0.6);
        Assert.Equal(1, r.H.Hub.Counters.GetValueOrDefault("host_grace_continued"));
    }

    /// <summary>
    /// The bound: however a host's link drops and comes back, he is out of the guests' stage group (lag_leave) or his instance has ended
    /// (host_lost) at most Relay:LagLeaveSeconds + World:HostGraceSeconds + Relay:LagLeaveSeconds (80 s, plus the housekeeping's passes)
    /// after his slot began to fill: the worst case of one absence (lagging 9.9 s, closed, back after 59.9 s, 10 s to catch up), unless
    /// he comes back for Relay:LagLeaveSeconds with his pads in time. Patterns: lagging before the first drop, a long first absence,
    /// then drops (closes or silences) of varying length that use up the connected allowance and the grace in different orders, and
    /// "burst" ones whose last 100 ms before each drop come in time (the real client's pads of the last ticks before a return, W7g:
    /// each ends the fill streak, so only the episode's deadline holds them). (Before, every pattern kept him in for good.)
    /// </summary>
    [Fact]
    public void A_flapping_host_is_out_or_his_instance_ended_within_the_single_absence_bound()
    {
        // (lagging before the first drop, the first absence, then repeated: away, back; silence: drops by silence (the grace from its
        // 4th second); burst: the last 100 ms of each return's pads in time)
        var patterns = new (double Lag, double First, double Away, double Back, bool Silence, bool Burst)[]
        {
            (9.5, 55, 3, 2, false, false),    // the grace's deadline first: host_lost
            (9.5, 50, 0.5, 9, false, false),  // the connected allowance first: lag_leave
            (9.5, 59, 1, 9.9, false, false),  // both at their ends: the tightest
            (0, 30, 2, 3, false, false),
            (9.5, 45, 6, 1, true, false),     // silences of 6 s
            (0, 5, 5, 0.5, true, false),
            (0, 3, 3, 4, false, true),        // pads in time before each drop: only the deadline holds
            (9.5, 2, 7, 0.3, true, true),     // W7g's shape: silences of 7 s, a short window with a burst
            (9.5, 59, 2, 9.8, false, true),   // back at the deadline's last second, a burst, gone again
        };
        var problems = new List<string>();
        foreach (var p in patterns)
        {
            var r = new FlapRig();
            r.Play(1000);
            var start = r.Seconds;
            r.Mode["a"] = 1; // his pads late from here: the slot begins to fill
            r.Play(p.Lag * 1000, () => r.Out("a"));
            Assert.False(r.Out("a"));
            r.Drop("a");
            r.Mode["a"] = 0;
            var ended = r.Play(p.First * 1000, () => r.Out("a"));
            var rounds = 0;
            while (!ended && r.Seconds - start < 300)
            {
                if (!r.H.Mem("a").Connected) r.Back("a");
                r.Mode["a"] = 1;
                if (r.Play((p.Burst ? p.Back - 0.1 : p.Back) * 1000, () => r.Out("a"))) break;
                if (p.Burst)
                {
                    r.Mode["a"] = 2;
                    if (r.Play(100, () => r.Out("a"))) break;
                }
                if (!p.Silence) r.Drop("a");
                r.Mode["a"] = 0;
                ended = r.Play(p.Away * 1000, () => r.Out("a"));
                ++rounds;
            }
            var elapsed = r.Seconds - start;
            if (!r.Out("a")) problems.Add($"pattern {p}: the host is still in after {elapsed:F1} s ({rounds} rounds)");
            else if (elapsed > FlapBoundSeconds) problems.Add($"pattern {p}: out after {elapsed:F1} s, beyond {FlapBoundSeconds} s");
            // Out by the lag rule, or the grace's end ended the instance; never both, never a guest's reconnect timeout for a host.
            else if (!(r.LagLeft ^ (r.W.EndReason == EndReason.HostLost))) problems.Add($"pattern {p}: lag_leave {r.LagLeft}, end {r.W.EndReason}");
            else if (r.H.Hub.Counters.ContainsKey("reconnect_timeout")) problems.Add($"pattern {p}: reconnect_timeout");
        }
        Assert.True(problems.Count == 0, string.Join("\n", problems));
    }

    /// <summary>
    /// What 2b009cee keeps (19.6.7, "back in time: play on"): a host back once after nearly the whole grace whose client takes 9 s to
    /// catch up plays on; back for longer than Relay:LagLeaveSeconds with his pads in time, a later drop is a new loss: a fresh grace
    /// (60 s on the guest's countdown), and its return gets the full Relay:LagLeaveSeconds again (8 s of late pads before that drop,
    /// 15 s away, 9 s of late pads after it: still in).
    /// </summary>
    [Fact]
    public void A_host_back_once_after_nearly_the_whole_grace_plays_on_and_a_stable_return_renews_his_grace_and_lag_allowance()
    {
        var r = new FlapRig();
        r.Play(1000);
        r.Drop("a");
        r.Mode["a"] = 0;
        r.Play(59_000);
        r.Back("a");
        r.Mode["a"] = 1;
        r.Play(9000);
        r.Mode["a"] = 2;
        r.Play(20_000);
        Assert.False(r.Out("a"));
        Assert.True(r.I.Streams[r.H.Mem("a").Slot]!.FillStreak < SlotStream.LagFills);
        // His line gets bad again (8 s of late pads: a new fill streak), then drops: a fresh grace, and its return the full lag
        // allowance again (a credit kept from the first return would leave him 2 s: the lag clock of before the drop).
        r.Mode["a"] = 1;
        r.Play(8000);
        Assert.False(r.Out("a"));
        r.Drop("a");
        r.Mode["a"] = 0;
        Assert.Equal(60u, r.AwayNotices().Last().Argument);
        r.Play(15_000);
        r.Back("a");
        r.Mode["a"] = 1;
        r.Play(9000);
        r.Mode["a"] = 2;
        r.Play(20_000);
        Assert.False(r.Out("a"));
        Assert.False(r.H.Hub.Counters.ContainsKey("host_grace_continued"));
        Assert.Equal(2, r.H.Hub.Counters.GetValueOrDefault("host_grace"));
        Assert.DoesNotContain(r.H.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse), n => n.Kind == WorldNoticeKind.Apart);
    }

    /// <summary>
    /// Pads in time between the drops do not renew the grace: a host whose link drops for 2 s every 5 s, his pads in time for 2.5 s of
    /// each return, flaps (one absence episode: 60, 55, 50 ... s on the guest's countdown) and his instance ends 60 s after the first
    /// drop. Back for 11 s between drops of 2 s (longer than Relay:LagLeaveSeconds), each drop is a new loss with a fresh grace and he
    /// plays on. (Before, both played on for good.)
    /// </summary>
    [Fact]
    public void A_host_whose_link_keeps_dropping_loses_his_instance_when_the_episodes_grace_is_over_unless_he_stays_back_between()
    {
        foreach (var backSeconds in new[] { 3.0, 11.0 })
        {
            var r = new FlapRig();
            r.Play(1000);
            var firstDrop = r.Seconds;
            for (var k = 0; k < 20 && !r.W.Ending; ++k)
            {
                r.Drop("a");
                r.Mode["a"] = 0;
                if (r.Play(2000, () => r.W.Ending)) break;
                r.Back("a");
                r.Mode["a"] = 1;
                if (r.Play(500, () => r.W.Ending)) break;
                r.Mode["a"] = 2;
                if (r.Play((backSeconds - 0.5) * 1000, () => r.W.Ending)) break;
            }
            Assert.False(r.LagLeft);
            if (backSeconds < 10)
            {
                Assert.Equal(EndReason.HostLost, r.W.EndReason);
                // The deadline fell into a return; the next drop (within 5 s) ended it at the following pass.
                Assert.InRange(r.Seconds - firstDrop, 60, 65 + 0.6);
                Assert.Equal(new uint[] { 60, 55, 50, 45 }, r.AwayNotices().Select(n => n.Argument).Take(4).ToArray());
                continue;
            }
            Assert.False(r.Out("a"));
            Assert.Equal(20, r.H.Hub.Counters.GetValueOrDefault("host_grace"));
            Assert.False(r.H.Hub.Counters.ContainsKey("host_grace_continued"));
            Assert.All(r.AwayNotices(), n => Assert.Equal(60u, n.Argument));
        }
    }

    /// <summary>
    /// A flapping guest (4) does not share the host's path: no grace, and his reconnects never restart his slot's lag clock, so the lag
    /// rule takes him out Relay:LagLeaveSeconds after his fill streak's first fill however his link drops and comes back (the reconnect
    /// timeout, Relay:ReconnectSeconds, counts each drop on its own); with his pads in time between the drops he plays on. The instance and
    /// the host's grace are untouched.
    /// </summary>
    [Fact]
    public void A_flapping_guest_is_taken_out_by_the_lag_rule_within_the_lag_limit_of_his_streak_and_plays_on_with_pads_in_time()
    {
        foreach (var inTime in new[] { false, true })
        {
            var r = new FlapRig();
            r.Play(1000);
            var firstDrop = r.Seconds;
            for (var k = 0; k < 12 && !r.Out("b"); ++k)
            {
                r.Drop("b");
                r.Mode["b"] = 0;
                if (r.Play(2000, () => r.Out("b"))) break;
                r.Back("b");
                r.Mode["b"] = 1;
                if (r.Play(inTime ? 500 : 3000, () => r.Out("b"))) break;
                r.Mode["b"] = inTime ? 2 : 1;
                if (inTime && r.Play(2500, () => r.Out("b"))) break;
            }
            Assert.False(r.H.Hub.Counters.ContainsKey("host_grace"));
            Assert.False(r.W.Ending);
            if (inTime)
            {
                Assert.False(r.Out("b"));
                continue;
            }
            Assert.True(r.LagLeft);
            Assert.InRange(r.Seconds - firstDrop, 10 - 0.3, 10 + 0.6);
            Assert.False(r.H.Hub.Counters.ContainsKey("reconnect_timeout"));
        }
    }

    /// <summary>The slot's clock itself: the first return of a fill streak restarts it, a later one moves it on by the absence only, a pad
    /// sealed in time ends the streak and the credit.</summary>
    [Fact]
    public void The_slot_clock_restarts_at_the_first_return_of_a_fill_streak_and_later_returns_only_skip_their_absence()
    {
        var f = Stopwatch.Frequency;
        var s = new SlotStream(0, 0, 3);
        s.Take(0, 1 * f, out _); // filled: the streak begins at 1 s
        Assert.Equal(1 * f, s.FillStreakSince);
        s.HostReturned(20 * f, 2 * f); // away 2..20 s: the clock from 20 s
        Assert.Equal((20 * f, true), (s.FillStreakSince, s.ReturnCredited));
        s.HostReturned(30 * f, 25 * f); // connected 20..25, away 25..30: 5 s of connected time counted, the clock at 25 s
        Assert.Equal(25 * f, s.FillStreakSince);
        Assert.Equal(SlotStream.Submit.Accepted, s.Offer(1, new Pad(1, 0, 0, 0, 0), 1, 10));
        s.Take(1, 31 * f, out var filled); // a pad in time: the streak and its credit end
        Assert.False(filled);
        Assert.Equal((0, false), (s.FillStreak, s.ReturnCredited));
        s.Take(2, 32 * f, out _);
        s.HostReturned(40 * f, 33 * f); // a new streak's first return: restarted again
        Assert.Equal((40 * f, true), (s.FillStreakSince, s.ReturnCredited));
        var quiet = new SlotStream(1, 0, 3);
        quiet.HostReturned(5 * f, 1 * f); // no fill streak: nothing to credit
        Assert.False(quiet.ReturnCredited);
    }
}
