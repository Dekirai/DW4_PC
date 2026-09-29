# Native string field object initialization

`igb_field_string_init` reconstructs USA `80071A9C`, the string descriptor's
object-default initializer, and its assignment helper `800715E8`. It is distinct
from `80071694`, which changes the descriptor's own default buffer. Evidence is
the supplied `main.dol`, SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.
Original instructions and assets remain outside the repository.

## Default wrapper

The wrapper first reads descriptor `+20` and the string pointer at that buffer's
first word. A null string skips pool creation and interning completely. A
nonnull string follows the original lazy `80562140` sequence: `80054140(16)`,
conditional `80053F28`, raw global publication, then `80054094` using the freshly
loaded pool and the captured source pointer. Initialization may overwrite a
callback's intervening pool replacement, retaining its displaced host owner
without a guest release.

Arithmetic string pointers resolve through `IgbFieldValuesServices::strings`.
Character storage uses the owner's `native_name` when present, otherwise its
explicit host string. Missing bindings remain pending; an unknown nonnull
character source is never treated as a null pointer. The existing
`make_native_igb_string_factory` can supply actual interning. Missing pool,
allocator, or release operations remain explicit dependency boundaries.

The returned intern pointer is stored in a represented four-byte temporary.
The wrapper invokes actual assignment and then rereads the temporary, decrements
its full 32-bit reference word, and calls `8007381C` only when the result is zero.
It neither substitutes a direct pointer copy nor omits temporary ownership.

## Assignment order and physical state

`IgbFieldStringAssignInvocation` exposes the actual assignment helper separately.
Its destination offset is captured from descriptor `+08` on helper entry, after
the wrapper's interning call. It then:

1. Reads the source pointer and increments that string's full reference word.
2. Reads the destination pointer, decrements that old string's full reference
   word, and invokes `8007381C` on zero.
3. Rereads the source pointer and stores it to the captured destination.

A callback can change the later source pointer. The first string retains the
increment while the later pointer supplies the store. A callback changing the
descriptor offset cannot redirect an assignment already entered. Self-assignment
still performs both reference operations, including unsigned wrap and a possible
zero-count release. Pending release does not repeat any earlier arithmetic.

The store updates only its four physical bytes. For a typed target field, common
name member `+0C` updates `name_string_address`, header identity, and the available
host name cache; object-field pool name `+40` updates `word_40`. Displaced host
owners and cached strings remain retained without inventing guest operations.
An unbound raw pointer copied after a callback stays numerically represented
with an unknown header, rather than becoming a known null pointer. Copying it
does not itself dereference its characters or reference header.

## Validation scope

Permanent tests cover null defaults without services, null-buffer failure order,
physical padding, canonical name and pool-name publication, destination changes
during interning, captured offsets across pending release, source rereads,
self-assignment, full-word wrap, lazy pool publication, late pointer/character
bindings, native `80054094` cached lookup, reference prefixes before a physical
read fault, recursion, and abandoned-operation lifetime. Prepared dependency
fixtures isolate wrapper order; the native-interner case composes actual table
lookup and retain behavior. These checks do not establish complete native
startup, all string-container allocation/release bodies, or playable gameplay.
