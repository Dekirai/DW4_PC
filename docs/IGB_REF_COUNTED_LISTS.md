# Reference-counted object and thread lists

The existing data-list registration and defaults modules also represent the
USA `igObjectList` and `igThreadList` classes. Their identities remain separate
from `igNonRefCountedObjectList` and `igNonRefCountedThreadList`.

| Class | Metadata cell | Registration wrapper/body | Parent |
| --- | --- | --- | --- |
| `igObjectList` | `80561708` | `80029054 / 8002907C` | `igDataList` |
| `igThreadList` | `80561534` | `800240CC / 800240F4` | `igObjectList` |

Both have native size `0x14` and execute prerequisite `80021B94` before generic
registration `80066204`. The thread class appends original extra callback
`80023C40` through the existing callback-list pipeline. It has no defaults body
of its own. No native instance or ready class is fabricated by binding a cell.

Full getters register on null or missing ready bit, then reload the canonical
cell. Allocation-only getters `80028F84 / 80023FE4` use the existing metadata
factory and pool lookup only on known null. Plain getter `80024180` reads the
object-list cell without testing readiness. These paths keep unknown state
distinct from a represented null pointer.

The probes reuse the actual base reset and preserve every ordered vtable store.
Object probe `80028FFC` captures ArkCore after its first vtable store; thread
probe `8002405C` captures it after its third. Each then reads offset `+394`
from that captured owner and reads the selected local word. A pending captured
core is not replaced when the singleton changes. Other local bytes remain
unknown unless the original reset or a reached store initialized them.

## Descriptor ownership

Actual defaults `8002910C` find the inherited `_data` descriptor, use its
captured clone virtual with argument one, and retain the clone while obtaining
the object-field prototype. They store that prototype at `+3C`, zero at `+38`,
and the separate descriptor-cell address `8056170C` at `+1C`. They then run
the existing field replacement operation `8006588C` and release the caller's
temporary reference using the full word and low-23-bit destruction test.

Unlike nonref defaults `80029724`, this body never writes clone flag `+42`.
Its value and initialized state survive the specialization. The source
descriptor, nonref specialization and ref specialization have distinct owners
and canonical global cells; they must not overwrite each other.

The replacement continuation borrows its parent's fault check while resuming
finalization, backing reserve and destructor callbacks. Completed original
prefixes remain visible, but a callback that catches recursive parent entry
cannot let later child stores publish a result after the parent was faulted.
The borrowed callback is not retained across resumes.

## Scope

This work covers registration, metadata lookup, probes and defaults. Runtime
construction and virtual dispatch for ref-thread instances at `80476BB4`
remain separate from the previously recovered nonref-thread instance profile
`80476CE0`. Registering either class is not proof of playable startup.

Primary evidence is the supplied USA DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, inspected through IDA and bounded
original execution. Original binaries and local comparison corpora are not
distributed. See [VALIDATION.md](VALIDATION.md) and
[IGB_OS_THREAD_WAKE.md](IGB_OS_THREAD_WAKE.md).
