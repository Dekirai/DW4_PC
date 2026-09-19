# Native reflected object initialization

`IgbObjectInitInvocation` reconstructs USA `800667F4`, allocation-base helper
`80068184`, field passes `80066F40`, `80066FF8`, `8006712C`, and default-mode
dispatcher `80063DD0`. It initializes an explicitly represented allocation and
typed file binding. The binding describes storage, not a successfully
constructed original object. Concrete field bodies remain separate operations.

## Header and allocation ownership

The caller captures ArkCore global `80562100`, reads the input metadata's
`+5C` word, then reads the captured core's `+394` offset. It writes that word
at the selected object offset before clearing and setting the packed reference
word to one. Subsequent virtual selection reads the physical object vtable;
it does not assume that the dynamic publication offset was zero.

The allocation-base helper calls fresh virtual `+58`, reads the returned
metadata's `+20`, and subtracts it from the object's original numeric address
with 32-bit wrapping arithmetic. Pool-owner lookup `800590A0` remains explicit.
A null pool writes zero to object byte `+04`; otherwise the captured pool's
virtual `+16C` supplies that byte from its low eight bits.

Physical storage is authoritative during construction. The public
`igb_object_set_reference_word` helper updates both its big-endian word and the
typed file's packed-reference view. Bound construction callbacks use this helper
for reference changes. The pool-byte store preserves the physical lower bytes
and refreshes the typed view, including changes made by a raw evidence provider.
After construction, native loader services use the returned file's canonical
`reference_word`; the allocation image is not a second mutable game object.

## Field passes and virtual defaults

Each pass obtains fresh metadata through object virtual `+58`. The first pass
skips only when the metadata is null or lacks class flag four, and the fresh
ArkCore has nonzero byte `+14` with zero byte `+00`. A null metadata result that
does not meet that skip condition faults at its reached field-list access.

For an executed pass, the code captures base `igObject` metadata global
`805622A4`, the derived field list, the base field list, the derived count, then
the base count in original order. The base count is the signed first index;
the derived count is the signed limit. Both counts and the derived list owner
remain captured. Physical field backing is read again for every iteration.

1. The first pass calls each field's `+84`.
2. The second calls each field's `+7C`, then object `+0C(true)` even for an empty
   field range.
3. The third calls field `+F0`. When the returned low byte is zero, it reads
   the same captured field's signed mode byte `+24`: zero calls `+70`, two
   calls `+78`, three calls `+74`, and other modes do nothing. It then calls
   object `+34(true)`, also for an empty range.

The tail calls object `+2C`, then `+24(false)`, then a fresh `+58`. Nonnull
returned metadata has its live `+2C` instance count incremented with unsigned
wrapping. Only afterwards is its `+30` instance list read. A nonnull list
selects the explicit `80053BC0` append operation.

## Suspension, boundaries and evidence

Unknown metadata fields, virtual implementations and allocation owners suspend
at their actual read or call. Represented null dereferences and invalid physical
ranges fail. Selected pending callbacks and owners remain captured, so retries
do not repeat header writes, prior fields or the instance-count increment.
Completed callbacks are released before subsequent live vtable reads. Callback
exceptions, recursion and abandoned partial initialization invalidate the
allocation rather than reporting a usable file.

This native profile bounds physical field indices and per-pass visits to
262,144, and validates an entire four-byte write before publishing it. These
are host representation guards, not claims about original malformed memory.
The current typed result is a `LoaderRequestFile`; this does not establish
factories for all runtime object classes.

Permanent tests exercise gate combinations, all signed-mode categories, F0
low-byte results, fresh metadata, captured counts with live backing, callback
teardown mutations, header mutations, wrapping pool/count arithmetic, pending
append, missing/null state, physical limits, abandonment and recursion.
Independent ignored evidence executes the original functions with explicit
registration, allocator and field-virtual endpoints. Prepared empty-field
metadata proves caller composition, not the actual file's reflected defaults.

The actual file virtual bindings and post-default pool callback are described
in [IGB_FILE_FACTORY.md](IGB_FILE_FACTORY.md) and
[IGB_FILE_POOLS.md](IGB_FILE_POOLS.md). Full original class registration,
field-default implementations and pool allocation remain required before the
original dependency file can finish construction. See
[VALIDATION.md](VALIDATION.md) for final local test and comparison results.
