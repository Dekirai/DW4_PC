# Original boot host bindings

This research identifies the native effects needed to execute the original
`func.lua` and `boot.lua` for the USA `dng101b1.igb` package. The bounded result
is an ordered common-file loading queue. Finishing those scripts does not mean
the common files have been initialized, `start.lua` has run, or the game is
playable. Script selection and the surrounding scheduler are described in
[STARTUP_RESEARCH.md](STARTUP_RESEARCH.md).

Evidence comes from the original `GDJEB2` DOL and the embedded Lua chunks in
`ndmw.afs` entry 179. The DOL SHA-1 is
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original payloads, disassembly and
local probes remain outside the source distribution.

## Executed script order

The top level of `func.lua` defines 17 global closures and calls no host method.
For example, defining `_ERRORMESSAGE` does not execute its `be:MsgBox` call.
Definitions for later player generation, stage changes, messages and layer
activation also have no effect until called.

The top level of `boot.lua` creates seven local helper closures, then executes:

1. Clear the common-file queue with `be:CFLclear()`.
2. Build the camera file list, passing the vararg name `animCamera01`.
3. Add the difficulty-specific generator file.
4. Add player model files.
5. Add weapon files.
6. Add the country-specific resource pack.

The area and lighting helpers are defined but are not called by this top level.
`SYSgetPlayerNinzu` is used by later functions in `func.lua`; it is not called
while executing the inspected `func.lua` and `boot.lua` top levels.

The executed native binding subset is therefore only `CFLclear`, `CFLsetName`,
`CFLgetFolderName`, `SYSisJoinPlayer`, `SYSgetPlayerName`, `SYSgetWeaponName`,
`SYSgetDifficulty`, `SYSgetCameraMode`, `SYSgetCountry` and `SYSgetIsPackOnly`.
The standard Lua global `gsub` is also needed for the branch with more than two
joined players. Unknown methods must stop execution rather than succeed as
empty stubs.

## Native registration and state sources

Registration at `0x802EB788` passes a 116-record descriptor stream at
`0x804242AC`, a name table at `0x804DCE50`, and dispatcher `0x802EA1FC` to
`0x8026C068`. The name table is not in dispatch-ID order. Each descriptor
contains its own method ID, argument count and name-table index. The dispatcher
uses that ID to select a branch from the table at `0x804DD004`.

The following arities exclude the colon-call receiver. Original numeric
results are Lua numbers; `SYSisJoinPlayer` returns numeric 0 or 1, not a modern
Lua Boolean.

| Method | Dispatch ID | Native implementation | Arguments | Verified result/effect |
| --- | ---: | --- | ---: | --- |
| `SYSisJoinPlayer` | 0 | `0x802EBA24` | 1 | Player record `ISUSE` byte |
| `SYSgetPlayerName` | 1 | `0x802EBA58` | 1 | Player record `DIGIMONNAME` string |
| `SYSgetPlayerNinzu` | 2 | `0x802EBB18` | 0 | Common record `NINZU` integer; not called by boot |
| `SYSgetWeaponName` | 3 | `0x802EBA8C` | 2 | Player `POWERSOCKETNAME` list element, or null past its count |
| `SYSgetDifficulty` | 5 | `0x802EBB4C` | 0 | Common record `DIFFICULTY` integer |
| `SYSgetCameraMode` | 7 | `0x802EBBC0` | 0 | Common record `CAMERAMODE` integer |
| `SYSgetLightMode` | 8 | `0x802EBC24` | 0 | Common record `LIGHTMODE` integer; not called by boot |
| `SYSgetArea` | 9 | `0x802EBC88` | 0 | Literal 1 in this USA executable; not called by boot |
| `SYSgetCountry` | 10 | `0x802EBC90` | 0 | Literal 1 in this USA executable |
| `SYSgetIsPackOnly` | 12 | `0x802EBCA0` | 0 | Literal 1 in this USA executable |
| `CFLclear` | 26 | `0x802EC7C0` | 0 | Clear the common-file string list |
| `CFLsetName` | 27 | `0x802EC8B8` | 1 | Append a name only if no exact duplicate exists |
| `CFLgetFolderName` | 28 | `0x802ECA60` | 0 | Active descriptor's inherited name |

