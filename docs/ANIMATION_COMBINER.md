# Original animation combiners and explicit motion effects

Version 0.19 implements the recovered full-binding animation combiner path in
the portable core. It retains states and source clocks, mixes their channels,
retires completed transitions and creates fixed snapshots from the last cached
pose. Explicit MOTIONSET helpers now change this owned state and the selected
source's play mode. The walking preview still uses its provisional controller
and direct single-clip selection.

This is a dependency for original ACTION2 execution. It does not construct the
full action manager, resolve ACTION requests, dispatch action command lists,
broadcast events or create playable actors. Four independently evaluated
combiners do not establish connected local multiplayer.

## Original evidence

Research uses the supplied USA GameCube DOL, `GDJEB2`, revision 0, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. All addresses below are guest addresses.
The reference checkout and provenance are recorded in [UPSTREAM_AUDIT.md](UPSTREAM_AUDIT.md).
Original bytes, disassembly, research probes and captures remain local and ignored.

| Routine | Recovered behavior |
| --- | --- |
| `801CBCBC` | Combiner metadata, serialized matrix cache, cache flag/time and request-time override |
| `801C9B50` | State metadata defaults, including new flag and speed one |
| `801ADAC8`, `801CFB8C`, `802069C8` | State pool resets metadata on release; track pool disables that reset and clears fields explicitly |
| `801D2810`, `801D26FC` | First-activation base and explicit incoming state creation |
| `801D1860` | Stable ascending track priority insertion |
| `801CFFB4` | State clocks, blend start/weight, completion and removal passes |
| `801CFEA8`, `801D239C` | Retire preceding priority tracks; remove states and force a sole survivor to replace mode |
| `801D0C14` | Same-time cache, single/multiple-state sampling and matrix publication |
| `801D2840` | Fixed state built from the last cached matrices |
| `8012BCDC`, `8012B8A8` | Float quaternion-to-matrix and snapshot matrix-to-quaternion conversion |
| `801D0A38` | Request timestamp uses the stored override or global system time |
| `802F1160`, `802F0EB0` | MOTIONSET and the subsequent two-phase motion update |

## Ownership and prepared data

`AnimationSourceRuntime` owns one explicitly qualified clip library, one clock
per original source reference and the shared state/wrapping caches. Clips must
have the same complete skeleton/bone binding and consistent exact serialized
durations. Several clip references can share a source clock. Each combiner keeps
its own states and matrices; source clocks remain shared within that runtime.
Copies share immutable decoded data and copy all mutable state. A combiner rejects
a separately constructed library even when its path and numeric references match.

The source string is an explicit caller qualification, not an automatic file
identity check. The caller must obtain the clip library and prepared combiner
from the same selected document. No API searches unrelated files by numeric ID.

`decode_animation_combiner()` requires an explicitly selected original combiner,
matching skeleton, typed empty state and per-bone track lists, zero base indices
and one finite 4x4 float cache matrix per bone. It preserves cache validity, the
full cached timestamp and the request-time override. All-ones means system time.
Nonempty serialized states/tracks require a separate restoration implementation.
Other hierarchy, result-quaternion and palette buffers are not restored by this
prepared-cache decoder; it is not complete actor deserialization.

Libraries are bounded to 4,096 clips, 1,024 bones and 4,194,304 decoded frame/track
samples. Combiners allow at most 1,024 states. Local object references are never
host pointers. Parser failures and unusable math raise `AnimationError` without
publishing partial runtime changes.

## Evaluation and interrupted transitions

The implemented state profile uses replace/blend modes 0/1, transition modes 4/1
and active/fixed/retired statuses 0/1/3. Original state creation and stable
priority order are separate: the update pass visits creation order, while a
finishing transition retires preceding priority tracks. Equal priorities retain
insertion order. A lower-priority completion does not remove a higher-priority
base. `update()` returns the count from before retirement, matching the original.

A new explicit blend begins at the supplied node timestamp and remains new
until evaluation or the motion-update pass clears the flag. Calling update again
before that clear restarts the origin. A zero start timestamp takes the original
missing-transition fallback on the next update: duration is the exact signed
animation duration divided by three and weight evaluation is skipped that call.
Nonempty named transition graphs are rejected at that unresolved boundary.

Evaluation first checks the complete cached timestamp. A same-time hit does not
advance clocks or sample again, even after a direct speed write. One-state and
multiple-state paths preserve the original new/status sampling decisions. The
current implementation samples complete immutable channels per selected bone to
retain source/cache call order; a later optimization must preserve that ordering.

