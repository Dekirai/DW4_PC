# Native reflected file allocation

`IgbFileFactoryInvocation` implements original USA `8002EBEC`, and
`IgbReflectedFactoryInvocation` implements its allocation caller `8006546C`.
They compose with the actual `IgbObjectInitInvocation` (`800667F4`) rather than
returning an empty `LoaderRequestFile` as an assumed completed factory.

## Wrapper and metadata selection

The file wrapper captures its incoming pool, calls `8002F178`, then reloads the
canonical `igIGBFile` metadata global `80561A04`. The registration wrapper
enters `80066188(8002F1A0)`: it invokes that callback, and if a callback list is
returned, calls its captured logical prefix through live physical backing and
releases the list. The native registration adapter now connects this runner,
the prerequisite once guard, and the generic type-registration caller. Actual
prerequisite/parent/field registration bodies remain reached boundaries. The
presence of a metadata global does not establish that registration can be skipped.
See [IGB_REGISTRATION_CALLBACKS.md](IGB_REGISTRATION_CALLBACKS.md) and
[IGB_TYPE_REGISTRATION.md](IGB_TYPE_REGISTRATION.md).

The reflected caller shares `IgbRuntimeMetaType` with the runtime type registry.
Its newly represented factory fields are optional: absent means unrecovered
state, while an engaged zero or null pointer represents original null. The
caller reads metadata `+3C` before the abstract flag or allocation pool. A nonzero
redirect selects that exact callback, then repeats the reflected caller with
the returned metadata and original incoming pool. Only abstract byte `+1A`
equal to one rejects construction. Other nonzero values are not rejected.

A supplied pool bypasses metadata `+60`. Otherwise a nonnull name with a nonzero
first byte selects `80060834`; a null result, null name, or empty name selects
`80058C80`. These providers remain explicit. There is no automatic host pool
substitution.
`make_native_igb_factory_pool_factory` can connect the current-pool endpoint to
the recovered `IgbCurrentPoolInvocation`, retaining its actual registry selection
and fallback behavior. Named lookup and allocation remain separate providers.

## Allocation and initialization

The caller captures metadata `+20`, reaches the selected pool's virtual table,
reads the current metadata size `+48`, and selects allocation virtual `+D8` with
their wrapping 32-bit sum. It adds the captured prefix to the allocator's
returned original address before testing for zero. The prefix is not reloaded
after a pending allocation or callback mutation.

`IgbObjectStorage` separates represented physical bytes, typed layout bindings,
and the original numeric address used for pointer arithmetic. That number is
never treated as a host address. Providers must explicitly bind a file owner at
the proper storage offset; this binding describes uninitialized storage and
does not execute its constructor. A wrapping adjusted address of zero returns
null without initialization. A null base plus a nonzero prefix instead reaches
the initializer and its physical storage checks; it is not treated as ordinary
allocation failure.

For a nonzero adjusted address, the caller retains allocation, prefix and
metadata through the real `800667F4` continuation. It exposes the typed file
only after initialization completes. The initializer performs the recovered
object header and field-pass orchestration. Missing field handlers, allocation
pool resolution, post-defaults, or instance registration remain pending at
their actual calls. This allocation module never manufactures an empty decoder
state, byte stream, field list or completed original document.

The concrete `igIGBFile` vtable `804724C4` identifies `80051698` at `+34`, the
post-default pool assignment operation. Its three independent current-pool
selections occur inside initialization before the dependency request applies
its own pool overrides. The stack layout probe `8002EC9C` is metadata evidence;
it is not the original runtime object construction path.

`make_native_igb_file_init_factory` connects that exact vtable's recovered
virtuals: `+0C`, `+24` and `+2C` are original return-only leaves, `+58` reloads
the canonical metadata global, and `+34` retains the real
`IgbFilePoolsInvocation`. Unknown vtables or slots delegate or remain absent.
The metadata getter can return represented null normally; its consumer decides
whether a subsequent dereference is reached. A missing global binding pauses.
These concrete virtuals do not substitute for the original field defaults.

## Continuations and validation

Each selected operation retains arguments, callback and returned owners while
pending. Reached absent metadata globals and optional fields pause; represented
null dereferences fault. Recursive or throwing callbacks fault their invocation
and cannot replay completed prefixes. An abandoned allocation awaiting
initialization remains retained with its services. The two production adapters
hold services weakly and retain the selected live owner in an active operation,
avoiding a recursive loader/decoder service ownership cycle.

Permanent tests cover registration and metadata reload, redirect ordering and
captured pool, exact abstract flags, null/empty/high-bit pool names, failed named
lookup fallback, reached null pools, wrapping sizes and adjusted addresses,
pending allocation mutation, missing initializer state, recursion, abandonment,
and both ordinary/dependency request adapters. Full caller-to-initializer tests
use explicitly prepared zero-field metadata and virtual endpoints; they do not
claim real file defaults or a completed original `igIGBFile` registration.
Additional integration tests use the recovered file virtuals and native
post-default pool assignments while retaining explicit field/default boundaries.

Redirect chains are bounded to 256 selected callbacks in this native profile.
Allocation and object byte/field limits belong to the selected providers and
initializer. Source provenance is the supplied USA executable; disassembly and
comparison fixtures remain ignored research artifacts. Final local counts and
independent comparison results are recorded in [VALIDATION.md](VALIDATION.md).
