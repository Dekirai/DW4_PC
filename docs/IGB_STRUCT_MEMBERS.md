# Inline struct members

This module reconstructs the reached inline-member operations of original
`igStructMetaField`, vtable `80470E00`. It is separate from constructing the
descriptor and its prototype in [IGB_STRUCT_FIELD_RUNTIME.md](IGB_STRUCT_FIELD_RUNTIME.md).

## Original operations

| Slot | Body | Behavior |
| --- | --- | --- |
| `+84` | `80064A0C` | No-op first field pass; no member clear |
| `+74` | `80063EB8` | No-op default mode |
| `+7C` | `80074A1C` | Read constructor `+38`; zero calls the verified no-op, otherwise call it with receiver plus live offset |
| `+80` | `80074A5C` | Read destructor `+3C`; zero calls the verified no-op, otherwise call it with receiver plus live offset |
| `+F0` | `80074B38` | Test whether the live constructor word is nonzero |
| `+70` | `80074A9C` | Zero constructor copies defaults; otherwise call destructor, then freshly reload offset and constructor before calling it |
| `+78` | `80074B0C` | Reload the descriptor vtable and dispatch its `+70` |

Constructor/destructor words are explicit guest code identities. Unknown words
remain unavailable; they are not treated as zero. Reached nonzero functions
use explicit native services and retain the selected operation across
suspension. The nonzero-constructor branch of `+70` does not independently
guard its destructor against zero: a selected null destructor, or a constructor
changed to zero after destruction, is an invalid indirect call.

The implementation retains physical descriptor and receiver identities,
including their byte owners. Nested invocations borrow their parent's guard
while resuming. A callback that invalidates either binding stops subsequent
work, even when it catches the recursive failure itself. Completed callbacks
are not replayed after suspension; newly reached operands are read afterwards.

## Default copying

`IgbFieldDefaultCopyInvocation` implements actual `80063E50`: capture member
offset `+08`, size halfword `+14`, then default pointer `+20`. A later source
binding does not change those captured values. A physical pointer changed
before its load selects the new source rather than a stale canonical buffer.

The actual `800031E8` loop compares unsigned guest addresses. It copies
backwards when source is below destination and forwards otherwise, one byte
read followed by one byte write. This order also matters for distinct
allocations when a source or destination byte becomes unavailable. The native
operation preserves the bytes already written, synchronizes their typed views
and permanently faults at the invalid access. It neither allocates a buffer
nor initializes unrelated padding. A zero count still follows the original
descriptor loads but does not read the source or write the destination.

The older scalar-field helper remains unchanged. No substitute descriptor
vtable is used to enter the new operation.

## Runtime integration

Concrete semaphore vtable `80470314` now exposes its actual metadata getter
`80093530` and verified object initialization no-ops through the existing
thread-runtime adapter. Its metadata cell aliases the registration services.
These object leaves do not implement semaphore activation, wait or signal.
The four object no-ops and metadata getter do not read their receiver. A null
or unreadable receiver therefore does not fail these selected leaf calls;
operations that actually access runtime storage retain their identity checks.

The cold integration admits the object getter and inline member operations
in sequence, retaining the same physical allocation and suspended original
startup caller. Its registered `_sema` descriptor has offset `+18`, size
twelve and zero constructor/destructor words; reset mode zero selects actual
default copying. Allocator-provided payload contents remain explicitly
prepared test input, not a claim that the original allocator clears memory.

The subsequent activation implementation now completes the manager with
explicit exclusion state and reaches concrete thread getter `8003AF84`;
see [IGB_SEMAPHORE_ACTIVATION.md](IGB_SEMAPHORE_ACTIVATION.md). Those effects
belong to activation, not to these no-op object leaves or inline defaults.

See [VALIDATION.md](VALIDATION.md) for observed boundaries and local evidence,
and [IGB_RUNTIME_OBJECTS.md](IGB_RUNTIME_OBJECTS.md) for runtime ownership.
Primary evidence is the supplied USA DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original artifacts remain local.
