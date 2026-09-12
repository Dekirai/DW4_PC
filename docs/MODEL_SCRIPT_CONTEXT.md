# Shared MODELCTRL script context

The `model_script_context` module implements the prepared context and HP merge
around original helper `8030B6EC`. It retains real work and matrix owners while
an explicitly supplied script service runs. It does not construct GAMERAM,
discover a script registry, execute missing Lua methods or choose starting HP.
The caller remains responsible for the complete script operation and its effects.

## Original fields and object ownership

Evidence comes from the owner's USA GDJEB2 revision-0 DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Function boundaries use
[dw4-gc](https://github.com/ivanno4317/dw4-gc) commit
`794294e17fef0b3ab5eeb15fda9bd57b0dc98a2b`; behavior follows the original
instructions and registered metadata. Original bytes remain local.

`ModelScriptInt` aliases `GameDataInt`, representing `beDataObjInt`: an optional inherited name at guest
offset08 and signed int32 `_data` at0C. Registration `802DD720` defines size10;
factory `802DD57C` uses type80535478. Field setup `802DD7B4` supplies an ordinary
integer field without a default override, so its initial data is exactly zero.
Null and empty names remain distinct.

Work registration `802C6D18` makes `_mHp` at08 and `_hp` at0C references to that
type and enables automatic creation for each. `8006A618` performs the two
separate factory calls. `ModelScriptWork` therefore starts with two distinct
zero-data shared objects. Work `_recNo` at10 is a signed scalar with explicit
default-1 (`802C6DAC..6DB0`), not another object reference.

Actual player preparation obtains existing `HP` and `MHP` objects from the
`GAMERAM/GETPLAYER` result and retains those references in work at
`8034D998..8034DA24`. A native owner must share those objects, because writes
through model work also affect their GAMERAM owner. The module allows explicit
replacement of `work.hp` and `work.max_hp`; it never copies their data into
detached replacement objects implicitly. The original zero-HP preparation
branch also copies MMP into MP. `bind_model_game_data` now implements that
post-GETPLAYER prefix against the actual captured row, including retained old
rows after reset. See [GAME_DATA.md](GAME_DATA.md). Complete player preparation
remains outside this module; zero factory data is not evidence of playable initial HP.

`ModelScriptWork` has immutable qualified target identity, source path, model
name, RAM number and matrix attachment. The matrix is a
`shared_ptr<AnimationMatrix>`. The event runtime now shares the node matrix
allocation directly, independently of controller lifetime. The caller must
supply an owning lifetime, not a no-op
deleter around temporary storage. Context publication keeps the live reference,
so later matrix writes remain visible. No identity matrix is supplied implicitly.

## Shared context publication and completion

`begin(work, request)` validates the qualified request and prepares an owned
`ModelScriptCall`, including `prefix_targetName.lua`. It publishes the fields
of original shared context80535914 in instruction order before any callback:

| Guest context offset | Value | Publication address |
| --- | --- | --- |
| 00 | Target RAM identity | 8030B7F4 |
| 04 | Target RAM number | 8030B80C |
| 08 | Live ONE node matrix reference | 8030B81C |
| 0C / 10 | Current maximum HP / HP integer values | 8030B828 / B834 |
| 14 | Signed target record | 8030B83C |
| 18 | Target model name | 8030B840 |
| 1C / 20 | Kind / first body ordinal | 8030B844 / B848 |
| 24 | Source name | 8030B84C |
| 28 / 2C | Source RAM / signed source record | 8030B850 / B854 |
| 30 byte | Literal1 direction response | 8030B858 |
| 34 | Event name | 8030B85C |

Native source qualification is retained alongside those fields. Scalar HP and
record values are captured at entry; the matrix remains a live reference. The
request and filename retain their own values when a nested call overwrites the
shared context. The original uses the controller's script owner at `8030B874`
to run the selected chunk's top level. Event `Hit` is context data, not a named
Lua function invocation. See [SCRIPTS.md](SCRIPTS.md) for ordered embedded lookup.

On successful script completion, the epilogue reloads the current work HP
reference, then compares its current value with the entry HP value. Only if
equal does it copy the current shared context HP into that object
(`8030B878..B88C`). Direct HP changes take priority. A replacement HP object
with the same entry value receives the context value; a replacement with a
different value preserves its own value. This is a value comparison, not an
object-identity test. Maximum HP and target record are not copied back.

Nested scripts overwrite the same context. Completion never restores an outer
snapshot. An outer epilogue can therefore read the inner call's current context
HP and direction. The returned optional byte is the live direction response,
including zero or noncanonical nonzero bytes. The collision owner performs any
later heading effects separately; see [MODEL_COLLISION.md](MODEL_COLLISION.md).

## Continuations and checked boundaries

`resume(invocation, dispatch)` invokes the supplied callback with the runtime
and retained call. A missing callback or false result leaves the invocation
pending without republishing context or merging HP. The callback owns any
committed internal prefix. Changing a callback between retries must preserve
that prefix; the module cannot reconstruct external script state.

An outer pending invocation can re-enter its callback to resume a retained
child. Each child must be resumed from its parent callback, and invocation
completion is strictly LIFO. A parent cannot finish with a pending child or
begin a replacement child while the prior child remains pending. Another
top-level invocation cannot bypass the pending outer call. Completed handle
retries return their saved direction without calling the service again.
When a child pauses, its parent callback must also return false promptly;
the callback owns the order of its remaining effects.

`context()` exposes the latest shared values for inspection.
`mutable_context()` is available only inside an executing callback. Nested
calls do not invalidate a retained `ModelScriptCall` reference. Callers must
retain the move-only invocation handle through suspension. Destroying or
overwriting an unfinished handle faults the shared owner instead of silently
abandoning its published state.

Qualified argument mismatches, null required owners, nonfinite matrices and
invalid text reject before publication. Every string and computed filename is
bounded to 4096 bytes; embedded NULs reject. At most 32 invocations can be pending
in one nested chain. Defaults and integer fields preserve signed int32 values.
Validation and allocation precede publication as an explicit native failure
boundary; the original helper does not provide equivalent host-error recovery.

A thrown script callback or invalid completion faults the owner because effects
may already have occurred. Pending work and the published context remain
inspectable; automatic replay is forbidden. A callback that removes its live
HP object cannot complete the required epilogue. Changes to fields the original
epilogue does not read are not reverted or implicitly validated there.

The present [Lua4 VM](LUA4.md) has its own execution and continuation contract.
A script adapter must honor that contract and the original native methods;
restarting a stopped chunk is not a valid way to resume a committed HP,
sequence or messenger prefix. This module supplies the surrounding ownership,
not those missing native effects.

## Verification

Synthetic tests cover distinct default objects, signed record defaults,
qualified argument rejection, null/empty names, exact retained requests,
shared GAMERAM HP identity, live aliasing matrix lifetime, missing/false service
retries, current HP replacement and direct mutation, nested shared-context
overwrite, LIFO completion, depth limits, abandoned handles and uncertain
exceptions. They verify the represented native lifecycle against recovered
instruction order; they do not claim execution of the original player
constructor, complete target script or combat system.
