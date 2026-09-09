# Original actor hierarchy and cached bone queries

Version 0.22 adds shared bone/joint matrix ownership, the original no-modifier
hierarchy calculation and explicit ACTION actor traversal. Named bone queries
read a copy of the retained matrix without advancing animation. The application
still uses its existing walking preview; original renderer context stacks,
appearance traversal, attachments and complete player scheduling remain pending.

## Original evidence

The reference is USA revision-0 `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Addresses identify inspected code
and local instruction comparisons. The distributed application does not execute
the DOL; original bytes and generated probes remain local.

| Original path | Address | Recovered behavior |
| --- | --- | --- |
| Node traversal | `8030FAA4`, clock `8030FA2C` | Publish node-adjusted time to the traversal context before children |
| Actor traversal | `801DCF30` | Save view attributes, select shared caches, write root, sample and update hierarchy |
| Successful local sampling | `801D1174..801D1198` | Publish local cache/time and invalidate the separate hierarchy flag |
| Skeleton no-modifier path | `80202FCC`, loop `80203024..802030A4` | Skip root0, accumulate helpers/bones and compute joint matrices |
| Full float matrix multiply | `80128690` | Initial rounded product, then three ordered fused multiply-adds per element |
| Shared-array allocation | `801D13DC` | Identity bone/joint allocation on skeleton assignment; distinct from ACTION actor allocation |
| Bone query | `802F18A4` | Active gate; first actor; selected cached array; full matrix copy to messenger result |
| Bone name lookup | `8020323C`, comparison `80077768` | First match using fixed ASCII uppercase folding |

## Skeleton and shared cache ownership

`AnimationHierarchyDefinition` owns the explicit skeleton reference, ordered
bone references/names/parents/joint indices and serialized inverse-joint float
matrices. Skeleton slot 4 selects the bone-info list, slot 5 the inverse matrix
memory and slot 6 the joint count. Bone slots 2/3/4 are name, parent and joint
index. The decoder checks types, widths, memory sizes and bounded allocations.

The current hierarchy requires an unparented helper at index zero and each later
parent to precede its child. Joint indices must have exactly one owner but can
be permuted. Zero-joint helper skeletons are valid. Limits are 1,024 bones,
256 joints, 4,096 bytes per name and 1 MiB aggregate names. Matrix evaluation
requires finite affine inputs and rejects unusable results.

Each combiner owns one shared pair of arrays, independently of the private
arrays allocated for its actors:

| Combiner slot | Runtime field | Meaning |
| --- | --- | --- |
| 8 | `+0x40` | Sampled local bone matrices |
| 9 / 10 | `+0x48` / `+0x50` | Local sample timestamp / validity |
| 11 | `+0x51` | Hierarchy validity; normally not serialized, default false |
| 12 | `+0x54` | Shared accumulated bone matrices |
| 13 | `+0x58` | Shared joint matrices |

`decode_animation_actor_combiner()` retains the authored shared arrays, validates
their sizes and binds the hierarchy to the actual clip library's skeleton and
ordered bone references. Missing arrays are an explicit boundary, with null
joint memory permitted only for zero joints. It does not allocate replacement
identities when data is absent. Serialized true hierarchy validity requires
live-state restoration and rejects. The older local-only combiner decoder remains
available for command/pose inspection and cannot perform actor traversal.

Nonempty mutable arrays must have distinct resolved memory identities within
and across the program's combiners. They cannot alias inverse-joint input.
The immutable inverse arrays may be shared. Native actor groups must match both
the bone and joint counts of their bound hierarchy; copying unsupported shared
mutable storage into separate vectors would lose original pointer effects.

All four original Agumon combiners contain 25 shared bone identities and 20
shared joint identities; their hierarchy-valid fields are absent. These are
separate from each RAM's three actor allocations. Copies share immutable
definitions but own their mutable arrays and validity state.

## Traversal order and math

`ActionRuntime::traverse_actor()` selects an active prepared actor. Its inputs
are parent traversal time and the current model-view/inverse-view attributes.
The supplied model-view already includes outer and node transforms; the actor
transform is applied once by this method. This is an explicit actor-entry
boundary, not a replacement for the entire original node/context traversal.

The method converts parent time through the ACTION node clock once, records
that local timestamp, and saves the actor's view attributes. The supported
empty-modifier, false-force profile sets `cache_used` false and uses shared
combiner arrays. It writes shared bone0 as `model_view * actor_transform` before
sampling, including a same-time cache hit. Private actor arrays remain unused.

After local evaluation, a false hierarchy-valid flag triggers calculation:

1. Start at bone1, preserving bone0.
2. A parent index of zero copies the local matrix and excludes the traversal
   root. Other parents multiply their accumulated matrix by the child's local
   matrix. Joint-free helper bones participate in this chain.
3. Each joint-bearing bone consumes the next serialized inverse matrix and
   writes `bone_matrix * inverse_matrix` to its explicit joint index. The
   inverse input order is independent of the output joint-index permutation.

`original_matrix_multiply(left, right)` implements full column-major `left * right`.
The original routine receives these operands in guest r5/r4 respectively. Each
element starts with one rounded float multiply for k0, then performs fused
multiply-adds for k1, k2 and k3. It uses neither double accumulation nor an affine
shortcut. The new hierarchy does not reuse the wider arithmetic of the existing
`SkinPose` inspection function as a claim of exact cached float values.

Startup calls `PPCSetFpNonIEEEMode` at `8025B954`; `80245DD8` sets FPSCR NI.
New-thread context initialization at `802617A0..802617AC` explicitly sets
NI=1 and RN=00, and context save/restore preserves those bits.
The implemented startup profile uses nearest-even rounding, retains subnormal
loaded inputs, and flushes each unrounded result below the smallest normal
binary32 magnitude to signed zero. This includes a tiny result that ordinary
rounding would promote to the smallest normal value. Fused accumulation retains
its single final rounding; a widened exact product and low residual only decide
the flush threshold. This rule applies to the new matrix helper, not a global
rewrite of the existing animation arithmetic.

The retained-input and ordinary pre-round flush rules follow Dolphin's
[NI hardware-test source](https://github.com/dolphin-emu/hwtests/blob/f28077b139eec18967f60db6ce1e15b182dfeac0/cputest/ni.cpp)
and [paired-single helpers](https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/Source/Core/Core/PowerPC/Interpreter/Interpreter_FPUtils.h).
The hardware-test source targets Wii; no console execution was performed here.
Flushing an extreme fused result such as `2^-126 - 2^-298` is an inference from
the pre-round rule. The helper and exact oracle preserve that deficit; Dolphin's
double intermediate can lose it, and the cited hardware test does not cover it.
These boundary vectors therefore require a physical Gekko comparison before a
hardware-parity claim. Older animation helpers retain their separate numeric
profiles.

Successful new local sampling invalidates hierarchy even when invoked through
the existing direct pose API. Same-time and no-state early returns preserve it.
Adding states, taking a snapshot or clearing states does not itself overwrite
the shared hierarchy. NDSP releases private actor arrays while retaining shared
arrays and their validity. Reactivation recomputes them when local sampling next
succeeds. Different LOD actors can overwrite root0 at the same timestamp while
sharing all other cached bone/joint matrices.

## Query and failure boundaries

`bone_matrix(name)` returns no value while inactive. When active, it selects the
first owned actor and the first matching skeleton bone. The fixed original
table folds ASCII a-z to A-Z and leaves every other byte unchanged. Duplicate
names retain first-match behavior. The query returns a matrix value; later
traversal cannot modify a previously returned copy. Missing names reject instead
of reproducing the original unchecked negative-index pointer arithmetic.

Queries never sample animation, update hierarchy or allocate identity matrices.
The original string-command messenger interface remains separate from this
typed query API. Update/render flags and nonempty modifier/forced-cache paths
outside the prepared profile still require their own runtime integration.

The original writes hierarchy validity before running its arithmetic and has no
finite-result rejection. The host computes candidates and publishes only on
success. Failure therefore preserves node timestamps, actor view attributes,
shared caches and source state. This is an explicit checked native boundary,
not a claim that the original rolled back invalid arithmetic.

## Validation scope

Asset-free tests cover strict graphs, noncommuting transforms, helper chains,
permuted joint indices, source ownership, cached queries, time conversion,
multiple LODs, direct-pose invalidation, NDSP/reactivation and failure atomicity.
All four Agumon instances pass 720 actor traversals and 18,000 cached bone
queries, including freeze, interrupted actions and all three appearances.

The independent matrix probe decodes actual paired-single instruction words
and uses exact integer binary32 rounding with the startup NI rule: 7,415 cases
pass, including 101,168 exact finite output words, 996 finite-input overflow
rejections and 96 invalid-input rejections. Separately, 64 cases execute the original
hierarchy's integer control flow and match all 12,976 output float words against
the production implementation. Count/copy/matrix services are explicit probe
boundaries; matrix arithmetic uses the independently checked instruction model.

The matrix corpus includes 1,271 finite subnormal/underflow cases. These are
instruction-model comparisons informed by original startup code and primary
hardware-test/interpreter evidence; this project has not executed these probes
on a physical console. Later runtime rounding-mode changes, renderer output,
physical multiplayer and complete gameplay are separate acceptance requirements.
See [VALIDATION.md](VALIDATION.md) and [ORIGINAL_BASELINE.md](ORIGINAL_BASELINE.md).
