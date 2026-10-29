"""Join log sizes with quantized pads (SEAMLESS.md 6.6), from recorded input.

Reads save state files (saves/states/*.dw4state: the stage snapshot plus every
step's four PadSamples since the stage load) as recorded input, builds the
relay's tick log for 1, 2 and 4 players from them and measures raw and
compressed sizes:

  r48 format   21-byte pads (connected u8, four float sticks, u32 buttons),
               what the direct sessions send today
  DW4R2        the relay's tick record: present mask u8, event count u8, then
               7 bytes per present slot (buttons u16 LE, four int8 sticks
               quantized to round(v * 74), the GameCube's stick steps)

Player k of n replays the recording from offset k * len / n (wrapping), so
the players differ. Long logs are measured per block of 600 ticks (the relay
stores and compresses its log in such blocks, each on its own), so a log of
20 or 30 minutes is its blocks; nothing compresses across blocks. A variant
adds controller noise (+-24 LSB on the SDL int16 axis, as critique
experiment C did) before the quantization, which shows how much of a real
stick's noise survives the GameCube's steps.

zstd comes from libzstd (Git for Windows ships mingw64/bin/libzstd.dll, or
--zstd PATH); zlib from Python.

    python log_sizes.py STATE [STATE ...] [--zstd PATH] [--json OUT]
"""
import argparse
import ctypes
import json
import os
import random
import struct
import sys
import zlib

TICK_HZ = 59.94
BLOCK = 600


def read_state(path):
    data = open(path, "rb").read()
    if data[:8] != b"DW4STATE":
        raise ValueError(path + ": not a save state file")
    (snap,) = struct.unpack_from("<I", data, 12)
    pos = 16 + snap + 1
    (count,) = struct.unpack_from("<I", data, pos)
    pos += 4
    steps = []
    for _ in range(count):
        pads = []
        for _ in range(4):
            c = data[pos]
            lx, ly, rx, ry, b = struct.unpack_from("<ffffI", data, pos + 1)
            pads.append((c, lx, ly, rx, ry, b))
            pos += 21
        pos += 1  # drawn
        steps.append(pads)
    return snap, steps


def quantize(v):
    k = int(round(v * 74.0))
    return max(-74, min(74, k))


def noisy(v, rng, lsb=24):
    # The host path: SDL int16 axis / 32767, scaled by 72/74 (main stick); the
    # noise sits on the int16 value. Zero stays zero (the dead zone).
    if v == 0.0:
        return 0.0
    raw = v / (72.0 / 74.0) * 32767.0 + rng.gauss(0.0, lsb / 2.0)
    raw = max(-32767.0, min(32767.0, raw))
    return raw / 32767.0 * (72.0 / 74.0)


def pad_r48(p):
    c, lx, ly, rx, ry, b = p
    return struct.pack("<BffffI", 1, lx, ly, rx, ry, b)


def pad_q(p):
    c, lx, ly, rx, ry, b = p
    if b > 0xFFFF:
        raise ValueError("a button mask above 16 bits: %#x" % b)
    return struct.pack("<Hbbbb", b, quantize(lx), quantize(ly), quantize(rx), quantize(ry))


class Zstd:
    def __init__(self, path):
        self.lib = ctypes.CDLL(path)
        self.lib.ZSTD_compressBound.restype = ctypes.c_size_t
        self.lib.ZSTD_compressBound.argtypes = [ctypes.c_size_t]
        self.lib.ZSTD_compress.restype = ctypes.c_size_t
        self.lib.ZSTD_compress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_int]
        self.lib.ZSTD_isError.restype = ctypes.c_uint
        self.lib.ZSTD_isError.argtypes = [ctypes.c_size_t]

    def size(self, data, level=3):
        cap = self.lib.ZSTD_compressBound(len(data))
        out = ctypes.create_string_buffer(cap)
        n = self.lib.ZSTD_compress(out, cap, data, len(data), level)
        if self.lib.ZSTD_isError(n):
            raise RuntimeError("zstd failed")
        return n


def player_stream(steps, offset, ticks, transform):
    n = len(steps)
    return [transform(steps[(offset + t) % n][0]) for t in range(ticks)]


