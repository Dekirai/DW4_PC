using DW4Online.Relay.Hub;

namespace DW4Online.Tests;

/// <summary>
/// M4 (a), round I1a: the delay rule's quantiles by selection instead of a sort (the same value), and the PING margin cached per
/// stream for <see cref="DelayController.PingMarginRecords"/> pads and dropped at every change of d.
/// </summary>
public class DelaySelectTests
{
    [Fact]
    public void Selection_gives_the_value_a_sort_gives_with_duplicates_and_any_order()
    {
        var rng = new Random(11);
        for (var round = 0; round < 400; ++round)
        {
            var n = rng.Next(1, 1800);
            var values = Enumerable.Range(0, n).Select(_ => round % 3 == 0 ? rng.Next(0, 8) * 2.5 : rng.NextDouble() * 60 - 20).ToArray();
            if (round % 5 == 1) Array.Sort(values);
            if (round % 5 == 2) Array.Reverse(values);
            var sorted = values.OrderBy(x => x).ToArray();
            foreach (var k in new[] { 0, n - 1, n / 2, rng.Next(n), (int)(n * 0.02), (int)(n * 0.98) }.Where(x => x >= 0 && x < n))
            {
                var copy = (double[])values.Clone();
                Assert.Equal(sorted[k], DelayController.Select(copy, n, k));
            }
        }
    }

    [Fact]
    public void The_ping_margin_is_kept_for_its_records_and_recomputed_after_them()
    {
        var d = new DelayController(3);
        for (var k = 0; k < 600; ++k) d.Record(10, false);
        var first = d.PingMargin();
        Assert.Equal(d.LowMargin(), first);
        // Lower margins come in: the cached value stays for its 300 records, then follows.
        for (var k = 0; k < DelayController.PingMarginRecords - 1; ++k) d.Record(2, false);
        Assert.Equal(first, d.PingMargin());
        for (var k = 0; k < 900; ++k) d.Record(2, false);
        Assert.Equal(d.LowMargin(), d.PingMargin());
        Assert.NotEqual(first, d.PingMargin());
    }
}
