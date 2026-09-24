# Named-object, thread and semaphore metadata

The authored native classes reconstruct the reached USA `main.dol` registration
and schema callers. Source SHA-1 is
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`; original code and data remain local.
These are metadata operations. Installing a thread or semaphore factory
redirect does not construct or start a platform thread.

## Registration and canonical getters

| Class | Metadata cell | Wrapper / body | Parent body | Size / abstract |
| --- | --- | --- | --- | --- |
| `igNamedObject` | `80561744` | `80029D30` / `80029D58` | `80066B08` | `0C` / no |
| `igThread` | `80561504` | `80023C40` / `80023C68` | `80029D58` | `30` / yes |
| `igSemaphore` | `805615EC` | `80026098` / `800260C0` | `80029D58` | `0C` / yes |

Sizes and offsets are hexadecimal. Every body executes prerequisite `80021B94`
before capturing generic registrar `80066204` arguments. The wrappers run the
actual callback-list caller; neither thread nor semaphore has a layout probe.
No additional prototype or deferred callback is invented for these profiles.

Full getters test the ready bit, register when necessary, then reload the
canonical pointer. Allocation-only getters test nullness without registering
the class. Semaphore lookup delegates to the existing `80026020` implementation
and shares ThreadManager's `805615EC` cell. Named-object plain getters
`80023CF4` and `800940B8` read the same `80561744` cell without testing flags.

The named-object probe `80029CA8` executes base reset `8006665C` on its actual
twelve-byte local, captures ArkCore, publishes vtable `8047650C` and writes a
null name at `+08`. It reads the selected word using the captured core's `+394`
offset, then republishes the derived vtable. Cleanup tests a saved literal-zero
register, so it does not reload or release a subsequently changed name pointer.
Unknown core state, known null, unavailable local bytes and pending operations
remain distinct. The represented probe profile is the aligned twelve-byte local,
not arbitrary adjacent stack or address-space behavior.

## Original field defaults

Named-object defaults `80029DE8` capture metadata and its field count, construct
one string descriptor, invoke actual null-string setter `80071694`, then run
schema `800659C0`. The `_name` member is at `+08`, with descriptor cell
`80561748`. Inherited fields and the new descriptor are established by the
actual append/schema operations.

Thread defaults `80023CFC` first construct all ten local fields:

| Field | Kind | Offset | Descriptor cell |
| --- | --- | --- | --- |
| `_priority` | Enum | `18` | `80561508` |
| `_type` | Enum | `0C` | `8056150C` |
| `_scope` | Enum | `10` | `80561510` |
| `_state` | Enum | `14` | `80561514` |
| `_arg` | Raw reference | `1C` | `80561518` |
| `_stack` | Raw reference | `20` | `8056151C` |
| `_stackSize` | Unsigned integer | `24` | `80561520` |
| `_function` | Raw reference | `28` | `80561524` |
| `_isMaster` | Boolean | `2C` | `80561528` |
| `_userStack` | Boolean | `2D` | `8056152C` |

Only after that batch completes do the four enum setters execute, with static
defaults `80, 0, 0, 0`. Each setter precedes its descriptor `+34` getter-function
store (`80023A70`, `80023B4C`, `80023B00`, `80023ABC`). The stack-size setter
passes the big-endian word `1000` through the descriptor's freshly read `+8C`
virtual. Stored enum getter functions are not called by these defaults.

Field selection preserves the captured metadata/count but reloads its live
registry for each selection. After schema returns, the thread caller reloads
canonical metadata and stores redirect `80023E20` at `+3C`. Semaphore defaults
`8002614C` only perform the corresponding live metadata store of `80026160`.
Physical bindings preserve ordered stores and failures; selected operations
and owners survive suspension without replaying completed effects.
Direct field selection uses canonical backing mappings. Physical redirect
stores require the explicit metadata allocation registry; canonical-only
representations do not invent an allocation. Name-string publication retains
the existing name setter's separate canonical representation.

See [VALIDATION.md](VALIDATION.md) for measured cold-composition progress,
original-code observations, prepared endpoints and remaining startup work.
