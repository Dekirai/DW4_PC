# Audio prerequisites in the original startup sequence

This research traces the next initialization dependencies after Static info
registration. It does not implement audio playback or claim that a retained
sound record is a working sound system. Evidence comes from the original USA
`GDJEB2` revision-0 DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, and the supplied AFS/IGB data.
Reference symbols establish function boundaries and Nintendo SDK function
names. Generated disassembly and original payload probes stay in ignored
`work/v13-*` files.

## A necessary refinement to the earlier loading model

The original file-list state machine registers preliminary `/s` resources
**before** loading the ordinary map member and executing `func.lua`/`boot.lua`.
The version-0.11 owned session retained a combined inspection routing list;
that aggregation must not become a claim that every retained occurrence first
registers at state 5.

State 100 begins at `0x802F9334`. It finishes the preliminary filename queue
through `0x802F9E98`, invokes deferred registration `0x802FA53C` at
`0x802F9348`, then clears the consumed deferred list at manager `+0x30`.
The final count-zero write is `0x802F946C`. It also clears the preliminary
filename queue and resets its ordinal. Consequently, the four entry-178 exports
Static 499, Sound 3, CriFx 5 and Se 232 initialize here, before the ordinary
entry-179 map is loaded. They must not be initialized a second time from the
later state-5 queue merely because the host owns them in one session.

The ensuing sound gate is observable original behavior:

| State / call site | Operation |
| --- | --- |
| 100, `0x802F9518` | Send `SOUND SEARCH` |
| 100, `0x802F9524` | Read boolean result 0 |
| 100, `0x802F9544` | If found, send `SOUND MAKEBINLIST` and enter state 101 |
| 100, `0x802F954C` | If not found, enter state 102 directly |
| 101, `0x802F9564` | Send `SOUND ISDONETRANS` |
| 101, `0x802F9570` | Stay in this state until boolean result 0 equals 1 |
| 102, `0x802F958C` | Build the ordinary package-member queue |

The message strings are at `0x804255D8`, `0x804255E0`, `0x804255E8` and
`0x804255F4`. `SEARCH` is a messenger-level operation, rather than an entry
in the Sound controller's command table. The retained Lua boot executor remains
useful in isolation, but a faithful startup orchestrator must preserve this
earlier registration and transfer boundary.

## Manager dependencies

The application creates `beCri` at `0x80319964`, before `beSound` at
`0x80319A04`. The important manager identities are:

| Manager | Factory | Type handle | Vtable | Size |
| --- | --- | --- | --- | --- |
| `beCri` | `0x802DFCF0` | `0x80535584` | `0x804DDCD4` | `0x48` |
| `beSound` | `0x802B8E70` | `0x80534774` | `0x804DF8DC` | `0x2C` |
| `beSeCtl` | `0x802BB5F0` | See its type registration `0x802BB808` | `0x804DF508` | Not required by this trace |

Sound uses the common base-controller acceptance handler. Its preparation hook
`0x80314904` finds the first exact `beCri` in the registry and stores an owned
reference at Sound `+0x20`. It also releases every existing item in its work
list at `+0x28`, clears the count and shrinks its storage. Base preparation
`0x802F24B0` separately establishes the system and messenger references.

Se preparation `0x80313040` resolves the first exact `beSound` into its
`+0x20` field. Its per-RAM initializer `0x8031308C` calls the messenger
registration routine with category **`ACTION_BROADCAST`**, a **null name** and
instance index **-1**. The category is the original string at `0x80426DC0`;
it must not be guessed as `SE` from the manager name.

Both managers require actual shared system/messenger/Cri ownership. Merely
creating empty placeholder managers does not implement their preparation.

## Sound info and per-RAM effects

`beSoundInfo` registration `0x802B8A4C` uses the member metadata table at
`0x804CF498`. Original preliminary entry 178 export 1 is reference 3, named
`beAsmSound2`. Its relevant fields are:

| Serialized slot | Native offset | Name / type | Original value |
| --- | --- | --- | --- |
| 5 | `0x14` | Inherited `_dataList`, object reference | 511, 81 sound data records |
| 6 | `0x18` | Inherited `_ramList`, object reference | 534, RAM 506 |
| 7 | `0x1C` | `_fnameList`, object reference | 61, six named objects |
| 8 | `0x20` | `_dataType`, string | `sfp` |
| 9 | `0x24` | `_baseVolume`, signed short | -30 |
| 10 | `0x26` | `_sndfVolume`, signed short | 75 |

IGB serialized scalar fields are little-endian. The signed short -30 has bit
pattern `0xFFE2`; reading it as an unsigned volume changes the original state.
The original PowerPC callback uses `lha`, preserving the sign.

After common RAM ordinal/backreference/state initialization, Sound's per-RAM
callback `0x80314AC8` performs the following effects in order:

1. Register this RAM with the shared messenger under exact category `SOUND`
   (`0x80427048`), **null name**, index **-1** at `0x80314AFC`.
   These keys are independent of the info name and newly assigned RAM ordinal.
