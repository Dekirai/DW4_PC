# Native bootstrap object allocation and base construction

The authored native implementation reconstructs USA functions `80066D60`,
`8006665C` and `80066710`, including the reached `80068184` base-address
selection and known base virtual `80042DEC`. Evidence comes from the supplied
USA DOL with SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.

## Allocation

The raw allocator captures wrapping `size + prefix` before any callback. With
no supplied pool it reads the explicitly represented default handle `805621E0`
and enters `800607F4`. A selected pool receives virtual `+D8(total bytes)`;
its returned arithmetic pointer receives the prefix even when allocation
returned null. A null selected pool enters `8009281C(total bytes)` and returns
that result **without** adding the prefix. These are different original paths,
not the reflected `8006546C` file allocator.

`IgbBootstrapPointer` retains represented physical storage and an original
32-bit offset. Its arithmetic address never becomes a host pointer. Nonzero
arithmetic results without physical storage remain invalid to dereference.
Allocation providers remain explicit; the generic pool adapter connects only
the recovered `800607F4` lookup, without fabricating a heap or pool allocation.

## Header and pool tag

`8006665C` stores the base vtable, reads the existing physical reference word,
replaces its low 23 bits with one, clears byte `+05` bit 7, then clears byte
`+04`. An unknown or truncated old reference word fails at its reached read,
after the vtable store. Native default member values do not initialize malloc
bytes. Physical word and subword writes synchronize represented file, metadata
or internal-list reference owners.

`80066710` performs its initial vtable store and base reset before reading
live ArkCore `+00`. An enabled core enters the fresh object virtual `+58`,
subtracts live metadata `+20`, selects the allocation pool through `800590A0`
and stores the low byte of pool virtual `+16C`. A null pool or disabled core
stores zero. The known base vtable selects the canonical `805622A4` metadata
cell; a changed unknown vtable remains an explicit operation. Repeating the
separate base reset clears an earlier pool tag, as required by the metadata
and internal-list constructors.

Missing state pauses at the reached boundary; represented null owners fault
where dereferenced. Pending operations retain their arguments and continuation.
Recursive use, competing construction and abandonment cannot silently reuse
partially constructed storage. Typed bindings preserve native object identity;
conflicting types or identities at the same offset are rejected.

## Validation and scope

The asset-free suite checks pooled and heap paths, wrapping size/address
arithmetic, null-plus-prefix results, callback retention and reentry, physical
failure prefixes, dirty header state, typed synchronization, live core/vtable
changes, allocation-prefix subtraction and construction lifetime.

The metadata and internal-list factories compose these bodies; their focused
original-instruction comparisons document their own prepared allocator and
registration endpoints. These constructors do not establish complete engine
startup, generic reflected construction, field registration or dependency loading.
