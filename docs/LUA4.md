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
Native bindings own their validation and effects. An absent handler, a rejected
native request or a callback exception stops execution; no call is skipped.

`Lua4RunResult` reports completion, returned values, executed instruction and
host-call counts, and the failure location and diagnostic. State already written
before failure remains inspectable. The stopped VM cannot resume or execute a
later chunk; the caller must construct a fresh VM. There is no implicit invocation
of the script's `_ERRORMESSAGE`, rollback, retry, or startup-stage advancement.

## Limits and unsupported operations

Default read limits are 4 MiB of chunk bytes, 262,144 instructions, 4,096
prototypes, 2 MiB of serialized string bytes and 32 prototype levels. Independent
hard caps include a 1 MiB value string, a 64 MiB cumulative parser-allocation
allowance, one million parsed records, 250 declared stack slots per prototype,
100 fixed parameters and 32 captured values. Caller limits are validated and
cannot remove the hard ceilings.

Default VM limits are one million instructions and 8,192 native calls per
execution, 64 nested calls, 16,384 aggregate reserved stack values, 65,536 arena
objects, 262,144 table entries and 4,096 globals. A cumulative 32 MiB allowance
charges retained programs, objects, copied/imported strings and string-processing
work. Repeated comparisons, key lookups and numeric conversions consume that
allowance again. Budgets do not reset between successful chunks, except the
explicitly per-execution counters. Host callbacks are trusted application code;
their own allocations and effects require separate bounds.

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

Release and fully instrumented MSVC AddressSanitizer runs pass 286 synthetic
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

Original chunks and all comparison traces remain in ignored `work/`. This
establishes the inspected scripting boundary, not loading completion, controller
registration, player movement or gameplay. Scheduling and embedded selection
remain documented in [STARTUP_RESEARCH.md](STARTUP_RESEARCH.md); the caller's
startup integration must stop at any unimplemented native stage.
