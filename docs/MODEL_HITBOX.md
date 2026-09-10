# Original MODELCTRL HITBOX lifecycle

`ModelSubcommandQueue` owns ordinary hitbox entries in the same ordered list as
sounds and effects. It preserves the original delayed start, repeated checks,
absolute expiry and exact-type AllClear behavior. Each actual check requires an
explicit service that owns geometry, debug drawing when applicable, collision
delivery and the conditional source script. Queue execution alone does not
implement target collision response, combat or a complete player runtime.

## Command and ownership

The original `beModelCtrlHITBOX` record has a nullable inherited name in slot2,
signed32 kind in slot3, float start/length in slots4/5, and offset/boxSize
float3 vectors in slots6/7. Integer/float fields occupy four serialized bytes;
vectors occupy twelve. The string has a length and padded text, not a reference.

The handler recognizes exact names `Set` and `AllClear`. Set appends one
`beModelCtrlSCHitBox`; AllClear removes only that exact class. Null names become
empty and unknown names complete without changing the queue. HITBOX itself does
not install a sequence wait. The queue API receives the already selected Set
payload as `ModelHitboxSubcommand`, with start named `delay`.

Each entry has a queue-local stable ID, elapsed frames, start threshold and a
published started flag. Its hitbox state owns the authored command, derived
bullet mode and absolute end frame. It is separate from the model's target
`_hitBody` and does not create a permanent collision object.

Classification uses signed integer division of kind by 10000, truncating toward
zero:

| Quotient | Bullet mode |
| --- | --- |
| 0 | 0 when kind < 1000; otherwise 2 |
| 6 | 1 |
| 7 or 8 | 2 |
| Any other value | 0 |

The later check selects HITPLAYERBULLET for mode0, HITPLAYERTALK for mode1, and
HITENEMYBULLET for mode2. The actual first Agumon SQ_ATCK1 Set has kind60000,
start1, length1, offset(0,4,7) and boxSize(3,3,3), before ACTION TALKWF. It
therefore schedules a talk-category check. Original data inspection found
352 HITBOX occurrences, comprising 344 Set and 8 AllClear occurrences, using
236 distinct records in that model's SEQ14 graph. These are authored occurrences,
including repeated references and labeled branches, not execution counts.

## Timing, clearing and suspension

Append initializes elapsed to zero and started to false. Length equal to exactly
-1.0f selects the finite absolute end value 100000000.0f. Other lengths use the
original scalar float sum of start and length. The native implementation follows
the startup NI=1/RN=00 profile: tiny inputs participate in the addition and tiny
results flush to signed zero. Finite input validation and overflow rejection
happen before insertion. Negative delays, lengths and extents are not clamped.

Update consumes the uncapped original system frame delta; it has no host clock.
It snapshots the list count and advances each original entry once in list order.
An unstarted entry publishes started before invoking its first check when
elapsed reaches delay. This first check occurs even if elapsed has already
passed the end frame. On subsequent passes, elapsed >= end removes the entry
without checking; otherwise the entry checks again. Zero and negative deltas
retain these comparisons. Callback appends wait until the next pass.

AllClear immediately releases matching hitbox entries and preserves the order,
state and cleanup ownership of every sound/effect entry. If a native pass is
suspended, removal adjusts its retained cursor and original remaining count.
Sequence cleanup removes hitboxes through their empty cleanup virtual; the
existing exception for effects that survive sequence changes is preserved.
There is no invented collision erase callback.

`ModelSubcommandServices::check_hitbox` accepts an owned request value containing
the entry ID, command, bullet mode and captured subcommand context. The callback
returns true only after its complete original check has finished. False retains
the same entry phase and frame delta. Unlike a simple unavailable manager call,
this callback may retain a completed internal prefix: its owner must resume the
same query or script instead of replaying committed effects. The queue never
adds elapsed time again on resume.

Once an invoked check returns false, both AllClear and sequence cleanup reject
until that check finishes. This preserves the original synchronous-call order;
there is no guessed cancellation of an outstanding collision or script effect.
A missing callback that has never been invoked may be cleared or cleaned up.
Recursive update, resume, clearing, cleanup and moving the executing queue are
rejected. Appending during an update callback remains supported. Any uncaught
service exception faults the queue and forbids replay or guessed cleanup.
Moving a suspended queue transfers its phase and entry ownership.

The context supplied by the queue is captured by value for the update pass.
The check owner must separately obtain and retain the appropriate live model
frame and script state, and qualify queue-local IDs by their actual source owner.
Request references are call-scoped; an asynchronous continuation retains its own
copy. Changing providers does not authorize discarding a pending check.

