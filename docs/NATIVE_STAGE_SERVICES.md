# Native stage service composition

`make_native_stage_factory` connects the recovered stage caller to three actual
native implementations:

| Stage operation | Implementation |
| --- | --- |
| `load_directory` | `LoaderDirectoryRequestInvocation`, original `8006FD34` |
| `register_info` | `ContextInfoInvocation`, original `8028A7DC` |
| `release_claim` | `LoaderDirectoryReleaseInvocation`, original `80070140` |

Each selected operation retains its original captured loader, filename,
context or info. Suspending within that operation resumes the same invocation.
The adapter publishes a directory result only after the request completes;
the stage caller still distinguishes requesting a directory from consuming
its ready exports on a later update.

The application retains `LoaderRequestServices`. Its `globals` refers to the
same `SystemStageServices` used by the surrounding stage caller, including
the string-pool singleton. The installed factory holds a weak service reference
so `globals -> factory -> request services -> globals` does not form a cycle.
A selected load/release operation then retains the service owner for its own
lifetime. An expired owner at selection is an explicit binding error.
Context info registration does not unnecessarily require loader services.

Other stage operations use the supplied fallback factory. Missing pool lookup,
loader preparation/cleanup or string-pool creation remains pending; the
adapter supplies no successful placeholder implementation. Likewise, native
directory request preserves its concrete IGB decoder and asynchronous queue
boundaries. Parsed assets alone are not a completed runtime scene.

The loader's decoder operation can now be bound through
`make_native_igb_loader_factory`. That adapter enters original load orchestration
and concrete stream/header/early-table helpers, retaining pending runtime type
construction and later lifecycle operations. It does not change the cached-scene
acceptance scope below or establish complete startup. See
[IGB_DECODE.md](IGB_DECODE.md).

## Integration profile

The permanent integration fixture starts with an explicitly prepared cached
directory and canonical exported owners. A real stage request finds it and
acquires one loading claim without registering exports on that update.
On the following update, actual context registration appends the first info,
suspends in the second prepared manager handler, then resumes through the
remaining occurrences. Real claim release reaches zero, removes the directory
from the loader cache, and lets the stage clear its pending directory.

Pool lookup, loader preparation/finish and manager acceptance callbacks are
explicit test endpoints. The fixture does not run the original file decoder,
manager construction or complete startup. It qualifies the connected request,
registration and release operations, including nonreplay and absence of a
service ownership cycle. See [LOADER_DIRECTORY.md](LOADER_DIRECTORY.md),
[LOADER_REQUEST.md](LOADER_REQUEST.md) and [CONTEXT_INFO.md](CONTEXT_INFO.md).
