# Original resource-loader getter

`LoaderGlobalInvocation` implements the complete three-instruction getter at
`8007042C`: read global `80562100`, read the captured owner's field `+40`, then
return that pointer. It performs no allocation, virtual call, null check,
loader initialization or original object-reference modification.

## Canonical owner and separate direct global

Global `80562100` points to `igArkCore`. Its reached native fields already live
in `IgbRuntimeTypeRegistry`: enabled byte `+00` and runtime type list `+18`.
The loader field is added to that same canonical owner. Creating another
independent object to represent this global would split runtime registration
and resource loading into inconsistent states.

The direct loader global `80562104`, represented by `SystemStageServices::loader`,
is a different storage location. Original system-stage operations read that
direct global; `8007042C` does not. During `initCore`, the original calls resource
factory `8002648C` at `8003D7A8`, publishes its result into the core's `+40` at
`8003D7AC`, reloads that field, and copies it to `80562104` at `8003D7B4`.
Both locations initially receive the same identity through this explicit
publication sequence. The native getter preserves their separate read sites.

Factory `8002648C` registers through `80026828 -> 80026850` and constructs the
metadata cached at `80561610` using `8006546C`. That registration names the
class `igResource` and records size `0x4C`. The getter does not implement this
factory or complete the surrounding startup sequence.

The `igArkCore` constructor at `8003D234` does not initialize `+40`. Its allocation
wrapper `8003E974` calls original `malloc` at `8009281C`. The native metadata
bootstrap therefore leaves `loader` disengaged rather than manufacturing a
represented null field. A later explicitly represented publication may set it
to a valid loader or an engaged null pointer.

## Availability and continuation

An absent canonical services object or disengaged `singleton` suspends before
the first global read. An engaged null core pointer faults at the reached
`+40` dereference. A present core with an unrepresented `loader` field suspends
after capturing that core. Replacing the global while this field is pending
does not replace the captured owner. Once the field is represented, its pointer
is returned, including null. A completed invocation keeps its returned value;
a new invocation performs fresh reads. Faulted invocations cannot restart.

No metadata enable flag, type-list backing, string pool or loader field is
dereferenced by this getter. Host references retain represented objects across
native continuation boundaries without introducing original retain/release
instructions. Unavailable native state is a recovery boundary; it is distinct
from the original null pointer value.

`make_native_loader_globals_factory(canonical_services, remaining)` binds only
`IgbDependenciesServiceKind::loader`. It composes with the dependency factory
without replacing its existing `SystemStageServices` string-pool global or the
separate direct loader field. An unavailable getter remains pending rather than
delegating to a successful substitute. Other operations pass to `remaining`.

## Validation

Permanent tests cover shared owner identity, independence from `80562104`,
unavailable globals and fields, represented null core versus null loader,
captured-owner continuation, completed snapshots, fresh subsequent reads,
fault poisoning and dependency-factory composition. Independent original
instruction evidence is retained in ignored `work/native-loader-globals-*`
artifacts. The getter proof does not qualify construction, registration,
dependency decoding or full startup.

The final production core matches 128 original-instruction getter cases,
including 10 reached null-core faults, with zero differences. The raw getter
executes 374 instructions across its three addresses and checks its exact
two-read/no-write profile. Separate raw-only checks execute the constructor
and the later publication prefix for four initial words each. Those checks
establish the untouched constructor field and ordered stores; they do not
substitute prepared metadata or allocator endpoints for complete startup.

The supplied USA executable has SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Public files contain authored source
and synthetic fixtures, with no copied instruction words or game assets.
