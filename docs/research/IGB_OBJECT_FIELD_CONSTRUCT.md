# Native object-reference member construction

`IgbObjectFieldConstructInvocation` reconstructs `0x8006A618`, selected by
object-reference descriptor table `0x804762FC` virtual `+0x7C`. It composes the
existing native reflected factory `0x8006546C`; it does not substitute a prepared
child pointer for that call. The new assignment invocation reconstructs the
reached helper `0x8006A47C`.

The supplied USA `main.dol` has SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Evidence is the original instruction
range `0x8006A618–0x8006A6F4`, assignment `0x8006A47C–0x8006A510`, and target-pool
helper `0x80068430–0x80068450`. No game binary or generated disassembly is part
of this module.

## Recovered order

1. Read the descriptor's construct byte `+0x34`. Zero returns without reading
   the target, a pool name, metadata, or any service.
2. Capture pool-name pointer `+0x40`. A nonempty first character selects
   `0x80060834`. Null, an empty first character, or a null lookup result uses
   `0x80068430`, reading the target's live reference high byte before passing it
   to `0x8005929C`. This is distinct from current-pool handle lookup.
3. Read descriptor metadata `+0x38` after pool selection and call actual
   `0x8006546C(metadata, pool)`, including its native allocation/initialization
   continuation. A represented null metadata dereference faults; an absent
   mapping pauses. An abstract factory result remains null.
4. Assign through actual `0x8006A47C`: read `+0x3C`, capture the old member using
   descriptor `+0x08`, and read `+0x35` only when `+0x3C` was zero. When either
   ownership byte is nonzero, increment the incoming full packed word, decrement
   the old full word, and call `0x80066E1C` if its low 23 bits become zero. Then
   reload descriptor `+0x08` for publication. A destructor can therefore change
   the final destination without changing the already captured old owner.
5. Reload the descriptor ownership bytes after assignment. If either is set,
   release the captured factory result once, with the same full-word decrement
   and low-23-bit destructor condition. These flags are not cached from step 4.

## Native representations and boundaries

Member pointers are explicit `IgbObjectInstance` values with arithmetic
addresses and validated storage bindings. `services->objects` maps old member
addresses and retains completed allocations independently of their original
packed reference counts. Raw stores preserve displaced host owners. Metadata
`+0x28`, `+0x34`, and `+0x38` publish known field-list, type-list, and parent
metadata identities alongside physical bytes; common scalar synchronization
preserves numeric pointer identity and unrelated padding. The reflected
`igMetaObject` `_metaFields` construct descriptor at `+0x28` uses this path.

The named-pool lookup, tagged-pool lookup, and `0x80066E1C` destruction remain
explicit service endpoints. Missing implementations pause at their reached
call; no default success, guessed pool, or automatic destructor is supplied.
The nested reflected factory retains its own allocation and virtual-operation
boundaries. This module does not claim complete recursive list construction,
arbitrary object schemas, startup, or gameplay.

Pending operations retain selected factories, callbacks, arguments, and all
earlier reference effects. Recursive entry and invalid physical identity fault
the operation. Synthetic tests cover construct/null/abstract gates, actual
reflected child initialization with prepared zero-field metadata, pool fallback,
late bindings, captured old owners, live destination and ownership reloads,
alias arithmetic, suspension, faults, and allocation lifetime. Those prepared
endpoints are test evidence, not completed engine services.
