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
tables cannot form leaked shared-pointer ownership cycles; the garbage
collector below frees unreachable ones. A handle carries its arena slot and the
slot's reuse generation, so a collected value fails cleanly ("invalid or
collected VM-owned handle") and never reaches a newer object in that slot.

The default environment is empty. A caller can explicitly install
`Lua4Value::host_object("be")`, symbolic host functions, and a strict
`Lua4HostHandler`. A callback receives the exact symbolic name, arguments and
source/prototype/program-counter location. For example, `be:CFLclear()` reaches
the handler as `be.CFLclear`, with the original receiver at argument zero.
Native bindings own their validation and effects. In synchronous `execute()`,
an absent handler, a rejected native request or a callback exception stops
execution and the VM; no call is skipped.

`Lua4RunResult` reports completion, returned values, executed instruction and
host-call counts, and the failure location and diagnostic. State already written
before failure remains inspectable; there is no rollback, retry or
startup-stage advancement.

Errors follow the original's protected calls. The game runs every chunk
through `igLuaState::runBuffer` (80272768): `lua_dobuffer` (80278810) ->
`lua_call` (80278648) -> `luaD_runprotected` (802789E4). On failure it only
logs `igLuaState::runBuffer(%s) failed - reason %s` and keeps the `lua_State`.
A script error (a Lua runtime error, a stack overflow or a port guard) goes
through `lua_error` (8027894C). First `message` (802788A4) looks up
`_ERRORMESSAGE` raw in the globals and, if it is a function, calls it with the
error text on top of the failing frames. `func.lua` defines it as
`be:MsgBox(a0, 0)`. An error inside that handler calls it again further up the
stack. The chain ends when one call completes, or when a break without a
message ends it (out of memory, or a second stack overflow, see Stack).
Then `luaD_breakrun` (8027898C) unwinds to the protected call:
`luaD_runprotected` restores the stack top, `Cbase` and the stack limit, and
`lua_call` removes the chunk closure. The port does the same. The invocation
reports `faulted`, its frames are released, and the VM, its globals and any
parent invocation go on (`Lua4Vm::stopped()` stays false). Out of memory
(`luaD_breakrun` with `LUA_ERRMEM`) skips `_ERRORMESSAGE`. Only a Lua closure
is called as `_ERRORMESSAGE`; the base library's C version (stderr through
`_ALERT`) has no port counterpart. A host pause inside the chain cannot be
served while unwinding and ends it.

Host and API faults are not script errors of the original, and they stop the
VM for good: a callback exception, a malformed host result, an unbound host
function in synchronous `execute()`, an abandoned invocation or a broken
invariant. A stopped VM cannot resume or execute a later chunk, so the caller
must construct a fresh VM. The engine (`run_script`) logs every failed run as
`script NAME stopped: ...`. It installs a fresh VM with `func.lua` only after
such a fault.

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
inspection remains available; host inspection does not charge string work.

`begin_nested(program)` is valid only inside an invoked resumable host callback.
The child executes on the same VM, globals, arena and root budgets. That logical
callback owns the child handle, and only reentry into that callback may resume a
pending child. One child may be pending per host call; a completed child permits
the next sequential child, matching an ordered script-registry walk. A parent
cannot report completion while its child is pending. Recursive self-resume,
unrelated native reentry and externally resuming a child reject before execution.
A child's script error ends only the child, the way a nested `lua_dobuffer`
has its own protected `lua_call`: the callback gets the faulted result and the
parent goes on. A child's fault or abandonment stops the shared VM and faults
the parent, even if a host callback tries to swallow that outcome. Shared
globals are not restored after nested chunks. This is an explicit native availability boundary around synchronous Lua
calls, not a Lua coroutine API or a separate VM copy.

## Limits and unsupported operations

Default read limits are 4 MiB of chunk bytes, 262,144 instructions, 4,096
prototypes, 2 MiB of serialized string bytes and 32 prototype levels. Independent
hard caps include a 1 MiB value string, a 64 MiB cumulative parser-allocation
allowance, one million parsed records, 250 declared stack slots per prototype,
100 fixed parameters and 32 captured values. Caller limits are validated and
cannot remove the hard ceilings.

Default VM limits are one million instructions, 8,192 native calls and 32 MiB
of string work (comparisons, key lookups, number conversions) per root
invocation, the original's 1,024-slot Lua stack (below), 1,024 live Lua frames,
65,536 live tables and closures, 262,144 live table entries, 4,096 globals and
32 MiB of live Lua data. The per-root counters reset with every root
invocation; retained nested chunks share them. Instructions and logical host
calls are charged once; a not-ready retry charges neither again. The
stopped/unbound logical host call still counts. Suspended frames keep their
stack slots and call depth. Host callbacks are trusted application code; their
own allocations and effects require separate bounds.

