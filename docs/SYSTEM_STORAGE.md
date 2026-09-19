# Native system storage and error orchestration

`SystemStorageInvocation` reconstructs the control flow of original USA
`80316974`, called near the end of the scheduled `beSystem` update
`80316258`. It samples an explicit storage-status provider, publishes original
error fields, sends actual native messenger commands, manages the original
deferred-message pause anchor, and advances the error-display and recovery
phases. Completion returns the original helper's boolean value: a completed
error or recovery operation returns zero, independently of native suspension.

This does not implement a console DVD drive or the missing viewer/manager
operations. Those operations are individually reached services; absence pauses
at that operation instead of reporting successful completion. A native host
must supply real storage/reset policy and original owners. This module does
not select a stage, manufacture a camera or establish manager registration.

## State and ownership

Evidence comes from USA main.dol, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Registration `802B779C` creates
`beSystem`, vtable `804DFB20`; virtual `+74` is `80316258`. Metadata
`802B7838` supplies the state defaults:

| Offset | Native field | Default |
| --- | --- | --- |
| +54 | `dvd_status` | 0 |
| +58 | `error_phase` | 0 |
| +5C | `is_dvd_error` | 0 |
| +5D | `is_now_freeze` | 0 |
| +5E | `is_now_pad_enable` | 0 |
| +5F | `ModelSceneSystem::reset_enabled` | 1 |

The first five fields belong to `SystemStorageState`; the reset gate is shared
with the surrounding system update. An aliasing shared pointer can retain
storage embedded in the same system allocation. The native execution lease
rejects overlapping, recursive, faulted or abandoned started invocations.
Already visible prefixes are retained after failures; completion does not
replay callbacks when resumed.

World-factor calls retain their captured context argument before an operation
binding is available, so replacing the system context while waiting cannot
redirect that call. Status, messenger and viewer lookup functions are captured at their reached
calls. A pending function retains its own continuation. Messenger lookup
returns a native messenger, delivery services and its own context owner. The
selected messenger allocation survives replacement of the binding's pointer.
The delivery-services and context fields remain live on that captured binding;
the pause setter reads the messenger context when actually reached.

Viewer lookup captures manager `+14` and, during restoration, `+18`. These
opaque retained handles identify unrecovered native owners; they are not
implemented rendering or graph objects. The selected field handles are copied
at lookup completion. Replacing the provider's result wrapper cannot redirect
a pending scene operation. A represented null manager fails; a null scene-info
field fails when the first scene dereference is reached, after any preceding
time/message effects.

## Status classification and messages

The provider returns the value observed at original `DVDGetDriveStatus`
`80250AE4`. This module does not infer it from elapsed time or substitute
constant success for missing storage integration.

| Returned status | State prefix |
| --- | --- |
| 0 | Clear `is_dvd_error` |
| -1 | Clear `reset_enabled`, then enter error handling |
| 4, 5, 6, 11 | Enter error handling |
| Every other int32 value | Preserve the preceding error fields |

On error, a previously clear error byte stores the status and phase1. An
already set byte with a changed status stores the new status and phase2.
An unchanged error leaves the phase alone. All error cases then store
`is_dvd_error=1` and resolve the original messenger independently.

Native transport selects `STAGECTL` with null name and index -1, appends
string `STATUS` and boolean0, and sends `PADINPUT`. It reads physical
boolean slot0 afterward. Nonzero skips the disable request. Zero selects
again, appends `DISABLE`, sends `PADINPUT`, then stores
`is_now_pad_enable=1`. The native buffer represents published elements only;
it does not retain original spare-capacity words after a list clear. An absent
represented slot therefore faults at this read boundary instead of inventing
false. This is a native profile limit: original logical count0 alone does not
prove that its raw backing read would fault. Selection clears preceding packet
fields in the same way as other native messages.

The phase is read only after those potentially nested callbacks. Consequently
callback edits to the live phase affect the branch actually selected.

## Error phases

| Live signed phase | Ordered behavior and return |
| --- | --- |
| <=0 | Return1 |
| 1 | Freeze setup below, increment live phase modulo32 bits, return0 |
| 2 | Select error switch, clear mask, select status bit, increment live phase modulo32 bits, return0 |
| 3 with error byte nonzero | Return0 |
| 3 with error byte zero | Restore below, clear phase and pad-restoration flag, still return0 |
| Other positive values | Return0 without phase-specific effects |

Phase1 resolves the viewer and then resolves the messenger again. It requests
world factor0 through the original `8028B054` boundary. If the selected
messenger's pause anchor is zero, it sets `is_now_freeze=1`, requests the
original manager-disable sweep, then sets deferred pause using that messenger's
own live context time. Otherwise it sets `is_now_freeze=0` and preserves the
existing pause. Time zero remains the original unpaused sentinel, even when
this invocation owns the freeze flag.

It next sends `CAMERA/ONFREEZE`, then `SOUND/ONFREEZE`, requests saved-viewer
child transfer, and registers the disc-error infos before incrementing the
phase. Pending messages retain the native `MessengerDispatch`, including
completed recipients and their packet state.

