# Unsigned-integer descriptor registration

The authored native implementation reconstructs the reached USA class callers
from `main.dol`, SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.
Original executable bytes and generated observations remain local.

## Shared class and prototype identities

| Operation | Original identity |
| --- | --- |
| Class | `igUnsignedIntMetaField`, size `34` hex, concrete |
| Metadata / prototype cells | `805614BC` / `805614C0` |
| Registration wrapper / body | `80022CB0` / `80022CD8` |
| Parent body / getter | `8002A6D8` / `80021D70` |
| Field callback | `80022D40`, forwarding to full getter `80022BEC` |
| Lazy / plain metadata getters | `80022BB0` / `80094138` |
| Layout probe | `80022C28` |
| Deferred prototype callback | `80022D60`, table `8055CF50` |
| Null-only / plain prototype getters | `80022DF8` / `80094140` |

Unlike the unsigned-byte class, body `80022CD8` does not call prerequisite
`80021B94`. It supplies the actual generic registrar arguments without local
defaults or a trailing callback. The original extra list is `[80022D60, 0]`.

The metadata getter shares `IgbMetaFieldFactoryServices::metadata`, the existing
unsigned-integer factory cell. The prototype shares
`IgbFieldOperationsServices::unsigned_int_prototype`, already used by scalar
operations. Its aliasing pointer retains the nested field-services owner;
replacing the outer services pointer cannot invalidate a captured cell.

## Probe and reflected construction

Probe `80022C28` calls specialized reset `80075A2C`, which runs common reset
`800638E0` and publishes derived vtable `80476E6C`. It captures ArkCore, reads
the offset at `+394`, captures the selected local word, then writes derived
and base (`80471914`) vtables. Name cleanup uses the wrapped `local + 0C`
guard and full-word reference decrement, releasing only an exact zero result.
The native profile retains the existing aligned `34`-byte local boundary.

Prototype `80022D60` uses the existing reflected allocation and registry
sequence: null gate, pool lookup, live metadata, `8006546C` initialization,
canonical publication, registry retain/append, live-cell release, fresh
registry count and prototype ordinal, then finalization. These are actual
callers; returning a prepared prototype is not required by the implementation.

The reflected initializer recognizes object vtable `80476E6C`; `+58` reads
plain metadata getter `80094138`. Verified `+0C/+24/+2C/+34` entries are empty
original leaves. Inherited field passes and existing scalar default operations
remain responsible for object contents. This extension does not add clone
wrapper `80022B34` or broaden unrelated virtual dispatch.

Thread defaults reach this class through factory `80022A90` for `_stackSize`.
The factory still follows the original enabled/disabled construction paths.
See [IGB_NAMED_TYPES.md](IGB_NAMED_TYPES.md) for that batch and
[VALIDATION.md](VALIDATION.md) for measured composition progress and remaining
startup dependencies.

## Cold composition

The actual cold chain completes this class and its reflected prototype, then
resumes Thread's ten-field batch. Four enum defaults and their getter words,
the `0x1000` stack-size default, schema globals and physical descriptor words
are published by the original callers. Thread and Semaphore metadata both
finish at flags `7`, with physical redirects `80023E20` and `80026160`.

The next pending callback is `80023A30`. Primary inspection identifies
`800755F8` underneath it, including concrete metadata redirection, reflected
manager/thread objects and main-thread capture. Those runtime operations have
not executed in the native composition. Original engine startup and gameplay
remain incomplete.
