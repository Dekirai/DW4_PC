# Native directory requests and load attempts

`LoaderDirectoryRequestInvocation` reconstructs the request caller at
`8006FD34`. `LoaderFileLoadInvocation` reconstructs its synchronous load wrapper
at `80070018`. Both are authored C++ and use the canonical loader/directory
owners from [LOADER_DIRECTORY.md](LOADER_DIRECTORY.md). They do not invent a
completed IGB load when the original decoder is unavailable.

Evidence comes from the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Addresses identify inspected original
instructions; no disassembly, original binaries or game payloads are shipped.
The permanent tests use explicitly prepared factory, allocation and decoder
endpoints and establish caller behavior within that scope.

## Request ordering

The original named lookup runs first. A found directory acquires exactly one
loading claim and returns the same owner. Its loading byte, pool fields and
loader configuration are not reinitialized. The numeric acquisition result is
ignored, including the semaphore failure result that leaves the claim unchanged.

On a miss, the native caller retains these original operations in order:

1. Call the actual `igIGBFile` factory with the captured loader `+38` pool.
   After it returns, read live loader `+44`, `+48` and `+08` into the file's
   `+24`, `+A4` and `+41` fields, in that order.
2. Read the three live loader pools at `+38`, `+3C`, `+40`. Each nonnull pool
   replaces the corresponding file pool at `+118`, `+11C`, `+120`; a null
   loader pool preserves the factory's field. Incoming references are acquired
   before old-field releases. A reached unrepresented final destructor faults
   after preserving that acquired reference.
3. Only loader byte `+1C == 1` copies `+34` to file `+115` and ensures the
   loading semaphore through `80043F4C`. An existing semaphore skips its
   factory/initialization. Otherwise the factory result is published at file
   `+44` before its initialization virtual `+5C`. A replacement published by
   the factory callback is retained when the original raw store overwrites it.
4. Store file success byte `+31 = 0`, then loading byte `+30 = 1`. Ensure the
   shared string-pool singleton, intern the captured request filename, replace
   the file name, and publish the nonowning loader parent.
5. Acquire the first loading claim, append the file to the live loader directory
   list, and only then reload loader byte `+1C`.
6. A zero byte calls `80070018`. Any nonzero byte acquires a second loading claim
   and invokes the captured queue virtual `+68(file, true)`. Thus byte `2` or
   `255` queues without performing the earlier exact-one semaphore preparation.

The original field name for directory `+44` is `_loadingSemaphore`. The shared
`LoaderDirectoryMutex` type represents its recovered acquire/release virtual
calls; it is not a claim to have implemented an OS semaphore or the async worker.
The original `_loadedRefCount` at `+34` is independent of object references.

`LoaderRequestServices::globals` points to the same `SystemStageServices` state
used by the outer stage caller, so `string_pool` is one canonical singleton.
Lazy creation publishes its returned pointer, including null, into the live
singleton. Nonnull filename interning remains its own reached operation. The
public request first performs the original filename lookup, which rejects a
null request filename before a new-file path can reach interning.

## List publication and continuation

The append caller `8006F404` reaches `80069128`. For signed `count < capacity`,
that helper publishes the wrapped 32-bit `count + 1` before reading physical
backing and writing the captured old index. A malformed negative index, null
backing or short allocation therefore faults with the count already changed.
The raw store does not release a preexisting spare-slot owner; native retained
storage preserves that reference.

On the growth path, `80041970` captures the old count, then `80041660` computes
capacity from at least four elements, doubling below `0x400` and adding `0x400`
afterward. The actual metadata/pool allocator `8004155C` is a named operation
boundary. Once it completes, the caller publishes the requested count and
reloads physical backing for the captured append index. The native profile
bounds physical slots and requested capacity to 262,144. It does not silently
substitute `vector::resize` for original metadata/allocator side effects; only
prepared tests implement that allocation endpoint this way.

Each selected call retains its argument owners, factory, operation and callback
while pending. Replacing a service binding, prefix, queue, list or pool after
selection cannot redirect that call. Later original read sites still observe
live fields. Completed calls do not replay. Recursive, failed, overlapping or
abandoned requests cannot silently restart an already published prefix. A
pending request retains its file with the loader if its invocation is abandoned.
These are synchronous continuation guarantees, not a host threading contract.

## Synchronous path attempts

`80070018` immediately returns null for a null directory without reading the
loader. Otherwise it tries, in order:

- A nonnull, nonempty primary prefix (`loader +14`) with `"%s/%s"`.
- If the first decoder result was not positive as a signed 32-bit integer, a
  nonnull, nonempty secondary prefix (`loader +18`) with the same format.
- If neither prefixed attempt returned positive, the current bare filename.

Each attempted call selects the concrete `igIGBFile` virtual `+74` afresh.
A pending attempt retains its exact formatted path and selected operation;
subsequent attempts reload the live prefix and filename. The final bare decoder
result is ignored, and the wrapper returns its captured directory even on that
failure result. Returning a directory therefore does not imply loaded content.

