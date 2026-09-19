# Native manager frame traversal

`ModelManagerFrameInvocation` reconstructs the common manager virtual `+74`
at USA `802F28F8`. It admits an update through the original system/manager
gates and visits the manager's ordered info and RAM occurrences. Each reached
RAM goes to that manager's selected virtual `+84` implementation. The
MODELCTRL/player implementation is the separate `8030CB2C` body described in
[MODEL_FRAME.md](MODEL_FRAME.md).

`make_model_manager_update` binds this body to a serial scheduler manager.
It retains a pending invocation and creates a new invocation after a completed
call. This represents one original manager operation; it does not establish
the game's complete startup registry, manager ordering, or all manager bodies.

## Original owners and defaults

The supplied USA `main.dol` has SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. The relevant player vtable is
`804E7DAC`: `+74` contains `802F28F8`, and `+84` contains `8030CB2C`.

| Original field | Native owner/field | Reflected default |
|---|---|---|
| Manager `+10` | `ModelSceneManager::system` | Explicit attached owner |
| Manager `+18`, info list | `ModelSceneManager::infos` | Auto-created empty `beBaseInfoList` |
| Manager `+1C`, `_isUpdateRun` | `ModelSceneManager::update_enabled` | Zero |
| System `+2C`, `_isActive` | `ModelSceneSystem::is_active` | Zero |
| System `+2D`, `_isFirstTime` | `ModelSceneSystem::is_first_time` | Zero |
| System `+2E`, `_isFrameSkip` | `ModelSceneSystem::is_frame_skip` | Zero |
| Info `+18`, RAM list | Existing concrete info owner's `rams` | Existing physical owner |
| List `+08/+0C/+10` | `count/capacity/backing` | Existing independent fields |
| Manager virtual `+84` | `ram_frame_factory` | Missing implementation remains pending |

Manager metadata `802E39A0` gives `_isUpdateRun` its zero bool default and
auto-creates the info list (`802E3A38`, with the creation flag stored at
`802E3A50`). A direct raw constructor's null pointer is not the completed
reflected default. System metadata `802B7838` gives fields four through six
their zero bool defaults. The relevant lifecycle slots `+24/+2C/+34` of
system vtable `804DFB20` are no-ops; they do not override these bytes.
Activating a prepared test explicitly does not prove the game's activation
and startup paths ran.

## Admission and ordered reads

The original body loads manager `+10` and calls `80316808` before reading the
manager's update byte. This leaf reads system `+2D` and then `+2C`; it returns
true only if `+2D` is zero and bit zero of `+2C` is set. An active byte of two
therefore blocks, while three admits. A null system remains a reached fault
even for a disabled manager.

After the system leaf admits the call, manager `+1C` must be nonzero. The body
then reloads manager `+10` and requires system `+2E` to be zero. Only after
these gates does it dereference manager `+18` and capture its signed count
at `802F2944`. These gates are not repeated after a RAM callback.

For every admitted outer occurrence, `802F294C..64` reloads manager `+18`,
reads that list's current backing and physical indexed info, then captures the
info's `+18` RAM-list owner and signed count. The outer captured count does
not change if a callback replaces the manager's list or count. Its next
physical indexed read does use the live replacement list/backing.

For each inner occurrence, `802F2970` reads the captured RAM list's current
backing. The manager vtable is loaded at `802F2974`, followed by the physical
RAM pointer and virtual `+84` target. The current RAM-list count/capacity are
not consulted again. Replacing an info's RAM-list field does not redirect its
already captured inner traversal; replacing that captured list's backing
does affect its next RAM read. The virtual implementation is selected again
for each newly reached call.

Neither loop checks the info or RAM active byte. Duplicates remain duplicates.
A null RAM is passed to the selected implementation unchanged: this caller
does not dereference it. Any concrete RAM-type/null checks belong to that
implementation. A null info or RAM list does fault at its reached dereference.
Nonpositive signed counts complete their loop without inspecting backing;
they do not bypass the preceding list-pointer dereference.

## Native continuation and physical profile

