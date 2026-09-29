# Memory-reference field defaults

`IgbMemoryFieldDefaultsInvocation` reconstructs the original USA caller
`8002AF40`, selected by `igMemoryRefMetaField` registration. It operates on the
canonical metadata cell `80561818` and shares the existing native field batch,
schema, scalar values, lazy metadata lookup and descriptor replacement code.
This is authored C++ source. Original instructions and executable data are
local research inputs and are not committed.

## Original order

The caller captures its metadata receiver and original field count, then calls
`80065924` with these eight factories. Schema configuration is deferred until
all intervening method replacements and defaults have completed.

| Factory | Name | Descriptor global | Member offset |
| --- | --- | --- | --- |
| `8002E1AC` | `_memSize` | `80561824` | `38` |
| `8002879C` | `_memType` | `80561828` | `3C` |
| `800385E0` | `_releaseOnCopy` | `8056182C` | `40` |
| `800385E0` | `_releaseOnReset` | `80561830` | `41` |
| `800385E0` | `_refCounted` | `80561838` | `42` |
| `8002E1AC` | `_alignmentType` | `8056183C` | `44` |
| `8002879C` | `_memTypeRef` | `80561840` | `48` |
| `800251EC` | `_poolName` | `80561844` | `4C` |

The original tables are `80464A14`, `80464A88`, `80464AA8` and `80464AC8`.
Global `80561834` belongs to the separate inherited reset-method replacement;
it is intentionally skipped by the local-field global table.

The caller searches for inherited `_shallowCopyMethod`, finds its identity
index, invokes its captured virtual `+54` with argument one, and supplies the
one-byte default two through wrapper `80040030`. It stores global-cell address
`80561820`, invokes actual replacement `8006588C`, and releases the creator's
live packed reference word. Low-23-bit zero reaches `80066E1C`.

It clears local zero's byte `+26`, then calls lazy base-field metadata getter
`8002A638` for local one. After that getter returns, it stores object metadata
at `+38`, clears byte `+3C`, and sets byte `+28`. Locals two and three receive
one-byte true defaults through `8003EC68` before reset-method replacement.

The reset path captures the field registry at `8002B068` **before** searching
for `_resetMethod`. Identity lookup uses that captured registry, while the
later replacement reloads the metadata's live registry. The constructor uses
a fresh `805621F4` pool handle and actual `800607F4`, then calls required
constructor `80031378`. Native configuration `80063BA8` receives the name,
global `80561834`, offset `24` and captured declaring metadata. Wrapper
`80046E58` forwards source `8055D2A8`, a represented big-endian word three, to
the selected descriptor's `+8C`. Only after completion does the caller store
method function `8002A5EC` at `+34`, repeat the global-cell store, replace the
old descriptor and release its creator reference.

Local four then receives true; local five receives signed minus one through
`80053650`; local six obtains a fresh `8002A638` result and receives its `+38`
metadata and byte `+28` stores. Its byte `+3C` is not cleared. Finally, actual
`800659C0` configures all eight local fields from the captured first index.

## Representation and suspension

Scalar wrappers preserve their original one-byte or four-byte input and
capture the field's fresh `+8C` virtual. Operations and continuations survive
suspension without selecting a new callback. Search captures the name
descriptor but reads the live count and backing at the original points.
Physical slot selection applies the original wrapping 32-bit index shift
before the host allocation bound.

Optional physical descriptor storage is checked when first reached. A type
getter runs before validation of its later destination store. Once physical
storage is reached, its allocation and adjusted host offset must remain the
same across pending callbacks. Clone and scalar dispatch read represented physical vtable bytes at the
original selection points; released or uninitialized headers fault before
invoking the selected virtual. Direct stores update big-endian bytes before
their canonical fields, preserving a faulting store's completed prefix.
Metadata addresses come from explicit typed bindings or physical allocations;
host pointers are never converted into original arithmetic addresses.

The method function has its own canonical `method_function` member. It is
separate from byte `flag_34` used by reference-field layouts. Replacements use
the existing native finalizer, physical pointer-list publication and packed
reference ownership. Canonical null, unknown bindings and expired owned weak
pointers retain their separate meanings.

## Remaining boundaries and validation

The constructor's cold original implementation selects vtable `804758E4` via
`80046F8C`; its enabled path registers the method class and enters reflected
construction. This module does not fabricate that constructor, its method
virtual, or an unknown inherited clone implementation. They remain reached
services until separately reconstructed. The static word-three source is an
input to the method operation, not evidence that the operation completed.

The permanent suite uses prepared constructor, clone and method bodies, plus
explicit allocation and string endpoints. It executes the real surrounding
batch, scalar values, schema, search, finalization and replacement behavior.
Checks cover all eight fields and both methods, physical stores at adjusted
host offsets, callback capture, reset-registry capture before a pending name
search, lazy-getter fault order, invalid physical rebinding, null inherited
fields, released/uninitialized physical headers, recursion and completed-call
idempotence. Coordinated builds and the
independent original-instruction comparison are recorded in the repository's
validation report after they run. These tests do not establish full game boot.
