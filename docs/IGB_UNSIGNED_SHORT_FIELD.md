# Unsigned-short registration, prototype and clone

The native unsigned-short descriptor uses the existing raw field kind and
vtable `80477054`. Its metadata registration profile is appended separately;
earlier enum indices and canonical cells do not change. Metadata `80561494`
and prototype `80561498` are distinct. The latter aliases the existing field
values service's prototype member, so registration and plain virtual getters
observe the same cell.

Registration wrapper/body `80021FF4/8002201C` declare
`igUnsignedShortMetaField`, size `0x34`, with `igMetaField` as parent,
probe `80021F6C`, no local defaults and extra callback `800220A4`.
This body does not call prerequisite `80021B94`. Its full metadata getter
checks null/readiness before registering and reloads the canonical cell;
plain getter `80094158` only reads that cell.

The layout probe runs specialized reset `80075F0C` before capturing ArkCore.
It saves the selected physical word, restores the base-field vtable, and
performs the actual guarded name cleanup. It has no reference-field tail.

## Clone and prototype

Clone virtual `+54`, original `80021EB4`, captures source and mode before
reading the enabled byte. The enabled branch always runs its registration
wrapper and then actual reflected clone `800680B4`. The disabled branch calls
`80066DD8(0x34, 0)`, conditionally runs `80075F48`, and returns the result.
That raw copy executes base copy `800639E4` before publishing its derived
vtable. A null allocation returns null without object tails. The separate
ordinary factory retains its own original allocation and tail behavior.

Prototype callback `800220A4` checks for a nonnull prototype before doing any
other work. Otherwise it selects the metadata pool, freshly loads metadata,
reflects through `8006546C`, publishes the result, appends a nonnull descriptor
to the real prototype registry, balances its temporary reference, writes its
ordinal and finalizes it. The null-only getter `8002213C` registers when the
prototype is null and reloads it afterward; it does not fabricate a descriptor.

The native initializer admits the exact metadata getter and proven object
`+0C/+34/+2C/+24` no-op entries. Reflected copying admits the original
`+48/+14/+18` no-op hooks. Scalar size/alignment remain two bytes; descriptor
allocation size remains `0x34`. Existing scalar member/default copying and
default-buffer ownership execute unchanged. Neighboring unsupported virtuals
continue to delegate explicitly.

## Connected startup

The retained cold startup now completes unsigned-short registration and its
prototype, clones the inherited struct `_size` descriptor, applies its
two-byte value `1`, replaces the inherited slot, and finishes all three struct
local schema fields. The base class keeps its original size descriptor/global
and default buffer. The struct alignment field receives a four-byte value `1`.

At the unsigned-short milestone this reached struct prototype callback
`800247E4`, with 26 published types and eleven prototypes. Subsequent struct
descriptor work now completes that callback and the concrete semaphore schema;
see [IGB_STRUCT_FIELD_RUNTIME.md](IGB_STRUCT_FIELD_RUNTIME.md). The runtime
semaphore's metadata getter and member initialization remain required.
Raw storage/pool services remain explicit test preparation. These results do
not establish complete native boot or playable gameplay.

See [IGB_STRUCT_FIELDS.md](IGB_STRUCT_FIELDS.md),
[IGB_PLATFORM_SEMAPHORE.md](IGB_PLATFORM_SEMAPHORE.md) and
[VALIDATION.md](VALIDATION.md). Primary evidence is the supplied USA DOL,
SHA1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`; original artifacts stay local.
