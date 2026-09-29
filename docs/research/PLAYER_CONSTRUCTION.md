# Original player construction and remaining integration

`ModelPlayerCreate` implements the USA player callback at `8034D61C` through
the existing startup return at `8034DF3C`, using the actual mutable model graph.
The registered controller's outer GENERATE branch at `8030D9CC` calls it through
virtual `+98`, then performs additional work before returning. The existing
post-GETPLAYER startup is reused directly after the real GETPLAYER delivery.
The result `outer_generate_required` explicitly returns to the outer caller;
it does not establish full GENERATE completion or a playable original frame.

The invocation captures its own live parent and first named ONE, installs the
normal built-in factory result in that ONE, applies the player-specific fields,
and performs the conditional point query. Placement freshly searches the RAM's
then-current data list. Its node reader retains that placement-scope ONE and
resolves its live canonical scene node at each original source/setter read.
GETPLAYER freshly reads the RAM number and retains the actual returned object0
row. Startup keeps the new work and earlier parent even if nested services
replace ONE.work, RAM.parent, RAM.data or GAMERAM's current row.

Nested dispatches, point-buffer push/pop, placement candidates and startup
phases survive service pauses without repeated factory or packet preparation.
Missing ONE, an unsupported concrete node setter and missing/wrong GETPLAYER
object0 are checked faults preserving completed effects. A missing map-context
reader remains an explicit resumable service boundary.

Evidence uses the supplied USA `sys/main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, and original Agumon resource
`_common/p_agumon.igb`. Original executable probes remain local and ignored.

## Fresh work and persistent node

The callback ensures the RAM data-list owner exists, then finds the first entry
named ONE. A reached null entry faults; it is not skipped. Allocating an empty
list does not create ONE or a node.
It unconditionally installs a fresh `beNDMWMdlPlayerInfoWork` in ONE, including
on repeated generation. Retained older work remains a distinct owner. The scene
node is a separate object and must not be reset or replaced with the work.

Factory `80338F60` uses reflected allocation through `8006546C` and `800667F4`.
The clear/create/default passes at `80066F40`, `80066FF8` and `8006712C` initialize
inherited fields as well as player fields. Reading only the ordinary C++
constructor would miss these defaults. The recovered fields include:

| Field / owner | Factory value |
| --- | --- |
| HP and maximum HP | Two distinct, unnamed, zero-valued beDataObjInt objects |
| Record number | -1 |
| Control enabled | Byte 1 |
| Wall, floor and fall checks | Byte 1 each |
| Saved high LOD range | Float -1 |
| PDMOVE data | Allocated empty igFloatList, distinct from null |
| Movement data | Allocated empty beModelCtrlMoveObjectList |
| Subcommands | Fresh beModelCtrlSubCom with its own empty igObjectList |
| Direction, target direction and turn limits | Three float zeros each |
| Player, panel and main-lobby flags | Byte 0 each |
| Body-attack and weapon-wait state | Zero |
| Status lists and turn-attack timer list | Null |
| Status messenger | Null non-owning reference |

The subsequent callback writes isPlayer=1, turn limits `(0,7,0)` and a fresh
eight-element zero integer turn-attack timer list. The maximum-HP fallback to
100 belongs to the later outer GENERATE tail, not this factory.

Original Agumon info1386 has four distinct RAM/ONE/node chains. All serialized
ONE work fields are null. The RAMs' serialized slot numbers are all zero;
original registration writes their live ordinals. Their authored node matrices
differ, so neither list ordinal nor an identity matrix can substitute for actual
registered RAM fields and node storage.

## Placement and retained calls

Without input vectors or matrices, the callback pushes the packet, selects
PLAYERPOINT, sends GETMATRIX with integer arguments0 and the freshly read RAM
number, and copies the first returned matrix. No returned matrix retains the
caller's locally initialized identity. It then pops the packet and appends that
matrix. Existing vector/matrix arguments skip this query.

The placement helper performs a new live RAM-to-ONE lookup. A nested point
provider can therefore change the node used for placement without changing the
work and parent captured earlier. [MODEL_PLACEMENT.md](MODEL_PLACEMENT.md)
describes the retained node reads and concrete setter. The following GAMERAM
GETPLAYER uses another fresh RAM-number read. Its actual returned row is passed
to [MODEL_PLAYER_STARTUP.md](MODEL_PLAYER_STARTUP.md).

## Outer GENERATE tail

After the player callback returns, the outer routine reloads work from its
earlier captured ONE. It applies the conditional HP fallback, retains the first
child's bounding box into work, clears the root box and synchronizes heading.
Presence of AIM, or otherwise AIH, causes a fresh default beModelCtrlInfoAI
allocation; the found record is not copied into that object.

It then directly invokes the ordinary controller with the retained sequence
override or SQ_WAIT. This requires more than sequence selection: the original
player entry callback queues the player-number effect, handles panel removal
and performs status reconciliation, including possible SQ_STUN recursion.
Finally, a non-null saved box is copied into the live root box and transformed
through the actual node matrix. Existing original box math is reusable.

Version 0.41 connects this direct entry and callback through retained actual
invocations; see [MODEL_SEQUENCE_ENTRY.md](MODEL_SEQUENCE_ENTRY.md). Each
already-started nested call keeps its captured work at a stable address. New
generation replaces an owner; assigning pose values is not replacement.
Original input, camera, collision, combat,
frame scheduling and connected local multiplayer remain separate requirements.
