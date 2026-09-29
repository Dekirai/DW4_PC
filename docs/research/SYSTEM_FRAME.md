# Native scheduled system update

`SystemFrameInvocation` reconstructs `beSystem` virtual `+74` at USA
`80316258`. It coordinates reset handling, the existing timing leaf, stage
processing, storage/freeze processing and the final first-time gate. The
separate stage and storage continuations preserve their own reached calls;
missing operations remain pending rather than reporting completed startup.
See [SYSTEM_STAGE.md](SYSTEM_STAGE.md) and [SYSTEM_STORAGE.md](SYSTEM_STORAGE.md).

`SystemFrameState` derives from the existing `ModelSceneSystem`, so player
managers and the scheduled system update share the same timing, context and
gate fields. Additional stage/storage fields belong to that allocation;
aliasing shared pointers retain it during child invocations. Installing
`make_system_frame_update` in a serial scheduler supplies the actual system
operation. It does not establish original registry order or run initialization.

## Defaults and actual initialization

The recovered reset-enable field `+5F` defaults to one. Reset current, last
and release bytes `+60/+61/+62` default to zero. System time and gate defaults
are described in [APPLICATION_FRAME.md](APPLICATION_FRAME.md) and
[MODEL_MANAGER_FRAME.md](MODEL_MANAGER_FRAME.md). Represented stage names and
the pending directory default to null; storage error state defaults to zero.
These are represented reflected fields, not a claim to implement the complete
system factory and every auto-created subordinate owner.

Actual initialization `80315E60` sets active to one, clears first-time, requests
`ndmw.igb`, prepares loader state and loads the disc-error scene through
`8031682C`. Consequently a factory-default system with no stage request is
only a prepared steady-state profile. The complete original initialized
startup must execute its actual loader, registry and scene operations.

## Reset and time publication

The system update has no ordinary model-manager active/first-time/frame-skip
admission gate. It first reads the latched reset-release byte. If nonzero,
sampling is skipped. Otherwise it copies current to last before calling the
platform sample, normalizes the full 32-bit return word into current, and
reloads last/current to publish:

```text
release = (last & (last ^ current)) != 0
```

This retains the original behavior for noncanonical raw bytes. Disabling
reset does not disable sampling or latching. A nonzero enable and release
then select, in order, display blanking with argument one, display flush,
synchronization, storage-presence check and reset action. A nonzero full
storage result selects reset arguments `(0,0,0)`; zero selects `(1,0,0)`.
If the reset action returns, this invocation still exits without performing
time, stage or storage/freeze work. Later changes to gate bytes cannot undo
the already admitted reset path.

Those platform operations are explicit services. An unavailable sample
preserves the already stored last byte; pending reset actions preserve all
earlier display effects and the selected disk decision. The native port does
not execute console hardware instructions or manufacture reset input. A
future PC reset adapter must implement game reset, not reboot the host PC.

On the normal path, `publish_system_frame_time` runs exactly once before
loading. It uses the live system context, wraps the 64-bit difference,
publishes the original uncapped float delta and retains the sampled world
timestamp. It does not sample a clock itself.

## Stage, storage and final gate

Stage processing preserves the request-versus-completion split: a new
directory request is not registered as complete in the same system update.
Its original pool selections, loader calls, info traversal and cleanup retain
their actual read and publication order. Unrecovered loader work stays at
its explicit service boundary. The storage helper then executes its status
classification, native message delivery and error-state progression; concrete
viewer, registry and platform operations still require their implementations.

Only a nonzero low byte of the completed storage helper's return value allows
the final first-time check. The system active byte here uses **any nonzero**
value, unlike the model-manager gate's bit-zero test. A nonzero first-time
byte is then cleared. A completed storage recovery phase can return zero, so
completed helper execution alone does not imply clearing first-time.

## Native continuation contract and integration

Selected callbacks and child invocations survive suspension. Replacing a
service function cannot retarget an entered call. One unfinished invocation
owns the system; overlap rejects without invalidating that owner. Recursive
entry, exceptions or abandonment fault it while preserving completed effects.
A completed retry never samples, republishes time or repeats a child operation.
The reusable scheduler binding starts a new invocation after completion.

The original-data integration now runs seven application updates through the
native serial scheduler, this system body and the player manager's authored
four-RAM traversal. Timing publication occurs inside the scheduled system,
and its first-time tail must admit subsequent player updates. A second-player
input suspension preserves the application's sampled time, dispatcher and
completed system prefix. The fixture explicitly supplies clock/reset/storage
samples, a dispatcher endpoint, registry order, gate activation, initial
viewer state and player input. It also explicitly detaches application
callbacks during teardown to release the prepared context/registry ownership
cycle; original registry shutdown remains separate work.

This qualifies the connected prepared path, not complete startup, native
storage availability, active camera, audio or a playable game. Exact build,
instruction-comparison and supplied-data results belong in
[VALIDATION.md](VALIDATION.md). Original evidence uses the supplied USA DOL
with SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.

The final production-core comparison covers 9,656 cases and 858,656 original
instructions across 174 addresses, with zero mismatches. It includes 4,452
reset exits and 5,204 actual timing-conversion/storage-helper executions on
the null-stage path. Platform SDK endpoints are explicit prepared fixtures;
nontrivial stage and storage-error paths have separate bounded comparisons.
The permanent system suite contains 109 checks, and the composed local build
passes all 129 CTest suites. These figures qualify only those stated paths.