## Stack

The original has one Lua stack of 1,024 `TObject` slots: `igLuaState`
(80272594) calls `lua_open(0, pool)` (8027E924 `f_luaopen` takes the default
size for 0). Every chunk, Lua frame and C call lives on it at absolute
positions, and `luaD_checkstack` (80278088) fails when `stack_last - top <= n`,
with `stack_last` = slot 1,023. The port keeps the same positions (a frame's
local 0 is the slot above its function, a CALL's top is the slot above its
last argument) and checks where the original checks:

| Check | Original | Reserve above the top |
| --- | --- | ---: |
| Lua function entry, arguments still on the stack | `luaV_execute` 802834F8 (0x80283540) | `maxstacksize` + 8 |
| C function call (`be:X`, `gsub`, ...) | `callCclosure` 80278340 (0x80278368) | upvalues + 20 |
| `be.X` / `be:X` lookup, the class's gettable method | `luaV_gettable` 80282A90 | 2, then 1 + 20 |
| reading a nil global, getglobal method | `luaV_getglobal` 80282D10 | 3, then 20 |
| assigning over a nil global, setglobal method | `luaV_setglobal` 80282DE4 | 3, then 20 |
| `^`, the math library's pow method | `call_binTM` in `luaV_execute` | 2 (event name, method), then 20 |

The tag methods are the igLua binding's: getglobal / setglobal for the nil
tag (80270BC4: 8026C8C8 / 8026C980, no upvalue) and gettable per class
(8026E65C -> 8026E590: 8026D2B8 with the class as one upvalue). A `be:X`
method closure carries its method as one upvalue (8026BB48), the library
functions registered by name carry none. A chunk the game runs starts with
two values already on the API stack (`Lua4Limits::api_stack_top`: closure at
slot 2, local 0 at slot 3); a chunk a host callback runs starts at the
callback's top, above its arguments and upvalues (what the binding itself
pushes before such a run is not modelled).

