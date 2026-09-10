# Loaded weapon definitions

`weapon_resource` decodes each exact exported `beWeaponInfo` into an immutable
`WeaponProgram`, retaining the supplied `ActorSceneResource`. It preserves the
ordered RAM, node, bone-name and sequence lists needed by the original
`PLAYERARMS` controller. Decoding does not register a controller, generate or
remove an attachment, change a sequence label, or construct a complete player.

The scene resource owns the immutable IGB document, source path and explicitly
assigned loaded occurrence. Every row exposes both its serialized reference
and canonical physical object reference. Repeated exports, repeated list
entries and distinct reference aliases remain ordered occurrences. Node rows
retain the same shared scene-node owner returned by the resource, including
when a node is also reached through a scene export. A separately loaded
occurrence gets separate owners even when its path and local references match.

## Original layout and checked decoding

The supplied USA DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, identifies the weapon info fields
at metadata registration `802B2E4C`. Info slots 7, 8 and 9 correspond to guest
offsets `+1C`, `+20` and `+24`. Generation `80318474` consumes the actual node
and parallel bone-name lists, followed by the ordered `beWeaponSeqData` rows
used for `MODELCTRL/SETLABEL`.

| Object | Decoded fields |
| --- | --- |
| `beWeaponInfo` | Nullable name in slot 2, raw active byte in 4, optional data list in 5, RAM list in 6, node list in 7, bone-name list in 8, sequence list in 9 |
| `beWeaponInfoRam` | Signed authored index in slot 2, raw active byte in 5, four signed words in 6–9, optional data list in 10 |
| `igNodeList` | Ordered shared scene-node owners |
| `igNamedObjectList` | Exact `igNamedObject` references and nullable names |
| `beWeaponSeqDataList` | Exact sequence-row references; nullable name and label strings in slots 2 and 3 |

The exported info list and all four weapon lists require their actual typed
objects. Valid authored empty lists remain empty. Missing lists and null list
elements fail validation. Node and bone-name counts must match. Lists retain
count, capacity, raw memory reference and canonical memory owner; storage must
contain exactly `capacity * 4` bytes. Counts and capacities use the verified
signed 32-bit serialization and cannot be negative.

The checked profile requires little-endian IGB data, exact field types and
widths, unique field slots, and exact weapon info/RAM/list/named/sequence
types. Derived info exports are skipped by the original exact-type selection;
derived RAM or row types fail the selected profile. Scene-node graph validation
belongs to `ActorSceneResource`; the weapon decoder does not substitute a
mesh or rebuild its geometry. Optional base-info data lists retain their
references after structural validation; their arbitrary controller effects
are not executed by this module. Serialized RAM parent/controller fields in
slots 3 or 4 are an unsupported restoration profile.

Source paths and individual strings are limited to 4,096 bytes without an
embedded NUL. One program permits at most 4,096 weapon infos, 4,096 total RAM
occurrences, 4,096 rows per weapon list, 65,536 exported infos, 131,072 total
decoded list links and 16 MiB of serialized string storage. Repeated aliases
still count toward metadata/occurrence budgets. The scene resource independently
bounds retained node graphs and geometry. Failures throw `WeaponResourceError`.

Null strings remain distinct from empty strings. In particular, the original
label helper preserves a null label at `8030B420` and `8030B5CC`; the decoder
does not turn it into an empty label. Executing consumers must preserve or
explicitly reject a profile they cannot represent.

## Registration and attachment ownership

The four serialized common-weapon RAM index values are all zero. They are not
their runtime player ordinals. Generic registration assigns the live ordinal
and parent for each occurrence. If references alias one RAM, later registration
writes affect that same owner while earlier messenger rows retain their own
registration keys. `WeaponProgram` preserves the evidence needed for those
final writes without applying them during decoding.

Original registration `80318298` allocates a manager's four attachment-tracking
slots only on its first weapon registration. These slots are shared by all
weapon infos on that manager. Consequently, removal through another weapon
info on the same manager/player slot must operate on earlier attachments.
The program's per-info metadata is not an alternative attachment-tracking
owner.

Actual generation creates one fresh bone selector for a matched weapon node
and shares it across every appearance returned by ACTION. Its weapon-node
child remains the original resource owner. The returned skeleton's ordinary
bone index is used, including helper bones with no skinning joint index.
Appearance mutation, retained query continuations, selector computation and
render capture are separate runtime responsibilities.

## Validation

The asset-free test suite covers explicit source lifetime, immutable program
copies, raw/canonical aliases, repeated exports and RAM occurrences, authored
signed values and noncanonical active bytes, null versus empty names/labels,
exact type selection, malformed fields/lists/storage and aggregate bounds.
The current Release suite passes 61 asset-free checks and 89 additional checks
with the supplied original resources, for 150 total.

The optional original-data invocation is:

```text
dw4_weapon_resource_tests EXTRACTED_ROOT
```

It checks `_common/w_common05.igb`, `w_common04.igb` and `w_common03.igb` from
`ndmw.afs` entries 916, 915 and 914. Their weapon info references are 36, 17 and
53. Together they contain 12 RAM occurrences, five node/bone pairs and 63 ordered
sequence rows. Checks include actual right/left bone names, left-node flags,
shared node identity, geometry children and the supplied axe/slash, gun and arm
sequence labels. This validates loaded definitions and ownership, not complete
weapon rendering or player construction.
