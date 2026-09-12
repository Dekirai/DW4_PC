# Original model sequence command runner

Version 0.17 adds retained native sequence state on explicitly prepared model
work. It reads the original SEQ graph, selects weapon labels, runs supported
control/motion commands and exposes ACTION execution as a required service.
Version 0.41 adds the actual graph profile below and connects ordinary entry,
native ACTION dispatch and player `+B8` through
[MODEL_SEQUENCE_ENTRY.md](MODEL_SEQUENCE_ENTRY.md). The outer original frame
remains incomplete. The walking preview uses its provisional host controller.

## Actual graph invocation profile

The actual `ModelSequenceRuntime` constructor binds canonical work fields and
the real RAM timer endpoint without a prepared program or a node-matrix read.
`ModelSequenceDataOwner` is one discriminated work+18 reference: either an
immutable prepared record or the exact mutable `ModelSceneSequence` owner.
`actual_owner()` never substitutes a copied command vector. Existing prepared
construction, selection validation and `run` remain a separate checked profile.

Each ordinary invocation retains a `ModelSequenceActualFrame`. Selection lookup
belongs to the outer controller: it reads the captured parent's current data,
first named SEQ record, current named sequence list and requested sequence.
`begin_actual_select` then publishes the nullable name and nullable selected data
before resolving labels. A failure at the later label read preserves those
stores. Missing label offsets remain -1 until the original cursor consumer;
missing data with no selected label reaches the later command-list read.
Mode/phase, PDMOVE count and wait-name resets precede external subcommand cleanup.
`finish_actual_select` clears hit scripts, enables wall/floor/fall checks,
clamps only ordered-negative invulnerability and stops movement entries in order.
Earlier fields and channel stops survive a later invalid entry. Movement cleanup
captures the initial count and reloads the actual list for each consumed entry.

`run_actual` captures work.command_data and the local cursor at command-loop
entry. Each subsequent command reads that retained data owner's current list
and backing. The original count test is equality with the local cursor, not a
logical-prefix range check; an unequal cursor requires an addressable physical
slot. The reached command and request identity are retained while an external
service is suspended. A callback may replace work.command_data or command_index;
those stores do not retarget the running loop. A real end, LABEL boundary or
WCHECK return publishes the local cursor; a native service suspension does not.
`frame.completed()` distinguishes those cases for the outer +B8 caller.

Model commands retain the actual reached `ModelSceneCommand`. Their raw control
overload reads name and parameters at the original consuming sites after timer,
unlock and heading prefixes. In particular, malformed MOVEEX data does not
prevent earlier original timer/channel allocation, and real clock callbacks can
change command parameters before those later reads.

JUMP uses a resumable direct controller callback. A child invocation may use the
same executor with a separate frame, and completed JUMP suppresses old-cursor
writeback. The current checked boundary permits same-work nested ordinary entry
through this verified JUMP path only; ACTION/Lua-triggered reentry is not yet
implemented. A caught invalid reentry faults its frame instead of allowing the
outer callback to consume the command. Callback exceptions likewise retain the
completed prefix and permanently fault the invocation.

`make_actual_resume_frame()` captures a later C3AC invocation without repeating
ordinary-entry resets, for the original wait-resume caller. Frames retain their
selected data and executor lease, but no strong back-reference to the runtime
or work. Canonical fields retain only a weak executor lease. Abandoning unfinished
work prevents replay by a replacement executor; retaining a completed frame
does not prevent a later healthy binding. The outer owner must retain cleanup,
wait, messenger, Lua and virtual-callback continuations and must complete +B8
separately. These helpers do not mark those services complete.

The asset-free actual suite checks nullable publication, label failure prefixes,
NaN invulnerability, partial movement cleanup, fields-only CTRL, physical backing
reads, changed work/data/list owners during suspension, ACTION/HITBOX/Lua request
retention, nested JUMP, cursor suppression, exception/reentry faults and owner
lifetime. Original local disassembly remains ignored; no game data is included.

## Source and serialized graph

Version 0.40 permits an actual named RAM timer endpoint on the canonical work
binding. `select`, `begin_select` and cleanup use `snapshot_work()` and
`publish_work()`, preserving control/node/list publication without copying,
reading or writing a timer. Reached MOVE/MUTEKI commands use their own live
RAM timer helper calls. Full runtime copying remains a prepared operation and
rejects an actual timer binding. Version 0.41 adds actual manager/context
resolution and the retained per-work execution bundle described above.
See [MODEL_TIMER.md](MODEL_TIMER.md) and [MODEL_CONTROL.md](MODEL_CONTROL.md).

