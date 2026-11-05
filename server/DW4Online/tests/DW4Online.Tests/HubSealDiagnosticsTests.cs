using System.Diagnostics;
using System.Security.Cryptography;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using Microsoft.Extensions.Logging.Abstractions;

namespace DW4Online.Tests;

/// <summary>
/// The sealing diagnostics of the load gate (SEAMLESS.md 19.17.4, round I1a's review): the hub's sealing lateness and the
/// sealing thread's passes that found a room's lock taken.
/// </summary>
public partial class HubLogicTests
{
    [Fact]
    public void Sealing_lateness_counts_each_instance_pass_against_its_first_due_tick()
    {
        var h = SealedPair(out _);
        var late = h.Hub.SealLateness;
        Assert.Equal(0, late.Count);
        // Passes every 4 ms: a tick (16.7 ms) is never more than 4 ms late.
        h.Seal(200);
        var onTime = late.Count;
        Assert.True(onTime >= 10, onTime.ToString());
        Assert.True(late.MaxMs <= 4.01, late.MaxMs.ToString());
        Assert.Equal(0, late.ShareOver(5));
        // A pass 30 ms late seals two ticks in one batch; it counts once, with its first (latest) tick's lateness.
        h.Advance(30);
        h.Hub.Seal(h.Now);
        Assert.Equal(onTime + 1, late.Count);
        Assert.True(late.MaxMs > 13 && late.MaxMs <= 30.01, late.MaxMs.ToString());
        Assert.Equal(1, late.Counts[^1]);
        Assert.Equal(double.PositiveInfinity, late.QuantileBoundMs(1));
        Assert.True(late.QuantileBoundMs(0.5) <= 5, late.QuantileBoundMs(0.5).ToString());
        // A pass with nothing due records nothing.
        h.Hub.Seal(h.Now);
        Assert.Equal(onTime + 1, late.Count);
    }

    [Fact]
    public void The_sealing_thread_counts_its_passes_that_find_the_lock_taken()
    {
        // (A hub without a sealing instance: the pass runs on the real clock, the harness's clock is a number.)
        var h = new Harness();
        h.Connect("a", 0);
        var tickets = new RelayTickets(RandomNumberGenerator.GetBytes(32), TimeSpan.FromSeconds(60), TimeProvider.System);
        using var relay = new RelayHub(new RelayOptions(), new SealingOptions(), new InstanceOptions(), tickets, NullLoggerFactory.Instance);
        using var held = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        var holder = new Thread(() =>
        {
            lock (h.Hub.Sync)
            {
                held.Set();
                release.Wait();
            }
        }) { IsBackground = true };
        holder.Start();
        Assert.True(held.Wait(TimeSpan.FromSeconds(10)));
        var before = Stopwatch.GetTimestamp();
        var next = relay.SealRoom(h.Hub);
        Assert.Equal((1L, 1L), (relay.SealPasses, relay.SealTryMisses));
        // A miss comes back a millisecond later.
        Assert.InRange(next - before, Stopwatch.Frequency / 1000, Stopwatch.Frequency / 10);
        release.Set();
        holder.Join();
        Assert.Equal(long.MaxValue, relay.SealRoom(h.Hub)); // nothing to seal
        Assert.Equal((2L, 1L), (relay.SealPasses, relay.SealTryMisses));
    }
}
