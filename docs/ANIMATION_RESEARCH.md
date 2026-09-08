# Original Agumon animation research

Inspected 2026-09-08 using the native, read-only IGB document API and the verified
USA `main.dol`. This records animation availability and reference relationships.
It does not implement the Enbaya codec or establish a reproduced animation frame.
The original files and local probes remain outside the public source distribution.

## Document graph

The catalog maps `_common/p_agumon.igb` to `ndmw.afs` entry 850. Its
`igAnimationDatabase` at global reference 810 is named `p_agumon`:

| Serialized slot | Referenced object | Observed contents |
| --- | --- | --- |
| 5 | `igSkeletonList` 778 | One skeleton, reference 701 |
| 6 | `igAnimationList` 779 | All 82 animation objects in the document |
| 7 | `igSkinList` 775 | Three stored appearance skins |
| 8 | `igAppearanceList` 777 | Empty |
| 9 | `igAnimationCombinerList` 774 | Four combiners |

The document contains 12 `igActor` instances and four animation combiners.
Those are serialized object counts; they do not by themselves establish playable
actor instances, active animation states, or a recovered scene update order.
The sampled actor's appearance and list references require scene traversal,
separate from the checked animation-to-skeleton mapping below.

The native census checked every one of the 82 clips:

- Slot 4 references an `igAnimationBindingList` with exactly one binding.
- Each binding points to skeleton 701 through slot 2. Its slot 4 count is 25;
  the slot 3 memory contains exactly 25 little-endian 32-bit integers, `0..24`.
- Clip slot 5 references a list of exactly 25 `igAnimationTrack` objects.
- Each track's slot 2 name equals the corresponding skeleton-bone name in
  `igSkeletonBoneInfoList` 491, including the unnamed bone at index 0.
- Track slot 3 references an `igEnbayaTransformSource`. Its slot 2 index is
  exactly the track-list index, `0..24`; slot 3 references the compressed source.
- All 25 transform sources in a clip point to one `igEnbayaAnimationSource`.
  Different clips use different sources: 82 sources and 2,050 tracks in total.

The mapped named bones include `RotateRoot`, `JointRoot`, `Back`, `Abdomen`,
`chest`, `Head`, `Chin01`, the left/right arm chains, `LegRoot`, and the
left/right leg chains. This is a verified named-track mapping; it is not a
decoded set of rotation or translation samples.

For example, clip `waitS` is animation 487, track list 477, binding list 488,
binding 492, and compressed source 3710. Its first track is reference 479.
The low/high geometry material or skin reference numbers must not be used as
animation track indices.

## Stored fields and timing

The original DOL's animation field-name table at virtual address `0x804B2730`
identifies `_priority`, `_bindingList`, `_trackList`, `_transitionList`,
`_keyFrameTimeOffset`, `_startTime`, `_duration`, and
`_useAnimationTransBoolArray`. Their order agrees with serialized animation
slots 3 through 10. The time fields are serialized as eight-byte
`igLongMetaField` values, even though the guest CPU is 32-bit.

All 82 clips have zero key-frame offset and start time. Their slot 9 duration
is a little-endian 64-bit integer. Dividing that integer by 1,000,000,000 agrees
with the little-endian float at compressed-stream byte offset 12 to within
`9.81e-10` across all clips. This establishes the observed nanosecond-to-float
duration relationship for these files; it does not establish the game's
simulation step or a presentation frame rate.

| Clip | Animation reference | Stored duration integer | Stream float at +12, approximately |
| --- | ---: | ---: | ---: |
| `waitS` | 487 | 1,016,666,650 | 1.016667 |
| `runD` | 910 | 683,333,337 | 0.683333 |
| `guardD` | 1365 | 516,666,650 | 0.516667 |
| `deadD` | 1638 | 1,200,000,047 | 1.200000 |
| `itemD02` | 1693 | 50,000,000 | 0.050000 |
| `sleepD` | 2050 | 1,516,666,650 | 1.516667 |

Names are original diagnostic labels. Suffixes are not assigned gameplay
meanings here. Loop behavior, action events, root motion, playback speed,
blending and frame sampling still require the original update semantics.

## Enbaya source evidence

The DOL field-name table at `0x804AFBE8` identifies the source's fields.
Serialized slots 2 through 5 correspond to:

