# Synchronous dependency directory requests

`LoaderDependencyInvocation` reconstructs original USA `8006FA6C..8006FD30`.
This is the request used by the dependency table at `800521EC`. It is distinct
from the ordinary request at `8006FD34`, documented in [LOADER_REQUEST.md](LOADER_REQUEST.md).
The implementation is authored C++20 and uses the original executable as
behavioral evidence; no original machine code is part of the native runtime.

## Recovered behavior

The caller performs the actual directory lookup, drops its returned temporary,
and acquires one loading claim when the directory was already cached. The
existing lookup represents host ownership; this caller explicitly accounts for
the original `8006F35C` temporary increment before its own decrement. The full
reference word is incremented/decremented, while final object destruction tests
only its low 23 bits. This preserves the zero and wrapping cases as well as
ordinary cache hits. Loading claims are a separate signed field.

For a cache miss, the caller selects the file factory's pool and then independently
selects each of the three file pool fields. A missing loader pool invokes
`80068430`: its byte load from original owner `+04` is represented by the high
byte of the loader's explicit packed reference word. The subsequent numeric
pool retrieval `8005929C` remains an explicit operation. Its returned null is a
real value: each file field is still released and cleared. The factory argument
is not reused as an assumed fallback for the three later calls.

For every field assignment the incoming packed reference is acquired before the
old field reference is dropped. A final old reference selects `80066E1C`; the
captured incoming owner survives a pause there. Callback-injected replacements
are preserved when the original raw store overwrites them without a release.
`ModelSceneObject::reference_word` and the equivalent loader/directory fields
represent independently mutable original words. Their default value of one
describes a represented constructed owner; it does not implement an original
factory or derive counts from `shared_ptr::use_count()`.

The loader's `+1C` byte only controls whether exact value one copies `+34` into
file `+115`. This caller creates no semaphore and does not enqueue a directory.
It publishes `succeeded=0` and `loading=1`, lazily obtains the original string
pool, interns the captured request pointer, and releases the live old filename.
String counts are full 32-bit counts at character data minus four, with final
container release `8007381C` at exact zero. They do not use the object mask.
The incoming canonical `IgbInternedString` is retained before that release, and
unknown old-string ownership remains pending until explicitly bound.

After publishing the filename and weak parent loader, the caller acquires one
claim, appends to the captured live loader list, and always enters the native
`LoaderFileLoadInvocation` (`80070018`). That helper implements primary prefix,
secondary prefix, and bare filename attempts with the original signed-positive
retry condition. The dependency caller captures its returned directory before
dropping the file factory temporary. A final temporary destructor can suspend
without replaying a decoder call or losing the captured result.

## Continuations and integration

Reached operations capture their arguments, selected factory, and callback.
Missing operations stay pending; completed prefixes are not replayed. A thrown
or recursive callback faults its invocation. Distinct requests may nest on the
same loader during synchronous decoding: there is no invented loader-wide
request lock. The existing claim and concrete file-load continuations retain
their own narrower overlap protection. Abandoned acquired owners are kept with
the loader instead of silently undoing already executed original reference
updates.
This includes selected final object destructors and old string headers whose
callbacks remove the remaining cache or registry holders before suspending.
Retaining their host storage does not increment the already updated original
count or report the destructor complete.

`LoaderDirectoryLookupInvocation` has an explicit tagged comparator overload for
captured scratch pointers. It performs comparisons at the original literal and
physical row read sites. The dependency comparator reads live bytes and preserves
the original equality path's left/right byte and aligned-word read order. A
descriptor-global pause does not freeze the request's later string comparison.
Null stored filenames still skip comparison. Native host strings represent
aligned character allocations with a terminator and up to three padding bytes;
mutable original scratch strings retain their actual initialized-byte bounds.

`make_loader_dependency_string_factory` adapts the canonical string operations
already used by the dependency table, including the concrete interning module
when supplied. `make_native_loader_dependency_factory` routes the table's
`load_dependency` operation into this caller. The decoder services are passed
to the existing `80070018` helper. The composition never constructs an empty
file and reports it as a loaded original document.
The routing factory holds its services weakly to avoid a recursive decoder /
dependency service ownership cycle; each selected operation retains the live
services it actually captured. An expired owner is a reached integration error.

Concrete `8002EBEC` reflected construction and post-defaults, numeric pool
retrieval, string-pool allocation/initialization, final object/container
destructors, and list reserve remain explicit operations unless a separate
verified provider implements them. Existing fields/list capacity do not prove
these providers exist. Reaching a missing provider is the next boundary.

## Validation and limits

Permanent synthetic tests exercise cache temporary reference edges, independent
fallback calls and high-byte captures, self assignment, null fallback clearing,
all relevant asynchronous-byte branches, pending old-object destruction and
callback mutation, full-width string counts, canonical aliasing, late ownership
binding, list reserve mutations, malformed physical slots, signed retry results,
final temporary destruction, live scratch names, nested loads, recursive faults,
and the production adapters. Factory, allocator and decoder bodies in these
fixtures are explicitly prepared endpoints; passing them does not establish
full original startup or deserialization.

The native profile bounds compared filename ranges to 4096 bytes and directory
list indices/capacity to 262144. Reached physical range errors preserve prior
count/reference publications and fault rather than reading unavailable host
memory. Original game data and raw instruction comparison artifacts remain
outside tracked source. Final local and independent comparison results are
recorded in [VALIDATION.md](VALIDATION.md).

Final local validation passed 136 permanent checks in this suite and all 145
core CTest suites. Independent comparison linked to core SHA-256
`5c672e23f1bdda08e6cd86281b6d0c41d2ab7c6892ae578f4d70a40bdd7147c7`
matched 2,225 cases, including 214 reached fault prefixes, while executing
785,153 original instructions at 512 addresses. The oracle executes the actual
request, lookup/`strcmp`, claim, append/growth and `80070018` caller bodies.
Pool resolution, reflection factory, final destruction, string operations,
mutexes, reserve allocation, decoder, and C formatting endpoints are prepared;
this caller comparison does not prove those bodies.

The supplied `ndmw.igb` integration now composes actual dependency-table loading,
the canonical loader getter, native string interning and this synchronous
request. With explicitly prepared startup owners and string arena endpoints,
it reaches the first concrete file factory `8002EBEC` at cursor 5381. That
factory remains pending; no dependency is reported as decoded by this result.
