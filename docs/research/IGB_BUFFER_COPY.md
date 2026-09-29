# Native allocation-buffer helpers

`IgbBufferCopyInvocation` reconstructs the USA executable's buffer-size helper
`8005641C`, buffer-free caller `80056508` and buffer-copy caller `8005647C`.
It includes the caller behavior of `8005645C`, `8005609C` and `800560C8` and
the directional byte loop `800031E8`. These are allocation-buffer operations;
they do not construct a field descriptor or replace reflected object creation.

The original evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. The implementation and permanent
tests contain authored C++ and synthetic data. Original binaries, generated
instruction listings and comparison corpora remain local and ignored.

## Original calls and return values

The size operation captures its source pointer, invokes allocation-owner lookup
`800590A0`, and calls the returned pool's virtual `+150` with that same pointer.
It returns the full 32-bit size. The free operation performs one owner lookup
through `8005645C`, then invokes pool virtual `+108` with its captured pointer.
It preserves that virtual's return value. The free caller does not itself clear
a descriptor member or mark a host buffer released; the bound allocator leaf
supplies the actual free effect.

Neither caller has a source-null guard. Owner lookup and pool methods remain
explicit operations and receive the original pointer, including zero. A null
pool faults when its virtual dispatch is reached. Missing services remain
pending and cannot supply an invented size, pool or completed free.

Buffer copying retains this exact sequence:

| Step | Original operation | Captured arguments/result |
| --- | --- | --- |
| 1 | `8005645C -> 800590A0` | Source pointer -> allocation pool A |
| 2 | `8005641C -> 800590A0` | Same source pointer -> size-query pool B |
| 3 | Pool B virtual `+150` | Same source pointer -> size A |
| 4 | `8005609C -> 800560C8`, pool A virtual `+D8` | Size A -> destination pointer |
| 5 | `8005641C -> 800590A0` | Same source pointer -> size-query pool C |
| 6 | Pool C virtual `+150` | Same source pointer -> size B |
| 7 | `800031E8` | Captured destination, captured source, size B |

The three pool lookups are independent, and their results may differ. The
allocation uses size A; the byte loop uses size B without truncating it to the
allocation request. The `+D8` endpoint receives only its pool and byte count,
not an invented source-pointer argument. This is distinct from aligned/default
allocation `+E0` and array/reallocation services.

The copy helper does not test the allocation result for null before the second
size query or byte loop. Zero size performs no byte or mapping access, so a null
allocation can be returned when size B is zero. Positive sizes preserve the
original read/write prefix before a reached null or unavailable-byte fault.

## Live storage and continuation state

`IgbRuntimeFieldDefaultSource` keeps the host buffer and its physical offset;
the separate 32-bit address represents the original pointer. An absent host
buffer for a nonzero address can be supplied later through
`IgbBufferServices::values->buffers`. Resolution preserves arithmetic identity
and rejects conflicting mappings. It does not read bytes, reset initialized
state or revive a released allocation. A known buffer remains captured even if
the caller later replaces its own member binding.

The byte loop chooses direction using the original unsigned pointer comparison:
source below destination copies backward, otherwise forward. Each byte is read
immediately before its destination store. Source contents are not snapshotted
before pool lookup, allocation or the second size callback. Those callbacks,
including their completed-operation teardown, can change the bytes that the
copy subsequently reads.

Guest addresses wrap in 32 bits. Host offsets remain separately bounded by the
represented allocation. Reached zero byte addresses, released storage,
uninitialized source bytes and unavailable destination bytes fault at the
corresponding access. Valid preceding writes remain visible. There is no blanket
size cutoff that erases those writes. Self-aliasing and overlapping ranges use
the same directional loop, with no shortcut around the required pool calls.

Pending operations retain the selected callback and captured arguments. A byte
already loaded before an unknown destination mapping stays captured across
that suspension; later source bytes remain live. Completed operations are
released before the next original read, and faults during teardown invalidate
the invocation. Recursive entry faults. Completed calls are idempotent;
unfinished or faulted calls retain their captured buffer/pool owners in the
service's unfinished storage without inventing guest reference changes.

## Focused validation and boundaries

`igb_buffer_copy_tests.cpp` covers the exact three-owner/two-size sequence,
distinct pools, unequal sizes, source changes during allocation, nonzero host
offsets, zero/null results, late bindings and captured bytes at pending stores.
It also covers overlap, wrapped addresses, physical fault prefixes, large size
results, callback failures, operation teardown, recursive invalidation,
idempotence and allocated-buffer retention on abandonment.

The pool lookup, size, allocation and free leaves in these tests are explicit
synthetic services. The module does not recover the pool manager, concrete
allocator bodies or a whole-game address space. It composes the reached
memory-member caller without supplying a finished cloned field; element
destruction and recursive memory/object copying belong to that caller's own
operations. See [IGB_FIELD_MEMBER_COPY.md](IGB_FIELD_MEMBER_COPY.md) for the
surrounding reflected field-copy boundary and [VALIDATION.md](VALIDATION.md)
for measured build, original-comparison and cold-composition results.