Phase2 resolves a viewer, then reaches the first-child/type path:
viewer scene root -> first `igAttrSet` -> first `igSwitch`. The exact metadata
globals are `80565378` and `805647B0`, with registrations `801C8C48` and
`801ADDF0`. The selected switch is retained independently. Clear helper
`80206760` clears its mask, and the original jump table at `804DFA90` selects:

| Saved DVD status | Enabled switch bit |
| --- | --- |
| -1 | 4 |
| 4 | 1 |
| 5 | 0 |
| 6 | 2 |
| 11 | 3 |
| Other values | None; leave the mask cleared |

The saved status is read after the clear operation. A selected pending bit
operation keeps its original bit argument even if later host-side field edits
change `dvd_status`. Switch selection, clear and set remain separate explicit
operations so their ordering and partial prefixes are visible.

Phase3 resolves the viewer's scene and renderer owners, then the messenger.
It removes the disc-error children, requests world factor1, and reads
`is_now_freeze`. If nonzero, it requests the manager-enable sweep, clears
deferred pause, and sends `SOUND/OFFREEZE`. It always sends
`CAMERA/OFFREEZE`, then restores the saved children. If the live pad flag
is nonzero, it selects STAGECTL, appends `ENABLE`, and sends PADINPUT.
Finally it clears the pad flag and phase. It does not clear `is_now_freeze`,
restore a remembered world factor or manager enable state, or return1 in
that same update. The next healthy update returns1.

## Explicit remaining operations

`SystemStorageServices::operation` binds an individual retained call. Binding
itself has no operation effects; an empty call means the operation is still
unavailable. The call owns any internal continuation and must not repeat its
completed effects. The exact remaining boundaries are:

| Operation | Original effect still required by the service |
| --- | --- |
| `set_world_factor` | `8028B054`, apply factor0 or1 to the live application owner |
| `set_managers_enabled` | Traverse the actual registered scheduler list; write +1C for beModelCtrl, beAction2 and beGenerater, preserving original count/backing/type-test order |
| `save_viewer_children` | `80316C08..78`, append each first child to system +50, then remove it from the current scene |
| `register_error_infos` | `80316C88..AC`, live +4C info list, context reload per registration |
| `select_error_switch` | `80316CD8..D4C`, actual first-child/type checks after the preceding viewer lookup captures +14, no invented switch |
| `clear_error_switch` / `set_error_switch_bit` | `80206760` / `802066A8`, physical original mask operations |
| `remove_error_children` | `80316E18..74`, remove current first children until the live list is empty |
| `restore_viewer_children` | `80316F58..FD4`, append/remove saved children and execute the actual viewer rebuild after each transfer |

Append helper `801EAC44` calls the parent's virtual+8C; remove helper
`801EADDC` calls the child's virtual+88 and updates parent state. These are
not equivalent to unchecked host vector mutations. Restoration helper
`8040B994` is a substantial rebuild of retained viewer lists and scene
attributes, not a no-op notification. The opaque service handles must not
be mistaken for completion of these still unrecovered operations.

The error-status branch also requires actual original messenger recipients.
Tests can install explicit authored recipients to verify packets and ordering;
that does not implement STAGECTL, camera or audio behavior in the game.

## Validation scope

Asset-free tests cover the full error1→2→3→0 lifecycle with real native
messenger delivery and labeled scene/manager service seams; all five status
bit mappings; ready, unknown and signed-negative states; noncanonical boolean
bytes; pause ownership and time-zero behavior; live phase/status edits at
callbacks; suspended sampling, sends and scene work; replaced providers and
owner wrappers; malformed physical replies; overlap, recursion, abandonment
and aliasing system lifetime. The final production-linked suite passes
195 checks; the full local build passes 129/129 suites. Broader original-data
validation is recorded in VALIDATION.md.

The ignored independent instruction corpus compares 1,816 cases with the final
production library. It executes 156,054 original instructions at 392 addresses,
including `80316974`, the actual pause setter `80304FA8`, and selection/send
wrapper `80305F58`. Final state words, pause anchor, selected mask bit and
ordered observable messenger/factor effects match with zero mismatches.
SDK status, manager lookup, message replies, scene mutations, mask helpers and
the factor setter are explicitly prepared endpoints on the original side.
The native side uses production storage and NativeMessenger with authored
recipients and the same labeled scene/manager seams. This validates the
orchestration for those supplied inputs; it does not qualify real storage,
error-scene rendering or the opaque service implementations.

The comparison used production core SHA-256
`132da6eb586e321be5dc20a613373a9625bb55b6bb06b1f78fec414a3b2610f8`.
Independent source reviews checked the phase order, live reloads, copied viewer
handles, retained lookup/factor arguments and false return after restoration.

These tests do not emulate a drive, load the error scene or claim an actual
storage-error UI can render. The surrounding system update must still execute
reset handling, timing and stage loading in the original order before this
helper, then use its returned low byte for the original first-time tail.
