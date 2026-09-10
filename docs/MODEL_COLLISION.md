# Prepared model collision targets

Version 0.27 connects the original base CHKBULLET target path to explicitly
prepared MODELCTRL work. Each target owns its authored HIT body cache and one
synchronous delivery continuation. Source HITBOX queries can reach checked
overlap, nested record lookup and the required target hit-script service.
Original damage, player construction, enemies and the complete game loop remain
required before combat works.

## Ownership and registration

`decode_model_hit_body` reads the exact exported MODELCTRL's first named HIT
record and ordered AABoxes. `prepare_collision_target` checks its source path,
loaded occurrence, model info/name and selected RAM against the existing
`PlayerRuntimeBinding`. It installs once. Deliveries must identify the selected
serialized RAM reference or its proven canonical reference in that same loaded
occurrence. Callbacks receive the actual accepted endpoint identity. Another
endpoint cannot acquire a second mutable cache for the same physical RAM.

`register_model` already registers MODELCTRL. Target preparation adds nonzero
authored categories in original order: HITPLAYERBULLET, HITPLAYERTALK, then
HITENEMYBULLET. Each uses the actual info name and prepared RAM number. Raw HIT
bytes use nonzero tests, including values other than one. Preparation does not
execute registration tail virtual `+9C`. Original Agumon flags are 0/1/0 for
PlayerBullet/EnemyBullet/PlayerTalk, so its four RAMs register only HITENEMYBULLET
here. They do not satisfy the first Set129's separate HITPLAYERTALK prerequisite.

The caller explicitly prepares the base path after controller virtual `+C4`
declines the command. Original PC hook `803AEBB4` searches sixteen status names;
CHKBULLET is absent, so it returns zero after resolving existing ONE work and
before later status-list allocation. This establishes that prepared PC path;
it does not implement full PC initialization or other controller hooks.

## Gates and body calculation

The base handler reads original state in this order:

1. Nonzero invulnerability returns handled without a result, except when both
   target and source names start with lowercase `p` and the target has its actual
   HP reference. Both signed zeros pass the ordinary gate.
2. A zero work `+44` control byte returns handled. Enabled targets require their
   actual current sequence; exact SQ_ERAS returns handled. SQ_ERAS2 does not.
   The separate messenger enabled gate is evaluated by message delivery.
3. The target obtains the incoming owned AABox and original system timestamp,
   then updates its body cache from its current ONE matrix. The first inclusive
   overlap supplies the body ordinal. No overlap appends no boolean and needs
   no target Lua or source record.
4. Only an overlapping target snapshots kind, source name/RAM, source matrix
   translation and target translation. Scalar copies preserve signed zero and
   subnormal words. Later Lua changes do not replace these positions.

The cache uses the exact 64-bit system timestamp, not an ACTION clock or frame
delta. A changed matrix at the same timestamp does not refresh it. First use at
time zero allocates original inverted factory bounds and skips transformation.
A changed timestamp computes the general box transform, its center and original
separate distances to local minimum/maximum. See [MODEL_HIT_BODY.md](MODEL_HIT_BODY.md)
and [COLLISION_MATH.md](COLLISION_MATH.md). Finite validation is an explicit
checked host boundary.

## Nested hit response and continuation

An overlap pushes the actual messenger buffer object. Leading-`p` sources use
record -1; other sources send MODELCTRL/name/RAM GETRECNO in the child buffer.
Exactly one integer supplies the record. Any other count uses -9, and an actual
sole value -9 has the same effect: skip target Lua and heading.

`ModelTargetHitRequest` preserves target identity, `hit` prefix, `Hit` event,
kind, first body ordinal and source name/RAM/record. `hit_script` must own the
complete original `8030B6EC` operation, including HP/max-HP/record wrappers,
global context preparation, actual Lua execution and conditional HP publication.
It returns the live context `+30` byte on completion. Missing service or nullopt
retains that operation; the service owns already completed internal effects.
There is no default damage or fabricated successful Lua result.

A nonzero completed direction byte forces Y heading toward the captured source
position using `atan2(sourceX-targetX, sourceZ-targetZ)`. Translation differences
use original single-operation boundaries. Force applies to the live post-Lua
pose, preserves its translation and does not create a motion timer. As with
existing model turning, native libm is used; exact original atan2/trigonometric
bit parity is not established.

The handler pops the child and restores its parked parent buffer object. It
appends true only if the restored boolean list is empty. An existing false
suppresses the append. The source BHit path checks result count, so that false
can still cause its configured source script to run. Pop restores buffer values
and owners, not previous message selection/cache state. See [MESSENGER.md](MESSENGER.md).

Missing time, record delivery and target Lua have distinct stop reasons. Retries
preserve completed gates, overlap, arguments, nested recipients and script work.
Target callbacks can replace live model state or retain their own nested
sequence/Lua operation; that operation must finish before publishing the hit
result. Public mutable sequence access stays guarded during such a suspension,
while the collision owner retains its stable model reference. External work
replacement remains blocked during a suspended send. Exceptions fault the target
and retain uncertain child work for inspection; there is no automatic pop or
replay. Recursive target delivery and mismatched child-buffer ownership reject.

## Evidence and validation scope

Source: USA revision-0 main.dol, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.

| Operation | Address |
| --- | --- |
| Base CHKBULLET dispatch and gates | `8030E56C` within `8030D910` |
| Body factory / timestamp update | `802C803C` / `80307EAC` |
| General AABox transform / overlap comparison | `80122D04` / `8012305C` |
| Scalar translation copy | `80128DAC` |
| Buffer push / pop | `80306604` / `803067A0` |
| Target Lua/context helper | `8030B6EC` |
| Captured-position heading / forced turn | `8030E81C..8030E850` / `8030C85C` |
| HIT registration / name and RAM helper | `803083CC` / `80305308` |
| PC status command prehook | `803AEBB4` |

Synthetic suites cover ordered gates, zero-time defaults, timestamp reuse,
first/equality overlap, record counts, missing services, nested buffer retention,
source/target ordering, live pose changes and uncertain effects. Optional
original-data integration covers all four Agumon body/RAM owners. Independent
bounded instruction comparisons cover geometric primitives and the messenger
stack; they do not establish full target-script execution, physical-console
parity or complete combat. Results are recorded in [VALIDATION.md](VALIDATION.md).