## Original service boundary

The complete start routine builds a temporary world AABox from the current
source node matrix and authored offset, then applies boxSize as full world-axis
extents. It optionally draws the debug box before sending the category-wide
CHKBULLET message. That message supplies kind, the owned AABox, source matrix,
attachment-or-source name and RAM, and enabled-only selection. It uses a null
selection name and index -1. A temporary AABox is not a fabricated RAM identity.

After delivery, the routine tests the returned boolean-list count, including a
nonempty list containing false. With a nonnull live source Lua BHit name, it
executes that source's BHit script before releasing the temporary box. The
collision and script prefixes require independent retained ownership inside the
check service. The lifecycle oracle below deliberately treats this entire
routine as one explicit boundary; it does not prove its geometry or consumers.

## Prepared runtime integration

`ActionEventRuntime` supplies the queue's check callback for its prepared model
owner. It captures a temporary owned AABox from the current model matrix, then
retains separate debug, category selection, collision delivery and BHit script
phases. A retry resumes that phase without rebuilding earlier geometry or
repeating a completed delivery. The messenger transports the temporary box as
its own object alternative, separate from null and source-qualified RAM objects.

The original matrix and attachment packet fields are read after debug display.
This matters when an actual debug callback changes live model work: the box
retains its earlier geometry while the later packet receives the updated matrix.
After collision delivery, the callback name is read from live model work; a
pending BHit script then owns its selected request. The source model identity
remains independent of an attachment used by the collision packet.

The debug service receives the retained request, including bullet mode, and the
world box. Original debug colors are mode0 RGBA(0,0,1,0.5), mode1(1,1,0,0.5) and
mode2(1,0,0,0.5), initialized at 80310100..8031013C and selected at 80310140.
The prepared debug flag represents the already established original controller
and debug-node gates; it does not construct that debug node.

The prepared runtime requires a represented collision category before sending.
A registered inactive recipient can produce an actual empty result; an absent
category is an explicit missing-scene boundary. Target activity and manager
callbacks must come from their real owners. The external Lua service must
establish the original shared model/script context and execute the requested
script. Missing recipients, managers, debug display or Lua execution do not
become successful checks. Full target body lookup, target hit scripts, damage
and collision response remain distinct downstream work.

## Provenance and validation

Evidence comes from the supplied USA revision-0 DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, and separately inspected local
Agumon records. No game data or generated disassembly is distributed.

| Original routine | Implemented boundary |
| --- | --- |
| 802CAC8C / 802CAD20 | HITBOX descriptor and serialized field metadata |
| 8030C134 | Set/AllClear name dispatch |
| 80310DAC | SCHitBox append, copied payload, mode and end frame |
| 80310754 / 803107E0 | Scalar elapsed update and ordered list pass |
| 803102F4 | Started hitbox expiry and repeated check |
| 80310A68 | Exact-class immediate clear |
| 803108A0 / 802E93B4 | Sequence cleanup and empty hitbox cleanup |
| 8030FEEC | Complete check delegated to the service |

The public asset-free HITBOX suite currently passes 65 checks; the existing
subcommand suite passes 67. Checks cover signed classification boundaries,
fractional and negative timing, the finite sentinel, tiny sums, overflow,
overshoot, selective clearing, mixed pending cursors, owned request payloads,
service suspension, moves, reentrancy, append snapshots and fault handling.

The ignored raw-instruction comparison matched 2,106 cases against the final
Release test executable linked to the production core. It executed
1,690,336 instructions at 577 distinct addresses. Original instructions determine
append arithmetic/classification, update order, virtual dispatch, expiry,
AllClear and sequence cleanup. Explicit hooks supply factory allocation, list
helpers, valid type4 sound/effect managers and the complete 8030FEEC check.
The scalar sum oracle uses exact integer binary32 rounding. This is a bounded
interpreter comparison, not physical-console or complete-collision validation.

Verified executable: `build/Release/dw4_model_hitbox_tests.exe`, SHA-256
`bec254bfc63c27594254733477e655b7e7d063ffef567c6694fe4ab7d5205ca2`.
The final linked HITBOX and existing subcommand suites also passed the 65 and
67 checks reported above. The local rerun command is:

```powershell
& 'C:/Users/shinz/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' work/v26-hitbox-oracle.py --native build/Release/dw4_model_hitbox_tests.exe
```

The ignored JSON report records the executable hash, complete cases, expected
snapshots and effect traces. Original disassembly and source-data probes remain
under ignored `work/`.
