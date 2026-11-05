using System.Diagnostics;

namespace DW4Online.Relay.Hub;

/// <summary>
/// How late a hub seals (SEAMLESS.md 19.17.4, the load gate's "sealing lateness": a SEALED's due time against its send):
/// per sealing pass of an instance with a due tick, the time of the pass (read with the hub's lock held) against the due
/// time of its first due tick, the latest one of the batch. Fixed buckets, no allocation; read and written under the
/// hub's lock.
/// </summary>
public sealed class SealLateness
{
    /// <summary>The buckets' upper bounds in milliseconds; a last bucket holds the rest.</summary>
    public static readonly double[] BoundsMs = [0.25, 0.5, 1, 2, 5, 10];

    private readonly long[] _counts = new long[BoundsMs.Length + 1];

    public long Count { get; private set; }
    public double MaxMs { get; private set; }
    public IReadOnlyList<long> Counts => _counts;

    public void Add(long lateTicks)
    {
        var ms = Math.Max(0, lateTicks) * 1000.0 / Stopwatch.Frequency;
        var k = 0;
        while (k < BoundsMs.Length && ms > BoundsMs[k]) ++k;
        ++_counts[k];
        ++Count;
        if (ms > MaxMs) MaxMs = ms;
    }

    /// <summary>The share of passes later than <paramref name="ms"/> (one of <see cref="BoundsMs"/>).</summary>
    public double ShareOver(double ms)
    {
        if (Count == 0) return 0;
        var k = Array.IndexOf(BoundsMs, ms);
        if (k < 0) throw new ArgumentException("not a bucket bound", nameof(ms));
        long over = 0;
        for (var b = k + 1; b < _counts.Length; ++b) over += _counts[b];
        return (double)over / Count;
    }

    /// <summary>The bucket bound the quantile <paramref name="q"/> lies under (infinity: in the last bucket).</summary>
    public double QuantileBoundMs(double q)
    {
        if (Count == 0) return 0;
        var need = (long)Math.Ceiling(q * Count);
        long seen = 0;
        for (var b = 0; b < _counts.Length; ++b)
        {
            seen += _counts[b];
            if (seen >= need) return b < BoundsMs.Length ? BoundsMs[b] : double.PositiveInfinity;
        }
        return double.PositiveInfinity;
    }
}
