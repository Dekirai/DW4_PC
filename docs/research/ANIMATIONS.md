# Bound original animation clips

Version 0.6 adds a checked link between a decoded skin and its original Enbaya
animation clips. The portable API owns its decoded data and has no graphics or
window dependencies. It implements a single-clip asset inspection path, without
actor state, action events, root-motion extraction or scene placement. Version
0.18 additionally exposes original integer-time sampling and ordered two-layer
arithmetic; full combiner behavior remains pending. See
[ANIMATION_TIMING.md](ANIMATION_TIMING.md).

## Graph and source profile

`list_igb_animations(document, skin)` visits the skin's explicit owning
`igAnimationDatabase` and its slot-6 `igAnimationList`. A database may contain up
to 4,096 distinct clips. The skeleton list must contain only the skin's skeleton;
the source bone references, names, parents and joint indices must match the skin.
There is no first-animation or same-name skeleton fallback.

Each supported clip requires:

- One `igAnimationBinding`, explicitly referencing the same skeleton.
- A complete identity track/bone mapping, with one track per bone, including
  helpers. Nonidentity mappings remain unsupported until their direction is verified.
- Matching track/bone names and `igEnbayaTransformSource` channel ordinals.
- One shared `igEnbayaAnimationSource` for every track.
- Translation/rotation driven channels (`3`), interpolation bytes `[1,3,1]`
  and stored play mode `REPEAT` (`0`). The viewer owns its inspection loop.
- Zero key-frame offset and start time; a positive duration up to 600 seconds.
- Serialized nanoseconds and compressed float duration agreeing within `1.1e-9`
  seconds. Header track count must also match the binding.

Bone/track counts are capped at 1,024 and total inspected list elements at
4,194,304. Counts and memory sizes are checked before indexed reads or allocation.
Null or ambiguous list elements are explicit errors. Listing reports per-clip
graph support and diagnostics; compressed-stream validity is checked when the
caller explicitly invokes `decode_igb_animation()` for a selected reference.

## Translation selection and pose evaluation

Skeleton slot 3 contains exactly one finite little-endian float3 constant per
bone. Animation slot 10 optionally references `_useAnimationTransBoolArray`, an
`igBitMask` with matching bit count and exact word memory. Bit `i` is the least-
significant-bit-first position `i % 32` in little-endian word `i / 32`.

A set bit selects the sampled translation. A clear bit selects the skeleton
constant instead. A null mask selects constants for all bones. Values are not
added together. This branch is established by original DOL arithmetic; see
[ANIMATION_POSE.md](ANIMATION_POSE.md).

`sample_animation()` returns quaternion/selected-translation pairs in complete
bone ordinal order. It accepts finite times in `[0, duration]`; looping is the
caller's responsibility. The decoded [Enbaya data](../ENBAYA.md) preserves original
sample-period and quaternion interpolation behavior, including nonunit output
from the near-angle interpolation branch.

`sample_animation_pose()` converts these values using the original XYZW matrix
signs without quaternion normalization. World transforms use `parent * local`;
skin transforms use `world * inverse_bind`. The recovered original no-modifier
routine imposes additional guards: bone zero must be an unparented helper with
an identity sampled local matrix within `1e-6`; all other parents must precede
their children; joint indices must follow bone traversal order. The implementation
does not silently apply a guessed transform to the original skipped root.

Source positions, UVs and indices remain unchanged. The output contains new
positions and joint origins for the native renderer's existing position-update
API. Manual inverse-bind posing remains a separate operation.

## Validation and use

Generated synthetic IGB graphs test binding ambiguity, incorrect types/references,
time and count mismatches, malformed masks, constant versus sampled translation,
unsupported channel settings, owned-data checks and the original pose guards.
Mask tests cross the 31/32 word boundary with distinct constant and sampled values.
The fixtures contain no original bytes. Run:

```powershell
.\build\Release\dw4_animations_tests.exe
.\build\Release\dw4_animations_tests.exe 'PATH\files\ndmw.afs' 850 5665
```

The optional original-data pass checks all 82 Agumon clips at zero, one quarter,
three quarters and the endpoint of each duration: 8,200 bone transforms, with
complete mesh/joint pose evaluation. This establishes native decoding and the
current composition profile. Comparison with captured original gameplay frames
and full combiner/actor behavior remain outstanding.

Version 0.18 adds five source play modes at six integer-time fractions per clip,
including beyond-duration times: 2,460 additional complete poses and finite
layer evaluation. The suite now has 66 asset-free checks and 5,659 additional
checks with the original Agumon data. Timed APIs publish source/wrapping caches
only after successful sampling. They do not change the viewer's existing loop.

See ANIMATION_VIEWER.md for native playback and CLI usage.
