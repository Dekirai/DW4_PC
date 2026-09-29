# Native viewer ownership

`ViewerSceneInfoManager` and `ViewerParameters` represent the actual camera
owner chain reached by original player input. `make_viewer_parameters()`
explicitly allocates the represented parameters with **zero projection and
view matrices**, matching the original reflected-field factory. It does not
select or update a gameplay camera.

The layout probe constructs identity matrices on its temporary stack object.
Those identities are not the runtime factory defaults. Following the actual
allocation and field initialization corrected that initial research assumption
before implementing this factory.

## Original owner chain

Evidence is from the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Guest addresses below record
source provenance; native code does not execute guest instructions or store
guest addresses as host pointers.

The nonzero-direction branch of player input `8034C3C8` loads:

| Original operation | Native owner |
| --- | --- |
| `8034C664`: player controller `+2C` | `ViewerSceneInfoManager` |
| `8034C670`: viewer manager `+14` | `ViewerParameters` |
| `8034C674`: parameters inline `+6C` | `view_matrix` |

The exact registered manager type is `igViewerSceneInfoManager`, registered at
`80404660`, with type global `8055C788`. Player setup `8034C2F0` resolves that
type through context lookup `8028A730` at `8034C338..348` and stores it at
`8034C34C`. Field-name entry `804E278C` names `_viewerSIManager`; offset entry
`804E27A4` is `0x2C`.

The manager's `_parameters` name and offset table entries are `804F0178` and
`804F02D0`. `igViewerParameters` is registered at `80405F1C`, uses type global
`8055C8D4` and vtable `804F125C`, and has original size `0x108`. Its matrix
fields are `_projectionMatrix` at `+2C` and `_viewMatrix` at `+6C`. This native
owner represents those two fields only; it does not invent values for the
remaining original parameters.

`igViewModel`, registered at `80408138` with vtable `804F1174`, is a separate
engine object whose view matrix is at `+0C`. The selected camera mode updates
that distinct owner before the later viewer publication. It must not be
substituted for the parameters owner consumed by player input.

## Actual matrix factory defaults

The parameters type-layout probe `80405D5C` temporarily installs its C++
vtable and calls identity helper `801285A0` on stack fields corresponding to
`+2C` and `+6C`, at `80405DA0` and `80405DA8`. Registration `80066204` invokes
this probe and retains its returned vtable at class metadata `+5C`; it does
not copy that temporary instance's matrix bytes into future objects.

Actual allocation follows `8006546C`. After allocating the registered object
size, `8006552C` calls `800667F4(instance, metadata)`. That function installs
the registered vtable and runs field construction and default publication:

1. `80066F40` calls each field's virtual `+84`.
2. `80066FF8` calls each field's virtual `+7C`.
3. `8006712C` checks field virtual `+F0`, then routes defaults through
   `80063DD0` for fields requiring publication.

Parameters metadata `80405FB8` registers both matrix descriptors through
factory `8011FD8C`, recorded in entries `804F0654` and `804F0658`. It installs
no explicit matrix default override. The concrete matrix descriptor vtable
is `80499F5C`: virtuals `+84` and `+7C` are the no-ops `80064A0C` and
`80064A04`; virtual `+F0` is `80064C48`, returning false. Its ordinary default
operation `+70` is `80063E50`.

Descriptor finalization `800659C0 -> 80063BA8/80063F14` prepares its default
buffer. `80063C74` obtains the matrix's 64-byte storage through
`80068254(..., clear = 1)`. The original built-in allocation path
`800833D4/8008A7A4` supplies zeroed bytes, as also recovered for the
[game clock's reflected defaults](GAME_CLOCK.md#state-and-default-construction).
Finalization calls descriptor virtual `+8C`, `80063D04`, with null input; that
leaves the zero buffer unchanged. Runtime `80063E50` copies those 64 bytes to
the instance's registered matrix offset. The parameters instance callbacks
reached during this construction are inherited no-ops and do not overwrite
either matrix.

Accordingly, both native matrices contain sixteen positive zero words after
`make_viewer_parameters()`. This is the recovered built-in factory profile,
not an assumption about uninitialized heap memory. The full engine type
registry and allocation system are not reconstructed by this small factory.

The direct viewer manager constructor `80404544` writes null to its parameters
field at `80404590`. Its metadata separately marks that field for typed default
creation at `80404800..814`. A native `ViewerSceneInfoManager` represents the
nullable field and does not run that complete metadata/startup process;
callers explicitly attach the prepared parameters owner.

## Captured matrix lifetime and missing setup

`capture_view_matrix()` resolves the selected manager's current parameters
once and returns an aliasing `shared_ptr` to the inline view matrix. The alias
retains the parameters allocation itself. Replacing or destroying the manager
does not destroy or retarget an already captured matrix. Live writes to that
selected allocation remain visible; a later capture observes the replacement.
Two managers sharing one parameters owner preserve that same matrix identity.

The helper rejects a represented null manager and a represented null parameters
owner separately. It never creates an owner, substitutes a host camera, copies
the matrix, repairs its values or samples time. Calls belong to the engine's
owning thread, like the player invocation consuming them.

The controller integration distinguishes an unimplemented viewer binding from
a represented null original pointer: an absent binding keeps the reached
operation pending; an explicitly null binding faults on dereference. Explicit
camera service overrides retain their existing precedence. A stationary player
never reaches this matrix operation. See [PLAYER_INPUT.md](PLAYER_INPUT.md).

Original input first initializes its local inverse destination to identity at
`8034C65C..660`, then calls `8012A92C` and ignores its status. A zero factory
view matrix is singular, so the recovered inverse leaves that initialized
local identity available. A prepared directional frame proceeding with that
owner therefore proves the owner chain and the original singular-input branch;
it does not prove that the gameplay camera has been selected or published.

## Validation and remaining camera work

`viewer_tests` checks exact zero default words in separate allocations, null
manager and parameters failures, inline address/control-block identity, live
matrix writes, replacement and manager destruction during retained use, final
alias release, and shared parameters across managers. Capture preserves raw
matrix words, including negative zero and a NaN payload, leaving numerical
handling to the consuming operation. These are synthetic ownership checks;
original-data player acceptance is recorded separately in
[VALIDATION.md](VALIDATION.md).

The native factory and aliases do not implement `beCameraCtrl`, camera-box
selection, the selected `beCameraMode`, graph-path evaluation, camera animation,
view-model-to-parameters publication, projection construction or viewport
policy. Original discovery and prepared static camera data provide the next
research inputs. The walking preview's provisional follow camera remains
separate from those original game operations.
