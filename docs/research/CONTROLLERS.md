# Preliminary loading and native controller state

Version 0.13 implements the recovered common accepted-info callback and its
StaticInfo and SwitchCtrl per-RAM effects. These are native C++ state changes:
manager lists, unique mutable RAM objects, messenger categories, ordered
recipient rows and activation membership. They do not execute PowerPC code.

## Original stage boundary

The original file loader registers preliminary `/s` infos before reading ordinary
package members or running func/boot. It then performs the SOUND SEARCH,
MAKEBINLIST and ISDONETRANS protocol. The last query depends on completed sound
bank transfer; successful IGB decoding alone cannot satisfy it. The preliminary
deferred list is cleared before ordinary loading. See
[AUDIO_STARTUP_RESEARCH.md](AUDIO_STARTUP_RESEARCH.md) for source addresses.

`load_startup_preliminary` owns the descriptor and only its ordered `/s` files.
It publishes routes after that subset has loaded and validated. It has no Lua
runtime and cannot read ordinary/common files as a side effect. Its source
identities retain original member ordinals, including repeated occurrences.
The existing `load_startup_session` remains an aggregate inspection path. Its
routes combine preliminary, ordinary and common documents, so replaying them as
the original later registration queue would register preliminary infos twice.

```powershell
.\dist\DW4_PC\dw4_boot.exe 'PATH\Extracted' --package dng101b1.igb --preliminary
```

This command creates a fresh callback context with explicitly prepared
system/messenger dependencies. It does not reconstruct the original complete
manager registry or audio backend. It runs supported preliminary callbacks in
order and returns 3 at the next native barrier. Boot inspection options are
rejected because they have no effect before this boundary.

## Supported effects

For an exact `beStaticInfo` or `beSwitchCtrlInfo`, the common callback records
the accepted info, visits its RAM-list occurrences in order, resets `_st0..3`,
assigns RAM ordinal and parent references, invokes the per-RAM effect and
activates the manager. Stored `_isRun` and RAM `_dataList` are preserved.

| Controller | Messenger category | Name/index | Additional effect |
| --- | --- | --- | --- |
| StaticInfo | `STATICINFO` | Info name, or empty for null; RAM ordinal | Append the parent info once per RAM occurrence to the Static data-info list |
| SwitchCtrl | `SWITCHCTRL` | Info name, or empty for null; RAM ordinal | No target-switch write during registration |

Static registration does not process its embedded sound records. Switch
registration does not issue ALLON or ALLOF. Those later commands remain separate
from constructing a recipient. Address-level evidence is in
[CONTROLLER_INIT_RESEARCH.md](CONTROLLER_INIT_RESEARCH.md).

Within one loaded document, repeated RAM references share one owned state.
Later occurrences replace the final backreferences and ordinal, while earlier
messenger rows retain their captured registration keys. Equal numeric references
in different file occurrences remain distinct. Categories preserve first-use
order; recipient rows are never deduplicated. Active managers are unique.

## API and failure behavior

`initialize_controller_callbacks` accepts explicit deferred ordinals for narrow
callback inspection. `initialize_controller_prefix` visits the supplied sequence
in order and stops at the first malformed or unsupported handler. Neither skips
an unknown controller to reach a later supported one. The `StartupSession`
adapters explicitly inspect its aggregate routes, rather than establishing an
original startup phase.

Returned state owns its values and remains valid after source documents are
destroyed. Each selected address must identify an actual explicit export
occurrence. Source validation and allocation precede publishing that callback's
effects. A failed callback preserves earlier successful state without a partially
accepted info. An empty valid RAM list still accepts and activates its manager;
a null list or null element is rejected.

Limits are 128 files, 4,096 exports per file, 65,536 combined export rows and RAM
occurrence visits, 256 explicitly selected callbacks and 8 MiB of combined
serialized/copied names. The underlying IGB reader retains its independent
reference, byte-range and schema checks. Prepared dependencies are required;
the native implementation reports missing services instead of reproducing the
original infinite messenger lookup loop.

For original `dng101b1.igb`, the preliminary prefix initializes info 499 and RAM
496 from `ndmw.afs[178]`, creating `STATICINFO / dng101b1 / 0`. It then stops at
`beSoundInfo`. The three embedded SFP banks remain unprocessed. No complete game
startup, controller update loop, message execution or multiplayer session is
claimed by this increment.
