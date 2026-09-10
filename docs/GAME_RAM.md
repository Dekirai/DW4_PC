# Generated GAMERAM runtime

`GameRamRuntime` constructs the recovered initial named game-data graph and
registers one generated `beNDMWGameRam` endpoint on an existing
`ActionEventRuntime`. It implements existing player-row queries, player-row
reset and the verified ordinary `P2PKOUKAMAGIC` return paths. Selection,
equipment/stat calculation, healing, skill growth and the full original game
startup remain separate prerequisites.

## Native owners and registration

The runtime allocates distinct native identities for the manager, info and RAM.
These identities have their own domain and never masquerade as IGB references.
The RAM belongs to the generated info/manager, whose registered category/name/
index is exactly `GAMERAM` / `gameram` / `0`; its active-byte provider returns
the original creation value 1. Registration installs the provider and category
row together, rolling back provider insertion if category registration fails.

One shared internal owner retains the generated `GameDataState`, identities
and manager callbacks. The action-event runtime retains that owner even if the
public `GameRamRuntime` facade is destroyed. The facade is neither copyable nor
movable and cannot register itself twice. Constructing another runtime creates
different owners; ordinary messenger registration and cache rules still apply.

`state()` exposes the existing common/player rows. It does not offer an unchecked
state-replacement operation. The returned row and named-wrapper shared pointers
retain their real identities and mutable values. Initial zero HP/MHP fields
are native factory defaults, not a selected player's starting stats.

## Implemented commands

The command dispatcher is the manager's structured hook, virtual `+90`.
It reads the live message's command on initial entry. The original fallback
at virtual `+8C` returns zero without effects.

| Command | Input | Effect |
| --- | --- | --- |
| `GETPLAYER` | signed integer[0], checked 0 through 3 | append that existing row to the object list |
| `GETPLAYER1` through `GETPLAYER4` | no required input | append fixed row 0 through 3 |
| `GAMEDATAINIT` | signed integer[0], checked 0 through 3 | replace the selected player row using the recovered generator |
| `FIGHTCALC` / `P2PKOUKAMAGIC` | target RAM, source RAM, kind | the bounded paths below |

Queries append the actual `MessengerDataList` object without clearing other
lists. A row query is not a controller-address result or a copy of its values.
Completed dispatch retries do not append another result or perform another
reset.

Every reset replaces the selected player-row identity. Row zero also repopulates
the existing common-list object with fresh field wrappers. Other rows and
previously retained objects remain alive and unchanged. Consequently, a
MODELCTRL holding an old HP/MHP wrapper still holds it after a reset; only an
actual later rebind changes that reference. The separate `bind_model_game_data`
helper consumes the captured completed `GETPLAYER` row and implements the
recovered HP/MHP preparation prefix.

## Bounded magic dispatch

`P2PKOUKAMAGIC` reads target RAM, source RAM and kind, then resolves the existing
target row **before** comparing the two RAM values. A missing integer or invalid
target row is a terminal checked error. The unequal-RAM path cannot bypass
this original list access.

For unequal RAM values, the handler completes without changing any buffer list
or player value. For equal RAM values, it computes signed `kind % 10000`:

- Remainders 40, 45, 55, 60, 50, -50, 65, 80 and 75 stop at an explicit missing
  original effect service. They do not fabricate healing, status changes or
  a final reply.
- Every other remainder clears only the integer list and appends integer 1.
  The other seven lists and the enabled-only byte remain unchanged. This is
  an original no-effect branch result, not evidence that a spell succeeded.

These paths do not modify HP. The Lua BLP wrapper subsequently reads current
integer[0]: an unequal-RAM call can therefore return its original target RAM,
while an equal-RAM default branch returns 1. BLT's separate `P2ESKILLUP`
consumer remains an explicit skill-growth boundary.

The known effect boundary retains its parsed command, arguments and actual
target-row shared owner, keyed by the same dispatch ID and recipient-row index.
Retries do not reparse a shared packet that nested calls may have changed.
Other unimplemented commands, including `GAMESTART` and other `FIGHTCALC`
subcommands, stop before their effects. There is no generic successful logging
handler. The runtime bounds unresolved deliveries to 256; an uncertain exception
faults its manager owner and prevents replay.