2. Write sign-extended info `_baseVolume` to Cri `+0x34` at `0x80314B0C`.
3. Pass sign-extended `_sndfVolume` to `0x802F7244`.
4. If the info's runtime `_dataType` pointer is null, replace that pointer with
   the interned literal **`acx`** (`0x80427050`). Empty and null are different
   here. A native implementation should keep an owned mutable override keyed
   by qualified info identity, while leaving the serialized document unchanged.
5. Compare the resulting string exactly with `acx`. Store Cri `+0x40 = 0` if
   equal, otherwise `1`, at `0x80314BB8`–`0x80314BD0`.

For the original `sfp` record this produces base volume -30 and data type 1.
This callback does not traverse `_fnameList` or `_dataList`, select a sound,
decode a waveform or start playback. It does, however, submit a real volume
configuration command when the one-time setter below takes effect.

Messenger registration `0x803050A8` preserves a null recipient name as null
in its parallel string list (`0x80305188`–`0x80305194`). A host recipient model
therefore needs an optional name; replacing every null with an empty string
would erase a source distinction needed for later faithful dispatch. Repeated
RAM occurrences append repeated recipients, even though the RAM object itself
has one mutable identity. An empty valid RAM list runs no per-RAM effects.

## One-time SNDF volume setter and driver queue

`0x802F7244` checks Cri `_sndfVolume` at `+0x38`. If it is already nonzero,
the function does nothing. Otherwise, it accepts values 1 through 127 and
replaces every other signed value with 127. It writes the resulting value
before calling `0x803C2734(0, 0xA0010000, volume)`. The caller ignores this
driver function's return value; the one-time value is not rolled back on a
queue failure.

For volume 75 the driver encodes **`0xA0014B00`**. The code at
`0x803C2734` is a real bounded driver-command producer, not direct waveform
playback. For this command class, bits 8–14 contain the seven-bit value and
the low five bits contain the channel argument. Its supported path:

- Rejects a busy reentry guard at driver `+0x864` with -3.
- Requires the command's top bit; invalid commands return -2.
- Disables interrupts around the ring-buffer mutation.
- Requires count `+0x841 < 128` and an empty destination word in the array
  starting at `+0x240`.
- Stores the encoded command, advances byte write index `+0x843` modulo 128
  and increments the queue count. A full queue returns -1.
- Restores interrupts and clears the reentry guard.

A native command queue can preserve these ordering and capacity semantics with
host synchronization and owned command data. Merely logging the call while
claiming that the driver accepted or applied it is insufficient. Queue
acceptance, consumption and audible backend output are distinct states.

## Cri construction and preparation are larger than info acceptance

Cri is not a base controller. Its registry-insertion initializer at
`0x802F4EF4` activates it, calls `0x802A9148(1)`, saves a global callback and
replaces it with `0x802F4F48`. That replacement returns the value stored at
`0x80534370`; this trace does not assign it a guessed semantic name.

The audio service initialization includes actual GameCube hardware/library
boundaries. `0x802A906C` calls `ARInit`, `ARQInit`, `AIInit`, `AXInit`,
`AXARTInit`, `MIXInit`, `AXRegisterCallback` and `AXFXSetHooks`, followed by
the sound driver setup at `0x803C4958`. These must become native services,
not success-returning no-ops. The native port need not reproduce GameCube ARAM
hardware, but must own equivalent loaded audio data and provide working mixing,
streaming, completion and error behavior.

Cri's later preparation hook `0x802F4F94` resolves `beSystem` into `+0x10`,
creates/replaces BGM and environment handle objects at `+0x20` and `+0x24`,
and initializes each audio member with mode 2 through `0x8031C99C`. It then
invokes `0x802F66C8` to create four mode-1 SE handles and 48 mode-(-1) handles
in the list at `+0x28`. The latter branch constructs the alternate SFP-style
audio object; the nonnegative branch opens streaming resources through
`0x802A878C`/`0x802A8878`. Handle names become null. These are real handle
initialization calls, even though no sound has been selected for playback yet.

Preparation also clears Cri's one-time pause byte at `+0x3C` and three arrays
of 16 words starting at `0x80535A6C`, `0x80535AAC` and `0x80535AEC`.
The member metadata at `0x804D2850` identifies:

| Cri offset | Original name |
| --- | --- |
| `+0x10` | `_system` |
| `+0x14` | `_execDvdErr` |
| `+0x18` | `_request` |
| `+0x20`, `+0x24` | `_hdBGM`, `_hdENV` |
| `+0x28` | `_hdSEs` |
| `+0x2C` | `_infoList` |
| `+0x30` | `_lsBinObjs` |
| `+0x34`, `+0x38` | `_baseVolume`, `_sndfVolume` |
| `+0x3C`, `+0x40` | `_pauseOnetime`, `_dataType` |

The info acceptance callback itself, `0x802F51C4`, is much smaller: accept
only exact `beCriFxInfo` (type handle `0x80535514`), append it to Cri `+0x2C`
and return true; otherwise return false. It does not use base RAM registration,
schedule Cri again or configure reverb at this point. Original info 5 is named
`REVERB`; slot 5 `_target = GC`, slot 6 `_reverb = 1`, slot 7 `_delay = -1`,
and slot 8 `_xbFxParam = null`. These values remain retained until later use.

