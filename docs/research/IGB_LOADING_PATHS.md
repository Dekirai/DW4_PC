# Native IGB directory paths and pool-name tables

`IgbDirectoryPathInvocation` implements original `800505E0`;
`IgbPoolNamesInvocation` implements `80052554`. Both use the canonical file,
stream and status words. `IgbLoadingPathsState` owns the mutable directory path
at file `+C8` and numeric pool-handle list at `+130`.

The supplied USA executable has SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. The previous provisional description
of `800505E0` as type/field preparation was incorrect: its body duplicates the
stream filename and removes the final path component. The public decoder stage
is now named `directory_path`. This naming correction does not change the
original decoder call order.

## Directory path

The original captures the current file pool, stream and stream filename before
calling pool virtual `+F0`. It publishes the returned allocation directly at
`+C8` without releasing the previous allocation. The native state retains that
otherwise overwritten owner separately; host lifetime does not synthesize a
missing original free.

After publication the routine measures the duplicate C string and scans
backwards for the last slash or backslash. It replaces that separator with NUL.
Without a separator it writes NUL at byte zero, including for an empty input.
A null duplicate is published before the reached length read faults. The helper
does not consume file bytes. Its final success word is a live global read;
missing status state can suspend after the path stores without replaying them.

## Memory-pool names

The table begins with two 32-bit words: total table bytes and signed name count.
The caller reads the eight-byte header as one element and, when requested,
swaps both words through the file's integer converter. The selected pool
allocates the total byte count, including the header, although the following
one-element read requests only total minus eight payload bytes.

A short header returns failure without allocation or list changes. A null
allocation fails before payload reading. A short payload frees the captured
allocation through the current file pool, then reads the live failure word.
An existing `+130` list has only its count cleared; capacity and physical words
remain intact. A null list reaches the actual `8003657C` constructor, whose
creator reference is published directly.

Each name reaches the actual memory-manager selection and concrete lookup
described in [IGB_MEMORY_POOLS.md](IGB_MEMORY_POOLS.md). The result is a raw
32-bit index or `FFFFFFFF`, not a pool pointer. The caller appends that result,
then measures the original live name to advance. After the signed name loop,
it appends the current default handle until at least 46 entries exist. More
than 46 supplied names are not truncated.

Fast append publishes count before the backing store. Growth preserves the
captured list and index, calls the allocator, publishes the captured requested
count over allocator mutations, then reloads backing. Each new append reloads
the file's list. Padding reads global `8055D89C` after possible allocation, so
an allocator callback can change the value stored. The final free selects the
live file pool before reading the success word.

## Native storage and remaining boundaries

`make_native_igb_path_pool_factory` supplies native duplicate/allocate/free
operations only for explicitly bound scratch-pool identities. A missing binding
remains pending; a represented null pool faults at the reached virtual access.
This is the same explicit native storage profile used by the stream helpers,
not a claim that the original general-purpose allocator is reconstructed.
The pool-list constructor and reserve allocation remain distinct services.

Selected callbacks retain their arguments across suspension. Native exceptions,
recursive entry and abandonment invalidate their operation state, preserving
published counts, bytes and allocations instead of replaying a partial load.
The host profile bounds allocations to 64 MiB, path length to 1 MiB, and pool
iterations/list slots to 262,144. Unknown or released scratch bytes fail at
the actual read. These limits do not assert malformed original input was safe.

Permanent tests cover mixed separators, embedded terminators, captured pending
pool/name inputs, raw path replacement, both byte orders, known/unknown/empty
names, zero and negative counts, more than 46 names, list reuse, growth-time
list/default mutations, status suspension, short reads and failure prefixes.
Original-data tests separately qualify the 46-name table in supplied `ndmw.igb`;
that table alone does not complete dependency loading or native game startup.

Final local validation passed 62 synthetic / 116 original-inclusive checks and
all 142 core suites without compiler warnings. The original pool table maps all
46 names to indices 0..45 and advances from offset5,381 to5,717. An independent
final-core caller comparison matched all 1,392 cases, including 78 reached
faults, across 570,062 original instructions and 261 addresses. Its stream,
allocator, list-constructor and pool-lookup endpoints are prepared explicitly;
the concrete memory-pool lookup has separate original-instruction validation.