`ModelManagerRamFactory` receives the exact manager and generic physical RAM
argument, and returns one `ModelManagerRamCall` for that invocation. This lets
the actual MODELCTRL binding construct one fresh retained per-RAM frame
without routing through a registration ID or assuming four players.

If the virtual implementation is unavailable, the manager suspends after
capturing the reached RAM. Installing the implementation resumes that call
without rereading the RAM pointer, earlier counts, or admission gates. Once a
factory is selected, its returned operation survives pending retries even if
the manager's binding changes. A selected factory returning an empty operation
is a malformed binding and faults; it cannot rerun its earlier side effects.

The known info profile reads the actual `rams` fields of `ModelSceneInfo`,
`ModelHitLandInfo`, and `ModelHitLandGeometryInfo`. Other concrete info types
are explicit boundaries until their field owners are represented. Physical
reads ignore current logical count/capacity and are bounded by the represented
allocation and the existing 262,144-slot native profile. Unavailable reached
slots fault after all earlier completed callbacks. No whole-list preflight
erases that prefix.

Native shared ownership retains captured manager/RAM/list owners across
suspension; it is not a claim about extra original reference-count operations.
`ModelManagerFrameExecutionState` is a separate continuation lease. Overlap
is rejected without poisoning the existing invocation. Exceptions, caught
recursive entry, or abandonment of an incomplete started invocation prevent
a new invocation from replaying the manager's completed callback prefix.
These are host continuation contracts, not original exception handling.

## Actual player binding

`ActionEventRuntime::bind_player_frame_update` installs the player's recovered
`8030CB2C` operation. The factory validates its original live runtime owner
and the reached nonnull RAM type, then creates a new `ModelFrameInvocation`.
Its closure holds that invocation across suspension and only a weak reference
to the runtime. It rejects a destroyed facade even if another owner retains
the underlying manager. Installing the factory neither registers infos nor
changes the original system/manager gate bytes.

The retained callback is selected once per reached physical occurrence.
Replacing the manager's factory or the physical RAM slot while that callback
is pending does not retarget it. A later occurrence selects the current
factory and receives a new invocation even for duplicate RAM pointers.
Registration IDs are not used to reconstruct the list or select the RAM.

## Validation

The 315 permanent checks cover all 256 active-byte values, remaining byte gates,
metadata defaults, disabled/null read order, signed counts, known info types,
inactive/null/duplicate RAM arguments, physical list aliasing, independent
count/owner/backing/virtual reloads, pending binding capture, reached failure
prefixes, overlap, recursion, abandonment, and the actual serial-scheduler
service connection.

The local original-body comparison executes the actual `802F28F8` and
`80316808` instructions for 4,096 fixtures: 448,733 original instructions at
58 addresses and 18,045 prepared RAM calls, with zero mismatches against the
final production `dw4_core` build.
Its prepared per-RAM endpoints perform explicitly
supplied field mutations; they do not stand in for validated gameplay bodies.
The corpus checks callback target/argument order and resulting list-owner,
count and gate fields. Full application registration and every reached
concrete manager/RAM operation remain separate requirements.
Local comparison evidence is in `work/native-manager-frame-result.json`;
the decoder, prepared endpoints and corpus scope also received independent
instruction review.

The actual runtime binding has additional checks in `model_frame_tests`,
including pending sound service completion, live virtual replacement,
duplicate occurrences, fresh subsequent frames, type/owner failures and
runtime facade lifetime without an ownership cycle.

The supplied original-data fixture explicitly registers the canonical Agumon
info and activates its manager/system, then runs seven serial batches through
this traversal and the real player binding: 28 completed player calls.
The first batch completes RAM zero before suspending RAM one at missing viewer
setup; retrying and supplying the viewer preserve the already completed
player's node and command time. Six more batches cover held and released
directional samples, SQ_WALK/RUNND1 and SQ_WAIT/WAITD1, per-player delayed
number effects, balanced native collision and scheduler cache cleanup.
Clock samples, device data, initial viewer state and registry construction
remain explicit. This is not complete boot, active camera publication or
connected controller gameplay. Exact results are in [VALIDATION.md](VALIDATION.md).
