# Native controller initialization research

This document records the verified common manager registration path and the
first deferred `beStaticInfo` from the USA GameCube release, `GDJEB2`, disc 0,
revision 0. Addresses refer to that release's original PowerPC `main.dol`.
The executable was inspected as data; no guest code was executed. Generated
disassembly and original resource dumps remain in ignored local research files.

Registration performs mutable runtime work. Keeping an exported info in a list
does not implement its manager callback. The narrow recovered profile below can
initialize a Static controller and its messenger recipient without processing
its embedded sound data. It does not establish the loading-screen gate, later
controller callbacks, layer/text setup, script startup or a gameplay update.

The first info belongs to the preliminary `/s` phase. Later research confirms
that this phase registers before func/boot and the deferred list is subsequently
cleared. The version 0.11 combined inspection list is not the original later
registration queue. See [AUDIO_STARTUP_RESEARCH.md](AUDIO_STARTUP_RESEARCH.md).

## Manager identity and dispatch

The original manager setup creates `beStaticInfoCtl` through `0x802B8078` and
inserts that object into the application registry at `0x803199A0`. Its concrete
type is registered at `0x802B82D8`, with type handle storage `0x80534728`, object
size `0x24`, and vtable `0x804E0368`.

| Vtable offset | Address | Verified role |
| --- | --- | --- |
| `+0x58` | `0x8031BCB4` | Concrete manager type |
| `+0x5C` | `0x802F24A8` | Registry insertion initializer; no operations |
| `+0x6C` | `0x802F251C` | Common exact-type info acceptance and initialization |
| `+0x78` | `0x802F2234` | Per-info initializer; no operations for Static |
| `+0x7C` | `0x8031BBAC` | Static per-RAM initializer |
| `+0x80` | `0x8031BB98` | Expected info type, exact `beStaticInfo` |
| `+0x88` | `0x8031BB4C` | Resolve and cache the messenger manager |

`beStaticInfo` is registered at `0x802B85E0`, uses type-handle storage
`0x80534730`, and has object size `0x1C`. Its verified ancestry is
`beStaticInfo -> beBaseInfo -> igInfo -> igNamedObject -> igObject`.
The common handler compares exact runtime type identities, rather than names,
prefixes or an inheritance match.

Application registration at `0x8028A268` suppresses repeated insertion of the
same manager pointer, assigns the inherited application-owner pointer at manager
`+0x0C`, then calls `+0x5C`. The info dispatcher at `0x8028A7DC` visits the
actual manager registry in order and stops after the first callback whose low
return byte is nonzero. It subsequently appends the info to application `+0x10`
even when every manager declined. Therefore that append, or an empty manager
registry, cannot prove that any controller was initialized. See also
[STARTUP_LOADING_RESEARCH.md](STARTUP_LOADING_RESEARCH.md).

## Common accepted-info effects

At `0x802F2534`–`0x802F2560`, the common callback compares the manager's
expected type from `+0x80` with the info's actual type from `+0x58`. A mismatch
returns false at `0x802F260C` without these accepted-info effects.

For an exact match, it performs these operations in order:

1. Append the info to manager `+0x18`, `_infoList`, at
   `0x802F2564`–`0x802F256C`.
2. Invoke the manager's per-info initializer at `+0x78`.
3. Read the info's RAM list at native `+0x18`, serialized slot 6. Traverse its
   entries in ascending list order, including repeated references.
4. Initialize each RAM object as shown below, then invoke the manager's
   per-RAM initializer at `+0x7C`.
5. Request manager activation through `0x8028A398` and set the byte
   `_isUpdateRun` at manager `+0x1C` to 1. Return true.

The RAM writes are at `0x802F25B4`–`0x802F25CC`. Member names and offsets are
defined by `beBaseInfoRam` registration `0x802E3284`, member table
`0x804D2CC0`, name array `0x804D2CF4`, and offset array `0x804D2D3C`.

