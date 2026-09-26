# Struct descriptor runtime and prototype

Struct descriptors use the existing native field owner and physical binding.
Three distinct optional words represent alignment at `+34`, guest constructor
address at `+38`, and guest destructor address at `+3C`. They do not alias
reference flags, memory extents, element pointers or method fields. Unknown
words remain distinct from known zero; guest function addresses are data here.

Actual reflected initialization `8006546C -> 800667F4` supplies these members
through the registered struct schema. Its inherited unsigned-short `_size`
default writes halfword `1`; the local signed-integer alignment default writes
word `1`; raw-pointer defaults populate the constructor/destructor words.
Exact-vtable synchronization updates a canonical word only when its complete
physical bytes are initialized. It does not initialize padding or other fields.

## Dynamic operations

Struct vtable `80470E00` has dynamic descriptor operations:

| Operation | Original body | Behavior |
| --- | --- | --- |
| Object `+58` | `80093AA4` | Fresh metadata cell `80561560` |
| Object `+0C/+34/+2C/+24` | Proven BLR entries | No member initialization |
| Field `+5C` | `80093AAC` | Fresh prototype cell `80561564` |
| Field `+64` | `8007496C` | Live unsigned halfword at descriptor `+14` |
| Field `+6C` | `80074A14` | Live full alignment word at descriptor `+34` |
| Field `+8C` | `80063D04` | Actual buffer preparation and optional source copy |

Physical size reads require exactly two bytes; unknown padding at `+16/+17`
must not prevent the original halfword read. Alignment stays a full word until
the layout caller applies its own truncation. Physically bound descriptors use
fresh physical values and retained allocation identity. Canonical-only fixtures
use their represented members; a missing alignment is not assumed to be one.

Actual `80063C74` handles existing buffer capacity, free-before-allocation and
fresh default-pointer publication. Payload allocation alignment remains one;
it is separate from the struct layout alignment. A changed size can require a
larger buffer. Neither struct object BLRs nor a null source proves that newly
allocated default bytes are zero.

The descriptor binding is checked before and after nested buffer callbacks.
The guard travels synchronously through value copying and buffer preparation,
so a swallowed recursive failure, changed binding, or released physical byte
owner stops the operation before further publication or copying. The guard is
borrowed for each resume; nested operations do not retain their parent callback.
Restoring a binding after a fault does not revive that operation.

## Prototype and connected startup

Callback `800247E4` shares canonical `values->struct_prototype` with field
`+5C`. It keeps the original null gate, pool lookup before fresh metadata,
reflected allocation, publication before null test, actual registry append,
fresh packed-reference release, ordinal write and finalization. There is no
invented struct null-only prototype getter.

The retained cold startup constructs the prototype from initially unknown
descriptor bytes and finalizes its one-byte payload. Factory `8002450C` then
constructs the concrete `_sema` descriptor. Its outer semaphore defaults write
size `12`; schema finalization allocates an independent twelve-byte payload.
In this cold path that descriptor previously had a null default pointer, so it
does not resize a one-byte allocation. Focused tests cover resizing separately.

Concrete semaphore metadata now finishes with seven fields and flags `7`.
The cold composition has 27 published types, 92 descriptors, 104 default buffers,
12 prototypes and 96 enabled allocations. Its raw semaphore allocation has a
distinct runtime identity and only its common header is initialized. The next
missing operation is **semaphore object `+58`**, its original metadata getter.
The manager's semaphore pointer and startup singleton remain unpublished.

Continue with the concrete semaphore's reflected initializer, the inline struct
member operations, and actual semaphore initialization. Inline constructor,
destructor, copy and synchronization behavior remain separate from constructing
a descriptor prototype. No scalar fallback implements those operations.

See [IGB_STRUCT_FIELDS.md](IGB_STRUCT_FIELDS.md),
[IGB_PLATFORM_SEMAPHORE.md](IGB_PLATFORM_SEMAPHORE.md) and
[VALIDATION.md](VALIDATION.md). Primary evidence is the supplied USA DOL,
SHA1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`; original artifacts stay local.
