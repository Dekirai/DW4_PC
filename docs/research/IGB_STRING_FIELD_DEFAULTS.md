# String-field defaults

`IgbStringFieldDefaultsInvocation` reconstructs original USA caller
`8002549C`. It captures canonical string-class metadata `805615B4` and its
first local field index, then composes the native batch, scalar values,
schema and inherited descriptor replacement operations. Code and fixtures
are authored C++; original executable bytes remain local research inputs.

## Batch, replacements and schema

The caller first runs `80065924` for one Boolean field. Its original tables
are constructor `8055D0F0`, name `8055D0F4`, global `8055D0F8` and offset
`8055D0FC`: factory `800385E0`, name `_refCounted`, descriptor cell `805615C4`
and member offset `0x38`.

Before applying that schema, it performs these ordered operations:

1. Find inherited `_shallowCopyMethod` through `800658F8`. After the search
   completes, reload the metadata's registry and obtain its identity index
   through `80053998`. Call the captured descriptor's `+54` with argument one.
2. Pass the one-byte value two to the clone's fresh `+8C` through `80040030`.
   Store global-cell address `805615BC`, execute actual replacement `8006588C`,
   then release the clone's live creator reference.
3. Repeat the search and identity-index sequence for inherited `_destruct`.
   Clone through `+54` with argument one, then pass the one-byte value one
   through `8003EC68`. Store global-cell address `805615C0`, replace and release.
4. Reload the original first local field through `800658E4`, set its one-byte
   default to true, then apply actual `800659C0` using the captured first index
   and the one-field schema tables.

Both identity-index registries are captured **after** name lookup. This differs
from the reset-method search in memory-reference defaults. Each replacement
performs the native finalizer, reloads the live registry, retains its incoming
descriptor, releases the old descriptor and publishes its physical pointer.
The caller's subsequent creator release decrements the full live word and
calls `80066E1C` only when the resulting low 23 bits are zero.

## State and physical representation

Unknown canonical cells and operation bindings suspend the actual caller.
Selected operations and their continuations survive suspension; completed
batches, searches, scalar inputs and reference changes are not replayed.
Changing the registration cell cannot replace the captured metadata receiver.
Name search retains its original registry and descriptor while observing the
original live count/backing reads. Index scaling wraps in 32 bits before the
physical slot bound is applied.

Known physical descriptor storage must provide initialized, live vtable bytes
at the original clone or scalar call-selection point. A canonical field cannot
conceal a released allocation or unknown physical header. Once reached, a
captured destination allocation and host offset cannot be replaced while a
callback is pending. Big-endian direct global/reference stores precede their
canonical publications, preserving a faulting operation's completed prefix.
Arithmetic field-pointer bindings remain explicit; host pointers are not
converted to original addresses.

The caller does not reinterpret a null clone as a skipped default. It reaches
the original immediate scalar dereference and fails. An unbound clone body is
an explicit pending service rather than a manufactured ready descriptor.

## Composition and validation scope

The inherited source types are signed byte (`80473F24`, clone `80037BC0`) and
Boolean (`80473E30`, clone `80038684`). Their real implementations can be
installed through the existing list factory. This module reconstructs the
caller without substituting those clone bodies or the enabled reflected-copy
path. Registration, prototypes and reflected object virtuals are documented
in [IGB_STRING_FIELD_REGISTRATION.md](IGB_STRING_FIELD_REGISTRATION.md).

Permanent tests prepare the constructor and inherited clone leaves, plus
allocation and string endpoints. They execute the actual surrounding batch,
scalar copy, schema, finalization and replacement. Checks cover the full
result and exact scalar order, both registry reloads after pending searches,
retained callbacks, captured metadata, late pointer bindings, unavailable
physical headers, pending physical rebinding, null clones, creator destruction,
recursion and completed-call idempotence. Build and original-instruction
comparison results belong in [VALIDATION.md](VALIDATION.md) after execution;
this caller suite alone does not establish complete string registration or
game startup.