The state selection helper at `0x802EB9C4` addresses `GAMERAM` through
`GETCOMMON` or `GETPLAYER1` through `GETPLAYER4`; player getters add one to the
script's zero-based slot. The count getter reads `NINZU` independently. It must
not be assumed to recompute a count from `ISUSE` flags.

The workbench's explicit inspection inputs stand in for the named game-state
records that have not been initialized by the original game. An all-unjoined,
difficulty-0, camera-mode-0 profile is a reproducible inspection state, not a
claim about a new game's or saved game's defaults. Country and pack-only differ:
the literal 1 results are proven properties of this exact USA DOL. Any override
would need to be labeled as an inspection override.

## Queue effects and resource lookup

The native system object holds the common-file list at offset `+0x34` and the
active descriptor name at `+0x38`. `CFLclear` releases existing string entries
and resets the count. `CFLsetName` scans existing entries with `strcmp`
(`0x8009D5D8`), preserves the first occurrence and original spelling, and appends
only when there is no exact match. It does not read a file, resolve an archive,
initialize an object or activate a scene.

`CFLgetFolderName` returns the descriptor's inherited name, not a filename stem
guessed by the host. For `dng101b1.igb`, both the inspected inherited name and
section are `dng101b1`.

Queue identity and later catalog lookup have different case rules. The physical
named resolver at `0x802A9828` copies the requested name through `0x80071F9C`,
then invokes `0x80072164` at `0x802A998C` before comparing it with reconstructed
catalog paths. That conversion adds `0x20` only for characters selected by bit
`0x80` in the 256-byte classification table at `0x80477400`. Reading the entire
table proves that the selected bytes are exactly ASCII `A` through `Z`.

Thus the queue should retain `aaaCamera.igb`, `ACMR_pack.igb`, `generatorN.igb`
and `USA_pack.igb`, while catalog lookup uses their ASCII-lowercase forms.
Unicode folding, filesystem case rules and suffix guessing are unnecessary.
Names differing only in case are not deduplicated by the original queue setter.

## Original queue for explicit zero-player inspection

With all four `ISUSE` values zero, difficulty 0, camera mode 0, and the native
USA country/pack-only constants, boot makes 105 native calls and requests 94
distinct paths. The counts below come from an independent static control-flow
oracle and match execution by the original Lua reference interpreter.

The ordered queue is:

| Queue indices | Names, in order |
| --- | --- |
| 0 | `dng101b1/separate/aaaCamera.igb` |
| 1 | `dng101b1/separate/ACMR_pack.igb` |
| 2 | `dng101b1/separate/generatorN.igb` |
| 3–18 | `_common/p_` + the 16 model names below + `.igb` |
| 19–25 | `_common/w_arm01.igb` through `w_arm07.igb` |
| 26–31 | `_common/w_axe01.igb` through `w_axe06.igb` |
| 32–38 | `_common/w_common01.igb` through `w_common07.igb` |
| 39–45 | `_common/w_cutlas01.igb` through `w_cutlas07.igb` |
| 46–51 | `_common/w_dagger01.igb` through `w_dagger06.igb` |
| 52–57 | `_common/w_glave01.igb` through `w_glave06.igb` |
| 58–65 | `_common/w_gun01.igb` through `w_gun08.igb` |
| 66–71 | `_common/w_katana01.igb` through `w_katana06.igb` |
| 72–78 | `_common/w_maul01.igb` through `w_maul07.igb` |
| 79–85 | `_common/w_saber01.igb` through `w_saber07.igb` |
| 86–91 | `_common/w_sword01.igb` through `w_sword06.igb` |
| 92 | `_common/w_sp0203.igb` |
| 93 | `dng101b1/separate/USA_pack.igb` |

The player model names in source order are `agumon`, `alpha`, `angelr`,
`beelzeb`, `dorumon`, `dukecrim`, `girumon`, `hekabut`, `impdrafm`, `vmon`,
`wargrey`, `weregaru`, `impdrapm`, `susanoo`, `metalgaru` and `bwargrey`.

All 94 paths resolve to available entries in the original catalog after the
proven ASCII conversion. The first three resolve to `ndmw.afs` entries 180,
182 and 184, and the final USA pack to `area.afs` entry 9. This checks catalog
locations; it does not perform the original runtime loading/registration.

