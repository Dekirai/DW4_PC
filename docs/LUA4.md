# Bounded Lua 4 chunk execution

The portable `lua4` module reads the original binary script format and executes
a bounded subset of Lua 4 instructions. It runs `func.lua` and `boot.lua` from
the inspected `dng101b1` package when the caller supplies the required native
bindings. It does not load files, compile Lua text, execute PowerPC code, or
provide an operating-system, network or standard-library environment.

## Format and semantic references

This is an independent C++20 implementation. The primary references are the
official Lua 4.0.1 [chunk reader](https://www.lua.org/source/4.0/lundump.c.html),
[instruction definitions](https://www.lua.org/source/4.0/lopcodes.h.html),
[interpreter](https://www.lua.org/source/4.0/lvm.c.html),
[call/result adjustment](https://www.lua.org/source/4.0/ldo.c.html), and
[type and configuration limits](https://www.lua.org/source/4.0/llimits.h.html).
The reference implementation is by the Lua authors at TeCGraf, PUC-Rio.
Upstream source is used for research and independent local comparisons; the
application neither vendors nor links that interpreter.

The supported header specifies Lua version `0x40`, four-byte integers, sizes
and instructions, a six-bit opcode, a nine-bit B operand and eight-byte IEEE
numbers. Both byte orders use checked byte reads. Strings retain their byte
contents, including embedded NULs, after validation of the final terminator.
Prototype constants, children, debug records and code are read in serialized
order. Counts, indexes, jump destinations and the final exact `END` are checked.
The caller's label supplies diagnostics; an embedded debug filename never
becomes a filesystem path.

Lua 4 has nil/number/string/table/function values; it does not use later Lua
boolean semantics. Only nil is false, including in conditional jumps. Closure
captures copy stack values; table values retain reference identity. Numeric
loops coerce their initial controls at `FORPREP`, then require numeric controls
at `FORLOOP`. The supported string ordering is the C-locale byte order.

Calls use the encoded base-relative function position. Ordinary calls truncate
or pad returned values with nil, while result operand 255 requests all values.
`TAILCALL` preserves the encoded return-prefix boundary. Fixed parameters are
truncated or nil-padded; varargs become an `arg` table with one-based numeric
entries and its original `n` field. The colon-call instruction retains the
receiver as the first argument. Table construction retains the original reverse
`SETMAP` write order and the verified 62-item `SETLIST` flush interval.

## API and ownership

`read_lua4_chunk(bytes, label, limits)` returns an immutable owned
`Lua4Program`. Input bytes may be released immediately afterward. A
`Lua4Vm` retains programs referenced by its closures and preserves globals across
successful `execute()` calls. Tables and closures use VM-owned handles; importing
a handle from a different VM fails. An arena owns these objects, so cyclic
tables cannot form leaked shared-pointer ownership cycles.

The default environment is empty. A caller can explicitly install
`Lua4Value::host_object("be")`, symbolic host functions, and a strict
`Lua4HostHandler`. A callback receives the exact symbolic name, arguments and
source/prototype/program-counter location. For example, `be:CFLclear()` reaches
the handler as `be.CFLclear`, with the original receiver at argument zero.
Native bindings own their validation and effects. In synchronous `execute()`,
an absent handler, a rejected native request or a callback exception stops
execution; no call is skipped.

`Lua4RunResult` reports completion, returned values, executed instruction and
host-call counts, and the failure location and diagnostic. State already written
before failure remains inspectable. A terminally stopped VM cannot resume or execute a
later chunk; the caller must construct a fresh VM. There is no implicit invocation
of the script's `_ERRORMESSAGE`, rollback, retry, or startup-stage advancement.

## Retained native-service calls

`begin_execution(program)` and `resume(execution)` explicitly opt into retained
execution. The move-only `Lua4Execution` owns its immutable program and live Lua
frames, including program counters, captures, local stacks, pending arguments
and call/result boundaries. Begin does not execute the chunk. Exactly one root
invocation may remain pending in a VM. Moving the VM or handle preserves its
internal owner identity; another VM, an expired owner or a moved-from handle
cannot resume it. Destroying or replacing an incomplete handle terminally faults
its VM, including when the handle is dropped inside a running callback.

The opt-in handler is installed with `set_resumable_host_handler`. Its
`Lua4HostResult` distinguishes a completed call from an explicit incomplete
service. Incomplete calls must return no Lua values; their diagnostic allows at
most 4,096 bytes. `Lua4RunResult::stop` distinguishes `completed`, `host_required`
and terminal `faulted`, while `completed` remains available to existing callers.
The original synchronous API uses only its ordinary handler and retains its
terminal-error behavior.

An incomplete host call keeps the same owned arguments and source site. A retry
invokes that same captured handler with stable `execution_id` and `call_id`;
it does not fetch another instruction or repeat earlier Lua/global/table effects.
Completed results are validated and imported once, then applied through the
saved fixed/all-result and tail-return rules. A host callback that has partially
completed an external effect must retain its own internal continuation. The VM
does not infer that callback's committed prefix. Exceptions and malformed host
results fault the entire shared VM and are never converted into retry requests.

An absent resumable handler produces `host_required`; installing the first
handler is allowed while paused because no handler owner was invoked. Once a
handler exists, replacing it while work is pending rejects. A stable handler can
consult an explicitly owned service resolver whose missing dependencies become
available later. Unrelated `execute`, `begin_execution`, global writes and handler
changes cannot overwrite a suspended invocation. Existing read-only global/table
inspection remains available and retains its usual cumulative string-work costs.

`begin_nested(program)` is valid only inside an invoked resumable host callback.
The child executes on the same VM, globals, arena and root budgets. That logical
callback owns the child handle, and only reentry into that callback may resume a
pending child. One child may be pending per host call; a completed child permits
the next sequential child, matching an ordered script-registry walk. A parent
cannot report completion while its child is pending. Recursive self-resume,
unrelated native reentry and externally resuming a child reject before execution.
Child faults or abandonment also fault the parent VM, even if a host callback
tries to swallow that outcome. Shared globals are not restored after nested
chunks. This is an explicit native availability boundary around synchronous Lua
calls, not a Lua coroutine API or a separate VM copy.

## Limits and unsupported operations

Default read limits are 4 MiB of chunk bytes, 262,144 instructions, 4,096
prototypes, 2 MiB of serialized string bytes and 32 prototype levels. Independent
hard caps include a 1 MiB value string, a 64 MiB cumulative parser-allocation
allowance, one million parsed records, 250 declared stack slots per prototype,
100 fixed parameters and 32 captured values. Caller limits are validated and
cannot remove the hard ceilings.

Default VM limits are one million instructions and 8,192 native calls per
root invocation, 64 nested calls, 16,384 aggregate reserved stack values, 65,536 arena
objects, 262,144 table entries and 4,096 globals. A cumulative 32 MiB allowance
charges retained programs, objects, copied/imported strings and string-processing
work. Repeated comparisons, key lookups and numeric conversions consume that
allowance again. Budgets do not reset between successful chunks, except the
explicitly per-root counters. Retained nested chunks share those counters.
Instructions and logical host calls are charged once; a not-ready retry charges
neither again. The stopped/unbound logical host call still counts. Suspended
frames keep their call-depth and aggregate stack reservations, and retained
execution/frame/argument storage also consumes the cumulative heap allowance.
Host callbacks are trusted application code;
their own allocations and effects require separate bounds.

Completed handles retain their owned results until released. Programs, closures,
tables and cumulative allocation work still have no full original garbage
collector. Repeated gameplay execution therefore requires a later lifetime and
collection implementation; per-invocation suspension does not reset or hide
those cumulative limits.

Unknown opcodes are rejected while reading. Executed `POW` requires an
unimplemented arithmetic tag method and stops. `LFORPREP`/`LFORLOOP` stop because
the original hash-table iteration order is not implemented. General tag methods,
userdata behavior, garbage-collector callbacks and the Lua standard libraries
are outside this profile. Number formatting uses 16 significant digits.
Arithmetic string conversion accepts finite representable decimal values with
optional surrounding ASCII whitespace; hexadecimal forms, embedded-NUL numeric
prefixes, underflow/overflow and non-finite numbers stop explicitly. These limits
are narrower than a general Lua 4 installation.

## Validation

The asset-free suite generates its own binary chunks. It covers both byte
orders, truncation at every byte boundary, malformed metadata and operands,
ownership, arithmetic, conditional stack behavior, numeric loops, closures,
parameter/result adjustment, varargs, table writes and cycles, strict native
calls and cumulative limits. It also checks reference-confirmed edge cases such
as rejecting the string `+-1` in arithmetic and rejecting a numeric-string loop
index assigned by the body. The optional original test reads two caller-supplied
chunks and checks persistent helper definitions and the first unsupported call.

The original synchronous checks remain in the expanded suite. Retained execution
checks cover fixed/all/tail results, captures/locals/varargs, stable host arguments
and sites, completed-prefix preservation, nested and sequential children, shared
globals/budgets, absent handlers, malformed results, exceptions, owner moves,
abandonment and guarded reentry.

Source-linked Release and fully instrumented MSVC AddressSanitizer runs pass 416 synthetic
checks plus 10 optional original checks. The original `func.lua` contains 18
prototypes and 2,516 instructions; its top level executes 35 instructions,
defines 17 functions and makes no native calls. `boot.lua` contains eight
prototypes and 761 instructions. With only the symbolic `be` object installed,
it stops at `main` instruction 9 on `be.CFLclear`, after ten instructions and
one attempted native call.

An independent local comparison builds the unmodified official Lua 4.0.1 core,
base library and string library as x86, so the original four-byte size fields
match. The [official archive](https://www.lua.org/ftp/) SHA-256 is
`df746e149cf6939e90009d2e540eee918d585b4d1bc6d68b19316a050d484d2a`.
Strict test bindings supply identical explicit player/weapon inputs to the
reference and native interpreters. Every ordered native call name and argument,
including the real reference `gsub` calls, and every final deduplicated queue
entry match:

| Explicit test profile | Native boot instructions | Native calls | Queue entries |
| --- | ---: | ---: | ---: |
| No joined players | 505 | 105 | 94 |
| One Agumon; sword, gun, empty weapon slot | 310 | 34 | 7 |
| Three selected Digimon with shared weapon names | 432 | 49 | 9 |

The comparison was rerun after the retained-frame implementation for both the
strict engine and the opt-in engine. The latter deliberately reports incomplete
once before every native effect. All 188 ordered calls and three final queue
rows still match the unmodified reference, and both engines retain the exact
instruction/call counts above. Original instructions and host effects before
each pause are not replayed. The test bindings remain explicit fixtures; this
does not supply missing gameplay services.

Original chunks and all comparison traces remain in ignored `work/`. This
establishes the inspected scripting boundary, not loading completion, controller
registration, player movement or gameplay. Scheduling and embedded selection
remain documented in [STARTUP_RESEARCH.md](research/STARTUP_RESEARCH.md); the caller's
startup integration must stop at any unimplemented native stage.