def tick_log(streams, pad_bytes, with_header):
    """The log bytes: per tick the header (mask, event count) and every player's pad."""
    mask = (1 << len(streams)) - 1
    parts = []
    for t in range(len(streams[0])):
        if with_header:
            parts.append(bytes([mask, 0]))
        for s in streams:
            parts.append(pad_bytes(s[t]))
    return b"".join(parts)


def measure(name, steps, zstd, rng_seed=7):
    n = len(steps)
    rows = []
    variants = [
        ("r48 21 B float pads", pad_r48, False, False),
        ("DW4R2 7 B quantized", pad_q, True, False),
        ("DW4R2 7 B quantized, stick noise first", pad_q, True, True),
    ]
    for players in (1, 2, 4):
        for label, pad_bytes, header, noise in variants:
            rng = random.Random(rng_seed)
            if noise:
                def transform(p, rng=rng):
                    return (p[0], noisy(p[1], rng), noisy(p[2], rng), noisy(p[3], rng), noisy(p[4], rng), p[5])
            else:
                def transform(p):
                    return p
            streams = [player_stream(steps, (k * n) // players, n, transform) for k in range(players)]
            whole = tick_log(streams, pad_bytes, header)
            blocks = [tick_log([s[i:i + BLOCK] for s in streams], pad_bytes, header) for i in range(0, n - BLOCK + 1, BLOCK)]
            if not blocks:
                blocks = [whole]
            raw_block = sum(len(b) for b in blocks) / len(blocks)
            z_block = sum(zstd.size(b, 3) for b in blocks) / len(blocks) if zstd else None
            d_block = sum(len(zlib.compress(b, 6)) for b in blocks) / len(blocks)
            row = {
                "source": name, "players": players, "format": label, "ticks": n,
                "raw_bytes": len(whole),
                "zstd3_whole": zstd.size(whole, 3) if zstd else None,
                "zlib6_whole": len(zlib.compress(whole, 6)),
                "block_raw": raw_block, "block_zstd3": z_block, "block_zlib6": d_block,
            }
            for minutes in (5, 20, 30):
                nblocks = minutes * 60 * TICK_HZ / BLOCK
                row["raw_%dmin" % minutes] = raw_block * nblocks
                row["zstd3_%dmin" % minutes] = z_block * nblocks if z_block is not None else None
                row["zlib6_%dmin" % minutes] = d_block * nblocks
            rows.append(row)
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("states", nargs="+")
    ap.add_argument("--zstd", default=None)
    ap.add_argument("--json", default=None)
    a = ap.parse_args()
    zpath = a.zstd
    if zpath is None:
        for cand in (r"C:\Program Files\Git\mingw64\bin\libzstd.dll", "/mingw64/bin/libzstd.dll"):
            if os.path.exists(cand):
                zpath = cand
                break
    zstd = Zstd(zpath) if zpath else None
    if zstd is None:
        print("no libzstd found: zstd columns empty", file=sys.stderr)
    all_rows = []
    for path in a.states:
        snap, steps = read_state(path)
        name = os.path.basename(path)
        vals = set()
        buttons = 0
        for st in steps:
            p = st[0]
            vals.update(p[1:5])
            buttons |= p[5]
        print("%s: snapshot %d B, %d steps (%.1f min), %d distinct stick values, buttons used %#06x"
              % (name, snap, len(steps), len(steps) / TICK_HZ / 60, len(vals), buttons))
        rows = measure(name, steps, zstd)
        for r in rows:
            z = r["block_zstd3"] or 0
            print("  %d players  %-40s raw/block %6.0f  zstd3 %6.0f (%4.1f %%)  zlib6 %6.0f (%4.1f %%)  "
                  "20 min: raw %7.0f KB, zstd3 %5.0f KB, zlib6 %5.0f KB; 30 min zstd3 %5.0f KB"
                  % (r["players"], r["format"], r["block_raw"], z, 100.0 * z / r["block_raw"], r["block_zlib6"],
                     100.0 * r["block_zlib6"] / r["block_raw"], r["raw_20min"] / 1024, (r["zstd3_20min"] or 0) / 1024,
                     r["zlib6_20min"] / 1024, (r["zstd3_30min"] or 0) / 1024))
        all_rows += rows
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(all_rows, f, indent=1)


if __name__ == "__main__":
    main()
