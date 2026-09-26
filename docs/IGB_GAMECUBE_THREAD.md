# Concrete thread registration

`IgbGamecubeThreadRegistrationServices` reconstructs the concrete
`igGamecubeThread` class reached after manager publication. It is distinct from
the existing concrete ThreadManager registration and from creating a running
host thread. Original abstract redirect `80023E20` selects full getter
`8003AF84`, canonical metadata `80562074`.

## Registration and physical probe

Wrapper `8003B0B8` runs body `8003B0E0` through the original callback runner.
The body executes prerequisite `80021B94` before capturing registrar arguments:
size `0x388`, concrete flag zero, parent registration `80023C68`, plain parent
getter `8003B154`, field callback `80023E20`, probe `8003AFC0` and defaults
`8003B15C`. Its extra callback list is `[80026098, 0]`; the trailing callback is
zero. Ready semaphore metadata does not justify dropping that callback.

The probe uses a represented `0x388`-byte local and actual common-header reset.
It captures ArkCore before publishing the named-object vtable, writes the
abstract and concrete thread tables, and clears only name `+08` and semaphore
`+380`. The selected ArkCore offset then determines the physical word returned.
The other thread members retain their preceding bytes and initialized state.

Cleanup restores the concrete, abstract and named-object tables in order.
Its semaphore cleanup tests a retained literal zero, so it does not release a
replacement member. Name cleanup instead freshly reads `+08`, decrements the
captured string header when nonnull and calls the original release endpoint
only when the full reference count becomes zero. Missing string identity or
release services preserve the completed prefix and selected continuation.

## Local defaults

Actual defaults capture the metadata and inherited field count, create nine
descriptors through `80065924`, then select fields through the live list.

| Local field | Factory | Offset | Specialization |
| --- | --- | --- | --- |
| `_thread` | Struct | `38` | Two-byte descriptor size `318` |
| `_master` | Raw reference | `350` | Original descriptor default |
| `_alarm` | Struct | `358` | Two-byte descriptor size `28` |
| `_result` | Raw reference | `30` | Original descriptor default |
| `_threadDoneSema` | Object | `380` | Lazy semaphore type `80026020`, then construction byte one |
| `_isDeferred` | Boolean | `34` | One-byte value one through `8003EC68` |
| `_stopRequested` | Boolean | `35` | Original descriptor default |
| `_atStoppingPoint` | Boolean | `36` | Original descriptor default |
| `_isActive` | Boolean | `37` | Original descriptor default |

The semaphore descriptor is captured before its lazy type lookup. Its physical
type pointer `+38` precedes construction byte `+34`; missing arithmetic identity
suspends before either publication. The Boolean descriptor is selected freshly
afterwards. Schema `800659C0` uses the original name/global/offset tables. Only
after schema completion does the code reload canonical metadata and store
`80023CD4` at `+40`.

Physical byte owners and guest addresses remain distinct from C++ pointers.
Selected operations survive suspension, completed effects do not replay, and
invalidated storage or recursive failure permanently faults the invocation.

## Connected startup boundary

The retained cold composition now finishes this class with flags `7`, thirteen
inherited fields and nine local fields. There are 28 registered types, 101
descriptors, 113 default buffers, twelve prototypes and 108 enabled allocations.
The two struct defaults have distinct `0x318`-byte and `0x28`-byte payloads.
The original factory next reaches its pool's `+D8` allocation for a `0x388`-byte
thread. No thread instance is supplied; its instance count remains zero.
The already constructed manager, list and semaphore retain their identities.
Raw allocator payloads and the earlier initial exclusion bit are explicit test
preparation. These checks do not establish native startup or playable gameplay.

Registration does not implement object initializer `80077E94`, adoption of the
original current-thread state in `80077EFC`, manager wait/list/signal operations
or host scheduling. These remain separate startup requirements. See
[IGB_SEMAPHORE_ACTIVATION.md](IGB_SEMAPHORE_ACTIVATION.md),
[IGB_THREAD_STARTUP.md](IGB_THREAD_STARTUP.md) and [VALIDATION.md](VALIDATION.md).
Primary evidence is the supplied USA DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`; original artifacts stay local.