These incomplete results are native service boundaries used to preserve the
original synchronous call. They do not claim the original manager scheduled
those effects asynchronously. The enclosing action-event delivery owns the
shared packet while suspended; unrelated top-level mutation is rejected.

## Original evidence

The reference is the supplied USA GameCube executable, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.

- Plugin `80346F74` creates the manager via factory `80344274`; initialization
  `8036D224` installs common/player-list owners and generates four rows through
  `8036CE1C`. The native schemas are `804EA930` and `804EA9D8`.
- `8036D7EC` creates the info/RAM only when its info list is empty, names the
  info `gameram`, links RAM parents, writes RAM index 0 and active byte 1, then
  registers category `GAMERAM` at `8036D8D0` through `80305308`.
- Vtable `804EB2C4` has structured hook `803717A4` at `+90` and fallback
  `802F24A0` at `+8C`. The latter is `li r3,0; blr`. The structured hook reads
  message command `+14` via `803787F8` before its command-enum dispatch.
- `GETPLAYER` executes `8037181C..80371850`; its fixed variants start at
  `80371854`, `80371878`, `8037189C` and `803718C0`. Object append is
  `80306E2C`. `GAMEDATAINIT` at `80371A14..80371A2C` calls the row generator.
- `FIGHTCALC` enum value 18 selects `803729D4`; `P2PKOUKAMAGIC` value 20
  selects `80376EE4`. The target list/row lookup at `80376F14..80376F28`
  precedes the unequal branch at `80376F30`. Default completion at
  `80377F48..80377F58` clears integers through `80306AB8` and appends 1
  through `80306A40`.

Original data and disassembly remain ignored. The public implementation uses
native owned objects and checked C++ state, not guest pointers.

## Validation

The asset-free GAMERAM suite passes 188 checks against the final Release
core. It covers identity domains, exact registration/activity, four existing
row returns, fixed query ordinals, retained endpoint lifetime, duplicate native
registration order, row/common reset identity, old MODELCTRL HP aliases,
unchanged/default magic results, signed remainder extremes, all effect gates,
non-integer buffer preservation, missing arguments and invalid row access.

An independent ignored probe executes the actual ordinary magic gate and its
list/integer access helpers for 39 cases, totaling 2,952 guest instructions.
Final Release core results match all 39 cases with zero differences. The guest supplies
four prepared row pointers and stops before each first effect instruction;
this validates the bounded return gate, not the whole manager constructor or
any healing/status operation.

The optional extracted-game run passes 233 additional checks against that same
core, for 421 total. It loads the unchanged entry179 `hit_p_agumon.lua`
(LuaInfo2783, list index26, 3,230 bytes) and prepares all four selected Agumon
MODELCTRL/HIT/script owners using RAM references 1725, 6417, 6420 and 6622. Actual
`GETPLAYER` deliveries and `bind_model_game_data` install their generated-row
aliases without supplying invented starting HP.

Eight scenarios exercise each target with an equal and a different source RAM.
For kind80000, the original chunk preserves `MDLsetDMGTurn(0)`, completes BLP's
verified ordinary branch and reaches `BLTcalcSkillUp` at main PC63 after 48
instructions and five logical host calls. Its actual `P2ESKILLUP` packet contains
the current source RAM/kind and the byte derived from BLP's signed result. The
missing skill-growth service remains incomplete. Retrying it does not replay
BLP, change the instruction/call counters or modify any player's HP owner.
These checks invoke the prepared original Hit-script path; they do not execute
full player creation or assert that combat is complete. They remain separate
from asset-free CI.

See [MODEL_SCRIPT_MESSENGER.md](MODEL_SCRIPT_MESSENGER.md) for Lua wrapper
packet/continuation semantics and [MESSENGER.md](MESSENGER.md) for message
matching and shared-object lifetimes.