| Native offset | Serialized slot | Member | Acceptance result |
| --- | ---: | --- | --- |
| `+0x08` | 2 | `_ramNo` | Current zero-based RAM-list ordinal |
| `+0x0C` | 3 | `_parentInfo` | Accepted info object |
| `+0x10` | 4 | `_parentManager` | Accepting manager object |
| `+0x14` | 5 | `_isRun` | Preserved; not written here |
| `+0x18` | 6 | `_st0` | Integer zero |
| `+0x1C` | 7 | `_st1` | Integer zero |
| `+0x20` | 8 | `_st2` | Integer zero |
| `+0x24` | 9 | `_st3` | Integer zero |
| `+0x28` | 10 | `_dataList` | Preserved; not written here |

The original Static RAM has no serialized `_parentInfo` or `_parentManager`
field. These are runtime references constructed by the callback, not missing
serialized pointers to guess or host addresses to reconstruct. Source documents
remain immutable; a native implementation needs separate mutable RAM objects.

The base manager metadata at `0x804D2DC8` names `_system` at `+0x10`,
`_messenger` at `+0x14`, `_infoList` at `+0x18`, and `_isUpdateRun` at `+0x1C`.
The game `_system` field is distinct from the inherited application-owner
pointer at `+0x0C`. Common acceptance does not initialize the game `_system`
field. Base member setup `0x802E39A0` configures `_infoList` as an automatically
created `beBaseInfoList`; Static member setup `0x802B8374` does the same for
`_staticInfoList` at `+0x20`.

## Static per-RAM effects

`0x8031BBAC` reads the RAM's newly assigned `_parentInfo`. If cached messenger
`+0x14` is null, it repeatedly invokes manager hook `+0x88`. The Static hook
`0x8031BB4C` asks `0x8028A730` for the first exact `beMessenger` manager,
identified by type-handle storage `0x80534FBC`, and caches the result.
The original loops indefinitely if no messenger can be found. A bounded native
implementation must report an unavailable dependency instead of spinning or
inventing a successful registration.

Once the messenger exists, `0x8031BC18` calls `0x803050A8` with:

| Argument | Value |
| --- | --- |
| Messenger | Resolved messenger object |
| Recipient | This RAM object |
| Category | Exact string `STATICINFO`, stored at `0x80452958` |
| Name | Parent info name; a null name uses the empty string at `0x80567378` |
| Index | This RAM object's current `_ramNo` |

The callback then appends the parent **info**, not the RAM, to Static manager
`_staticInfoList` at `+0x20`, using `0x80069128` at `0x8031BC24`.
This happens once for every RAM occurrence. It is not a unique-info set.
The metadata name `_staticInfoList` and offset are at
`0x804CF46C`/`0x804CF474`.

This callback neither visits info `_dataList` nor decodes, uploads or starts
sound data. Its name does not imply that every static asset has been activated.
Messenger registration must implement the proven recipient-list behavior;
recording the call alone is insufficient. Sending later messages and executing
the selected data handlers are separate effects.

## Empty lists and alias identity

A non-null, valid RAM list with zero entries is an accepted info: the manager's
`_infoList` grows, activation is requested and `_isUpdateRun` becomes true.
There is no per-RAM messenger lookup, no recipient and no `_staticInfoList`
append in that case.

A null RAM-list pointer is dereferenced at `0x802F2598`; it is not the same as
an empty list. A null list element is also dereferenced by the RAM writes.
The native bounded profile must reject those inputs explicitly. Counts,
capacities, references and aggregate traversal work need validation before
publishing a successful initialization.

Repeated references identify one mutable RAM object within a loaded document.
Each occurrence overwrites its ordinal, parent info, parent manager and four
state integers. Earlier messenger recipient rows and Static info-list entries
remain appended. Their stored name/index key is separate from the RAM object's
possibly later-overwritten fields. The same rule applies when two infos in one
document alias the same RAM: the last successful occurrence supplies its final
backreferences, while earlier registration effects remain observable.

