# Prepared MODELCTRL player-start prefix

`model_player_start.hpp/.cpp` continues the original MODELCTRL player caller
after an already completed GETPLAYER delivery. It retains that returned data
row, binds its HP/MHP wrappers to prepared model work, performs the original
short modifier resets and GAMESTART branch, queues PLAYERARMS GENERATE, then
sends RECALCPLAYER. Its terminal result is `later_ui_required`: the subsequent
NUMBERCTRL/METERCTRL work has not run within this prefix API. Version 0.35's
`ModelPlayerStartup` owns this prefix and continues its UI/map tail through
actual recipients; see [MODEL_PLAYER_STARTUP.md](MODEL_PLAYER_STARTUP.md).
Neither API establishes full player creation, original controller registration
or a player-ready flag.

The reference is the supplied USA GameCube `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, routine `8034D61C`, specifically
`8034D990..8034DCF4`. Original files and disassembly are not distributed.
Related owners are described in [GAME_DATA.md](GAME_DATA.md),
[GAME_START.md](GAME_START.md), [GAME_NAME.md](GAME_NAME.md),
[GAME_RAM.md](GAME_RAM.md) and [MESSENGER.md](MESSENGER.md).

## Supplied owners

`ModelPlayerStartCapture` is the canonical entry used by the actual player
construction callback. It retains the new work's `ModelScriptFields`, the
earlier captured parent's nullable name cell and the actual live RAM-number
cell. It needs no script name, target identity or node matrix. Its constructor
validates work, row and live-RAM owner presence only; a null parent-name anchor
represents the original captured null parent. It faults only if GAMESTART
reaches the name read, after appending the live RAM number. Name and number
values are checked at their original consumption sites. In particular, a GETPLAYER recipient may change
the RAM number without introducing an artificial snapshot-equality failure.
`capture()` exposes these retained anchors. `owner()` is available only for
the separate prepared adapter described below.

`ModelPlayerStartOwner` supplies an existing `ModelScriptWork`, the captured
parent-info identity and source, a shared nullable parent-name field, and a
shared live RAM-number field. Work and parent info must be serialized owners
from the same source occurrence and source path. The initial RAM number must
match the work and be in zero through three. This validates the supplied
identity relationship; it does not discover or reconstruct an original RAM.
The caller must provide these actual field owners from its prepared state.

The parent-info owner represents the capture made before GETPLAYER in the
original routine. It must not be re-read from a replacement live parent after
GETPLAYER returns. Its name remains live until GAMESTART's packet is built.
The RAM number is independently re-read for the original packet/routing uses.
Prepared tests can mutate those shared fields during nested callbacks without
silently retargeting the captured work or old data row.

The constructor takes the actual captured GETPLAYER object-zero data row.
It deliberately does not require equality with GAMERAM's current player row:
a later recipient or nested reset may have replaced that row. The continuation
and `ModelGameDataBinding` retain the consumed row and wrapper identities.

`ModelPlayerStartDefaults` owns four byte strings. Its original-data reader
reads the four big-endian pointers at `804E7C70` and bounded terminated names
from initialized DOL data sections. The actual names are Shift-JIS; no original
name bytes are compiled into the port. The reader rejects invalid pointers,
executable sections, truncated ranges and a missing terminator within64 bytes.
An explicit array constructor supplies synthetic test data through the same
owned-string interface. Neither route performs font conversion itself: actual
GAMESTART delivery must reach the existing prepared GAMERAM font service.

## Ordered effects

| Original interval | Represented effect |
|---|---|
| `8034D990..DA74` | Use `bind_model_game_data`: publish existing HP/MHP wrappers; on exact HP zero copy MHP to HP, then MMP to MP. Negative HP skips both copies. |
| `8034DA78..DACC` | Zero existing UPDATA indices0 through6 using the supplied zero at `804E7C68`; do not extend a short list. |
| `8034DAD0..DB24` | Zero existing DOWNDATA indices0 through5 using the supplied zero at `804E7C6C`; do not extend a short list. |
| `8034DB28..DB98` | If captured-row ISUSE byte equals0, select GAMERAM/null/-1; append live RAM number, captured-info name (null becomes non-null empty), and that RAM's default name; send GAMESTART. |
| `8034DB9C..DC4C` | After GAMESTART completes, fill every existing captured-row BASE_ISUSE element with the supplied byte1 at `80561408`. |
| `8034DC50..DCA8` | If captured-row POWERSOCKETNAME has an element0, retain that nullable name and live RAM index, prepare a PLAYERARMS request, then schedule GENERATE with delay1. |
| `8034DCD4..DCF4` | Select GAMERAM/null/-1, append another live RAM number, and send RECALCPLAYER without adding a modifier byte. |

Generated UPDATA and DOWNDATA have11 and7 entries. Consequently UPDATA[7..10]
and DOWNDATA[6] survive the two reset loops. Noncanonical nonzero ISUSE bytes
also skip GAMESTART and the BASE_ISUSE fill; they do not skip HP binding,
modifier resets, optional weapon enqueue or recalculation.

An empty POWERSOCKETNAME list schedules nothing. A present null element0 is
different from an empty string element and retains that difference in the
deferred target name. Scheduling captures the old row's selected name before
later list changes; no weapon-data object is placed in this packet.

GENERATION is deferred. The caller proceeds directly to RECALCPLAYER without
waiting for the weapon recipient. The queue owns a distinct retained argument
buffer and computes the original deadline modulo2^64:
`context_time + 16666666`. There is no implicit tick or weapon creation here.

The primary `resume` overload takes a required `ModelPlayerClock` callback.
It reads the live prepared context clock after deferred preparation and before
scheduling, so a GAMESTART callback can advance that clock before enqueue.
The callback must only read that clock. A numeric overload is explicitly a
constant-clock convenience for callers whose context cannot advance during
nested callbacks; it does not implement the original system clock/provider.

## Continuations and checked boundaries

Missing nested providers retain the same GAMESTART or RECALCPLAYER dispatch,
including completed recipient prefixes. Retrying requires the same messenger
and event runtime. The caller must resume with the retained messenger reference
through an existing service continuation; it must not select another packet
while that delivery is suspended. Shared packet changes follow actual nested
sends; no private packet snapshot or automatic buffer restoration is added.

The returned phases expose which service is pending. `prefix_completed()` means
only the terminal `later_ui_required` checkpoint. Repeating that terminal call
does not bind HP again, refill rows, repeat a send or enqueue a second weapon.
A failed callback or later checked exception permanently faults the owner,
preserving its already committed prefix. Wrong-owner, recursive-entry and
missing-clock preflight rejections occur before advancing it.

The existing HP helper prevalidates its required fields before publication.
Later lists are checked when reached; malformed later input can therefore
leave the earlier HP/reset/GAMESTART prefix committed. Referenced lists must
exist and contain at most65,536 entries; names must be bounded and free of
embedded NULs. These are host failure boundaries, not claims about the original
unchecked invalid-memory behavior. First-name lookup retains the common typed
game-data rule: a wrong-type first matching name is an error.

## Validation scope

The version 0.35 suite passes95 synthetic prefix checks and613 with the supplied
original extraction, now including the complete prepared UI/map tail, actual
HP meter binding, connected weapon generation and replacements.

The synthetic suite checks old-row retention across a reset, live routing fields,
captured names, exact short-array tails, null/empty weapon names, raw ISUSE bytes,
clock changes inside GAMESTART, exact deferred deadline/order, incomplete
GAMESTART and RECALCPLAYER, count-once callbacks, wrong owner and source identity,
uncertain faults, moves, and malformed DOL/list/string inputs.

The optional original-data suite prepares four actual Agumon MODELCTRL works,
consumes actual native GETPLAYER results, then uses original database exports,
DOL default names and an explicitly prepared original font converter. It checks
shared HP, all BASE_ISUSE entries and four independently routed deferred common05
requests. Delivery now reaches the actual weapon manager, pauses at one missing
ACTION scene owner, then resumes through real PLAYER/ACTION/MODELCTRL endpoints.
Eight weapon replacements preserve both hands across all twelve appearances;
twenty-four common bone-selector visits use the actual selected actor caches.
Removal through another weapon info exercises shared manager-ordinal tracking.
The integration also registers the original HUD's NUMBERCTRL/METERCTRL RAMs,
checks digit UV updates and live HP binding, and executes real metal/main-lobby
map branches. Complete player initialization and HUD drawing remain required. Name
output uses the already validated conversion helper as an integration check;
this is not an independent instruction execution of the full constructor.