| Slot | Field | Observed value for all 82 sources |
| --- | --- | --- |
| 2 | `_enbayaAnimationStream` | Owned memory reference to the compressed bytes |
| 3 | `_interpolationMethod` | Three bytes: `1, 3, 1` |
| 4 | `_drivenChannels` | One byte: `3` |
| 5 | `_playMode` | Enum: `0` |

Nearby DOL enum tables name interpolation values `NONE=0`, `LINEAR=1`,
`BEZIER=2`, `SLERP=3`; channel bits `TRANSLATION=1`,
`QUATERNION_ROTATION=2`, `EULER_ROTATION=4`, `SCALE=8`; and play modes
`REPEAT=0`, `CLAMP=1`, `BOUNCE=2`, `LOOP=3`, `OFFSET_CLAMP=4`.
The byte value 3 in slot 4 is a channel mask, not a codec version.
These enum identities do not decode compressed samples or determine the
evaluation behavior of the three interpolation entries on their own.

The 82 streams total 119,620 bytes; individual sizes range from 282 to 4,078
bytes. All have the following checked little-endian prefix values:

| Byte offset | Observation | Current interpretation |
| --- | --- | --- |
| 0 | `0x002ABF84` | Unexplained header value; no version or magic meaning assigned |
| 4 | Integer 25 | Agrees with the checked track count |
| 12 | Finite float | Agrees with the animation duration conversion above |
| 16 | Integer 30 | Constant in this sample; sampling-rate meaning not established |

The production native document reader exposes these bytes safely, but does not
decode the packed channels as a float/quaternion array. The checked `dw4-gc`
reference project has no named Enbaya codec implementation or matching
animation-source symbols. The original DOL does contain the class
registrations: `0x801C1674` for the transform source and `0x801C1F0C` for the
animation source. These are useful future reverse-engineering entry points,
not working decoder implementations.

## Independent codec compatibility probe

A separate implementation, [korenkonder/enbrip](https://github.com/korenkonder/enbrip/tree/461b7818ec4b1b20ed68b4a7935250f3b4b4ce18),
was inspected at commit `461b7818ec4b1b20ed68b4a7935250f3b4b4ce18` under its MIT
license. Its [`enbaya.h`](https://github.com/korenkonder/enbrip/blob/461b7818ec4b1b20ed68b4a7935250f3b4b4ce18/src/enbaya.h)
describes an 80-byte header, including a signature, quantization error, sample
rate and fourteen packed-stream length fields. All 82 original Agumon streams
exactly satisfy `80 + sum(lengths) == input byte size`. In this candidate layout,
the constant at byte 16 is a sample rate; this remains separate from game
simulation and display frequency.

An ignored scratch copy adapted only GCC alignment declarations for MSVC.
It evaluated all 25 tracks at zero, one quarter and three quarters of each
clip's stored duration: 6,150 quaternion/translation tuples in total. No sampled
component was non-finite, and every clip changed between sampled times.
Observed quaternion norms range from `0.981297845` to `1.00000012`; normalization
behavior must not be assumed from the type name alone.

For `waitS` at time zero, the probe returns a `JointRoot` translation of
approximately `(0, -0.8, -1.1766)` and nonzero rotations for `Back` and `Head`.
This is useful evidence that the compressed bytes carry the missing transforms.
It does not establish their full composition with stored offsets or reproduce
the original renderer's character pose.

The compatibility probe uses padded input because the reference decoder lacks
checked per-stream cursors. It has no sanitizer-based memory proof and no
original-frame comparison. Finite sampled results are encouraging compatibility
evidence, not sufficient validation for shipping that decoder. No reference
codec code is copied into the production target in this milestone. Future
adaptation must retain the license notice and implement bounded reads, explicit
byte order, malformed-stream fixtures, verified timing and pose comparisons.

## Implementation boundary

Actual animation playback needs a safe, verified compressed-channel decoder, time
sampling/interpolation rules and skeleton-local transform interpretation,
followed by comparison with a recorded original frame. The stored skeleton
translation array alone does not reproduce all inverse-bind positions; filling
missing rotations with identity would therefore create an unverified pose.

A manual joint pose is a separate inspection operation. If offered by the
viewer, it must be labeled as manual and must not be called an original idle,
walk, run, attack or other decoded animation. No procedural substitute is used
to claim completion of original animation playback.

The local census reads the existing document API, validates all lists and
references described above, and emits `work/animation-census-ndmw850.txt`.
Those original-data checks are optional local evidence rather than asset-free
CI fixtures. No production animation decoder or public playback API is added
by this research note.
