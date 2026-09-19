# Native IGB decoder orchestration

`IgbDecodeInvocation` reconstructs the original USA `igIGBFile` read entry
`800528DC` and failure tail `80052794`. It connects directory path requests to
actual stream opening, header processing, the first three table helpers and
runtime type/slot reconciliation. It is not yet a complete deserializer: reached
type/list factories, field preparation, object population, activation,
postprocessing and cleanup still have explicit reached
operations. A pending operation leaves the original published prefix intact.

The supplied original DOL has SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Source is authored from the original
instruction behavior; original bytes, generated disassembly and local reference
drivers are not public deliverables. See [IGB_STREAM.md](IGB_STREAM.md),
[IGB_DECODE_TABLES.md](IGB_DECODE_TABLES.md),
[IGB_TYPE_MAPPING.md](IGB_TYPE_MAPPING.md) and
[LOADER_REQUEST.md](LOADER_REQUEST.md) for the connected bodies.

## Ownership and integration

`LoaderRequestFile::igb_decode` owns `IgbDecodeState`. Its stream fields point
weakly back to the canonical file, and table state shares scratch allocations
with those stream fields. Header object count and the directory's live object
list count are different fields; only the latter is the decoder's final return.
Absent decoder state pauses before entering the represented body. Constructing
zeroed C++ state does not implement the reflected `igIGBFile` factory or its
post-default pool setup.

`make_native_igb_loader_factory` binds the existing `decode_igb` request to this
entry. `make_native_igb_stage_factory` binds `8004F870`, `8004F974`, `800514AC`,
`80051794`, `8004FBE8` and `8004FC94` to their concrete authored bodies. Remaining operations
go to an explicitly supplied factory or retain a named pending diagnostic.
Neither adapter turns a successful document parse into a loaded directory.
Installed factories use weak service captures; selected operations retain the
actual services they need until completion.

The existing loader can try its primary path, secondary path and bare name on
the same file. Completed decode attempts release their native invocation lease;
ordinary original status failure is a completed attempt returning signed -1.
Exceptions, abandoned work and caught recursive entry poison the interrupted
prefix instead of allowing it to replay. Distinct files have independent leases.

## Original control flow

The entry captures its input filename, formats `readFile(%s)` into native
diagnostic storage, then samples the current global timer's binary64 seconds.
Only afterward does it store succeeded=0, loading=1 and objects-initialized=0.
Embedded NUL bytes terminate formatting as in the original C string operation.
The native diagnostic bounds filenames to 1 MiB; original unchecked `sprintf`
overflow is not reproduced.

Every checked helper's returned 32-bit word is compared for equality with the
**live** failure global in `stream->statuses`, after that helper completes.
The original success word is not a boolean. Missing globals pause exactly when
read, retaining the completed helper result so its effects are not repeated.

After open and header, signed version below five with nonzero external flag
always fails. An unsuppressed warning first receives the same captured stream
filename in both original `%s` positions. Exact result two suppresses later
warnings; other results do not. An already suppressed warning skips the stream
dereference. The initial version is reused for the version-at-least-three gate.

The ordinary sequence is:

1. Field-type and alignment tables when the captured version is at least three.
2. Object-type bytes, runtime type construction and type/field preparation.
3. `800521EC` when the current version is at least five; optional pool names.
4. Three shared-entry stages, or the separate unshared stage. The selected
   shared branch does not retest its flag between its three calls.
5. `800520E8` when the current version is at least five; object population and
   the reached file payload virtual `+84`.
6. Field types for current version below three, `800520E8` for version below
   five, then `800521EC` after a separate version-below-five reload.
7. Final table, info export and object activation. Activation's returned status
   word is ignored; its actual effects are still required.
8. The reached file postprocess virtual `+78` only when byte `+115` equals one.

All later gates reload their original live fields. Raw nonzero shared/pool
flags and exact-one postprocessing are intentionally different conditions.
Partially identified table stages retain their original entry addresses in
the API until their individual bodies and full semantics are recovered.

## Success, failure and timing

Success closes the stream, runs cleanup, stores succeeded=1 then loading=0,
and conditionally logs timing. Failure instead runs cleanup, stores loading=0
then succeeded=0, closes the stream and returns signed -1. Cleanup or closing
may suspend or fail before later stores; they are never silent no-ops.

The required cleanup implementation must preserve the two separate releases
of **live** field `+BC`, with a reload between them, and the unconditional pool
free of `+108` even when null. Conditional closing must preserve its signed
auto-opened test and live stream reload after the close callback. These effects
are explicit remaining helper boundaries, not implemented by the caller alone.

Timing suppression accepts any nonzero byte. When logging is enabled, the
global clock is reloaded and sampled again; elapsed time is binary64 subtraction
without float narrowing. `NativeGameClock::time_seconds()` implements the real
`800936A0` signed quotient/remainder conversion. Exact logger return two sets
suppression to one. The final object count is read after the logger, so its
callback may affect that returned word. See [GAME_CLOCK.md](GAME_CLOCK.md).

## Acceptance scope

Permanent tests cover original stage order, each tested failure edge, live
status/version mutations, exact flag rules, warning/log results, pending
callback retention, diagnostic/timer ordering, recursion and abandonment.
The actual loader-to-decoder-to-header/table composition stops at an absent
runtime registry rather than fabricating a successfully initialized scene.
Caller tests using prepared remaining helpers qualify their orchestration only.
Original-data and instruction comparisons are recorded in
[VALIDATION.md](VALIDATION.md).

The type mapper now reconciles registered metatypes and inherited serialized
field slots, retaining individual boundaries for unrecovered constructors and
field/registry operations. The bounded native bootstrap registration profile is
derived from original registration/default routines rather than serialized
counts; it does not establish full engine initialization. With those records
and prepared list allocation endpoints, supplied `ndmw.igb` can reach the next
runtime boundary, type/field preparation `800505E0`. This must lead to real
object constructors, graph/reference population and original post-load effects.
Until that chain and startup registration are connected, neither the original
`ndmw.igb` boot nor complete native gameplay is established.
