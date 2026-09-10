# MODELCTRL script messenger services

`ModelScriptMessenger` implements the recovered `be.BLPcalcKoukaMagic` and
`be.BLTcalcSkillUp` Lua wrappers through the current shared MODELCTRL script
context and the existing `ActionEventRuntime` messenger. It prepares their
original packets, retains incomplete delivery and returns the original result
shape. The receiving GAMERAM `FIGHTCALC` effects remain separate services.

## Binding and lifetime

Construct the bridge with the same `ActionEventRuntime` that owns the current
collision/script delivery, then install `handle` through
`ModelScriptRuntime::set_host_service`:

```cpp
ModelScriptMessenger bridge(events);
scripts.set_host_service([&](auto& context, const auto& call) {
    return bridge.handle(context, call);
});
```

The bridge is neither copyable nor movable. It and the action-event runtime must
outlive every captured callback and pending call. There is no private messenger,
copied argument buffer, implicit buffer push/pop or context restoration.

Calls require the actual `be` receiver, a published shared context, an executing
context callback and nonzero retained Lua execution/call IDs. The bridge keeps
up to 32 nested calls. Each pending frame belongs to the same context address,
method, execution ID, call ID and exact numeric argument words. A child requires
a distinct Lua execution and resumes through its active owning parent. Completed
children may be followed by another child. Completing a parent while its child
remains pending faults the bridge.

## Original packet and result contracts

Both methods select category `GAMERAM`, null name, index `-1`. Selection clears
the current buffer's typed lists and enabled-only flag. Each wrapper then
appends its arguments and sends `FIGHTCALC` through the ordinary ordered
recipient/manager path.

| Method | String list | Integer list, in order | Lua results |
| --- | --- | --- | --- |
| `BLPcalcKoukaMagic()` | `P2PKOUKAMAGIC` | current target RAM, source RAM, kind | one signed current integer[0], converted exactly to binary64 |
| `BLTcalcSkillUp(first, second)` | `P2ESKILLUP` | current source RAM, kind, first byte, second byte | none |

The RAM values are signed runtime numbers from the live shared script context.
They are distinct from controller identity, source occurrence, record number and
body ordinal. BLT never appends target RAM.

BLT's two descriptor type-9 arguments accept Lua Numbers and compare each
binary64 value with zero. Both positive and negative zero produce byte 0;
finite nonzero values, including fractions and subnormal values, produce byte 1.
The bytes are zero-extended into integer arguments. This is separate from Lua 4
truthiness, under which Number0 is true, and from integer truncation.

BLP reads integer[0] only after every selected delivery completes. It reads the
then-current buffer, including changes made by nested selections and callbacks.
If a manager leaves the original input integers intact, the target RAM remains
the return value. There is no assumed Boolean result or buffer restoration.
The following BLT call reads the then-current shared context rather than the
earlier BLP inputs. BLT ignores all returned lists.

## Suspension and checked boundaries

A missing GAMERAM registration reports incomplete before selection or packet
construction. This is an explicit native availability boundary: the original
unregistered send itself is synchronous. Supplying registration can allow the
same logical call to prepare its packet on retry.

Once prepared, the bridge stores one `MessengerDispatch`. Missing managers or
incomplete callbacks resume that same handle through `ActionEventRuntime`.
The packet is never rebuilt or reselected on retry. Messenger retains each
invoked manager and its completed recipient/hook prefix; an incomplete manager
must retain its own partially executed effects. Callback replacement does not
replace an already captured manager.

The action-event runtime guards its shared buffer during a suspended top-level
delivery. Its enclosing collision/sequence owner must remain active when a
nested call resumes. The bridge uses public messenger access only to prepare a
new packet or to read a completed BLP result. The context runtime separately
guards publication and mutable access. These constraints do not make a retained
raw mutable messenger reference safe for unrelated callers.

A completed BLP delivery with no integer[0] is a terminal checked fault. The
original getter performs an unchecked access; the native bridge cannot invent
zero or replay completed effects to obtain a later result. An asynchronous
manager must report incomplete before claiming completion. Throwing callbacks,
faulted sends and failures after packet construction fault the bridge and its
owning script path; retries cannot replay uncertain effects.

Unknown method names, unverified argument counts/receivers and wrong numeric
tags remain explicit service boundaries. The supported host contract requires
exact argument counts and finite Numbers. The original general argument wrapper
has a returning type-diagnostic path and ignores extra arguments; that broader
diagnostic behavior is not implemented by these two checked bindings.

## Provenance and validation

The reference is the supplied USA GameCube executable, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Packed descriptor `80424880`
registers BLP as ID108; dispatcher case `802EB45C` calls wrapper `802EFC64`.
Its selection, string append, three integer appends, send and current integer
read occur at `802EFC8C`, `802EFCA0`, `802EFCBC/802EFCD8/802EFCF4`,
`802EFD08` and `802EFD1C`. The case returns exactly one signed-integer-derived
Lua number through `80273388`.

Descriptor `804247F4` registers BLT as ID95 with two type-9 arguments.
Dispatcher case `802EB1EC` calls `802EF198`, whose send is at `802EF254`.
Conversion table entry `8026A6D8` compares a Number against binary64 zero and
stores its nonzero byte. The current MODELCTRL context is at `80535914`:
target RAM `+04`, source RAM `+28` and kind `+1C`. Both wrappers use the
messenger referenced from `80535904`.

The asset-free suite passes 156 checks against the final Release core. It
verifies four target RAMs, signed extreme fields
and results, duplicate recipient order, clearing every typed list, unchanged
integer returns, nested current-buffer selection, 36 finite BLT argument pairs,
sequential live-context reads, missing registration/manager boundaries, captured
callback identity, paused child ownership, argument-word stability, guarded
reentry and terminal missing-result/throw behavior. Its GAMERAM managers are
explicit synthetic fixtures; their replies do not establish original combat
effects.

An independent ignored comparison executes the original wrappers, finite
type-9 argument conversion and messenger selection/send/access instructions.
The final Release core matches 43 wrapper scenarios and four separately checked
service prefixes with zero differences, across 225,904 guest instructions.
Prepared hooks supply allocation, category lookup, Lua value access and
recipient effects. The comparison checks current selection, recipient order,
typed-list counts, integer/string values, enabled gate, shared-buffer identity,
current context and the signed return. It includes Number zeros, fractions,
subnormals and finite extremes, along with replaced, empty and nested buffers.

The four service-prefix checks are native availability boundaries: missing
category compares unchanged state before selection, and missing manager compares
the constructed packet before the recipient hook. They do not claim original
asynchronous behavior. Empty-result BLP cases stop before the original unchecked
integer load and match the native terminal usable-result check; they do not
claim the original raised that error. Original bytes, disassembly and all
comparison artifacts remain in ignored `work/`.

See [MODEL_SCRIPT_RUNTIME.md](MODEL_SCRIPT_RUNTIME.md) for retained Lua and
shared-context ownership, and [MESSENGER.md](MESSENGER.md) for original message
matching, live cache records, buffer identities and dispatch continuation.
