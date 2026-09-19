# Native igMetaObject registration

`igb_metadata_registration` reconstructs the original `8002A0A8` wrapper,
`8002A0D0` registration body, allocation-only getter `80029F84`, and full
getter `80029FC0`, also reached through `8002A144`. Its canonical `8056174C`
cell is the existing `IgbMetadataFactoryServices::metadata`; the module does
not create another cache or infer runtime metadata from serialized type names.

Evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Source and synthetic fixtures are
authored C++; original instructions and assets remain outside the repository.

## Registration order

`8002A0A8` calls the native callback runner `80066188(8002A0D0)`. The body
passes these exact arguments to the existing native `80066204` registrar:

| Argument | Original value |
| --- | --- |
| Canonical metadata cell | `8056174C` |
| Name, abstract flag, size | `igMetaObject`, `0`, `0x64` |
| Parent registration/getter | `80066B08` / `800237D0` (`igObject`) |
| Type getter | `8002A144` |
| Layout probe | `80029FFC` |
| Defaults callback | `8002A164` |
| Extension callbacks | `8002DD18`, `8002E054`, `80066AE0`, zero terminator |
| Trailing callback | Null |

Unlike the file and ThreadManager registration bodies, `8002A0D0` does not
call prerequisite guard `80021B94`. Missing prerequisite bytes therefore do
not prevent this body from reaching its own original pool/cache operations.
The generic registrar still resolves the metadata pool before inspecting
the cache, including when metadata is already ready or busy.

Layout and field registration remain explicit operations. `80029FFC` is a
real stack-object probe: it calls metadata reset `80065218`, reads a word at
the offset selected by live ArkCore `+394`, and releases the temporary pool
and name strings afterward. Returning the known metadata vtable constant
unconditionally would omit that behavior.

Defaults `8002A164` capture the inherited field count, append **25** actual
field descriptors through `80065924`, configure selected defaults/reference
types, and finalize names, global descriptor cells and offsets through
`800659C0`. Its factory table begins at `80464304`; names, globals and offsets
begin at `80464478`, `804644DC` and `80464540`. The fields include integer,
boolean, string, object-reference, pointer and short descriptors. For example,
`_metaFields` at `+28` uses the internal-list metadata and automatic construction;
`_parent` at `+38` calls the allocation-only metadata getter; required alignment
defaults to four. The defaults callback is not implemented by manufacturing
an empty field list or treating every pointer as a null scalar.

## Two different getters

The allocation-only getter `80029F84` returns any existing nonnull cache owner,
without inspecting its ready bit or running registration. A known null cache
reads the metadata pool handle, performs native `800607F4(handle)`, calls the
actual metadata factory `80029E64`, and publishes its captured result directly
to the canonical cell. This raw store overwrites callback replacement without
an additional cache check or guest reference release. Host retention keeps
displaced owners alive. A captured null factory result is also published.

The full getter checks ready bit `4`. Null or not-ready metadata invokes the
complete native wrapper and callback-list cleanup, then reloads the canonical
cell. It does not recheck readiness after that call. Missing represented state
pauses at the reached read; an engaged null final result is preserved.

Selected pool, factory and registration invocations survive suspension. Handle
changes do not redirect an already selected pool call; callback replacement
does not restart the selected continuation. Completed calls are released before
the next original store/read. An already returned result retains its identity.
Adapters use weak service ownership until selection, then hold the services
needed by their retained operation.

## Bootstrap requirement and recursive failure boundary

The original startup sequence constructs ArkCore with enabled byte zero,
publishes it, and enters `8003D2F8`. That routine calls `8002A0A8` at
`8003D3EC` while the byte is still zero. Only after registration and its
deferred callbacks return does `8003D3F4` store enabled byte one. The disabled
metadata factory can therefore construct the canonical owner before normal
reflective construction requires it.

Starting the opposite profile, enabled ArkCore with a null `8056174C`, creates
the cycle `80029E64 -> 8002A0A8 -> 8002A0D0 -> 80066204 -> 80029E64` before
publication or the metadata busy flag. The native adapter reports a bounded
cycle error when the same canonical null cell is reached again on an active
call with represented enabled ArkCore. It does not manufacture a ready owner,
turn the engine off, publish a once flag, or report success. This protects the
host stack instead of reproducing the original nonterminating invalid-startup
profile. Published busy metadata remains eligible for the real registrar's
recursive short return. A suspended independent call is not an active stack
entry, and disabled recursive construction is not rejected by this guard.

Permanent tests cover exact arguments and canonical alias lifetime, the missing
prerequisite distinction, direct cold metadata construction to its real parent
boundary, ready/busy gates, retained layout/default boundaries, both getters,
captured pool arguments, callback-replaced raw cache publication, recursive
bootstrap failure and adapter lifetime. Prepared field-list, layout or reflected
outputs are labeled isolated caller fixtures; they do not establish complete
metadata registration or cold startup. Local build and supplied-data results
are recorded separately in [VALIDATION.md](VALIDATION.md).

A composed cold-start fixture runs the actual ArkCore constructor and bootstrap
caller into the native registration runner, registrar, disabled metadata factory,
metadata constructor and internal-field-list constructor. It suspends first at
a prepared physical backing allocator, then enters the actual `igObject`
parent registration `80066B08`. A distinct second metadata/list pair is
constructed, the real root callback list is initialized, and native string
interning and the base-object probe run. It reaches the specialized descriptor
defaults `80066B7C`, with `igMetaObject` busy flag `8` and `igObject` flags `9`.
ArkCore remains disabled and later type-count snapshots remain unwritten.
Platform/manager work, initial ArkCore list owners, their reserve operation,
physical allocation and string-arena allocation endpoints are explicitly
prepared. No ready metadata seed, registration-success callback or premature
enabled store is used. See [IGB_OBJECT_REGISTRATION.md](IGB_OBJECT_REGISTRATION.md).
