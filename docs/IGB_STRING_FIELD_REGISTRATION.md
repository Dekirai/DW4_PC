# String and signed-byte field registration

The native registration framework represents `igStringMetaField` and
`igCharMetaField`, the signed-byte descriptor class used by inherited copy
method fields. Their metadata and prototype cells share the existing factory
and field-value owners. They remain distinct kinds of object.

Evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. The implementation and synthetic
fixtures are authored C++; original binary and instruction corpora stay local.

## Original class profiles

| Property | igStringMetaField | igCharMetaField |
| --- | --- | --- |
| Wrapper / body | `800253DC` / `80025404` | `80037D00` / `80037D28` |
| Metadata cell | `805615B4` | `80561E1C` |
| Native size / abstract flag | `0x3C` / `0` | `0x34` / `0` |
| Parent body / getter | `80027360` / `80025494` | `8002A6D8` / `80021D70` |
| Metadata callback | `80025474` | `80037D90` |
| Registered getter | `8002530C` | `80037C3C` |
| Layout probe | `80025348` | `80037C78` |
| Defaults callback | `8002549C` | Null |
| Trailing callback | Null | Null |
| Extra-callback table | `8055D0E8` | `8055D670` |
| Extra callbacks | `80025628`, zero | `80037DB0`, zero |

Each wrapper calls callback runner `80066188`; each body calls native
registrar `80066204` without prerequisite guard `80021B94`. String inherits
`igRefMetaField`; signed byte inherits `igMetaField`. Their registered getters
test the canonical pointer and readiness bit four, conditionally invoke the
wrapper, then reload the same cell. Neither profile has a represented
allocation-only getter. Address `80037C00` is inside the signed-byte clone's
disabled branch and is not a lazy metadata getter.

The cells alias `IgbMetaFieldFactoryServices::metadata_slots[string]` and
`metadata_slots[signed_byte]`. Registration, enabled factory and reflected
metadata lookup therefore observe the same owners. Missing representation
remains pending; a known null remains an original null pointer.

## Layout probes and reflected object virtuals

The string probe uses a `0x3C` local and actual reset `80071E44`. It captures
the fresh ArkCore `+394` selector and selected local word, then writes the
derived, reference and base vtables before releasing the local `+0C` string.
It has no object-field `+40` cache or memory-field `+48/+4C` cleanup.
The signed-byte probe uses actual reset `8003FF90`, captures the same kind of
selected word, then writes its derived and base vtables before name cleanup.
The return remains the word captured before cleanup callbacks.

| Object operation | String `8047693C` | Signed byte `80473F24` |
| --- | --- | --- |
| `+0C` | `800667A4`, return-only | `800667A4`, return-only |
| `+24` | `800667B0`, return-only | `800667B0`, return-only |
| `+2C` | `8007144C`, default-buffer tail | `800667D0`, return-only |
| `+34` | `800667CC`, return-only | `800667CC`, return-only |
| `+58` | `800940F8`, plain metadata cell | `80093E8C`, plain metadata cell |

String tail `8007144C` calls the return-only base tail, reloads its physical
vtable and selects `+8C` with null input. The shared descriptor-tail operation
then runs the existing native default-buffer preparation. It is not an empty
tail. The string's separate `+30` cleanup (`80071414`) is outside these reached
initializer adapters. Unknown virtuals remain explicit service operations.

## Prototypes and following clone work

| Class | Prototype callback | Prototype cell / values alias | Plain prototype getter |
| --- | --- | --- | --- |
| String | `80025628` | `805615B8` / `string_prototype` | `80094100` |
| Signed byte | `80037DB0` | `80561E20` / `signed_byte_prototype` | `80093E94` |

The callbacks use the existing native sequence: null-only gate, fresh pool,
live class metadata, actual reflected construction, raw publication, registry
append, live prototype release, fresh ordinal count and finalization. The
aliases belong to `IgbFieldValuesServices`; no duplicate prototype cache is
introduced. Separate lazy prototype getters `800256C0` and `80037E48` are not
implemented merely by adding these callback adapters.

String defaults append one Boolean field and replace inherited shallow-copy
and destruction descriptors before final schema configuration; see
[IGB_STRING_FIELD_DEFAULTS.md](IGB_STRING_FIELD_DEFAULTS.md). Their captured
clone virtuals require the real signed-byte and Boolean implementations.
Enabled clones additionally require original reflected copy `800680B4` and
the copy operation selected by `80068060`; metadata registration or a cold
copy constructor does not establish that path.

The repository's validation report records executed local tests, original
comparisons and the composed cold boundary. Earlier measured object, integer,
memory and Boolean evidence remains scoped to those profiles. These adapters
do not themselves establish complete startup or playable game behavior.