`animCamera01` is passed to the camera helper but is not separately queued
because the native pack-only result is 1. The same is true of the 29 names
passed to the country helper: only its pack is requested. The native country
value 1 selects index 2 (`USA_`) in the Lua country-prefix array.

## Explicit joined-player branches

The player and weapon helpers both test slot 0 first. If slot 0 is not joined,
they request the full fallback model/weapon lists even if another slot is
joined. This must not be replaced with a generic `joined_count == 0` test.

When slot 0 is joined, the player helper queries all four join flags to count
participants, then queries them again while adding model names. At most two
joined players retain their `DIGIMONNAME` values. More than two invoke Lua
`gsub(name, "p_", "q_")` before adding the path; this is the actual script's
replacement operation, not a heuristic model association.

The weapon helper visits slots 0–3 and weapon indices 0–2 in ascending order
for each joined player. Null weapon-list results become Lua nil and add no
path. An empty string is still truthy in Lua 4; the host must not silently
substitute another weapon. Duplicate names can cause multiple setter calls
but occupy only one final queue entry.

For the explicit one-player input `p_agumon` and weapon list
`[w_sword01, w_gun01, nil]`, the queue contains seven paths: the same three
camera/generator paths, `_common/p_agumon.igb`, `_common/w_sword01.igb`,
`_common/w_gun01.igb`, then the USA pack. There are 34 native calls.

An additional explicit three-player oracle uses `p_agumon`, `p_vmon` and
`p_dorumon`, with repeated sword/gun names. It yields the three corresponding
`q_` models and nine queue entries after deduplication. Its 49 calls include
three standard-library `gsub` calls. All seven/nine paths also resolve in the
original catalog. These inputs are test profiles, not recovered save data.

## Lua chunk ABI

Both inspected chunks begin with `1B 4C 75 61 40 01 04 04 04 20 06 09 08`.
They use Lua 4.0, little-endian storage, four-byte integers, four-byte size
fields, four-byte/32-bit instructions, six opcode bits, nine B-argument bits
and eight-byte floating-point numbers. The following IEEE-754 double test
value is `314159265.35897934`. The script storage byte order is independent of
the big-endian PowerPC executable.

Lua 4 vararg helpers use a local `arg` table, the `arg.n` count and one-based
numeric entries. This is not modern Lua's `...` convention. Lua nil is false;
numeric zero and empty strings are true. Original host numeric predicates are
compared explicitly with 1 where the script requires a joined player.

## Independent reference execution

The official [Lua 4.0.1 source archive](https://www.lua.org/ftp/lua-4.0.1.tar.gz)
was checked against its [published SHA-256](https://www.lua.org/ftp/):
`df746e149cf6939e90009d2e540eee918d585b4d1bc6d68b19316a050d484d2a`.
Its unmodified core, base library and string library were compiled with MSVC
for x86, where `size_t` is four bytes and `double` is eight bytes. A separate
local harness supplied only the inspected host methods and explicit test
inputs; `gsub` ran through the official string-library implementation.

Executing the original `func.lua` followed by `boot.lua` succeeded for the
zero-, one- and three-player profiles. Every ordered call name and argument,
plus every final queue entry, matched the independent static oracle exactly:
105/94, 34/7 and 49/9 calls/entries respectively. `func.lua` made no host calls.
This supplies a reference for the project's bounded interpreter without
depending on that interpreter's implementation. It does not execute native
PowerPC code or recover uninitialized game-state records. The official
reference source, executable and original chunks remain in ignored local work.

## Stop boundary

After `func.lua` and `boot.lua` return, the original file-list manager stores
state 4 at `0x802F9734`. State 4 reads the common list and submits it to
`0x802F9E98` at `0x802F97D8`; actual IGB loading is requested through
`0x8006FD34` at `0x802FA4E8`. Loading completion, sound handling, deferred
registration and later layer/text initialization precede `start.lua`.

The bounded host can therefore execute real script instructions, mutate its
real deduplicated queue and report checked resource locations, then stop at
this common-load/registration barrier. Calling `start.lua`, fabricating later
host results, or marking startup complete would cross that boundary. No player
creation, first-script switch activation, input, collision or gameplay follows
merely from a successfully built queue.
