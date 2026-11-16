using System.Diagnostics;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using DW4Online.Relay.World;

namespace DW4Online.Tests;

/// <summary>
/// A link that keeps dropping ("flapping", SEAMLESS.md 19.6.7, fix-flap). The host's absence debt: from the start of a grace on, the
/// time he holds up the guests of his stage group (away, or his slot filling) counts up, the time he plays with his pads in time (or
/// connected outside such a group) counts down, one for one, to 0, where the episode ends. A grace lasts at most World:HostGraceSeconds +
/// Relay:LagLeaveSeconds minus the debt (World:HostGraceSeconds at most); at that debt a lagging host is out of the stage group
/// (lag_leave). So a stretch without play in time ends within the worst case of one absence (Relay:LagLeaveSeconds + 70 s), a host who
/// plays in time at least as long as he holds his guests up plays on, and a host outside a stage group with a guest gets fresh graces.
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
        /// <summary>Seconds a played in I with his pads in time (mode 2, connected).</summary>
        public double InTime { get; set; }
        /// <summary>The first HostAway notice of each grace <see cref="DropAndNote"/> started (the grace's length in seconds).</summary>
        public List<uint> GraceStarts { get; } = [];
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
        public bool Connected(string name) => H.Mem(name).Connected;

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
                var t0 = H.Now;
                var inTime = Mode["a"] == 2 && Connected("a") && H.Mem("a").Instance == I;
                H.Seal(17);
                if (inTime) InTime += (H.Now - t0) / (double)Stopwatch.Frequency;
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

        /// <summary>Drops <paramref name="name"/> and notes the length of the grace it started (the guest's first notice of it).</summary>
        public void DropAndNote(string name)
        {
            var before = AwayNotices().Count;
            Drop(name);
            if (AwayNotices().Count > before) GraceStarts.Add(AwayNotices()[before]);
        }

        /// <summary>The HostAway notices b got (their argument: the grace's seconds left).</summary>
        public List<uint> AwayNotices() => H.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse)
            .Where(n => n.Kind == WorldNoticeKind.HostAway).Select(n => n.Argument).ToList();

        /// <summary>The seconds b's last HostAway notice gave (the grace just started, or its countdown).</summary>
        public uint LastAway => AwayNotices().Last();
    }

    /// <summary>The worst case of one absence (2b009cee): lagging up to Relay:LagLeaveSeconds before it, the whole grace away, then
    /// LagLeaveSeconds to catch up; each of the three ends noticed by a housekeeping pass (the rig's 255 ms) on top.</summary>
    private const double FlapBoundSeconds = 10 + 60 + 10 + 3 * 0.255;

    /// <summary>A flapping pattern: lagging before the first drop, the first absence (a close, or a silence: its grace from the 4th
    /// second), then repeated: the return's phases (mode, seconds), the next drop (a close, or a silence) and its absence.</summary>
    private sealed record Flap(string Name, double Lag, double First, double Away, (int Mode, double Seconds)[] Return, bool Silence = false,
        bool FirstSilence = false);

    /// <summary>
    /// The bound: however a host's link drops and comes back, he is out of the guests' stage group (lag_leave) or his instance has ended
    /// (host_lost) by the time the time his slot held them up (from its first fill) exceeds the time he played with his pads in time
    /// since by the worst case of one absence: Relay:LagLeaveSeconds + World:HostGraceSeconds + Relay:LagLeaveSeconds (80 s, plus the
    /// housekeeping's passes and a pass per stretch in time). So a stretch without play in time ends within 80 s. Patterns: lagging
    /// before the first drop, a long first absence, drops (closes or silences) of varying length, "bursts" whose 100 ms before each
    /// drop come in time (the real client's pads of the last ticks before a return, W7g), and (review of 54410943, finding 1) long
    /// absences with 10 s of play in time between, which 54410943 and 0ef550e9 renewed for good (59 s away, 10.5 s back: 85 % away),
    /// and late returns with two in-time bursts (P8: the slot 99 % filled, in for good).
    /// </summary>
    [Fact]
    public void A_flapping_host_is_out_or_his_instance_ended_once_he_held_his_guests_up_longer_than_one_absence_could()
    {
        var late = (double s) => new[] { (1, s) };
        var patterns = new Flap[]
        {
            new("deadline first", 9.5, 55, 3, late(2)),
            new("lag allowance first", 9.5, 50, 0.5, late(9)),
            new("both at their ends", 9.5, 59, 1, late(9.9)),
            new("short closes", 0, 30, 2, late(3)),
            new("closes of 2 s, 3 s late", 0, 2, 2, late(3)),
            new("silences of 6 s", 9.5, 45, 6, late(1), Silence: true),
            new("silences of 5 s", 0, 5, 5, late(0.5), Silence: true),
            new("silences of 5 s, 2 s late", 0, 5, 5, late(2), Silence: true, FirstSilence: true),
            new("burst before each close", 0, 3, 3, [(1, 3.9), (2, 0.1)]),
            new("W7g's shape", 9.5, 2, 7, [(1, 0.2), (2, 0.1)], Silence: true),
            new("back at the deadline, a burst", 9.5, 59, 2, [(1, 9.7), (2, 0.1)]),
            // Review of 54410943, finding 1: back long enough for a fresh grace each time.
            new("P1: 59 s away, 10 s in time", 0, 59, 59, [(1, 0.5), (2, 10)]),
            new("P1s: 63 s silent, 10 s in time", 0, 63, 63, [(1, 0.5), (2, 10)], Silence: true, FirstSilence: true),
            new("30 s away, 10 s in time", 0, 30, 30, [(1, 0.5), (2, 10)]),
            new("20 s away, 10 s in time", 0, 20, 20, [(1, 1), (2, 10)]),
            new("P8: late with two bursts", 0, 59, 59, [(2, 0.1), (1, 5), (2, 0.1), (1, 5.1)]),
        };
        var problems = new List<string>();
        foreach (var p in patterns)
        {
            var r = new FlapRig();
            r.Play(1000);
            var start = r.Seconds;
            r.InTime = 0;
            var stretches = 0;
            r.Mode["a"] = 1; // his pads late from here: the slot begins to fill
            r.Play(p.Lag * 1000, () => r.Out("a"));
            Assert.False(r.Out("a"));
            if (!p.FirstSilence) r.Drop("a");
            r.Mode["a"] = 0;
            var ended = r.Play(p.First * 1000, () => r.Out("a"));
            while (!ended && r.Seconds - start < 300)
            {
                if (!r.Connected("a")) r.Back("a");
                foreach (var (mode, seconds) in p.Return)
                {
                    r.Mode["a"] = mode;
                    if (mode == 2) ++stretches;
                    if (r.Play(seconds * 1000, () => r.Out("a")))
                    {
                        ended = true;
                        break;
                    }
                }
                if (ended) break;
                if (!p.Silence) r.Drop("a");
                r.Mode["a"] = 0;
                ended = r.Play(p.Away * 1000, () => r.Out("a"));
            }
            var elapsed = r.Seconds - start;
            var net = elapsed - 2 * r.InTime; // (held up minus played in time)
            var bound = FlapBoundSeconds + 0.35 * stretches;
            if (!r.Out("a")) problems.Add($"{p.Name}: the host is still in after {elapsed:F1} s");
            else if (net > bound) problems.Add($"{p.Name}: out after {elapsed:F1} s, {r.InTime:F1} s in time: {net:F1} s beyond {bound:F1} s");
            // Out by the lag rule, or the grace's end ended the instance; never both, never a guest's reconnect timeout for a host.
            else if (!(r.LagLeft ^ (r.W.EndReason == EndReason.HostLost))) problems.Add($"{p.Name}: lag_leave {r.LagLeft}, end {r.W.EndReason}");
            else if (r.H.Hub.Counters.ContainsKey("reconnect_timeout")) problems.Add($"{p.Name}: reconnect_timeout");
        }
        Assert.True(problems.Count == 0, string.Join("\n", problems));
    }

    /// <summary>
    /// Review of 54410943, finding 2: a host who is mostly there and plays in time between brief drops plays on, every grace a fresh
    /// one (60 s on the guest's countdown): closes of 1 s every 9 s and every 10.5 s (P5; 54410943 ended the instance after 63 s:
    /// the drops less than 10 s apart were one episode whatever he played between), half-open stalls of 4.5 s with 5 s of play (P6:
    /// host_lost after 70.7 s), closes of 2 s every 5 s with 2.5 s in time (54410943's own test ended it 60 s after the first drop).
    /// He holds his guests up at most as long as he plays in time: his debt goes back to 0.
    /// </summary>
    [Fact]
    public void A_host_who_plays_in_time_between_brief_drops_plays_on_with_a_fresh_grace_at_every_drop()
    {
        var shapes = new (string Name, double Away, bool Silence, double Late, double InTime)[]
        {
            ("P5: a close of 1 s every 9 s", 1, false, 0.5, 7.5),
            ("P5: a close of 1 s every 10.5 s", 1, false, 0.5, 9),
            ("P6: half-open stalls of 4.5 s, 5 s of play", 4.5, true, 0.5, 4.5),
            ("closes of 2 s every 5 s, 2.5 s in time", 2, false, 0.5, 2.5),
        };
        var problems = new List<string>();
        foreach (var s in shapes)
        {
            var r = new FlapRig();
            r.Play(1000);
            var start = r.Seconds;
            var drops = 0;
            while (!r.Out("a") && r.Seconds - start < 300)
            {
                ++drops;
                if (!s.Silence) r.Drop("a");
                r.Mode["a"] = 0;
                if (r.Play(s.Away * 1000, () => r.Out("a"))) break;
                if (!r.Connected("a")) r.Back("a");
                r.Mode["a"] = 1;
                if (r.Play(s.Late * 1000, () => r.Out("a"))) break;
                r.Mode["a"] = 2;
                if (r.Play(s.InTime * 1000, () => r.Out("a"))) break;
            }
            var graces = r.H.Hub.Counters.GetValueOrDefault("host_grace");
            if (r.Out("a")) problems.Add($"{s.Name}: out after {r.Seconds - start:F1} s (ending {r.W.Ending}, lag_leave {r.LagLeft})");
            else if (graces < drops - 1) problems.Add($"{s.Name}: {graces} graces for {drops} drops");
            else if (r.AwayNotices().Any(n => n != 60) || r.H.Hub.Counters.ContainsKey("host_grace_shortened"))
                problems.Add($"{s.Name}: a grace not fresh: {string.Join(",", r.AwayNotices().Distinct())}");
        }
        Assert.True(problems.Count == 0, string.Join("\n", problems));
    }

    /// <summary>
    /// Review of 54410943, finding 2 (P7): a host out of the guests' stage group (lag_leave) whose link keeps dropping while he plays
    /// apart does not end the instance: no guest plays with him, so every grace is fresh (also with his debt from an absence of 59 s
    /// just before). 54410943 ended the instance 60 s after his first drop apart and sent his guest home.
    /// </summary>
    [Fact]
    public void A_host_playing_apart_whose_link_keeps_dropping_keeps_his_instance_with_a_fresh_grace_at_every_drop()
    {
        foreach (var away59 in new[] { false, true })
        {
            var r = new FlapRig();
            r.Play(1000);
            if (away59)
            {
                r.Drop("a");
                r.Mode["a"] = 0;
                r.Play(59_000);
                r.Back("a");
            }
            r.Mode["a"] = 1;
            Assert.True(r.Play(15_000, () => r.LagLeft));
            Assert.Null(r.H.Mem("a").Instance);
            var start = r.Seconds;
            while (!r.W.Ending && r.Seconds - start < 300)
            {
                r.DropAndNote("a");
                if (r.Play(2000, () => r.W.Ending)) break;
                r.Back("a");
                if (r.Play(3000, () => r.W.Ending)) break;
            }
            Assert.False(r.W.Ending);
            Assert.Same(r.I, r.H.Mem("b").Instance);
            Assert.Same(r.H.Mem("a"), r.W.Host);
            Assert.True(r.GraceStarts.Count >= 59);
            Assert.All(r.GraceStarts, g => Assert.Equal(60u, g));
        }
    }

    /// <summary>
    /// Review of 54410943, finding 4 (P3): a stall of the client (4.5 s of silence: a grace of half a second) does not shorten a real
    /// absence 9 s later: a close of 55 s gets a fresh 60 s grace and he plays on (54410943 gave it 51 s and ended the instance). The
    /// half second of the stall's grace is paid off by the play in time after it.
    /// </summary>
    [Fact]
    public void A_client_stall_does_not_shorten_a_later_absence()
    {
        var r = new FlapRig();
        r.Play(1000);
        r.Mode["a"] = 0;
        r.Play(4500);
        Assert.True(r.W.HostAway);
        r.Mode["a"] = 2;
        r.Play(9000);
        Assert.False(r.W.HostAway);
        r.DropAndNote("a");
        r.Mode["a"] = 0;
        Assert.Equal(60u, r.LastAway);
        Assert.False(r.Play(55_000, () => r.W.Ending));
        r.Back("a");
        r.Mode["a"] = 1;
        r.Play(2000);
        r.Mode["a"] = 2;
        r.Play(20_000);
        Assert.False(r.Out("a"));
        Assert.False(r.H.Hub.Counters.ContainsKey("host_grace_shortened"));
    }

    /// <summary>
    /// Review of 54410943, finding 4 (P2): a stall of 4.5 s while the host catches up 1 s after an absence of 57 s gets what his debt
    /// leaves (57 s away, 1 s late, 4 s silent: 8 s of grace) instead of ending the instance at once (54410943: one episode, a grace of
    /// 0 s, host_lost); back, he catches up and plays on.
    /// </summary>
    [Fact]
    public void A_client_stall_while_catching_up_after_a_long_absence_does_not_end_the_instance()
    {
        var r = new FlapRig();
        r.Play(1000);
        r.Drop("a");
        r.Mode["a"] = 0;
        r.Play(57_000);
        r.Back("a");
        r.Mode["a"] = 1;
        r.Play(1000);
        r.Mode["a"] = 0;
        r.Play(4500);
        Assert.True(r.W.HostAway);
        Assert.InRange(r.LastAway, 7u, 9u);
        Assert.False(r.W.Ending);
        r.Mode["a"] = 1;
        r.Play(2000);
        Assert.False(r.W.HostAway);
        r.Mode["a"] = 2;
        r.Play(20_000);
        Assert.False(r.Out("a"));
    }

    /// <summary>
    /// Review of 54410943, finding 3 (P4): a host's return after a new absence gets Relay:LagLeaveSeconds to catch up again, also when
    /// no pad of his came in time since his last return: away 20 s, back, both members lag 12 s (everyone lags: nobody out), a drops
    /// again for 20 s and comes back with late pads: lag_leave 10 s after his return (54410943 kept the first return's credit and took
    /// him out 0.02 s after host_back, as before 2b009cee).
    /// </summary>
    [Fact]
    public void A_return_after_a_new_absence_gets_the_whole_lag_allowance_again()
    {
        var r = new FlapRig();
        r.Play(1000);
        r.Drop("a");
        r.Mode["a"] = 0;
        r.Play(20_000);
        r.Back("a");
        r.Mode["a"] = 1;
        r.Mode["b"] = 1;
        r.Play(12_000);
        Assert.False(r.Out("a"));
        r.Mode["b"] = 2;
        r.Drop("a");
        r.Mode["a"] = 0;
        r.Play(20_000);
        Assert.False(r.W.Ending);
        r.Back("a");
        var back = r.Seconds;
        r.Mode["a"] = 1;
        Assert.True(r.Play(15_000, () => r.Out("a")));
        Assert.True(r.LagLeft);
        Assert.InRange(r.Seconds - back, 10 - 0.3, 10 + 0.6);
    }

    /// <summary>
    /// Review of 54410943, finding 3 (P9): a drop just past Relay:LagLeaveSeconds after a return, before the next housekeeping pass
    /// (9.9 s lagging, 59 s away, back 10.013 s with late pads, 59 s away again) has what the debt leaves (1 s): the instance ends within
    /// the bound of one absence (54410943: outside its episode window, a fresh grace; out only 138 s after the first fill).
    /// </summary>
    [Fact]
    public void A_drop_between_two_housekeeping_passes_stays_inside_the_bound_of_one_absence()
    {
        var r = new FlapRig();
        r.Play(1000);
        var start = r.Seconds;
        r.Mode["a"] = 1;
        r.Play(9900);
        r.Drop("a");
        r.Mode["a"] = 0;
        r.Play(59_000);
        r.Back("a");
        var back = r.Seconds;
        r.Mode["a"] = 1;
        // Up to the last pass before 10 s after the return, then on without a pass: the drop comes before the lag rule looks.
        while (r.Seconds - back < 9.7) r.Play(17);
        r.H.Seal((back + 10.013 - r.Seconds) * 1000);
        Assert.False(r.Out("a"));
        r.Drop("a");
        r.Mode["a"] = 0;
        Assert.True(r.Play(59_000, () => r.Out("a")));
        Assert.Equal(EndReason.HostLost, r.W.EndReason);
        Assert.InRange(r.Seconds - start, 70, FlapBoundSeconds);
    }

    /// <summary>
    /// Review of 54410943, finding 5: the debt's limit is World:HostGraceSeconds + Relay:LagLeaveSeconds, so a server that gives laggers
    /// more time (Relay:LagLeaveSeconds 30 or 60) is more lenient with a host's drops too, never stricter (54410943's episode window was
    /// LagLeaveSeconds of wall clock: at 30 and 60 a drop 25.5 s after a return of 2 s got 33 s instead of 60). A 2 s drop, 25.5 s in
    /// time, a 15 s drop: a fresh 60 s grace at 10, 30 and 60; a 59 s absence, 5 s in time, a drop: 16 s, 36 s, 60 s.
    /// </summary>
    [Fact]
    public void A_longer_lag_allowance_makes_the_episode_more_lenient_not_stricter()
    {
        foreach (var lagLeave in new[] { 10, 30, 60 })
        {
            var r = new FlapRig(lagLeave);
            r.Play(1000);
            r.DropAndNote("a");
            r.Mode["a"] = 0;
            r.Play(2000);
            r.Back("a");
            r.Mode["a"] = 2;
            r.Play(25_500);
            r.DropAndNote("a");
            r.Mode["a"] = 0;
            r.Play(15_000);
            r.Back("a");
            r.Mode["a"] = 2;
            r.Play(5000);
            Assert.False(r.Out("a"));
            Assert.Equal(new uint[] { 60, 60 }, r.GraceStarts);
        }
        foreach (var (lagLeave, second) in new[] { (10, 16u), (30, 36u), (60, 60u) })
        {
            var r = new FlapRig(lagLeave);
            r.Play(1000);
            r.Drop("a");
            r.Mode["a"] = 0;
            r.Play(59_000);
            r.Back("a");
            r.Mode["a"] = 2;
            r.Play(5000);
            r.DropAndNote("a");
            Assert.InRange(r.GraceStarts.Single(), second, Math.Min(60u, second + 1)); // (the notice rounds up)
        }
    }

    /// <summary>
    /// What 2b009cee keeps (19.6.7, "back in time: play on"): a host back once after nearly the whole grace whose client takes 9 s to
    /// catch up plays on. A drop before he played in time as long as he held his guests up gets what his debt leaves (59 s away, 9 s
    /// late, 20 s in time: 48 s of debt, a grace of 22 s); once he did (80 s more in time), a drop is a new loss with a fresh grace, and
    /// its return gets the full Relay:LagLeaveSeconds again.
    /// </summary>
    [Fact]
    public void A_host_back_once_after_nearly_the_whole_grace_plays_on_and_a_later_drop_gets_what_his_debt_leaves()
    {
        var r = new FlapRig();
        r.Play(1000);
        r.DropAndNote("a");
        r.Mode["a"] = 0;
        r.Play(59_000);
        r.Back("a");
        r.Mode["a"] = 1;
        r.Play(9000);
        r.Mode["a"] = 2;
        r.Play(20_000);
        Assert.False(r.Out("a"));
        Assert.True(r.I.Streams[r.H.Mem("a").Slot]!.FillStreak < SlotStream.LagFills);
        r.DropAndNote("a");
        r.Mode["a"] = 0;
        r.Play(15_000);
        r.Back("a");
        r.Mode["a"] = 1;
        r.Play(6000);
        r.Mode["a"] = 2;
        r.Play(80_000);
        Assert.False(r.Out("a"));
        // Paid off: a fresh grace, and 9 s of late pads after a 15 s absence are inside the lag allowance.
        r.DropAndNote("a");
        r.Mode["a"] = 0;
        r.Play(15_000);
        r.Back("a");
        r.Mode["a"] = 1;
        r.Play(9000);
        r.Mode["a"] = 2;
        r.Play(20_000);
        Assert.False(r.Out("a"));
        Assert.Equal(3, r.GraceStarts.Count);
        Assert.Equal(60u, r.GraceStarts[0]);
        Assert.InRange(r.GraceStarts[1], 21u, 23u);
        Assert.Equal(60u, r.GraceStarts[2]);
        Assert.Equal(1, r.H.Hub.Counters.GetValueOrDefault("host_grace_shortened"));
        Assert.DoesNotContain(r.H.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse), n => n.Kind == WorldNoticeKind.Apart);
    }

    /// <summary>
    /// A drop soon after a long absence: away 50 s, back for 3 s of late pads (53 s of debt), gone again: the grace is the 17 s the debt
    /// leaves (the guest sees 17), and the instance ends (host_lost) when they are over. A return just after the deadline, before the
    /// housekeeping's pass, does not save it.
    /// </summary>
    [Fact]
    public void A_drop_soon_after_a_long_absence_gets_what_the_debt_leaves_and_its_end_ends_the_instance()
    {
        var r = new FlapRig();
        r.Play(1000);
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
        Assert.InRange(r.LastAway, 17u, 18u);
        var deadline = r.W.GraceUntil / (double)Stopwatch.Frequency;
        // Just past the deadline, between two housekeeping passes: a reconnect is not in time.
        r.H.Advance((deadline + 0.05 - r.Seconds) * 1000);
        r.Back("a");
        Assert.True(r.W.HostAway);
        Assert.True(r.Play(1000, () => r.W.Ending));
        Assert.Equal(EndReason.HostLost, r.W.EndReason);
        Assert.InRange(r.Seconds - secondDrop, 17 - 0.5, 17 + 0.8);
        Assert.Equal(1, r.H.Hub.Counters.GetValueOrDefault("host_grace_shortened"));
    }

    /// <summary>
    /// The debt's lag limit: a host whose debt is spent while he lags is out of the stage group by the lag rule, the instance going on
    /// (59 s away, 9 s late: 68 s; a drop of 0.5 s, a grace of 2 s; back with late pads: lag_leave when the debt reaches 70 s, about 1.5 s
    /// after his return, though his lag clock restarted). He stays the instance's host and plays apart.
    /// </summary>
    [Fact]
    public void A_lagging_host_whose_debt_is_spent_is_taken_out_by_the_lag_rule_and_keeps_his_instance()
    {
        var r = new FlapRig();
        r.Play(1000);
        r.Drop("a");
        r.Mode["a"] = 0;
        r.Play(59_000);
        r.Back("a");
        r.Mode["a"] = 1;
        r.Play(9000);
        r.DropAndNote("a");
        r.Mode["a"] = 0;
        Assert.InRange(r.GraceStarts.Single(), 2u, 3u);
        r.Play(500);
        r.Back("a");
        var back = r.Seconds;
        r.Mode["a"] = 1;
        Assert.True(r.Play(5000, () => r.Out("a")));
        Assert.True(r.LagLeft);
        Assert.InRange(r.Seconds - back, 1.0, 2.2);
        Assert.False(r.W.Ending);
        Assert.Same(r.H.Mem("a"), r.W.Host);
        Assert.Contains(r.H.P("b").All(Msg.WorldNotice, WorldNoticeMsg.Parse), n => n.Kind == WorldNoticeKind.Apart);
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
}