Numeric references from different loaded documents are different objects, even
when both documents came from the same archive entry. Native RAM identity must
include the loaded source and local reference. Export ordinal identifies an
acceptance occurrence; it must not duplicate the underlying RAM object.

## Activation and lifecycle boundary

`0x8028A398` uses application `+0x0C`, a separate active-manager scheduler,
not the manager registry at application `+0x08`. It calls the scheduler's
membership function at `+0x68`, then its add function at `+0x60` only when the
manager is absent. Repeated accepted infos do not add another active-manager
entry, but they still set `_isUpdateRun` and append their own accepted-info and
recipient occurrences. This establishes activation membership, not an update
tick or rendering schedule.

For later teardown research, the common cleanup `0x802F2628` clears RAM parent
manager/info pointers, releases the accepted-info list, requests removal through
`0x8028A400`, and clears `_isUpdateRun`. Static's separate hook at
`0x8031B9FC` releases its `_staticInfoList`. These are not run by the accepted
registration path. This document does not claim a complete engine lifecycle.

## Original first deferred controller

The first deferred occurrence is preliminary package member `[0.178]/s`,
`ndmw.afs` entry 178, explicit export 0, reference 499, named `dng101b1`.

| Field | Original value |
| --- | --- |
| Info type | Exact `beStaticInfo` |
| `_dataList`, slot 5 | `beBaseInfoDataList` reference 500, three entries |
| Data entries | `beCriSfpData` references 501, 502 and 504 |
| Data names | `common_blst.sfp`, `dng00.sfp`, `e_nume.sfp` |
| `_ramList`, slot 6 | `beBaseInfoRamList` reference 497, one entry |
| RAM entry | Exact `beBaseInfoRam` reference 496 |
| Stored RAM state | `_ramNo = 0`, `_isRun = true`, `_st0..3 = 0`, `_dataList = null` |

After faithful registration, RAM 496 has parent info 499 and parent manager
`beStaticInfoCtl`, ordinal zero, preserved `_isRun = true`, four zero state
integers and the unchanged null RAM `_dataList`. The manager retains info 499
once in each of its two lists. The messenger receives one
`STATICINFO` / `dng101b1` / index 0 recipient pointing to that RAM. The manager
is active and `_isUpdateRun` is true. The three sound-data records remain
unprocessed at this boundary.

An explicit controller callback inspection can demonstrate these effects while
the original loading-screen gate is still pending. It must not mark the full
startup sequence complete or skip the next unsupported deferred callback.

## Switch controller registration

`beSwitchCtrl` uses factory `0x802B7D74`, type storage `0x80534720`, size
`0x20` and vtable `0x804DF9F8`. Its common accepted-info path is the same
`0x802F251C` routine, with exact expected type `beSwitchCtrlInfo`. The per-RAM
hook `0x80315D40` calls messenger helper `0x80305308` with category
`SWITCHCTRL`. That helper uses the RAM's parent-info name, falling back to an
empty string for null, and its current RAM ordinal. The callback does not read
or change the target `igSwitch`.

The original ordinary map member exports `beSwitchCtrlInfo` reference 3471 at
export 9. RAM list 3475 contains RAM 3443; target slot 7 references switch 2877.
Explicit inspection of this callback creates `SWITCHCTRL / empty name / 0` for
RAM 3443. It leaves switch 2877's stored selection unchanged. This info appears
at aggregate deferred index 11 in the one-player inspection profile, but that
ordinal is not evidence that preliminary controllers may be skipped or replayed
at a later original stage.

Messenger insertion `0x803050A8` searches categories by exact string equality
through `0x800695CC`, creates a category only when missing, and appends recipient,
name and index to parallel ordered lists. It does not deduplicate equal keys or
RAM aliases. Messenger insertion initialization `0x80304BB0` activates the
messenger itself. Base preparation `0x802F24B0` resolves the system and messenger;
the native callback API requires these dependencies explicitly. It does not
claim that the original whole manager registry or its other preparations ran.
