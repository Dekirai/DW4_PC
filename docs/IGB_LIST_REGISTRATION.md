# Native internal-list registration

`igb_list_registration` reconstructs the registration bodies, wrappers,
metadata getters and stack-object layout probes for the two internal list
classes. Evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. The repository contains authored
C++ and synthetic fixtures; original instructions and game data remain local.

## Canonical cells and registration arguments

The module uses the existing `IgbTypeListServices::registry_metadata` and
`children_metadata` shared cells. The former can directly alias
`IgbFieldRegistryServices::metadata`; neither registration nor lookup creates
a parallel metadata cache.

| Original argument | Internal object list | Internal non-reference-counted object list |
| --- | --- | --- |
| Wrapper / registration body | `8002DD18` / `8002DD40` | `8002E054` / `8002E07C` |
| Canonical metadata cell | `805619CC` | `805619D4` |
| Name | `__internalObjectList` | `__internalNonRefCountedObjectList` |
| Parent registration body | `8002E07C` | `80066B08` |
| Parent getter | `8002DDCC` | `800237D0` |
| Field callback | `8002DDAC` | `8002E0E8` |
| Stack layout probe | `8002DCE4` | `8002E020` |
| Defaults callback | `8002DDD4` | `8002E108` |
| Abstract flag / native size | `0` / `16` | `0` / `16` |
| Extra callbacks / trailing callback | Null / null | Null / null |

Each wrapper invokes the actual native callback runner `80066188` with its
registration body. Each body passes these arguments to the existing native
`80066204` registrar. Neither body calls prerequisite guard `80021B94`.
The registrar preserves the original pool lookup before the metadata ready
and busy gates, then performs parent registration, inheritance, naming,
layout probing, specialized defaults and publication in their existing order.
See [IGB_TYPE_REGISTRATION.md](IGB_TYPE_REGISTRATION.md).

The object list's parent getter `8002DDCC` is a plain read of `805619D4`.
Calling the parent's full getter there would introduce another registration
and callback-runner operation absent from the original caller.

## Three getter behaviors

Allocation-only getters `8002DC6C` and `8002DFA8` return an existing nonnull
canonical owner without a ready-bit check. A known null cell reads the live
metadata pool handle `805621F4`, runs actual `800607F4`, and invokes native
metadata factory `80029E64`. The captured factory result is stored directly
into the canonical cell and then reloaded. A callback's intervening cell
replacement is overwritten without a guest reference release; host retention
preserves that displaced owner. Missing represented state pauses at its
reached read, while represented null remains distinct from missing state.

Full getters `8002DCA8` and `8002DFE4`, also reached through aliases
`8002DDAC` and `8002E0E8`, inspect ready bit `4`. Null or not-ready metadata
invokes the corresponding wrapper, including its callback runner and list
cleanup, before reloading the canonical cell. They do not recheck readiness
after registration returns. Plain getter `8002DDCC` only reads the children
metadata cell and can return represented null without constructing anything.

Selected pool, constructor and callback operations survive suspension.
Changing a handle or service factory does not redirect an already selected
operation. Completed getters preserve their returned owner rather than
rereading global state. Recursive entry poisons the active invocation.

## Actual layout probes

`8002DCE4` supplies a 16-byte stack local to actual reset `80053CF4`, which
calls base list reset `800536F8` and then stores vtable `80475CB0`.
`8002E020` calls `800536F8` directly, leaving vtable `80475C54`.
Both reset paths write reference word `1`, null backing and count zero.
They allocate no backing storage.

After the reset, each probe captures live ArkCore `80562100`, reads its
`+394` offset, and returns the selected physical word from the local object.
There is no destructor or cleanup afterward. The implementation therefore
does not substitute either known vtable constant: an offset of four returns
the actual reset reference word, and the other represented offsets read
their physical bytes. A pending offset read retains the captured core;
a core that becomes available after reset is captured only at that later
original read.

`local_storage` supplies the physical representation of this stack local,
with the correct typed field-list or metadata-list binding. It is not an
original heap allocator endpoint. The bounded native profile accepts aligned
word offsets within the 16-byte local. Null core, inconsistent bindings,
uninitialized physical reads and offsets outside that profile fail at the
reached boundary after preserving earlier reset effects.

## Composition and current boundary

The callback-function adapter handles the four exact wrapper/body addresses.
The type-registration adapter handles parent bodies, the seven getter
addresses and the two probes. Separate factory adapters connect the enabled
metadata-list and field-list constructors to their original wrappers.
Adapters hold weak service references until selection, then retain the
services required by a pending operation.

The specialized list defaults are a separate native module; see
[IGB_LIST_DEFAULTS.md](IGB_LIST_DEFAULTS.md). This registration module leaves
an unbound defaults callback pending rather than fabricating empty fields or
completed metadata. The existing cold integration can consequently enter
deferred `8002DD18`, descend through `8002DD40` into parent `8002E07C`, and
run real inheritance and the parent probe before entering `8002E108`.
Its two local field constructors now bring the chain to 29 descriptors.
Getter `80028BFC` then reaches object-field prototype registration `800289F8`
with canonical cell `805616EC` still null. The schema customization and both
list types' publication remain incomplete at this boundary.
Completed registration of one type does not imply completed startup or an
updated gameplay preview.

`igb_list_registration_tests` covers exact arguments and canonical aliases,
original gates and missing dependencies, physical probe reads and reset
effects, all getter modes, retained callbacks, raw publication ownership,
adapter selection and recursive failure. Its isolated metadata factory tests
explicitly prepare the internal field-list allocation endpoint. They do not
claim to recover that allocator from those fixtures. Broader cold composition
and original-instruction proof results are tracked separately in
[VALIDATION.md](VALIDATION.md).
