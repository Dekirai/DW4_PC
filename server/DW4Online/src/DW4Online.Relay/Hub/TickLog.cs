using System.IO.Compression;
using System.Numerics;
using DW4Online.Relay.Protocol;

namespace DW4Online.Relay.Hub;

/// <summary>
/// An instance's tick records since its visit's snapshot (SEAMLESS.md 6.8): blocks of <see cref="BlockTicks"/> records,
/// every full block compressed with Deflate on its own (zlib 6 is as good as zstd 3 on these blocks, 6.6). A forked
/// child shares its parent's records up to the fork's last common tick (<see cref="Prefix"/>) instead of copying them.
/// </summary>
public sealed class TickLog
{
    public const int BlockTicks = 600;

    private readonly TickLog? _prefix;
    private readonly uint _prefixCount;
    private readonly List<byte[]> _blocks = []; // compressed full blocks of this log's own records
    private readonly List<int> _blockRaw = [];
    private List<byte[]> _open = new(BlockTicks); // this log's own records after the last full block
    private uint _own;

    public TickLog()
    {
    }

    /// <summary>A child's log: the first <paramref name="prefixCount"/> records of <paramref name="parent"/>, then its own.</summary>
    public TickLog(TickLog parent, uint prefixCount)
    {
        if (prefixCount > parent.Count) throw new ArgumentOutOfRangeException(nameof(prefixCount));
        _prefix = parent;
        _prefixCount = prefixCount;
    }

    public TickLog? Prefix => _prefix;
    /// <summary>Records shared with the parent (they cannot be dropped).</summary>
    public uint PrefixCount => _prefixCount;

    /// <summary>Records 0..Count-1.</summary>
    public uint Count => _prefixCount + _own;

    /// <summary>Uncompressed bytes of this log's own records.</summary>
    public long RawBytes { get; private set; }

    /// <summary>Uncompressed bytes of the records 0..Count-1 (with a parent's whole log: an upper bound).</summary>
    public long TotalRawBytes => RawBytes + (_prefix?.TotalRawBytes ?? 0);

    /// <summary>Bytes this log's own records take in memory (compressed blocks plus the open block).</summary>
    public long StoredBytes => _blocks.Sum(b => (long)b.Length) + _open.Sum(r => (long)r.Length);

    public void Append(byte[] record)
    {
        _open.Add(record);
        ++_own;
        RawBytes += record.Length;
        if (_open.Count == BlockTicks) CloseBlock();
    }

    /// <summary>Drops every record from <paramref name="count"/> on (a barrier discards the ticks sealed after B).</summary>
    public void TruncateTo(uint count)
    {
        if (count >= Count) return;
        if (count < _prefixCount) throw new InvalidOperationException("a log cannot drop records it shares with its parent");
        var keep = Range(_prefixCount, count).ToList();
        _blocks.Clear();
        _blockRaw.Clear();
        _open = new List<byte[]>(BlockTicks);
        _own = 0;
        RawBytes = 0;
        foreach (var r in keep) Append(r);
    }

    /// <summary>The records from..to-1 (to clamped to <see cref="Count"/>).</summary>
    public IEnumerable<byte[]> Range(uint from, uint to)
    {
        to = Math.Min(to, Count);
        if (from >= to) yield break;
        if (from < _prefixCount)
        {
            foreach (var r in _prefix!.Range(from, Math.Min(to, _prefixCount))) yield return r;
            from = _prefixCount;
        }
        var ownFrom = from - _prefixCount;
        var ownTo = to - _prefixCount;
        var block = (int)(ownFrom / BlockTicks);
        while (ownFrom < ownTo)
        {
            IReadOnlyList<byte[]> records = block < _blocks.Count ? Decompress(block) : _open;
            var start = (int)(ownFrom - (uint)block * BlockTicks);
            var end = (int)Math.Min((uint)records.Count, ownTo - (uint)block * BlockTicks);
            for (var i = start; i < end; ++i) yield return records[i];
            ownFrom = (uint)(block + 1) * BlockTicks;
            ++block;
        }
    }

