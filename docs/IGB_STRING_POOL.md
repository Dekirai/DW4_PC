# Native IGB string interning

`IgbStringInternInvocation` reconstructs original `80054094` with lookup wrapper
`80073164`, ordered search `80073448`, insertion wrapper `80073204` and sorted
insertion `80073574`. The native implementation preserves the original table
layout and operations; it does not replace them with an unordered string cache.

## Representation and ownership

An explicitly bound string-pool identity owns a table at `+00` and an arena at
`+04`. The table has independent unsigned count `+04`, search step `+08`, capacity
`+0C` and physical backing `+10`. Logical counts do not prove physical storage.
Each entry has a header owner and a captured character allocation. Header fields
are the container pointer at characters minus eight and a full 32-bit reference
word at characters minus four. These counts are independent of host references.

`IgbInternedString::native_name` retains authoritative physical bytes. Its `value`
is a stable host identity/cache made from bytes already copied, not an extra
validation scan. In particular, a changed source can lose its terminator after
the original length measurement; an insertion into an empty table still does
not invent another scan. Later native reads inspect the physical allocation and
can fail at the actually reached missing byte. The dependency restore operation
uses this physical name when available. The interning adapter maps registered
host identities back to their physical names before the selected operation.

## Lookup and insertion order

A null input pointer returns null before dereferencing the pool or reading a
global. A nonnull input captures the pool's table and enters the lookup wrapper.
The wrapper reads mutex global `805622F4`, optionally acquires its `+84` operation
with the original true argument, performs the search and reloads that global
for optional `+7C` release. Mutex return values are ignored here. A selected
pending callback retains its mutex and arguments; later release can select a
different live global owner.

Search starts with the table's captured step and lower bound zero. Each iteration
tries lower plus step, skips candidates beyond the live count, compares against
the physical header at candidate minus one, and halves the step. A match
increments the full reference word with unsigned wrapping. A miss returns the
insertion position. Comparisons retain original `strcmp` byte order, alignment,
word loads beyond a terminator and unsigned ordering. Empty searches consume no
input characters, even when the captured input pointer is invalid.

On a miss, interning measures the live input with original byte-based length,
adds one, reloads the pool's arena, calls `80073738` and copies exactly that
captured length. It then reloads the table, takes the insertion lock and writes
container/reference-one into the new header. A second search determines the
insertion position. If another operation inserted an equal string between the
two locks, that second search still increments the existing header and the
original inserts the new header as well; native code preserves both effects.
Separate invocations can nest during unlocked allocation. Recursing into the
same unfinished invocation invalidates it.

When count equals capacity, the original requests `capacity * 8` bytes from
`8005624C`, publishes its returned backing and then publishes doubled capacity.
It moves the live suffix, writes the new header, increments the live count and
updates the search step. A null allocation is published before a reached
physical failure. No successful reserve or rollback is invented.

## Explicit implementation boundaries

`make_native_igb_string_factory` binds the dependency caller's `intern_name`
operation and canonical ownership registry. It composes with the loader getter
and synchronous dependency-request adapters. It does not implement string-pool
allocation `80054140`, initialization `80053F28`, arena allocation `80073738`,
physical realloc `8005624C`, final container release `8007381C`, mutex creation
or global startup. Those operations remain explicit services.

Character copies within the same scratch allocation use original `800031E8`
forward/backward ordering based on offsets. Distinct native allocations have
no comparable original addresses, so that path requires complete nonoverlapping
source/destination ranges before copying. It does not claim original partial
fault ordering for invalid ranges between different allocations. Name scans are
bounded to 1 MiB and physical table operations to 262,144 slots.

Permanent tests cover sorted insertion, cache identity, empty names, case,
reference wrapping, alignment and short physical ranges, live pointer changes,
pending/mutated mutexes, second-search duplicates, null results, raw growth
publication, unterminated copied names, overlapping copies and recursive or
abandoned continuations. Prepared arena/realloc endpoints are identified in the
tests. Matching these operations does not establish complete resource loading
or native game startup.

Final local validation passed all 145 permanent string-pool checks and all
145 core CTest suites. An independent final-core comparison found zero
differences in 1,104 cases across 375,246 original instructions at 334 addresses.
The comparison executes actual interning, both table searches, insertion,
`strcmp`, `strlen`, byte copy and bounded `memmove`; arena allocation, realloc
and mutex endpoints are explicitly prepared. It includes mutable names,
unterminated copied payloads, insertion-lock mutations and reached endpoint
failure prefixes. Pool construction, final release and partial malformed copies
across distinct host allocations remain outside that evidence.