## Static SFP bank transfer

Sound command 20, `MAKEBINLIST`, enters at `0x803157A4`. It sends
`STATICINFO GETINFOLIST`, obtains the retained Static info list, examines Sound
filename entries whose extension matches `_dataType`, deduplicates resolved
base filenames, and finds matching named data in the Static infos. Matching
objects are appended to Cri `_lsBinObjs`. The detailed path helper semantics
remain a separate implementation task; the archive selectors in the original
filename list must not be treated as ordinary host paths.

The first Sound info's filename list includes `sound/sfp/GC/allse.sfp`,
`sound/sfp/GC/COMMON_BLST.sfp`, two explicit archive selectors, and the
`dng00.sfp`/`e_nume.sfp` names. Initial Static info 499 owns these three
`beCriSfpData` objects:

| Ref | Name | Serialized payload bytes | `_tblSize` | `_pcmSize` |
| --- | --- | --- | --- | --- |
| 501 | `common_blst.sfp` | 40960 | 1248 | 37344 |
| 502 | `dng00.sfp` | 43008 | 1248 | 39456 |
| 504 | `e_nume.sfp` | 108544 | 1664 | 106080 |

The inherited payload memory is serialized slot 4/native `+0x10`; its byte
length is slot 5/native `+0x14`. Own fields from metadata `0x804D2628` are
slot 6/native `+0x18` `_portNo`, slot 7/`+0x1C` `_tblSize`, slot 8/`+0x20`
`_pcmSize`, slot 9/`+0x24` `_isTransfer`. All three original port values are
zero and transfer flags are false before runtime work.

Direct payload inspection confirms the magic **`gcaxDTPK`** and big-endian
bank-header words, even though the enclosing IGB scalar fields are
little-endian. The original driver `0x803C2418` validates those first two
magic words and a version word before using the bank's ranges. The sample
version word is `0x02410000`; its `+0x10` table and `+0x18` PCM sizes match
the IGB values above. No endian rule should be applied indiscriminately to
both the IGB and embedded sound bank.

`0x802F6AA8` assigns each selected bank's port index from its list ordinal,
requires exact `beCriSfpData`, and calls `0x802F69B8`. That function skips an
already transferred bank; otherwise it updates bank-size tables, calls driver
allocation `0x803C1DD0`, then uploads through `0x803C2418`. A successful upload
releases the CPU payload and sets `_isTransfer`. The driver upload includes
`ARQPostRequest` at `0x803C264C` and a completion wait at `0x803C2650`.
The driver supports 16 bank ports; host validation must bound list ordinals
before accessing the corresponding fixed tables.

`ISDONETRANS` command 21 calls `0x802F6DF8`, checks transfer state via
`0x803C5D2C`, and publishes the resulting boolean through the messenger. Its
zero/already-complete states, outstanding state 1, successful state 2 and
failure states need real state. A missing host audio bank loader cannot simply
return true to pass the original preboot gate.

## Faithful implementation order

1. Preserve qualified info/RAM identity and nullable messenger recipient names.
   Implement exact Static, Sound, CriFx and Se acceptance effects independently
   of their future command handling. Keep manager preparation explicit.
2. Add owned Cri runtime state and a real bounded audio command queue, preserving
   the one-time volume setter, signed values, ordered effects and failures.
   Distinguish queued configuration from backend-applied configuration.
3. Implement the required native audio service and handle lifetimes. Use actual
   buffer/stream objects and completion state; do not mark a backend ready merely
   because its descriptor was allocated.
4. Reconstruct `MAKEBINLIST`, parse and retain validated original SFP banks,
   and expose genuine transfer/readiness results. Host-owned sample memory
   replaces console ARAM without fabricating successful loads.
5. Integrate preliminary registration and the sound gate into a staged startup
   orchestrator, then load ordinary members and run the existing original Lua
   func/boot phases. Only newly deferred ordinary/common infos belong to the
   later registration stage.

Meaningful tests include repeated Sound infos/RAM aliases, an empty RAM list,
negative base volume, first versus repeated SNDF volume, every out-of-range
volume class, null versus empty versus `acx`/`sfp` data type, exact CriFx type
acceptance, queue overflow, bank bounds/endianness, failed transfers, resource
lifetime and absence of duplicate preliminary registration.

## Layer/text boundary before start.lua

After state-5 registration, file-list state 6 sends `LAYERCTL ORDER` at
`0x802F99F0`. When the messenger reports at least one result, it sets up
`LAYERCTL` arguments `LOADING`, `SET` and executes `SWITCH`. It then iterates
held scene infos after descriptor slot 0, requests `GETLAYERNODE` by each
scene root's name, and attaches the original root with `0x801EAC44` if an
object result exists. State 7 sends `TEXTCTL INITIALIZE` at `0x802F9B0C`
before calling `start.lua` at `0x802F9B50`.

These are native messages before the script, not calls inside `start.lua`.
The loading-node time gate, actual layer/text handlers, original player creation,
and later sound playback still require their own implementations. None can be
inferred complete from a successful audio-info registration.