A Dolphin run confirms the model. `work/dref/fix_r22lua/tools/mk_stacklog.py`
makes an AR code that hooks `luaD_checkstack` and `igLuaState::runBuffer` and
logs each new peak (top + reserve, the top, the reserve, the caller) and the
top at every chunk start through OSReport. It ran over a new game up to
`dng101b1` (`work/dref/scripts/newgame_dng101b1.txt`, 5,600 fields). Every one
of the 29 chunk starts was at top 2. The peaks after the binding's own setup
(4 and 24) were 27 (`func.lua`, a setglobal tag method at top 7), 35 / 38 /
39 / 40 (the first `boot.lua`, the gettable method of `be` at tops 14 / 17 /
18 / 19, reserve 21), 48 (a 37-slot chunk at top 3) and 61 (a 13-slot
function at top 40 in `dng101b1`'s `boot.lua`). The port, on the same input,
asks for exactly the same checks with the same values, and its peaks after
those chunks are 27, 40 and 61 (it reports a peak when a chunk ends). An
overflow raises "stack overflow" as a Lua error: `stack_last` grows by
`EXTRA_STACK` = 40 slots (+0x280 at 0x802780DC) so that `_ERRORMESSAGE` can
run, and a second overflow before the protected call restores the limit
(`restore_stack_limit` 80278100) breaks with `LUA_ERRERR` and no message.
`heap_stats().stack_peak` is the highest top + reserve any check asked for,
and `DW4_TRACE_LUA_STACK=1` logs each new high in the game.

Completed handles retain their owned results until released (they are
collector roots while the handle lives).

## Garbage collection

The original (GDJEB2) runs stock Lua 4.0.1 inside the Alchemy `igLuaState`
(80272594): `lua_open(0, pool)` (8027E9D4) gives the fixed 1024-slot stack of
`f_luaopen` (8027E924) and stores an `igMemoryPool` in a field added after the
stock `lua_State` (offset 0x70); `luaM_realloc` (8027BD18) allocates through
that pool's virtual functions and fails with `luaD_breakrun(LUA_ERRMEM)`. The
collector is unchanged: `nblocks` (0x60) counts object sizes, `GCthreshold`
(0x5C) starts at twice the state after opening, `luaC_checkGC` (80279894) runs
after every `luaD_call` (80278448), at CREATETABLE/CONCAT/CLOSURE
(`luaV_execute` 802834F8) and in `lua_concat`/`lua_setgcthreshold`;
before every chunk load `protectedparser` (80278708) collects when
`nblocks/8 >= GCthreshold/10` (unsigned divisions, 0x80278730..0x80278744:
about 80 % of the threshold, not `luaC_checkGC`'s `nblocks >= GCthreshold`) and
then adds the new chunk's blocks to the threshold; `luaC_collectgarbage`
(80279830) marks, sweeps and sets the threshold to twice the live bytes. The game itself never changes
the threshold: `lua_setgcthreshold` (80273C58) is reached only from the base
library's `collectgarbage`, which the binding setup (80270BC4) runs once as
`collectgarbage()`. The binding installs `gc` tag methods per bound class
(80270BC4, 8026E65C) that release the C++ peers of collected userdata; no `gc`
method exists for nil.

The port keeps that model. Allocations of tables (24-byte hash, 40-byte
nodes), closures (16 + 16 per upvalue), strings (21 + length), globals and the
program of every run (as the original undumps the chunk per run) raise the live
estimate. Before a chunk loads, the 80 % test of `protectedparser` above
decides a collection. At every instruction boundary, where all live values sit
in frames, a collection runs once the estimate reaches the threshold, or before
a step could exceed the live object/entry limits. It marks from the globals and
every live execution (frame stacks, captures, pending call function and
arguments, kept results), traverses tables and closures through a work list
with marks per arena slot, erases table entries and globals whose value is nil,
sweeps the arenas in slot order and sets the threshold to twice the measured
live bytes.

Erasing dead keys approximates the original. `traversetable` (in `markall`
802790CC, 0x80279184..0x802791C4) calls `luaH_remove` (80280F48) for a node
with a nil value and then still marks its key and value. `luaH_remove` keeps
number keys and string keys of 30 bytes or less as they are; a longer string,
a table or a function key becomes an unused number key with the same main
position. No node is freed until the next rehash, which moves only non-nil
entries. A nil-valued node reads as absent everywhere (table and global gets,
the probes of `getn`, host `table_get`), and the port has no `next`, `foreach`
or LFOR traversal (no game chunk contains LFORPREP), so the difference is not
observable: it changes only the live estimate (when collections run) and the
`max_table_entries` / `max_globals` guards. Strings are shared C++ values that
free themselves; they are counted once per distinct string. Nothing in the
collection depends on addresses or hashing order, so runs are deterministic.
The port's scripts create no userdata (`be` is a permanent host global), so no
`gc` tag method runs. Live data above `max_heap_bytes` after a collection ends
the invocation with "not enough memory", a break without `_ERRORMESSAGE` like
the allocator's `LUA_ERRMEM`; the original's pool fails far earlier (the
GameCube has 24 MB of main memory). `collect_garbage()` is `collectgarbage()`
between root invocations and `heap_stats()` reports the counters. A table or closure that only the host
still holds is collected like an unanchored value of the Lua C API.

`POW` (`^`) has no numeric meaning in Lua 4 itself. `luaV_execute`'s
`call_binTM` looks for a `pow` tag method on the first operand's tag, then on
the second's, then for the global one. The game opens the math library
(`igLuaState` 80272594), and `lua_mathlibopen` (8027BC14) sets `math_pow`
(8027B668) as the `pow` method for numbers. So when either operand is a
number, the port calls it the same way: `math_pow(a, b, "pow")` reads argument
2 before argument 1 (`luaL_check_number` 80274214; a numeric string converts,
anything else is ``bad argument #n to `?' (number expected, got TYPE)``) and
returns `pow(a, b)`. Two operands that are not numbers (two numeric strings
included) give "undefined operation". The port uses `std::pow`, while the
original uses fdlibm's `__ieee754_pow` (800A042C behind 800A2D14); inexact
results may differ in the last bit. No game chunk contains `POW`: a scan of
all 7,300 embedded scripts (2,902 distinct chunks in 98 catalog resources and
package members) found no `POW` and no `LFORPREP`. The decompiled scripts in
`work/port-research/dec` agree.

Unknown opcodes are rejected while reading. `LFORPREP`/`LFORLOOP` stop because
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
calls, live limits and the collector (a hit-script-like chunk run 2 x 20,000
times under a 96 KiB heap and 64 objects, globals kept across collections,
cycles and dead keys freed, identical counters in two VMs, stale handles,
out of memory, collection inside a nested chunk). It also checks reference-confirmed edge cases such
as rejecting the string `+-1` in arithmetic and rejecting a numeric-string loop
index assigned by the body. The optional original test reads two caller-supplied
chunks and checks persistent helper definitions and the first unsupported call.

The original synchronous checks remain in the expanded suite. Retained execution
checks cover fixed/all/tail results, captures/locals/varargs, stable host arguments
and sites, completed-prefix preservation, nested and sequential children, shared
globals/budgets, absent handlers, malformed results, exceptions, owner moves,
abandonment and guarded reentry.

Source-linked Release and fully instrumented MSVC AddressSanitizer runs passed 416 synthetic
checks plus 10 optional original checks before the collector; the suite now
has 438 synthetic checks (Release). The original `func.lua` contains 18
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
