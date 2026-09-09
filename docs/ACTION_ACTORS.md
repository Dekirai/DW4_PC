# Original ACTION actor lifetime

Version 0.21 adds `action_actors` and an actor-aware ACTION runtime. It owns
every actor in one explicitly prepared RAM, allocates the original bone/joint
matrix arrays on activation, and releases them through NDSP. Reactivation
retains the original node clock while creating fresh action work.

This implements actor buffer lifetime. It does not yet implement actor traversal,
cached bone queries, rendering, manager registration or complete player creation.
The walking preview continues to use its existing provisional controller.

## Evidence

The reference is the supplied USA revision-0 `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. The program reads the DOL as data;
original bytes, disassembly and local instruction probes are not distributed.

| Original path | Address | Recovered effect |
| --- | --- | --- |
| First ACTION activation | `802F1E94..802F20E0` | Enable RAM, prepare work, set request time, attach combiner and allocate ordered actor arrays |
| Actor allocation loop | `802F1F60..802F20B8` | Stop the whole loop at the first existing bone array; otherwise create identity bone/joint arrays |
| NDSP | `802F1B10..802F1DDC` | Clear node attributes and visibility, release work and timers, clear combiner states, free actor arrays |
| Combiner clear | `801D215C` | Empty returns false; populated removes states/tracks and invalidates the local cache |
| Work factory / metadata | `802E54C4`, `802E57D8` | Generic zero scalar/boolean defaults and null owned references |

The existing combiner link at actor `+0x20` survives NDSP; only the matrix
pointers at `+0x24/+0x28` are freed and nulled. Node speed/bias are not reset.
Combiner clearing preserves the last sampled matrices, cached timestamp,
request-time override, source library and owned identity counter. These retained
values must not be replaced with freshly decoded defaults during cleanup.

## Prepared graph and ownership

`decode_action_actor_group(document, ram_reference, combiner_reference, source)`
reads RAM slots 12/13 as typed appearance/actor lists. The explicit combiner's
skeleton supplies bone count and joint count. Each actor's appearance must be
present once in the owning RAM and refer to a skin. The decoder validates joint
ownership, unique actor/appearance identities, field types, widths, references
and list capacity/memory ranges.

The current profile requires little-endian data, null serialized actor combiner
and matrix references, an identity actor transform, empty animation modifiers,
default traversal flags and an empty node attribute list. Existing live caches,
modifiers, attributes or aliased ownership require a restoration path and reject.
Limits are 64 actors, 1,024 bones, 256 joints and a 4,096-byte explicit source.
These are bounded native acceptance limits, not claims about every game asset.

`ActionActorGroup` keeps definitions and ordered mutable state. Activation
assigns the selected combiner and creates identity arrays for each actor before
the first already allocated bone array. That early exit also leaves all later
actors untouched. An existing joint-only array is replaced when its actor has
no bone array. Deactivation frees both arrays for every actor, retaining links.
Native references remain document-qualified integers, never guest pointers.

`decode_action_actor_runtime()` combines this group with the prepared program,
node and combiner. Constructor checks bind source, RAM, combiner and bone count,
and reject an already attached group. Runtime copies own independent buffers.
The older `decode_action_runtime()` remains the command-only preparation API;
it rejects NDSP because no actor cleanup ownership was supplied.

## NDSP and reactivation

NDSP is recognized before ordinary action lookup and does not execute its
authored null command list. It can clean an already inactive runtime. It clears
owned appearance overrides, hides the instance, discards work/cursor/suspension,
removes every named timer, clears combiner states and frees actor arrays. It
does not emit an ACTION broadcast.

The explicit host service protocol still requires delivery of any earlier
pending broadcast before accepting another ordinary action or NDSP. A missing or
throwing earlier service therefore leaves NDSP unaccepted. This retry rule is
not an additional original NDSP event.

The same action can start again after cleanup because its old work and command
identity were released. New actor arrays are identities and the first base clip
uses request time `100000000`. Node phase and speed survive. In particular,
deactivating a frozen runtime leaves node speed zero even though the new work's
freeze flag defaults false; an explicit unfreeze restores speed one.

## Remaining traversal boundary

Original actor traversal at `801DCF30` selects private actor caches only when
modifiers or the force-cache flag require them; otherwise it uses shared
combiner hierarchy/blend arrays. `GETBONEMATRIX` at `802F18A4` reads the first
actor's selected cached matrix and copies it into the messenger result. It does
not force a new pose calculation. Allocation alone cannot provide that behavior.

Consequently the new identity buffers are never advertised as evaluated bone
matrices. `ActionRuntime::pose()` retains its existing independent skin-pose
inspection path. Actor traversal, hierarchy/cache validity, original transform
composition and the query/renderer consumers are the next integration boundary.
See [ACTIONS.md](ACTIONS.md) and [ORIGINAL_BASELINE.md](ORIGINAL_BASELINE.md).

## Validation

The asset-free actor suite passes 60 checks, including malformed typed graphs,
all ordered appearances, identity initialization, first/middle activation
barriers, joint-only replacement, independent copies and complete deallocation.
The ACTION suite passes 124 checks including actor-aware cleanup/reactivation
and retained frozen clocks. Combiner clearing is included in its 86-check suite.

Local original-data checks cover all twelve Agumon actors with 25 bone and
20 joint matrices each. All 182 ordinary actions complete, deactivate and
reactivate. An ignored original-instruction probe covers 14 lifecycle cases;
13 buffer transitions match 208 fields against the production native group.
That comparison excludes external allocation/destruction/metadata/timer/combiner
service bodies and guest pointer/refcount identities. See [VALIDATION.md](VALIDATION.md).

```powershell
.\build\Release\dw4_action_actors_tests.exe 'PATH\Extracted'
.\build\Release\dw4_actions_tests.exe 'PATH\Extracted'
```
