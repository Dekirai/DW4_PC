# Original animation pose composition

The native viewer can evaluate one decoded animation clip against its explicitly
bound skeleton. This is an original-asset inspection operation: actor placement,
animation transitions, modifiers, action events and a comparison with a recorded
original renderer frame remain separate work. Manual inverse-bind posing stays
available through its existing independent API.

The following evidence was recovered on 2026-09-08 from the USA executable with
SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Addresses below are guest
addresses, never host pointers. Original bytes, frame exports and disassembly
are ignored local research inputs rather than distributed fixtures.

## Translation selection is a replacement

An animation's serialized slot 10 is `_useAnimationTransBoolArray`, identified
by the field table at `0x804b2730`. At runtime this is animation offset `0x38`.
The combiner initialization at `0x801d1d8c` through `0x801d1dc8` selects its
bit using the binding-mapped track ordinal:

```text
use_translation = words[track / 32] & (1 << (track % 32))
```

A null bit-mask object means false. The shift uses the least significant bit
first; the IGB reader separately converts each stored little-endian word. The
native profile requires a complete identity track/bone binding, exact mask
count/capacity and one checked bit per bone.

When the bit is false, `0x801d1e70` through `0x801d1e98` copies that bone's
skeleton translation to the combiner's constant-translation slot. In the
single-state evaluator, `0x801d0d64` through `0x801d0dcc`, false requests only
the animated quaternion; true requests both quaternion and translation directly
into the result slots. The selected translation is copied to matrix elements
12, 13 and 14 at `0x801d0e1c` through `0x801d0e30`.

Therefore the absolute local translation is **either** the decoded animated
translation **or** the skeleton's stored translation. These values are not
added. No compensating root offset is invented to force an animated frame to
equal the inverse-bind inspection reference.

The Enbaya transform-source methods at `0x801e6194` and `0x801e6204` forward the
track ordinal and result pointers to the shared animation source. The combined
source evaluator at `0x801e52fc` reads the decoded frame pair, applies the source
interpolation modes and writes its output without a quaternion component
reorder or additional normalization.

## Quaternion interpolation

The source evaluator calls the original quaternion routine `0x8012be90` for
the observed SLERP mode. It first computes the four-component dot product and
negates the second quaternion when the dot is negative. When
`1 - abs(dot) <= 0.08f`, it uses linear weights `1 - blend` and `blend`.
Otherwise it evaluates the original bicubic polynomial weights. Neither branch
normalizes its output, and the inputs are not normalized by this routine.

The native `original_interpolate_rotation` preserves those coefficients,
single-precision operation order and fused multiply/add operations. Its
polynomial approximation is deliberately retained at blend values zero and
one too; replacing these with exact endpoints changes the original arithmetic.
The function rejects non-finite inputs, blend values outside `[0,1]`, and
overflow. Finite nonunit quaternions are not silently corrected.

The original source computes blend as elapsed time since the first frame
divided by the full sample period at `0x801e53f4` through `0x801e5418`.
Translation LINEAR uses `0x8012d3f4`. A shortened final interval does not change
that denominator. Clip repeat behavior in the workbench is an explicit viewer
control; it does not reproduce the game's animation-state transitions.

The pinned independent Enbaya reference uses trigonometric spherical
interpolation and normalizes that branch. It remains useful for codec
compatibility checks through `sample_enbaya`. Production animation inspection
uses the separately named `sample_enbaya_original` and the recovered polynomial.
See [ENBAYA.md](ENBAYA.md) for codec limits and provenance.

## Quaternion-to-matrix convention

The combiner passes its selected quaternion to `0x8012bcdc`. With components
`(x,y,z,w)`, its flat matrix arithmetic gives the following host matrix, acting
on column vectors:

```text
| 1-2y²-2z²    2xy+2zw     2xz-2yw    tx |
| 2xy-2zw     1-2x²-2z²    2yz+2xw    ty |
| 2xz+2yw      2yz-2xw    1-2x²-2y²   tz |
|    0            0           0        1 |
```

These signs follow the executable arithmetic and the point-transform indexing
already established in [SKINNING_RESEARCH.md](SKINNING_RESEARCH.md). In this host
convention they differ from the common Hamilton active-rotation matrix. A
positive Z quaternion maps X toward negative Y. The original routine does not
normalize the quaternion before matrix construction.

