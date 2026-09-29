# Bounded Enbaya animation decoding

`dw4_core` decodes the observed little-endian Enbaya stream profile into owned
quaternion and translation samples. It does not execute the original decoder,
follow serialized pointers, or require padded input. Animation-to-skeleton
binding and matrix composition are separate from this compressed-channel API.

The packed-value and interpolation algorithms are adapted from
[korenkonder/enbrip](https://github.com/korenkonder/enbrip/tree/461b7818ec4b1b20ed68b4a7935250f3b4b4ce18),
commit `461b7818ec4b1b20ed68b4a7935250f3b4b4ce18`, under the MIT license.
The copyright and permission notice is preserved in
[`licenses/enbrip-MIT.txt`](../licenses/enbrip-MIT.txt).
DW4_PC replaces the reference's unchecked pointers and mutable forward/backward
decoder state with checked cursors and an immutable decoded clip. No encoder
or external codec library is linked into the application.

## Public API

```cpp
dw4::EnbayaClip clip = dw4::decode_enbaya(compressed_bytes);
std::vector<dw4::EnbayaTransform> tracks = dw4::sample_enbaya_original(clip, seconds);
```

`EnbayaTransform` contains `rotation`, a four-float quaternion in XYZW order,
and `translation`, a `Vec3`. Output order is compressed track ordinal, not
joint-palette order. `EnbayaClip` reports `track_count()`, `sample_rate()`,
`duration()` and `frame_count()`. The input bytes may be released immediately
after decoding. Sampling a default-constructed empty clip throws `EnbayaError`.

`sample_enbaya_original` is the runtime sampling path. `sample_enbaya` retains
the independent reference implementation's interpolation for compatibility
comparisons. Both use the same immutable integrated frame sequence and timing.

Sampling accepts finite times and clamps them to the clip's duration. Repeat,
clamp, transition blending, action events and other playback behavior belong
to the caller. The source sample rate is a codec parameter; it does not select
the game's simulation frequency or the viewer's presentation rate.

## Stream layout and limits

The first header word is a serialized guest value rather than a format tag:
all 82 Agumon clips store `0x002ABF84`, while NPC clips such as the lobby
staff store `0x002ABF8C` with an otherwise identical layout. The decoder accepts
the observed `0x002ABFxx` range.
The 80-byte header has track count at byte 4, float quantization error at byte 8,
float duration at byte 12, sample rate at byte 16, and fourteen 32-bit byte
lengths from bytes 20 through 72. Byte 76 contains an opaque serialized guest
word. It is never converted to a host address.

Header length order and physical payload order differ:

| Header length index | Stream | Physical payload position |
| --- | --- | --- |
| 0 | Initial 2-bit selectors | 6 |
| 1 | Initial signed 8-bit values | 7 |
| 2 | Initial signed 16-bit values | 3 |
| 3 | Initial signed 32-bit values | 0 |
| 4 | Delta 2-bit selectors | 8 |
| 5 | Delta 4-bit selectors | 9 |
| 6 | Delta signed 8-bit values | 10 |
| 7 | Delta signed 16-bit values | 4 |
| 8 | Delta signed 32-bit values | 1 |
| 9 | State 2-bit selectors | 11 |
| 10 | State unsigned 8-bit values | 12 |
| 11 | State unsigned 16-bit values | 5 |
| 12 | State unsigned 32-bit values | 2 |
| 13 | Per-track flags | 13 |

Positions and indices in this table are zero-based. Each scalar uses explicit
little-endian decoding; packed selectors consume the most significant bits
first. Signed values are extended arithmetically through a wider signed type,
without signed shifts or out-of-range unsigned-to-signed conversions.

The decoder checks these limits before allocating decoded frames:

- At most 16 MiB of compressed input and exactly `80 + sum(lengths)` bytes.
- Between 1 and 1,024 tracks, each with exactly one seven-channel flag byte.
- Finite quantization error in `(0, 1]` and finite duration in `(0, 600]` seconds.
- Between 1 and 600 samples per second.
- At most 1,048,576 decoded transforms, approximately 28 MiB of channel samples.
- Complete 16-bit and 32-bit words within the corresponding declared streams.

Every read is limited to its own stream; truncating one stream cannot consume
the following stream. Flag bit 7 is unsupported. Initial and integrated zero
quaternions, non-finite norms, components and sample times are rejected.
State runs use unsigned counters and bounded component traversal, so a run
cannot cause signed overflow or an unbounded loop. Declared streams may contain
unused values beyond the requested duration; their lengths still bound every
read. Trailing bytes outside the declared streams are rejected.

The whole clip is decoded once. This bounds work during loading and makes
backward seeks independent of mutation history. Sampling requires only a frame
lookup and interpolation of the requested tracks.

## Integration and interpolation semantics

Initial integers are scaled by quantization error. Quaternion frames are
normalized. Subsequent packed values update cumulative component deltas;
those accumulated deltas are scaled and added to the previous frame, whose
quaternion is then normalized again. A disabled channel flag skips reading
new acceleration values; it does not discard an accumulated delta.

Translation sampling is linear. The reference comparison path,
`sample_enbaya`, normalizes both quaternion endpoints, chooses the shorter sign
and uses spherical interpolation. Its near-angle branch, where
`1 - dot <= 0.08`, returns a linear combination without normalizing the result.
The larger-angle branch uses trigonometric functions and normalizes its output.

The original USA source wrapper at guest address `0x801E52FC` instead calls the
engine quaternion interpolation routine at `0x8012BE90`. That routine uses the
same shortest-sign selection and `0.08` threshold but does not normalize its
inputs or output. Its larger-angle weights use a polynomial approximation.
`sample_enbaya_original` selects this recovered math through
`original_interpolate_rotation`, preserving the raw integrated quaternion at
time zero. It does not replace the decoded channel sequence with procedural
motion. The two APIs intentionally preserve these different numerical behaviors.
A sampled quaternion need not have exactly unit length. The original
quaternion-to-matrix convention is established separately by the pose module.

Frame times use a float sample period. The end condition retains the reference
codec's `0.00001` second tolerance, and times below `0.000001` second select the
initial sample. A final partial interval divides the blend by the full sample
period, not by the remaining duration. Consequently the sample at the duration
may lie between the last two integrated frames. These rules reproduce the
candidate codec; they are not a claim about original animation state updates.

## Validation

The asset-free `enbaya` test suite performs 201 active checks, including:

- Constant multi-track clips, signed initial widths, owned-data lifetime and
  deterministic backward sampling.
- All compact delta values and escape widths, signed boundaries, packed-byte
  boundaries, cumulative integration and channel-state toggles.
- Linear translation, spherical quaternion interpolation, equivalent opposite
  quaternion signs and the unnormalized near-angle branch.
- Partial final intervals, finite time clamping, per-stream exhaustion, every
  byte truncation of a valid fixture, invalid headers, zero quaternions and
  allocation limits.
- Original sampling's raw initial quaternion, shared translation timing and
  separate normalization behavior from the reference comparison path.

The suite passes in MSVC Release and in a separate AddressSanitizer Debug build.

An optional local original-data comparison checked all 82 Agumon streams from
`ndmw.afs` entry 850, totaling 119,620 compressed bytes. All clips decoded fully.
Samples at zero, one quarter and three quarters of duration produced 6,150
quaternion/translation tuples matching the pinned reference at nine significant
decimal digits, with no differing components. A second comparison at every
half-sample interval through the endpoint produced another 55,950 tuples, also
without differing components. These are comparisons to the independent codec,
not to a recorded original game frame. The original streams, reference probe
binaries and generated sample output remain in ignored local `work/` storage.

This module establishes compressed-channel compatibility. It does not establish
scene placement, game update order, action transitions, collision, root motion,
or playable character behavior.
