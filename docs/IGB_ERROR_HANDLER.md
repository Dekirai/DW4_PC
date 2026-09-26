# Native error-handler registration and startup

The USA error-handler class is an eight-byte object registered by wrapper
`80031250` and body `80031278`. Its canonical metadata cell is `80561C2C`.
The authored native module composes the existing prerequisite runner, generic
registrar, callback lists, pool lookup, reflected factory and object initializer.
It does not replace the original error-handler singleton with a host dialog.

The class derives from `igObject`, uses vtable `804759D8` and adds no fields
of its own. Full getter `800311D4` registers on null or missing ready bit,
then reloads the cell; field callback `800312F0` calls that getter. Plain
vtable getter `80094010` only reads the cell. There is no allocation-only
getter at `80031198`, which is the end of an unrelated preceding function.

Layout probe `80031210` calls the original base reset on its represented
eight-byte local, captures ArkCore, stores the derived vtable, reads offset
`+394` from the captured owner and returns the selected local word. The native
profile admits aligned offsets zero and four. A missing offset suspends after
the completed stores; changing the singleton cannot replace a captured core.

## Two separate callback lifetimes

Defaults `80031310` append `80031358` through actual
`80065D94 -> 8003E4D8 -> 8003E8B8` to ArkCore's bootstrap callback list at
`+28`. They schedule later teardown; they do not execute it during defaults.

The registrar's separate trailing callback `80031338` executes through its
returned callback list. It calls `80047080 -> 800470F4`, which checks:

1. Singleton `80562120`: a nonnull value skips construction.
2. Metadata `80561C2C`: null skips construction.
3. Guard byte `80562124`: nonzero skips construction.
4. Pool handle `805621F0`: only the reached constructor path reads it.

The pool handle aliases the same canonical owner used by thread startup;
it is distinct from metadata allocation handle `805621F4`. Unknown inputs
remain pending. Allocating `IgbErrorHandlerState` does not initialize them.

Object factory `8003119C` always executes registration wrapper `80031250`,
then freshly reads metadata and calls `8006546C` with its captured incoming
pool. Native reflected initialization writes the actual header and updates
instance ownership. Only the returned object is published to the singleton,
which is then freshly read for the caller's result.

The runtime adapter admits only verified literal base no-ops at vtable slots
`0C / 24 / 2C / 34` and plain getter `94010` at `58`. Other slots remain
explicit dependencies. A raw allocation binds the distinct `error_handler`
runtime identity; it does not supply a preinitialized header or ready instance.

## Teardown and continuation guards

Deferred `80031358 -> 800470A0` captures the singleton and, when nonnull,
decrements the full reference word at `+4`. It freshly tests the low 23 bits
and calls `80066E1C` only when they are zero. The selected destructor must
return before publishing a null singleton and then guard byte one. No host
delete or successful placeholder substitutes for that destructor.

The reflected factory and object initializer accept a borrowed enclosing
fault check. Allocation and initializer callbacks cannot swallow recursive
outer entry and then permit later initialization or singleton publication.
The check exists only during a resume, while the selected child continuation
survives suspension. Existing registrar/prerequisite/bootstrap callback
operations retain their own local guards; this change does not claim complete
parent-guard propagation through those separate APIs.

Primary evidence is the supplied USA DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, inspected through IDA and bounded
original execution. Original bytes and comparison corpora remain local.
See [VALIDATION.md](VALIDATION.md) and
[IGB_REF_COUNTED_LISTS.md](IGB_REF_COUNTED_LISTS.md). Registration and this
singleton alone do not establish full startup or playable gameplay.

At this milestone, the retained cold composition reaches context-extension wrapper
`800374E8` with 31 types, 102 descriptors, 114 default buffers, 12 prototypes,
119 enabled allocations and six runtime objects. The error handler inherits
the actual two base descriptors and owns one initialized eight-byte instance.
The bootstrap list retains both its earlier thread-manager teardown and the
new error-handler teardown, while immediate callback cleanup has completed.
Original BSS production, pool allocation and OS startup remain explicitly
prepared prerequisites in this test.