The constants 2 and 1 are read at `0x80566a28` and `0x805669e0`. The small-data
base `r2 = 0x8056e220` is established by initialization at `0x80005418`.
`original_bone_transform` implements the recovered indexing in host doubles,
copies the already selected absolute translation and rejects a non-finite or
out-of-float-range result. This establishes the operation, not bit-for-bit
equivalence with every PowerPC floating-point rounding mode.

## Full bone hierarchy and inverse joints

The original skeleton method `0x80202fcc`, reached through the `igSkeleton`
virtual-method table at `0x804b3810`, contains a path without modifiers at
`0x80203024` through `0x802030a4`. It starts at bone ordinal 1. For a bone whose
parent is zero, it copies the local matrix. Other bones multiply that local
matrix by the previously evaluated parent frame. A bone with `_bmIdx != -1`
then produces a skin matrix using the next stored inverse-joint matrix and
selects the output slot by `_bmIdx`. Joint-free helpers still participate in
the hierarchy.

The called multiplication routine `0x80128690` was checked separately using
its paired-single load, scalar-lane multiplication and fused-add arithmetic.
For host column-major matrices, arguments `r4=A` and `r5=B` produce `B*A`.
Two noncommuting synthetic integer affine matrices distinguish this from
`A*B`. Consequently the host equations are:

```text
bone_world[bone] = bone_world[parent] * bone_local[bone]
joint_world[joint] = bone_world[bone whose joint_index equals joint]
skin_matrix[joint] = joint_world[joint] * inverse_bind[joint]
```

The standalone mathematical API `evaluate_bone_transforms` accepts one explicit
absolute local matrix per bone, including every helper. It validates all
parents, cycles, joint ownership, finite affine matrices, inverse references,
palettes, influences and posed positions. Its limits remain 1,024 bones,
256 joints and 1,048,576 vertices. It does not reuse the helper-skipping
`bind_local` caches used by manual inspection.

The original-animation wrapper additionally requires an unparented helper at
bone zero, an identity local matrix there within `1e-6`, parents before children
and joint indices increasing in traversal order. These guards make the generic
evaluator equivalent to the recovered skipped-root/sequential-inverse path.
It requires the same skeleton and exact bone references as the decoded clip.
The recovered path does not read skeleton bone flags; no meaning is assigned
to the observed flag values. This operation explicitly excludes actor modifiers
and does not apply an actor or scene transform.

For the paired-single instruction interpretation, the investigation consulted
the [Dolphin interpreter](https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/Source/Core/Core/PowerPC/Interpreter/Interpreter_Paired.cpp)
and its [instruction table](https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/Source/Core/Core/PowerPC/PPCTables.cpp).
No Dolphin implementation is copied or linked into the native project.

## Validation

The asset-free skinning suite now contains 153 checks. New cases reconstruct
an explicit full-bone reference, move a joint-free root, combine a helper's
X rotation with its child's noncommuting Z rotation, preserve unaffected
branches, and reject malformed helper hierarchies and matrices. Separate cases
verify quaternion component order, signs, translation placement and preservation
of a nonunit sample. Manual posing/reset remains independently checked.

The animation-math suite adds 49 checks using synthetic values independently
evaluated through a bounded scalar interpreter of the original interpolation
routine. They cover both branches, threshold neighbors, shortest-sign handling,
polynomial endpoint approximation, nonunit inputs, invalid times and overflow.
Tests retain active checks in Release builds and do not include original bytes.

An additional ignored local comparison evaluated all adjacent decoded frame
pairs from the 82 Agumon clips at blends zero, one half and one: 82,650 tuples.
It also evaluated 750 deterministic synthetic tuples. All 83,400 native results
matched the independent scalar arithmetic probe exactly as float32 values on
the tested MSVC build. Of the original intervals, 672 in 38 clips entered the
polynomial branch, so this difference from reference trigonometric SLERP is
exercised by actual supplied files.

The probe models the relevant arithmetic and explicit float rounding. It does
not emulate PowerPC exception/status registers, guest execution, rendering or
the complete animation system. This evidence supports the recovered single-clip
pose operation; it is not a recorded original-frame comparison or a playable
character milestone.