Snapshots use the last evaluated matrices, even if update has already advanced
state time. They replace live states with one fixed, source-free state. Subsequent
evaluation reconstructs that state's rotation and translation. It does not
normalize quaternions or replace the snapshot with a resampled clip endpoint.
Float matrix writes preserve the seven fields outside the rotation block; the
combiner then writes translation, retaining the other four affine fields.

`pose()` widens the recovered float matrices into the existing checked hierarchy
and skin evaluator. The verified profile requires an identity skipped helper
root, parents before children and joints in traversal order. Hierarchy failure
does not publish partially advanced source or combiner state. See
[ANIMATION_POSE.md](ANIMATION_POSE.md) for the existing skinning boundary.

## MOTIONSET and motion update

`apply_animation_motion()` accepts a clip reference already resolved in the
owning database, the authored blend-frame value and interpolation/loop strings.
It consumes the original no-interpolation flag, snapshots when at least two
states exist, appends the incoming blend and updates at node-adjusted time.
The request timestamp uses system time or the serialized override. `S` in either
of the first two interpolation characters stops the corresponding first/last
state at **system time**, which can differ from the node blend timestamp.
An initial loop character `L` selects repeat; the other branch selects offset
clamp. The source mode changes after update and does not invalidate its cache.

`advance_animation_motion()` preserves the separate motion-work phases: state 1
only becomes 2; state 2 updates a multi-state combiner and clears its new flags.
When one state remains, it aligns that state's speed at node time and returns
work state to zero. The original `is_motion` flag remains set. Callers supply
node time and speed; automatic node/actor timer integration is still pending.

## Validation

- 73 asset-free combiner checks cover state lifetime, priority order, interrupted
  snapshots, same-time caching, MOTIONSET phases, separate timestamps, four owned
  state clocks, shared source identities and malformed prepared graphs.
- The animation math suite has 151 checks, including 24 independent synthetic
  snapshot matrices spanning all branch choices, ties, nonunit affine values and
  nonpositive radicands. The graph suite has 69 asset-free checks.
- A bounded interpreter of the actual `801CFFB4`/`801CFEA8` instructions matches
  10,000 updates in 500 programs (17,692 total commands). Creation starts from
  prepared metadata state; list/pool removal, replace-index rebuilding, signed
  division and empty named-transition lookup are explicit service boundaries.
  This probe does not independently validate those services or a complete actor.
  NaN payloads are excluded from comparison.
- Matrix instruction comparisons match 30,556 conversions / 305,224 float
  fields: 10,250 decoded original quaternions, 5,000 synthetic inputs, their
  inverse conversions and 56 additional branch/affine cases. Nonfinite guest
  outputs require native rejection.
- An ignored reciprocal-square-root probe compared the original estimate plus
  three double Newton steps with float `sqrt`: 25,166,839 positive float cases,
  zero differences. It exhausts both normal exponent parities' mantissas, all
  positive float subnormals and normal exponent boundary values. Powers-of-four
  scaling covers the remaining normal exponents without double underflow in
  these intermediate operations. Only this verified float-result equivalence
  is used by native matrix conversion.
- Original integration loads all 82 Agumon clips and four prepared combiners
  (`6396`, `6463`, `6530`, `6596`) from `ndmw.afs` entry 850. Eight explicitly
  selected original MOTIONSET records provide the four weapon idle/run names
  and authored mode/frame fields. A staggered 60-frame test evaluates 240 full
  poses, checks 329,040 finite vertex positions and interrupts all four combiners.
  Every final state retains its own weapon-specific running clip.

The local estimate probe consulted Dolphin's pinned
[FloatUtils.cpp](https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/Source/Core/Common/FloatUtils.cpp).
Its GPL implementation/table remain only in ignored research files; no Dolphin
code or table is incorporated into this native runtime or distributed fixtures.

Run the asset-free suites with CTest. Optional original-data integration:

```powershell
.\build\Release\dw4_animation_combiner_tests.exe 'PATH\Extracted\files\ndmw.afs' 850 5665
```

These original-data tests use production sampling and skinning. They are not
independent codec validation or comparisons with original gameplay frames.

## Remaining boundaries

Full ACTION2 work construction, database/name dispatch, same-action suppression,
action command cursors, WAIT/FADE/FLASH behavior, events and outer player callbacks
remain pending. So do partial bindings, mirrored tracks, modifiers, cycle matching,
nonempty named transitions and the optional `cleanCombinerStates` path. The
original option's BSS default is false; this implementation covers that default.
The remaining actor and node hierarchy preparation must be recovered before this
path replaces direct animation selection in gameplay. Original camera, collision
response, combat, audio, progression and saves are separate unfinished systems.
