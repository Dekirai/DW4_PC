# Original weapon queries and initial equipment

The portable game-item owners implement the original weapon database queries
and the initial DEVICEFOLDER and POWERSOCKET operations. They use generated
`GameDataState` rows, real typed database results and the existing shared
messenger. They do not complete GAMESTART, calculate player stats or construct
a ready player.

The reference is the supplied USA GDJEB2 revision 0 DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original data and instruction
probes remain outside the tracked source.

## Weapon identity and values

The native `GameDataWeapon` corresponds to `beNDMWItemWeapon`, factory
`80343168`, registration `80343288`, size `0x1C`. It inherits the unnamed item
base through `beNDMWItemRevisionPocketBase`. Its represented data are:

| Guest offset | Native field | Type |
| --- | --- | --- |
| `+08` | `kind_no` | signed 32-bit integer |
| `+0C` | `record_no` | signed 32-bit integer |
| `+10` | `is_equipped` | unchanged byte |
| `+14` | `revision` | signed 32-bit integer |
| `+18` | `pocket` | signed 32-bit integer |

The revision and pocket descriptors both use `igIntMetaField`; pocket is not
a pointer or list. Initializer `803B0CB4`, as invoked by initial equipment,
writes kind zero, the selected record, equipped zero, revision zero and pocket
zero. Each initial weapon has a distinct shared owner even when records repeat.
This explicit initializer profile does not infer unobserved save data.

## Every item query sends again

`GameWeaponQuery` retains a weapon owner and an owned nullable column name.
Its integer and string paths follow `803B06F0` and `803B0790`. The weapon
virtual method `803B0C8C` calls `803B06A0`, which selects
`DATABASE / weapon / 0`, sends `GETDATABASE`, and takes the current shared
object at index zero after delivery. Each new item query performs this send;
it does not reuse the GAMERAM shinka or pcTech lookup cache.

After the database callback completes, the query reads the weapon's **current**
record number. A provider that changes that record therefore changes the
subsequent lookup. The returned integer or string becomes an owned result and
does not change when the item or shared message buffer changes later. Repeating
`resume` on a completed query returns that result without another delivery.

Queries preserve the [database rules](DATABASE.md): signed char/short/int
precedence, missing integer column zero, exact column bytes and checked bounds.
A null string-cell name becomes an owned empty string. A missing string column
is a checked error. A nullable query column remains distinct from an empty
column name, and neither permits skipping the original send.

## Ordered initial equipment entries

`GameDeviceFolderInitialization` represents `8036E124`. On its first resume
it captures the selected player's current DEVICEFOLDER inner list and
DIGIMONNO value. It clears that same list before fetching shinka, then visits
`INI_WEAPON1`, `INI_WEAPON2`, `INI_WEAPON3` in order. It allocates each weapon
before the possible shinka query, applies the explicit initializer and appends
the new owner. The shared `GameDatabaseCache::shinka` retains the actual
returned database. A paused lookup keeps its item, species, list and cursor;
replacing the player row or the named wrapper's list does not retarget it.

`GamePowerSocketInitialization` separately represents `8036E3B8` with its flag
byte zero. Its first resume captures the then-current player row, DEVICEFOLDER,
POWERSOCKET and POWERSOCKETNAME inner lists. It clears the two destination
lists, then captures DEVICEFOLDER's count. This order matters when the source
and object destination share an inner list.

For each physical source position within that captured count, it checks whether
the current socket count equals three. For a supported weapon whose observed
kind is zero, it appends the **same** item to POWERSOCKET, performs the fresh
MODEL query, appends the name, and finally writes the live equipped byte to
one. A pause after the item append exposes that prefix and does not repeat it
on resume. A changed future source element is read when its position is reached;
new elements appended beyond the captured count are not visited. The original
early exit tests equality with three, rather than a greater-than-or-equal cap.

Known Disk/Waza/Weapon owners with other kind values are skipped without
rewriting equipped state. A kind-zero object with another dynamic item type
requires its own original database implementation and is rejected explicitly.
The nonzero-flag equipment selection path is outside this component.

## Continuations and checked boundaries

All resume calls receive the caller's live `NativeMessenger` and
`ActionEventRuntime`. Equipment also receives the same game-data and database
cache owners on every retry. A missing or incomplete database provider returns
an incomplete `MessengerCallResult`; its nested dispatch and committed prefix
remain owned. Completed operations are idempotent. The owners cannot be copied,
and recursive resume or a different owner is rejected.

An exception during an operation faults that owner so uncertain effects cannot
be replayed. Result getters reject unavailable or wrong-kind results. The host
checks source RAM zero through three, list presence, at most 65,536 entries,
column length and embedded NUL bytes. Initial list validation precedes clears;
later validation does not undo an already published prefix. If a nested callback
removes storage required by the original captured iteration, the host stops
explicitly instead of reproducing a guest out-of-range access.

These checks and retained host continuations are native execution boundaries.
They do not claim that the original synchronous code validated malformed host
objects or exposed an equivalent public asynchronous API.

## Validation

`dw4_game_items_tests` covers fresh and suspended queries, post-callback record
reads, distinct initial weapons, list/item aliases, nested player reset and
wrapper replacement, publication order, captured counts, exact three-item exit,
nullable versus empty names, malformed lists and terminal provider failures.
It passes 121 synthetic checks and 157 checks with the supplied original data.

The optional original-data invocation is:

```text
dw4_game_items_tests <EXTRACTED_ROOT>
```

It uses the actual `dng101b1/separate/usa_pack.igb` database exports for each
of four explicitly prepared source RAMs. Shinka database `1879`, Agumon row
zero, supplies initial records `266`, `275`, `274`. Weapon database `1643`
returns models `w_common05`, `w_common04`, `w_common03` and SKILL values
`0`, `4`, `5`. These tests exercise the equipment entries and queries; they do
not claim complete original initialization or an independent execution of the
whole guest equipment routine.

A separate ignored instruction harness compares 54 synthetic cases against the
native core with zero mismatches across 12,228 original instructions. It executes
the two equipment entries, weapon integer/string wrappers, virtual database
selection and the weapon initializer. Typed named lookup, allocation, list
append, string interning and database recipient/results are explicit prepared
hooks. It compares callback-visible counts and equipped bytes as well as final
fields and names, including nested row replacement, live record changes and
source/destination list aliases. Native providers pause once per query; the raw
side executes synchronous callbacks. This validates the represented continuation
prefix against the guest operation order, without claiming original asynchronous
execution or a whole-GAMESTART comparison.
