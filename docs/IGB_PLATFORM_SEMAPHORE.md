# Concrete semaphore class registration

The native implementation of metadata redirect `80026160` now calls the
actual concrete getter `8003B238`. It returns metadata to the existing
reflected factory; it does not allocate or initialize a runtime semaphore.
Canonical cell `805620A0` remains distinct from abstract `igSemaphore` cell
`805615EC` and from the manager's instance member.

Registration wrapper/body `8003B31C/8003B344` run the existing prerequisite
and registration machinery. The class is `igGamecubeSemaphore`, size `0x24`,
with abstract parent registration `800260C0`, plain parent getter `8003B3B4`,
field callback `80026160`, probe `8003B274`, and defaults `8003B3BC`.
There is no extension or trailing callback. Full metadata lookup respects the
original null/ready gate and reloads its retained canonical cell afterward;
plain parent lookup performs no registration.

## Physical layout probe

The probe runs common reset `8006665C` on its represented stack object. It
stores the named-object vtable before capturing ArkCore, then writes the
abstract semaphore vtable, zero name pointer and derived vtable `80470314`.
The selected physical word is read using the captured core's `+394` offset.
The probe saves this result before restoring derived, abstract and finally
named-object vtables. Thus offset zero returns `80470314`, while the final
physical vtable is `8047650C`. Untouched derived bytes stay unknown.

The original cleanup tests a retained literal zero, so it does not load or
release a changed name pointer. Null core faults and missing physical bytes
preserve the preceding stores; missing core state can resume without repeating
the common reset or prior publications.

## Field defaults

Defaults capture the class and existing field count, then append all four
local descriptors before applying any setters or schema:

| Factory | Field | Instance offset | Default specialization |
| --- | --- | --- | --- |
| `80022A90` | `_initCount` | `0C` | `80075AC4(field, 1)` |
| `80022A90` | `_maxCount` | `10` | `80075AC4(field, 1)` |
| `800385E0` | `_isActive` | `14` | Inherited Boolean default |
| `8002450C` | `_sema` | `18` | Descriptor size halfword `+14 = 0C` |

Each integer setter passes its actual four-byte source through fresh field
virtual `+8C`; it does not directly replace the field's default buffer.
Field selections use the captured class's live registry. Schema `800659C0`
then publishes the names, offsets and global slots `805620A4..805620B0`.
Only afterward does the code reload canonical `805620A0` and write `8002612C`
to metadata `+40`. It does not write a factory redirect at `+3C`.

## Reached cold boundary

The integrated cold test executes the concrete probe and parent inheritance,
then constructs the two unsigned-integer descriptors and Boolean descriptor.
Its fourth factory, **`8002450C` for `igStructMetaField`**, now runs registration,
its physical probe and three local descriptor factories. Unsigned-short
registration/prototype and inherited `_size` clone `80021EB4` now finish struct
defaults and schema. Struct prototype `800247E4` and its actual reflected
descriptor now let the semaphore finish both count setters, the size-12
specialization and its schema. The class is ready with seven fields and flags
`7`; its post-schema `+40` callback is `8002612C`.

The cold composition contains 27 published types, 92 descriptors, 104 default
buffers, twelve prototypes and 96 enabled allocations. Both the thread-list
and semaphore runtime instances are constructed by their actual field passes.
Before activation, the semaphore has count defaults one, an inactive flag and
the twelve bytes copied from its explicitly prepared descriptor payload.
Its manager assigns member `+0C` and calls activation `+5C`. That operation now
initializes queue/count and active/status state, with an explicitly prepared
prior exclusion bit; padding remains unknown. The manager completes and is
published by the actual startup caller, which next reaches concrete thread
getter `8003AF84`. The main thread remains unallocated.

Continue with concrete thread registration and subsequent platform operations.
Activation is implemented separately in
[IGB_SEMAPHORE_ACTIVATION.md](IGB_SEMAPHORE_ACTIVATION.md). Wait/signal semantics
and host scheduling remain required. This metadata implementation does not
provide a host mutex or a playable game. See [IGB_STRUCT_MEMBERS.md](IGB_STRUCT_MEMBERS.md),
[IGB_STRUCT_FIELD_RUNTIME.md](IGB_STRUCT_FIELD_RUNTIME.md),
[IGB_STRUCT_FIELDS.md](IGB_STRUCT_FIELDS.md),
[IGB_RUNTIME_OBJECTS.md](IGB_RUNTIME_OBJECTS.md) and
[VALIDATION.md](VALIDATION.md).

Primary evidence is the supplied USA DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`; binary and disassembly stay local.