    public byte[]? At(uint tick) => tick < Count ? Range(tick, tick + 1).First() : null;

    /// <summary>
    /// The records 0..Count-1 as they are now, readable without the room lock while this log keeps growing (a state
    /// transfer is built outside it): the compressed blocks are never changed once made, the open block is copied.
    /// </summary>
    public TickLogView Capture() => new(_prefix?.Capture(), _prefixCount, [.. _blocks], [.. _blockRaw], [.. _open], Count);

    internal static List<byte[]> Unpack(byte[] packed, int rawLength)
    {
        var raw = new byte[rawLength];
        using (var z = new ZLibStream(new MemoryStream(packed), CompressionMode.Decompress))
            z.ReadExactly(raw);
        var list = new List<byte[]>(BlockTicks);
        var pos = 0;
        while (pos < raw.Length)
        {
            var len = RecordLength(raw.AsSpan(pos));
            list.Add(raw.AsSpan(pos, len).ToArray());
            pos += len;
        }
        return list;
    }

    private void CloseBlock()
    {
        var raw = new MemoryStream(_open.Sum(r => r.Length));
        foreach (var r in _open) raw.Write(r);
        var packed = new MemoryStream();
        using (var z = new ZLibStream(packed, CompressionLevel.Optimal, leaveOpen: true)) z.Write(raw.GetBuffer(), 0, (int)raw.Length);
        _blocks.Add(packed.ToArray());
        _blockRaw.Add((int)raw.Length);
        _open = new List<byte[]>(BlockTicks);
    }

    private List<byte[]> Decompress(int block) => Unpack(_blocks[block], _blockRaw[block]);

    /// <summary>The length of the tick record at the start of <paramref name="s"/> (the layout of <see cref="TickRecord"/>).</summary>
    /// <exception cref="ProtocolException">The bytes end inside the record.</exception>
    public static int RecordLength(ReadOnlySpan<byte> s)
    {
        if (s.Length < 2) throw new ProtocolException("tick record cut short");
        var mask = s[0] & Wire.SlotMask;
        var events = s[1];
        var pos = 2 + Pad.Size * BitOperations.PopCount((uint)mask);
        for (var i = 0; i < events; ++i)
        {
            if (pos + 3 > s.Length) throw new ProtocolException("tick record cut short");
            pos += 3 + (s[pos + 1] | (s[pos + 2] << 8));
        }
        if (pos > s.Length) throw new ProtocolException("tick record cut short");
        return pos;
    }
}

/// <summary>A <see cref="TickLog"/>'s records captured at one moment (<see cref="TickLog.Capture"/>).</summary>
public sealed class TickLogView(TickLogView? prefix, uint prefixCount, byte[][] blocks, int[] blockRaw, byte[][] open, uint count)
{
    public uint Count => count;

    public IEnumerable<byte[]> Range(uint from, uint to)
    {
        to = Math.Min(to, count);
        if (from >= to) yield break;
        if (from < prefixCount)
        {
            foreach (var r in prefix!.Range(from, Math.Min(to, prefixCount))) yield return r;
            from = prefixCount;
        }
        var ownFrom = from - prefixCount;
        var ownTo = to - prefixCount;
        var block = (int)(ownFrom / TickLog.BlockTicks);
        while (ownFrom < ownTo)
        {
            IReadOnlyList<byte[]> records = block < blocks.Length ? TickLog.Unpack(blocks[block], blockRaw[block]) : open;
            var start = (int)(ownFrom - (uint)block * TickLog.BlockTicks);
            var end = (int)Math.Min((uint)records.Count, ownTo - (uint)block * TickLog.BlockTicks);
            for (var i = start; i < end; ++i) yield return records[i];
            ownFrom = (uint)(block + 1) * TickLog.BlockTicks;
            ++block;
        }
    }
}
