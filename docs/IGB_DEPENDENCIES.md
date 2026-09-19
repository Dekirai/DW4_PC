# Native IGB dependency-directory table

`IgbDependenciesInvocation` reconstructs original USA caller `800521EC`. It
reads the dependency table, temporarily changes the loader's base path, issues
each dependency request, appends the returned directory owners, frees the table
allocation and restores the base path in the original order.

Evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original bytes and disassembly remain
outside the public repository. The caller uses the existing concrete byte
stream and endian helpers, rather than treating a parsed list of filenames as
completed dependency loading.

## Table and loader state

The file's byte `+D5` gates the whole helper. Zero immediately reaches the live
success word without reading the stream, path or loader. Any nonzero value
reads a twelve-byte header as one element. A short header returns failure
before publishing table fields.

The header is conditionally converted using the actual integer-field width.
The caller publishes `body_bytes = header[0] - 12` at `+E4` with 32-bit wrapping,
captures the signed count from the third header word, then allocates through
the selected file pool's virtual `+DC`, with alignment 16. The allocation is
published directly at `+E0`, without an intervening null-result check. Its body
read reloads the live stream, `+E0` and `+E4`. A short body returns failure with
the allocation still published; it does not free the body or change paths.

After a successful read, the caller captures the body allocation, conditionally
converts the first captured-count length words, and computes the first name
pointer after that length array. The count stays captured for the dependency
loop. Signed nonpositive counts skip names, while still executing the reached
loader/path transactions and final free.

`IgbDependenciesState` owns body `+E0`, byte size `+E4`, saved base-path string
`+E8`, and the directory's dependency list `+18`. The file's existing weak
`loader` field remains the single canonical representation of `+20`; the
dependency module does not create a competing loader field. The mutable path
at `+C8` is shared through `IgbLoadingPathsState::directory_path`.

A null file loader reaches the actual getter `8007042C` and publishes its
result. Getter setup is explicit, including the possibility of a null result;
the caller does not manufacture an initialized loader. Expired represented
nonnull loader owners fail at their reached use.

## Interned strings and four transactions

The original interned string has a container pointer at data minus eight and
a full 32-bit reference count at data minus four. `IgbInternedString` represents
these separately from the host string allocation. `IgbInternedStringRegistry`
maps the existing canonical `SystemStageString` allocation to that explicit
reference owner, keeping the loader's existing path consumers compatible.
Equal text in a different allocation is not interchangeable ownership.

Intern operations use the existing `SystemStageServices::string_pool` singleton
`80562140`. When its captured null-test word is zero, the caller reaches
allocation `80054140(16)`, conditionally initializes a nonnull result through
`80053F28`, and publishes the result. It reloads the live singleton for the
subsequent intern operation. Creation is not silently completed by constructing
a host string or arbitrary pool object.

The caller performs four distinct transactions:

1. Capture the current loader prefix, intern it and replace saved field `+E8`.
2. Capture the file's directory-path pointer and current loader, intern that
   path, then replace the captured loader's live prefix.
3. After dependency loading and body free, capture the then-current loader and
   saved prefix, intern the saved value and replace that loader's live prefix.
4. Intern a null pointer, then release and replace the live saved field `+E8`.

Each incoming intern result precedes the old-field read and decrement. A
nonnull old string's full count is decremented with unsigned wrapping; only
the complete word becoming zero selects `8007381C`. The low-23-bit object rule
does not apply to strings. The final container-release operation can pause or
fail after the decrement and before publication. Unknown old-string ownership
pauses at that reached decrement, preserving the captured old value and the
already acquired incoming owner; it never invents a reference count.

The name-source variant distinguishes a null pointer, an existing canonical
string and a lazy scratch-buffer pointer. Capturing a scratch pointer does not
read its bytes. The explicit intern/load endpoint uses `igb_dependency_name`
only when it consumes the name. This retains byte changes made while a reached
operation is pending and avoids validating unread suffixes prematurely.

## Requests, append and restoration

Each loop iteration captures the live file loader and the current name pointer
for synchronous request `8006FA6C`. This is distinct from the previously
implemented `8006FD34` request path; its unrecovered factory/load behavior
remains an explicit named operation. A parsed filename or a null result is not
replaced with a fabricated loaded directory.

After the request returns, the caller loads the live dependency list and
executes the original append. It increments a nonnull incoming directory's
full packed object reference word before accessing the list. The fast path
publishes the new logical count before reading backing and storing. The growth
path preserves the captured index/count, calls the actual allocator boundary,
then overwrites any allocator count mutation and reloads physical backing.
Original raw stores do not release overwritten spare entries, so native state
retains those owners. A returned null directory is appended as null.

Only after append does the caller read the current physical length word and
advance the name pointer. An unavailable length can therefore leave an already
retained and published directory entry intact. The selected lengths allocation
stays captured even if callbacks replace live `+E0`.

After the loop, the caller reloads the live pool and live `+E0` for virtual
`+108`. It clears `+E0` only after that operation returns. It then reloads the
file loader before restoring its prefix. If that loader is null, restoration
and saved-prefix clearing are skipped, as in the original. No exception or
short-read path invents a transactional rollback that the original lacked.

## Native bindings, continuations and validation

`make_native_igb_dependency_pool_factory` provides concrete allocation/free
only for identities explicitly registered by `igb_bind_native_scratch_pool`.
New bytes start unknown and become initialized through actual byte reads.
Free invalidates the captured allocation for later alias reads. Unbound pools
delegate or remain pending. The loader getter, string-pool allocation/init,
interning, final string release, synchronous dependency request and list
allocation are separately selected operations.

Pending operations retain their factory, callback and argument owners.
Completed operations do not replay. Initial singleton null-test decisions and
earlier loader captures survive missing-binding pauses, while subsequent
original reads remain live. Recursive use, malformed state or abandonment
faults the shared dependency prefix and preserves acquired references rather
than permitting an automatic restart.

Permanent tests exercise both byte orders, ordered and repeated names, exact
prefix identity restoration, all four string transactions, lazy pool setup,
full-word reference underflow/high bits, short header/body prefixes, nonpositive
counts, pending loader/name captures, live loader/list replacement, delayed
ownership/status binding, count-first append failures and post-append malformed
lengths. The native profile bounds allocations to 64 MiB, physical directory
visits to 262,144 and consumed scratch C strings to 4,096 bytes. These are host
bounds, not claims that malformed original accesses were safe.

Prepared intern/loader endpoints in synthetic and instruction-comparison tests
do not establish their original bodies or complete game startup. Current
suite counts, comparison scope and original-data stopping points are recorded
in [VALIDATION.md](VALIDATION.md).

Final validation passed 179 permanent dependency checks and all 142 core CTest
suites. An independent final-core comparison found zero differences in 1,103
cases, including 104 matching reached faults, across 154,934 original
instructions and 311 addresses. It executes the actual `800521EC`, byte-swap,
object-append and growth bodies. Stream/pool operations, loader getter,
string-pool creation/intern/release, dependency requests and list reserve are
explicit prepared endpoints in that comparison. Their production bodies and
complete dependency loading are not implied by the matching caller results.
