# Enum-field class defaults

`IgbEnumFieldDefaultsInvocation` reconstructs the USA `main.dol` function
`80031614`. The evidence is the supplied image with SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`; no original binary or disassembly is
distributed with the module.

The receiver is captured from canonical metadata cell `80561C30`, followed by
its field count through `80065D88`. The original one-entry batch `80065924`
uses pointer-field factory `800274EC`. After that complete batch, including
its append and creator release, the caller reloads the captured metadata's
current field registry and selects its saved index through `800658E4`.
Selection preserves the original 32-bit left-shift wrapping and bounds the
resulting host slot. It does not substitute the new constructor result for
the selected field when callbacks have replaced the registry.

The selected field's byte `+26` is cleared in physical storage and canonical
state. A known physical binding must still identify the same descriptor and
provide the reached byte; an unavailable binding faults after the preceding
batch effects. A canonical-only descriptor remains supported. The store does
not read the previous byte and does not change neighboring padding or the
derived word `+34`.

The final `800659C0` schema runs with these original tables:

| Table | Address | Entry |
| --- | --- | --- |
| Factory | `8055D518` | `800274EC` |
| Name | `8055D51C` | `_getMetaEnumFunction` |
| Descriptor cell | `8055D520` | `80561C38` |
| Member offset | `8055D524` | `34` hexadecimal |

The schema performs native name assignment, buffer preparation, finalization
and global publication. Its captured table owners retain live entries across
suspension. Missing constructors or other reached services remain pending;
the defaults adapter creates neither a finished descriptor nor a ready class.
Completed phases are not repeated, and callback recursion invalidates the
caller. Abandoned or faulted invocations retain captured metadata, descriptor
and physical allocation owners through their services.

This class is `igEnumMetaField`, distinct from the `igMetaEnum` metadata object
class. Registration describes its `_getMetaEnumFunction` member; this defaults
callback does not invoke that getter, assign a function to an enum instance,
or implement enum conversions. The later memory-field caller's explicit
function assignment remains separate.

## Connected class lifecycle

The descriptor factory `80031378` uses the original enabled registration and
reflected initializer. Its disabled path executes `80046F8C` and `80046FD8`
through the direct field base. Neither body initializes the derived `+34`
word. The layout probe `800314D4` reads the actual local storage using the live
core's offset and performs direct-base cleanup.

The one-field defaults batch registers the pointer descriptor class through
`80027704`. Its original name is **`igRawRefMetaField`**, with vtable `80476630`,
metadata cell `80561694` and prototype cell `80561698`. The internal C++ kind
remains `pointer`. Its probe uses the reference base and has no object or memory
cache members to release.

Both classes share the native prototype lifecycle: actual reflected allocation,
global publication, registry append, reference decrement, ordinal assignment and
finalization. Enum metadata/prototype cells `80561C30`/`80561C34` remain separate.
Null-only prototype getters `80031728`/`80027824` register their class when
needed; the plain virtual getters only read the corresponding canonical cell.

Enum values use the original four-byte size/alignment leaves and common default
buffer setter `80063D04`. The memory-field caller's `80046E58` dispatch therefore
uses the same allocation and copy sequence. Reflected pointer-member stores
synchronize the enum's `+34` function word without treating it as reference
ownership flags. Enum text/value conversions and the cold clone wrapper
`8003141C` remain outside this recovered slice.

The memory class's replacement reset descriptor has size four at offset `+24`.
Its big-endian default `00000003` is copied before the subsequent inherited
one-byte defaults at `+25`, `+26` and `+27`. Those later writes yield physical
word `00000102`. The reflected instance's reset byte is therefore zero; the
direct raw memory reset stores byte three. Keep the original copy widths and
ordering instead of forcing these distinct construction paths to agree.

Focused synthetic tests execute the real batch, pointer-list append, schema,
field-value and finalization callers around prepared constructor, pool and
string endpoints. They cover receiver/count capture, registry replacement,
wrapped indices, physical flag faults and padding, pending table ownership,
late service binding, null versus unknown metadata, abandonment and recursion.
Build and original-code comparison results belong in
[VALIDATION.md](VALIDATION.md). This module alone does not establish full engine
startup or playable behavior.
