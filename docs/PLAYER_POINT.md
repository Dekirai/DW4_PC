# Original PLAYERPOINT queries

`player_point` implements the original `bePoint01` manager's position-query and
saved-group commands. Player construction can now send `PLAYERPOINT/null/-1,
GETMATRIX` through the existing messenger and receive the actual authored
point matrix. This module does not create a player, run scene transitions,
choose the original frame order or draw the placement visualization.

Evidence is the supplied USA GameCube `sys/main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Addresses below are guest virtual
addresses. Original executable bytes, disassembly and instruction probes stay
in ignored `work/`; the public source and tests contain no original payload.

## Loaded and mutable owners

`decode_player_points` takes the same shared `ActorSceneResource` that owns the
loaded document. It accepts exact `bePoint01Info` records from the actual
`igInfoList`, preserves export and RAM occurrences in order, and records raw
references alongside canonical physical owners. Repeated references share
group/point owners. Different source occurrences remain independent.

Each info retains its original nullable name, active byte, data reference,
RAM-list identity, optional polygon switch and optional group-list identity.
Group definitions preserve names, child-list owners, null groups and null
children. Point definitions retain the actual canonical `ActorSceneNode`, with
its source and document lifetime. The mutable `PlayerPointNodeState` is shared
by every occurrence of that physical point within the manager. `set_matrix`
updates this represented live matrix without replacing its identity. Queries
read the current matrix; they do not reread an immutable authored copy.

The represented matrix owner must also be consulted by any future dynamic
point traversal. The module does not silently rewrite the original document or
claim that an unrelated renderer already observes these mutations.

`register_source` performs the represented generic per-RAM initialization:
write parent manager/info and ordinal, reset the four generic state words and
append one PLAYERPOINT registration row for every occurrence. The original raw
RAM active byte survives. Aliased RAM occurrences share one live RAM; the final
parent/ordinal writes are visible while earlier registration rows retain their
names and indices. Null info names register as the original empty name. The
manager and loaded owners survive destruction of the inspection facade while
the event runtime retains their registered providers.

This registration does not substitute for the original outer context's manager
allocation, acceptance loop, scheduler attachment or manager ordering.

## Command behavior

Manager handler `80312A70` uses dictionary `802C1028` and calls matrix helper
`803129E0`. Its body has no active-byte gate. Existing messenger enabled-only
filtering still reads the live raw RAM byte before delivery.

| Command | Original effect |
| --- | --- |
| GETMATRIX | Capture manager `_pointNo`; use integer argument 1 as player index |
| GETMATRIXDIRECT | Use integer 0 as group and integer 1 as player index |
| SETGENERATEPOS | Store integer 0 directly in the manager's signed `_pointNo` |
| GETGENERATEPOS | Append that signed word to the current integer list |
| GETPOINTNAME | Append each group name in list order; null name becomes empty |
| SHOWPOLYGON | Require the real switch operation that sets every mask bit |
| HIDEPOLYGON | Require the real switch operation that clears every mask bit |

Unknown commands, including differently cased spellings, are originally
declined. They do not become an unsupported-effect success claim. Missing
required arguments and unsafe owner/list accesses fault the native dispatch
with a checked diagnostic.

`_pointNo` is one manager-wide signed integer at offset `0x20`. It is not
per-player, per-RAM or inferred from integer 0 of GETMATRIX. Loading another
source does not reset it. Metadata `802C1334` installs an `igIntMetaField`
through constructor `8002E1AC`, name table `804D004C`, offset table `804D0054`,
without an explicit override. The generic default path
`80063F14 -> 80063D04`, then `8006712C -> 80063DD0 -> 80063E50`, creates and
copies zero storage. Concrete factory hooks in vtable `804DF3EC` at
`+0C/+24/+2C/+34` are the original no-op functions
`800667A4/800667B0/800667D0/800667CC`; none overrides `_pointNo`. Its initial
native value is therefore zero, not a guessed startup group.

### Exact lookup and null-group branch

`803129E0` reads the actual info group-list count. A signed group index greater
than `count-1` falls back locally to zero. It does not alter the saved manager
word. Negative indices are not clamped. An empty list, negative selected group,
missing child list, invalid player index or null selected child is an original
unsafe access that becomes a checked host failure.

A **null group is a separate, deliberate original branch** at `80312A40`:
copy matrix `804F89B8` and append it. Its identity value is verified from static
initializer `8012F57C`: `8012F594` establishes base `804F88B8`, and
`8012F630..8012F634` calls constructor `801285A0` on base+`0x100`. The constructor
writes one at offsets `00/14/28/3C` and zero elsewhere. The corresponding DOL
constants at `805669E0/805669D8` are `3F800000/00000000`. Consequently, a null
group returns the original initialized identity even with an invalid player
index; the player index is never dereferenced in that branch.

A nonnull group returns the selected child's current matrix at offset `0x20`.
There is no parent composition, visualization-switch test, normalization,
coordinate conversion or heading extraction in this query. Appending uses the
existing `MessengerBuffer::add_matrix` implementation, including its original
paired-single tiny-value copy behavior. Existing typed arguments/results and
the actual buffer owner survive the append. Retrying a completed delivery does
not append again.

### Visualization service boundary

The original SHOWPOLYGON/HIDEPOLYGON paths dereference info+`0x1C`, then the
switch mask owner at +`0x20`, and call `801D6834/801D6908`. The shared mask
implementation remains external to this module. `PlayerPointServices` receives
the actual resource, canonical switch identity and desired all-on/all-off
operation. A missing or false service pauses delivery; it is never treated as
successful drawing or mask mutation. The continuation retains its captured
target and operation across retries. A service with a committed internal prefix
owns its own resume behavior. A null switch is an invalid dereference, distinct
from an unavailable implementation of a valid switch operation.

## Player construction integration

The original player callback `8034D61C` uses existing vector or matrix arguments
when supplied. Otherwise `8034D8DC..8034D950` pushes the argument-buffer owner,
selects PLAYERPOINT/null/-1, appends integers `(0, live RAM number)`, sends
GETMATRIX, copies the first result when present, pops the parent buffer owner
and appends the selected matrix to it. Placement helper `8030D610` then applies
that matrix to the actual generated model node. This provider supplies the
real query portion of that sequence; it does not perform the surrounding
construction steps itself.

The pre-existing [placement inspection API](PLAYER_PLACEMENT_RESEARCH.md) still
serves explicit static inspection. Its stricter four-point/affine selection
profile and invalid-index rejection are intentionally separate from the
manager's original signed fallback and null-group behavior.

## Parser limits and verification

The profile checks little-endian source storage, exact consumed object/field
types and widths, duplicate field slots, list count/capacity/storage agreement,
source ownership and finite matrices. Lists are bounded to 65,536 entries and
262,144 aggregate decoded edges, with an 8 MiB name budget and 4,096-byte names.
The shared actor scene additionally bounds canonical node owners, depth and
subtrees. It accepts finite static exact `igTransform` point nodes with target
zero and null transform input. Dynamic input evaluation and other concrete
point classes require their original services rather than a guessed matrix.
The query itself does not require affine matrices or exactly four points.

Synthetic tests cover manager state, all query branches, signed extremes,
positive fallback, distinct null/invalid accesses, aliases and repeated source
loads, ordered registration, nullable/case-sensitive names, current matrix
mutation, retained nested buffers, raw activity, publication conflicts,
completed retries, visualization pauses and malformed schemas/storage.

Local original integration uses dng101b1 package member `[0.179]` from
`ndmw.afs`, exact info115, groups39/41 and all eight authored point transforms.
An ignored independent instruction probe compares 980 cases against the
original handler, lookup helper and identity constructor (58,760 executed
instructions, zero mismatches). Dictionary resolution, append/copy helpers and
switch mutation are prepared boundary hooks; unsafe original accesses are
tested as native checked failures, not executed as guest memory corruption.
This evidence does not claim physical-console or complete original-frame parity.
