# Original model sequence command runner

Version 0.17 adds retained native sequence state on explicitly prepared model
work. It reads the original SEQ graph, selects weapon labels, runs supported
control/motion commands and exposes ACTION execution as a required service.
The outer player/controller lifecycle and complete animation actions are still
missing. The walking preview continues to use its provisional host controller.

## Source and serialized graph

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

Supported command payloads are MOVE/MOVEEX/CTRL/PDMOVE from the existing decoders,
LABEL, LABELCTRL, ACTION and JUMP. LABELCTRL adds a sequence-key string at field 3.
Unknown command types retain their type and source reference without interpreting
their payload. They stop execution when reached. Successful corpus decoding does
not mean that unsupported commands execute.

Only the verified little-endian profile is accepted. Limits are 4,096 sequences,
65,536 combined command occurrences, 8 MiB of copied/serialized text, 4,096 bytes
per name, 4,104 serialized string bytes and 256 bytes per type name. List capacity,
count, field widths, references, null entries and accumulated alias work are
checked before publishing a program. Original bytes remain unchanged.

## Owned state and selection

An immutable `ModelSequenceProgram` owns the source label and decoded records.
Runtime copies share that immutable program and independently own model control,
label selections/cache, current sequence, command cursor and LABEL count.
The original IGB document can be destroyed without invalidating the program.

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

This is **not complete sequence entry**. The original also resets subcommands,
wait/callback strings, wall/floor flags and another timer field, and invokes
controller virtual callbacks. The caller must implement those effects and the
surrounding player preparation; this API does not acknowledge them as executed.
`SQ_ERAS` and `SQ_ERAS2` have different original lifecycle paths and are excluded
from ordinary selection.

## Command execution and required services

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

Unknown command types similarly stop at the exact unconsumed occurrence.
Neither an unsupported action nor a work-budget failure rolls back already
executed external effects or earlier commands. This is an ordered runtime prefix,
not a transaction over the whole game. It is also not a frame scheduler:
original wait conditions (`8030BDB8`, player-specific `8034D214`) and outer update
callbacks still decide when execution resumes.

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

- **65 asset-free checks** exercise ordered ownership, inherited SEQ fields,
  label caching, weapon barriers, nested jumps, explicit unsupported/action
  boundaries, failed/reentrant callbacks, sequence-stop fields and input budgets.
- **193 additional original-data checks** use both groups/all four entry matrices
  and all four Agumon weapon sections. Original WAIT/WALK lists request the
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