Evidence uses the owner's USA `GDJEB2` revision-0 DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, and checked original IGB objects.
Function boundaries and SDK names use [dw4-gc](https://github.com/ivanno4317/dw4-gc)
commit `794294e17fef0b3ab5eeb15fda9bd57b0dc98a2b`. Native code is independently
written from instructions and metadata; original dumps/probes are not published.

| Address | Evidence |
| --- | --- |
| `802C6C7C`, names/offsets `804D0834 / 804D09C4` | Work sequence name `14`, data `18`, cursor `1C`, LABEL count `20`, label sequence/name/offset lists `24/28/2C` |
| `8030B014` | Set or replace a sequence's selected label and invalidate its cached offset |
| `80054094 / 8004270C` | Intern strings and find the first matching pooled-string pointer |
| `8030D084` | Resolve the selected LABEL ordinal, using the retained cache |
| `8030C3AC` | Ordered command loop, handler stops, LABEL barrier and nested JUMP cursor preservation |
| `802CC600 / 802CC3DC` | Register exact LABEL / LABELCTRL types at globals `80534F30 / 80534F28` |
| `8030E944..8030EAA0` | Ordinary sequence-entry path inside base dispatch `8030D910` |
| `8030EA1C..8030EA74` | XYZ sequence-stop handling and unconditional rotation deactivation |
| `8030943C / 8030BCB8` | ACTION messenger request / JUMP through the outer controller |
| `802CCD64`, names/values `804D1084 / 804D1108` | 33 native command names handled before ordinary named sequences; exact comparison at `800633C8` |

`decode_model_sequences` requires an explicitly supplied exact
`beModelCtrlInfoDataSeq` named `SEQ` and a nonempty source identity. Its inherited
integer is field 3; its named list is field 4. Each exact `beModelCtrlData` has
name field 2 and a command-list reference at field 3. Ordered list occurrences,
aliases and duplicate names are preserved; lookup uses the first match.

Supported command payloads are MOVE/MOVEEX/CTRL/PDMOVE/MUTEKI from the existing decoders,
LABEL, LABELCTRL, ACTION, JUMP, exact WCHECK, HITBOX and LUACALL. LABELCTRL adds a sequence-key string at field 3.
Unknown command types retain their type and source reference without interpreting
their payload. They stop execution when reached. Successful corpus decoding does
not mean that unsupported commands execute.

Only the verified little-endian profile is accepted. Limits are 4,096 sequences,
65,536 combined command occurrences, 8 MiB of copied/serialized text, 4,096 bytes
per name, 4,104 serialized string bytes and 256 bytes per type name. List capacity,
count, field widths, references, null entries and accumulated alias work are
checked before publishing a program. Original bytes remain unchanged.

## Owned state and selection

Version 0.26 decodes exact HITBOX fields 2..7: name, signed kind, start, length,
offset and box size. Set and AllClear require the real ordered queue service;
false retains the command cursor. Other names are original successful no-ops.
Every typed field is still validated, including unused AllClear payloads. The
command does not itself wait. See [MODEL_HITBOX.md](MODEL_HITBOX.md).

LUACALL at `8030B8A4` uses nullable prefix/mode fields 2/3. Exact BHit, WHit and
FHit assign their corresponding owned callback prefix, converting null prefix
to non-null empty. Other modes, including null, invoke the required Lua/context
service with source info name, RAM and unchanged nullable event. No service
means no command consumption. Once an invoked service returns false, the live
command remains pending and sequence/mutable-work replacement is forbidden
until its retry completes. A throwing Lua service faults that sequence owner;
uncertain effects cannot be replayed by selecting or running again.

After subcommand cleanup, ordinary entry clears all three prefixes, enables
wall/floor/fall checks and clamps only strictly negative invulnerability to +0,
then applies existing channel stops (`8030E9DC..8030EA74`). Missing complete
player entry/status callbacks remain separate from these owned field effects.

An immutable `ModelSequenceProgram` owns the source label and decoded records.
The original IGB document can be destroyed without invalidating the program.
Version 0.38 binds the runner to shared `ModelSequenceFields`, which contain the
actual nullable sequence name (`+14`), command-data reference (`+18`), signed
command index and LABEL count (`+1C/+20`), label fields and wait/player state.
There is no separate authoritative runtime index, cursor, label count or wait
copy. Inspection reads the canonical fields, including negative signed values;
null and empty names remain distinct even though `current_sequence()` exposes
both as an empty string view.

The retaining constructor accepts a shared immutable program, prepared model
view, info name, RAM number and shared sequence fields. A complete work owner
can embed the sequence fields and supply an aliasing shared pointer to them.
The host label-field group is a separate owned allocation with three initially
null list references; it never points back to the complete work. Existing
value-program and label-only constructors retain their prepared APIs.

Selection stores an aliasing `shared_ptr<const ModelSequenceData>` into the
actual immutable program allocation. This retains both the data record and its
source provenance after the facade or original IGB document is destroyed.
The runner checks that its live command-data pointer and shared ownership both
belong to that exact program. An equal-looking copied record or another program
with the same source text/reference numbers is not accepted as the owner. The
nullable sequence name is a separate original field and is not regenerated from
the selected data name. Negative or out-of-range command indices stop before an
unsafe command read; the exact end index completes. Cursor increments retain
original 32-bit wrapping if a callback changes the live signed value, followed
by the next checked access. A nonzero signed LABEL count keeps its boundary.

Prepared runtime copies share immutable program/data owners and detach mutable
sequence, label, wait and model values. Assignment publishes into the existing
destination field/list allocations, keeping the wait object's address stable.
Publication cannot abandon an executing, pending or faulted destination facade.
Move construction transfers that same binding and rejects a currently executing
facade. Each field generation has one executor, enforced by a private weak lease
in host metadata and a strong lease held only by its facade. A second binding
is rejected. The fields cannot themselves be copied or moved; prepared runtime
copies explicitly detach guest values. Copying an executing, pending or faulted
facade creates an inspection-only snapshot whose execution/mutation is blocked.
Only moving the original facade transfers its resumable continuation.

Pending ACTION, HITBOX, Lua and jump-cleanup state belongs to that executor,
outside canonical guest fields. The pending marker starts only when a callback
is invoked; a never-supplied service creates no external prefix. False results
and ACTION/HITBOX exceptions retain their existing retry boundary. Destroying
an executing, pending-selection, pending-external or faulted executor marks its
field generation unavailable; expired weak ownership cannot then revive a
possibly completed external prefix. No external cleanup runs on destruction.
A completed healthy executor may release its lease and permit later binding;
an ordinary wait name without a pending external pass does not poison it.
A caller must retain the generation/facade together with its separate wait
resolver while that resolver has an external pass in progress.
Replacing ONE's work reference leaves the retained old generation intact. Its
`ModelWaitRuntime` can therefore keep the same raw state pointer through work
replacement and facade move, while a fresh generation has independent wait and
command fields. Neither field group caches a strong pointer back to its facade.

Version 0.37 stores labels in the actual three nullable work references:
`ModelSequenceLabelFields::sequences`, `names` and `offsets`. Their ordered list
contents are authoritative. `labels()` returns detached inspection values, not
a competing mutable zipped cache. A constructor can bind the exact supplied
field owner, including an alias into a future canonical player-work allocation.
The first SETLABEL allocates the sequence list, name list and offset list in
the original order (`8030B0F4`, `8030B19C`, `8030B248`); later updates preserve
those list identities. Host staging precedes publication; guest allocation
failure, allocator callbacks and exact backing-capacity growth are not modeled.

Copies detach list values while preserving null, partial shapes and aliases.
Assignments publish through existing destination lists when their shape permits.
Move construction transfers the original binding. Partial/unequal list counts
and aliased string-list owners are explicit checked boundaries at consumption;
they are not silently repaired. Allocated-empty lists remain distinct from null.
External retained list owners outlive the sequence facade without a back-reference.

`set_label` replaces the first sequence-key entry or appends it, retaining list
order. Every assignment invalidates the cached offset, even if its text matches
the previous value. Null and empty keys are distinct, matching the original
interned-string distinction. Serialized zero-length LABELCTRL scopes retain the
null form; the original corpus contains no null/empty LABELCTRL scope examples.

`select` implements the owned part of ordinary local sequence selection. Names
do not require an `SQ_` prefix. The original 33 native command names and two
special erase sequences retain priority even if same-named sequence data exists.
They require outer controller execution. Matching remains case-sensitive. Missing
label configuration starts at command zero. An uncached configuration scans
for the first exact LABEL with the selected name, then stores that ordinal.
An absent label produces original offset -1; the native boundary rejects it
instead of indexing before the list or silently choosing another section.

Selection resets LABEL count, CTRL mode/phase, PDMOVE parameter count and the
owned fall-check flag. Flagged XYZ channels run their original stored stop
parameters; unflagged XYZ channels are unchanged. Rotation is always deactivated
and its speed cleared, preserving its other fields. Existing timers and position
lock remain unchanged. Invalid selection leaves the owned state untouched.

Version 0.24.1 exposes the original visible entry phases for a prepared cleanup
owner. `begin_select` validates selection and stop parameters, publishes the
new name/data/cursor and label cache, then resets LABEL/CTRL/PDMOVE fields.
It preserves channels and the fall flag while cleanup runs. `finish_select`
applies those post-cleanup fall/channel effects and permits command execution.
Mutable model access and unrelated selection/label changes reject while pending.
`select` stages a detached selection plan and model values for both phases before
publication. It no longer copies the whole runtime to select a sequence. Cached
offset writes mutate the retained offset list, and model assignment publishes
through its existing node, PDMOVE and movement-entry owners. `begin_select`
clears only PDMOVE's logical count, retaining its actual list and backing floats.
Local JUMP with an owner cleanup hook uses the phased path, retaining original
jump metadata across a suspended callback while the destination is already
visible. A retry without that required hook stays suspended. Version 0.26 also
applies the post-cleanup callback-prefix, wall/floor and invulnerability resets
described above. Player virtual entry/status callbacks remain separate. See
[ACTION_EVENT_RUNTIME.md](ACTION_EVENT_RUNTIME.md).

The optional pure `run` cleanup preflight validates the actual cleanup owner
immediately before a visited local JUMP publishes its destination. It therefore
sees reference-slot changes made by earlier ACTION commands in that same run.
A rejection leaves the cursor at JUMP and preserves the completed command prefix,
without publishing a pending selection that would prevent the old update from
resuming. Unvisited/nonlocal jumps and retries of an already-entered cleanup do
not call this validation.

This is **not complete sequence entry**. The sequence owner requires its caller
to perform original subcommand cleanup; the integrated event runtime supplies
that ordered queue operation. Complete player preparation and controller virtual
entry/status callbacks remain required and are not acknowledged by this API.
`SQ_ERAS` and `SQ_ERAS2` have different original lifecycle paths and are excluded
from ordinary selection.

## Command execution and required services

Version 0.25 decodes exact `beModelCtrlWCHECK` inherited name field 2. Serialized
null becomes an owned empty string. Execution assigns the wait name, consumes
the command and returns `waiting` without a condition query. Subsequent `run`
calls retain the cursor while any wait name is engaged, including empty. The
separate [model wait runtime](MODEL_WAIT.md) clears it before resuming commands.
Ordinary `begin_select` clears only the name before cleanup; control and attack
state/mode fields survive selection and WCHECK assignment.

`run` resumes the retained command cursor. Supported model commands apply to
the same owned `ModelControlState` used by turning and motion. The first LABEL
sets the retained LABEL count to one and is consumed. A subsequent LABEL stops
before consumption. Calling `run` again does not reset this section boundary.

LABELCTRL updates the selected label for future selection, without moving the
current cursor. An ordinary local JUMP selects its destination and continues
there; it never overwrites the new state with the previous sequence's cursor.
Erase and nonlocal/controller targets return an explicit controller requirement.
The runner is bounded to 65,536 visited commands per call, including jumps.

ACTION uses an explicit `ModelActionDispatch` service receiving source identity,
command reference, info name, RAM number and action name. The original handler
sends category `ACTION` through the messenger; it does not directly play a clip.
Without a dispatcher, the runner returns `action_required` before consuming
the action. A supplied dispatcher must execute the required effect or throw.
An exception leaves that action unconsumed; already completed prefix effects
remain. A recording callback is useful for testing the boundary but is not
animation playback. Reentrant sequence mutation/execution during the external
callback fails explicitly.

The optional fourth `run` argument, `ModelActionTryDispatch`, takes precedence
over the void callback. False returns `action_required` at the same unconsumed
ACTION. Its owner must retain any partially completed ACTION/messenger work.
`ActionEventRuntime::run_action_sequence` supplies that continuation for a
registered live ACTION runtime and preserves caller-owned shared random state.

Unknown command types similarly stop at the exact unconsumed occurrence.
Neither an unsupported action nor a work-budget failure rolls back already
executed external effects or earlier commands. This is an ordered runtime prefix,
not a transaction over the whole game. It is also not a frame scheduler:
the separate original wait kernel (`8030BDB8`, player-specific `8034D214`) and
required outer update callbacks decide when execution resumes.

## Original Agumon action boundary

Model 1386 points through SEQ 14 to named list 6636 with 28 sequences. WAIT data
1534/list 80 and WALK data 169/list 123 each have sixteen commands, grouped into
four sections: LABEL, CTRL, MOVE, ACTION. Labels dual/slash/arm/gun begin at
ordinals 0/4/8/12. These original lists now drive the supported native command
effects; command references are not manually selected by the integration test.

The separate `beAction2Info` 546 uses database 810 and data list 5550 with 183
actions. The following serialized records also drive the prepared ACTION2
runtime introduced in version 0.20; see [ACTIONS.md](ACTIONS.md):

| Weapon | WAIT action / motion command | RUN action / ordered motion commands |
| --- | --- | --- |
| dual | WAITD1: data 5487 -> list 4412 -> 12 (`waitD`) | RUNND1: data 4411 -> list 3075 -> 12, 3702 (`waitD`, `runD`) |
| slash | WAITS1: data 2240 -> list 3044 -> 1242 (`waitS`) | RUNNS1: data 2759 -> list 3076 -> 1242, 726 (`waitS`, `runS`) |
| arm | WAITA1: data 3043 -> list 2758 -> 3701 (`waitA`) | RUNNA1: data 1998 -> list 3458 -> 3701, 496 (`waitA`, `runA`) |
| gun | WAITG1: data 1300 -> list 2760 -> 724 (`waitG`) | RUNNG1: data 2886 -> list 780 -> 724, 497 (`waitG`, `runG`) |

These `beAction2MOTIONSET` records have strings `RR`, `LP` and float value 8.
Their command lists, blending, timers and owned action states now execute in the
portable core. Complete actor ownership and broadcast consumers remain required
before replacing the preview's direct clip selection.

## Validation

- **440 asset-free checks** exercise ordered ownership, inherited SEQ fields,
  label caching, weapon barriers, nested jumps, explicit unsupported/action
  boundaries, WCHECK, HITBOX/MUTEKI fields, Lua suspension/fault ownership,
  failed/reentrant callbacks, sequence-stop fields and input budgets. Canonical
  field tests include actual data/source ownership, live signed fields, detached
  publication, pending assignment rejection and retained wait continuation
  through work replacement and facade move. Executor-lease checks reject second
  bindings and copied/abandoned ACTION, HITBOX, Lua and cleanup continuations,
  while allowing completed healthy generations to bind again.
- **884 additional original-data checks** cover all 259 WCHECK, 352 HITBOX and
  62 MUTEKI occurrences, both groups/all four entry matrices and all four Agumon
  weapon sections, for **1,324 checks** with assets. Original WAIT/WALK lists request the
  matching actions and retain approximately 8.275 local travel over 18 Move and
  six Stop updates at explicitly cached delta 1. A supplied recording service
  validates ACTION requests, not animation playback or full player construction.
- **9,060 SEQ programs / 80,621 sequence occurrences / 447,274 command
  occurrences** decode across 2,273 archive IGBs with zero rejections. Counts
  include aliases and commands whose execution remains unsupported.
- **6,000 local instruction comparisons** of the command runner have zero
  mismatches. Synthetic list/string/channel allocation and ACTION dispatch are
  supplied boundaries. Finite float state is compared by bits; NaN sign/payload
  is excluded as in the existing motion profile.
- **3,000 label programs / 24,539 update-and-lookup pairs** match the original
  label/cache routines, including missing-label results rejected by the native
  boundary. These probes use preallocated lists and an interned-string service.

```powershell
.\build\Release\dw4_model_sequence_tests.exe 'E:\path\Digimon World 4 - Extracted'
```

These comparisons establish the stated state/data boundaries. They do not
establish full sequence-entry callbacks, the ACTION manager, original gameplay
timing, collision, camera, combat or connected multiplayer. Continue through
those effects toward [ORIGINAL_BASELINE.md](ORIGINAL_BASELINE.md).
