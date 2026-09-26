# Native context registration

The context prerequisite reached after error-handler startup is wrapper
`800374E8`, which runs `80066188` with body `80037510`. It registers
`igContextExt` and first enters its abstract parent `igContext`, body
`80037798`. Both use the original generic registrar and share the existing
callback-list owner.

| Class | Canonical metadata | Parent | Defaults | Extra callbacks |
| --- | --- | --- | --- | --- |
| igContext | `80561E00` | igObject | `8003782C` | `80037140`, `80036F6C` |
| igContextExt | `80561DF8` | igContext | none | none |
| igContextExtInfoList | `80561DE4` | igObjectList | none | `80037330` |
| igContextExtList | `80561DE0` | igObjectList | none | `800374E8` |

The first two classes have native size `0x14` and registration flag one.
They are abstract: their registrar branch skips the layout probe. Registering
them preserves the parent's inherited type word `804717F0`; it does not
invent a concrete object, derived vtable or active context.

## Original defaults and distinct dependencies

Defaults `8003782C` capture context metadata and its inherited field count,
then append object, integer and object descriptors through actual `80065924`.
The later schema assigns `_pExtList`, `_iNumExts` and `_pExtInstances` at
offsets eight, twelve and sixteen. Their descriptor globals are
`80561E04 / 80561E08 / 80561E0C`.

After the whole batch, defaults capture the descriptor at the original first
index, invoke allocation-only getter `80037058` and store its returned
`igContextExtInfoList` metadata into descriptor `+38`. They then freshly
select descriptor `first+2`, invoke allocation-only `80036E84`, store the
separate `igContextExtList` metadata at `+38`, and set only this descriptor's
construct byte `+34` to one. The first object's byte is preserved.
Actual `800659C0` then applies the schema using the captured context and count.

Both lazy getters reuse the existing pool lookup and metadata constructor.
Their canonical cells remain separate, and an allocated metadata object is
not automatically a registered class. The descriptor being configured remains
captured across a pending getter; a later indexed access uses the current
field-list backing. Physical stores retain the selected descriptor's storage
identity and use the shared metadata address bindings.

## Concrete list profiles and callback order

The two concrete dependency lists extend the existing native data-list
registrar. Both use the reference-counted `igObjectList` parent and the same
size `0x14`. Their stack probes write the common three base vtables, capture
ArkCore, then write their own final pair:

| List | Final probe vtables |
| --- | --- |
| igContextExtInfoList | `80474294`, `80474230` |
| igContextExtList | `8047435C`, `804742F8` |

The probes read the selected physical word through the captured ArkCore
offset; bytes outside the common header remain unchanged. Full getters
register when required and reload their global. Allocation-only getters
test null without inventing readiness. Context plain getter `80037594`
only reads its canonical parent metadata.

The outer wrapper runs the parent's returned callbacks in order. The first
list reaches its own extra callback `80037330`, registration of
`igContextExtInfo`. That separate class is the current implementation boundary.
The second list's callback returns to `800374E8` through the actual registrar
ready/busy gates; it must not bypass an unfinished earlier callback.

## Continuation ownership

Context defaults pass a borrowed fault check through field batch and schema
operations. Schema forwards it through field configuration, name assignment,
buffer allocation and finalization; raw append forwards it into pointer-list
growth. Checked and legacy callbacks share the same retained child invocation.
The enclosing check is borrowed for each resume and is never saved in a child.

When a lower callback catches recursive entry into the outer defaults, later
append, creator release, buffer/backing publication and schema publication
must not proceed. Completed original stores remain visible. An unconsumed
constructor result remains retained without an invented guest release.
Field configuration also retains the captured byte-buffer owner, rejecting
replacement after a callback before later descriptor stores. Unknown bytes
and existing failures on the same buffer retain their prior access order.
The separate registrar, metadata-allocation, pool, meta-field-constructor and
string-dependency internals retain their existing interfaces; full propagation
inside those independent operations is not claimed by this change.

Field references and interned names still use the existing canonical owner
representations. In particular, batch retain/release does not synchronize the
optional descriptor bytes at `+04`, and name publication does not supply a
physical pointer at `+0C`. Comparisons check those canonical values explicitly;
they do not claim that every optional raw descriptor byte is synchronized.
The new dependency pointers/construct byte and the represented schema stores
are checked physically. Completing the remaining physical bindings is separate
work before using those raw words as authoritative runtime inputs.

Primary evidence is the supplied USA DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, with IDA exports checked against
the original bytes. Corpora and disassembly remain local. This registration
module does not implement OS context transfer, an active gameplay context,
complete startup or playable gameplay. See [VALIDATION.md](VALIDATION.md) and
[IGB_ERROR_HANDLER.md](IGB_ERROR_HANDLER.md).
