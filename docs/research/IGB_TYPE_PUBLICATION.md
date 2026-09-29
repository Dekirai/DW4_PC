# Runtime type publication

The native core reconstructs the USA executable's metadata publication caller
`800660AC`, pointer-identity search `80053998`, ArkCore append wrapper
`8003E450` and retained append prefix `80053E6C`. Appending then enters the
existing native `80053BC0` pointer-list implementation, including its actual
allocation-size queries, growth, zeroing and pointer-before-count stores.

The evidence is the supplied USA `sys/main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. This is authored C++ control flow;
the original instruction bodies provide reference evidence only.

## Original ordering

`IgbTypePublicationInvocation` first reads canonical singleton `80562100` and
its type list at `+18`. The identity search captures the signed count at list
`+0C` and backing at `+08` once, then returns the first identical pointer or
`-1`. It neither compares names nor checks whether reflection is enabled.
Nonpositive counts do not dereference a backing slot. Null target pointers
participate in the comparison normally.

The caller stores that result at metadata `+0C` before deciding whether an
append is required. An existing identity returns immediately without changing
references. For `-1`, it reloads the singleton and selected type list, increments
the incoming metadata's full 32-bit reference word with wrapping arithmetic,
then calls the raw append. This is not the low-23-bit destructor test used by
release operations elsewhere.

After append returns, the caller reloads the singleton and type list again,
performs a second identity search, and stores that exact result. A replacement
registry may produce another index or still return `-1`; publication does not
substitute the appended slot index, retry the append, or enable reflection.

## Native representation and continuation

`IgbTypePublicationServices` shares `IgbRuntimeTypesServices` and
`IgbPointerListServices`. Registry pointer-owner mappings follow the existing
canonical runtime lookup model: mapped native owners represent original pointer
identities. The signed count remains independent of vector length and cached
capacity. A reached slot requires physical owner mapping, with the same 262,144
slot safety bound as the existing lookup profile. A matching early slot does
not validate an unread malformed suffix.

The raw append additionally needs the metadata's explicit original arithmetic
address in `pointer_lists->values`. Missing identity pauses after the original
retain. Conflicting identities fail; host pointers are never converted into
guest addresses. The existing raw append then validates actual backing bytes,
initialized ranges and typed store mappings. This module does not synthesize
an allocator, infer allocation size from a host vector, or reconstruct unknown
pointer mappings.

Unknown singleton storage pauses at its reached read. A represented null
singleton or list fails when dereferenced. A null metadata input first executes
the search and then fails at its required index store. The initial index and
retain remain published after a later append failure. Pending append operations
retain their selected list and continuation; resumption does not repeat the
first search, index store or retain. Completed callback teardown precedes the
second global read. Abandonment preserves an acquired metadata owner without
decrementing its original reference word. Recursive or faulted invocations
cannot continue.

`make_native_igb_type_publication_factory` binds only the generic registrar's
`publish_type` operation. Its factory holds services weakly; selected operations
retain them. Other registration operations remain delegated. Layout, class
defaults and whole-runtime bootstrap are separate operations, not implied by
successful publication.

## Validation scope

The permanent asset-free suite exercises identity versus equal spelling, null
targets, duplicate entries, signed counts, malformed reached backing, disabled
reflection, existing registrations, wrapping reference words, native append and
allocator order, allocator suspension, registry replacement, a still-missing
second search, late address binding, partial failure, abandonment, recursive
entry and adapter lifetime. The allocation endpoints are explicitly prepared;
the publication, search and raw list bodies execute their native implementations.

An independent original-instruction differential comparison and the composed
cold-registration test are recorded with final results in
[VALIDATION.md](VALIDATION.md). These bounds do not establish complete class
registration or a completed original game boot.

The final comparison passed 1,022 cases, including 875 bounded failure
prefixes, with zero differences across 51,074 executed original instructions
and 253 addresses. Search, publication, retained/raw append, reserve/growth
and memset execute their original bodies; only backing-owner/pool lookup and
pool allocation virtuals are prepared. It compares core type-list pointers,
list reference/backing/count, physical backing words and metadata reference/
index fields through explicitly matched pointer-owner mappings. It does not
claim whole-object memory equivalence, arbitrary MMU behavior or complete
startup. The permanent publication suite passed 72 checks.

Final core SHA-256:
`933392B281D25E8CB8A7CCC7E8C6781ADF363B54D80DD1C65526358BA6C66931`.
The local build passed all 166 CTest suites; the ignored report
`work/native-type-publication-result.json` records matching source/driver hashes.