Prefix formatting preserves original C-string truncation at embedded NUL bytes,
forward slashes and empty-prefix skipping. It does not normalize host paths.
The represented original formatting buffer is 2,048 bytes; oversized output and
a null `%s` filename fail before decoder dispatch instead of overflowing host
memory. Those malformed-input checks are native safety boundaries, not claims
about the original library's undefined access behavior.

## Concrete factory and decoder boundary

The factory is not inferred from a temporary layout probe. The inspected path
is `8002EBEC -> 8002F178/8002F1A0 -> 8006546C`, registering exact type `igIGBFile`,
size `0x134`, with vtable `804724C4`. Its post-default callback at virtual `+34`
is `80051698`; it initializes all three file pools via separate `80058C80`
calls. Merely zero-initializing a `LoaderRequestFile` is not that original
factory. The native struct represents reached fields; a missing concrete
factory remains pending with no file publication.

The concrete load virtual `+74` is `800528DC`. Its beginning obtains the original
float timer sample, clears success, sets loading, clears file `+38`, opens the
stream through `8004F870`, and reads its header through `8004F974`. Its subsequent
version/flag-dependent pipeline includes type metadata, references, object and
memory reconstruction, external data, post-load dispatch and cleanup. Reached
calls include `800514AC`, `80051794`, `8004FBE8`, `8004FC94`, `800505E0`,
`800521EC`, `80052554`, `800509D4`, `80050C68`, `80050E80`/`80050F58`,
`800520E8`, `80051AE0`, virtual `+84 = 80053218`, `80051CB0`, `80051450`,
`800527D8`, optional virtual `+78 = 80052E78`, `80051E04` and `80051E30`.
Individual status checks branch to failure cleanup `80052794`; the call order
also differs by file version. This list identifies the recovery boundary and
is not an implementation or proof of those callees.

Only the success tail at `80052DBC..80052DC4` sets success `+31 = 1` and loading
`+30 = 0`. Request and path wrappers never make those stores on behalf of a
missing decoder. The exact `decode_igb` service identifies this concrete body;
its absence leaves the invocation pending and its current loading prefix intact.

Existing authored components can support that next implementation:

- `MountedResources::read` resolves a validated exact logical resource path to
  the supplied archive payload, with read bounds. Original stream-opening path
  rules and status mapping still need connection before using it as the stream
  endpoint.
- `read_igb_document` reconstructs the validated v6 serialized graph, including
  object/memory references, external-directory names and `info_list_reference`.
  Parsing those bytes alone does not execute original engine constructors,
  external-directory loads or post-load callbacks.
- One `ActorSceneResource` per actual loaded occurrence provides canonical node
  and model object identity. Its shared `ModelSceneObjects` registry can produce
  the ordered original info owners from the document's info list, preserving
  aliases across exports. Unknown native object lifecycles and initialization
  callbacks must remain explicit until recovered.
- The native stage adapter can already route a real directory request, info
  registration and claim release through these caller modules. It does not
  supply missing stream, factory, queue, post-load or decoder behavior.

The next decoder increment should connect those components at proven original
boundaries, retaining the actual document/resource owner through directory
exports, and qualify its status and cleanup paths against original instructions.
It must not equate successful document parsing with the decoder's success tail.

## Validation

The permanent asset-free suite passes **128 checks**. It covers exact cache
identity, ignored failed claim results, live pool fields and preserved factory
defaults, exact-one async setup, later nonzero async selection, retained factory,
queue and decoder continuations, captured versus reloaded paths, signed decoder
result edges, list growth/captured index and count-first malformed failures,
final-pool release preservation, callback-injected semaphore ownership, null
factory/queue errors, reentry and abandonment.

An ignored raw-instruction corpus separately executes **3,072 cases** across
**408,082 original instructions** at **340 distinct instruction addresses**.
It covers the actual request and path wrappers, actual claim acquisition,
semaphore preparation and fast append callers. Lookup, concrete factories,
semaphore virtuals, filename interning, C formatting, queue execution and the
actual decoder are explicitly prepared endpoints. This corpus uses valid
physical backing, an existing string pool and nonfinal object references;
permanent native tests cover the additional reached failure/pending cases.
The final native driver comparison reports **zero differences** in all 3,072
cases. This validates the caller/control/publication profile with those prepared
endpoints, not their internal behavior or a completed original file load.

- Final core SHA-256:
  `65c56346284a5dac62019929c029f91b0bef6fee011e9f811e1ecf8f0d1585b9`.
- Driver SHA-256:
  `62f8f12c90b143ff9c48bfe2101cd9ca20f635592bc3172fffbf671169f23032`.

Independent read-only reviews checked the original request/path instruction
order, live field reloads and signed result classification. They also identified
the two ownership prefixes now covered by permanent regressions: preserving the
incoming pool reference when the old-field release cannot proceed, and retaining
a callback-injected semaphore overwritten by the original raw factory store.
