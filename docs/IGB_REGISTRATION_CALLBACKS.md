# Registration callbacks and the file registration entry

`igb_registration_callbacks` reconstructs the USA DOL callback runner
`80066188`, callback-list constructor `8003E848`, append `8003E8B8`, runner
cleanup `8003E85C(list, 1)`, prerequisite guard `80021B94`, and the
`8002F178 -> 8002F1A0` file registration entry. The module composes the native
`80066204` registrar and preserves the original function identities at each
remaining boundary. It does not manufacture registered metadata or completed
file field defaults.

The source evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. No original binary or generated
instruction listing is included in the repository.

## Callback list behavior

The callback list has backing at `+00`, signed count at `+04`, and capacity at
`+08`. This layout differs from the engine's reflected `igDataList` layout.
`igb_initialize_registration_callbacks` performs the original zero stores in
backing/capacity/count order after a separately represented allocation.

The runner invokes its captured registration function first. A null returned
list completes immediately. Otherwise it captures the signed count once and
reloads backing and the indexed function at every iteration. Mutating count
inside a callback does not change the admitted iteration count; replacing
backing changes the next function read. A null or physically unavailable
function fails when its indirect call is reached. Nonpositive counts skip
function calls but still reach list cleanup.

Cleanup captures nonnull backing for `80056378`, then calls `800564E8` for the
list. These allocator operations remain explicit. A paused callback or free
operation retains its selected operation and arguments. Successful list
release marks the represented list released; future native reads reject it.
The native representation does not emulate allocator metadata or arbitrary
reads through freed raw backing pointers.

Append grows only when signed count equals capacity. Zero capacity becomes
four. Nonzero capacity uses the original wrapping expression:

```text
new_capacity = old_capacity * trunc(signed32(old_capacity * 3) / 2)
```

Consequently capacity four grows to twenty-four, not six. Capacity is stored
before allocation. Null backing uses the captured ArkCore owner and its
`+34` pool with `80056138`; nonnull backing uses `8005624C`. The allocation
result is published directly, including null. Count and backing are then
reloaded, wrapping count plus one is published, and the captured physical
slot is written. An invalid slot preserves that incremented count prefix.
The original shifted byte index wraps at 32 bits before the physical bound
check.

The production adapters do not silently provide allocators. Synthetic tests
prepare physical allocations explicitly. Overwritten raw backing owners and
abandoned selected list/free operands are retained without inventing original
reference-count updates.

## Prerequisites and exact file arguments

`80021B94` is a global prerequisite guard, **not** the parent type registrar.
Its two represented optional bytes correspond to `80561488` and `80561489`.
Missing byte storage pauses at the reached read. An initialization byte of
zero resets the started byte and writes initialization one. A zero started
byte is set to one before these wrappers execute:

| Wrapper | Registration callback | Type |
| --- | --- | --- |
| `80023880` | `800238A8` | `igThreadManager` |
| `800240CC` | `800240F4` | `igThreadList` |
| `80023C40` | `80023C68` | `igThread` |
| `80031250` | `80031278` | `igErrorHandler` |
| `800374E8` | `80037510` | `igContextExt` |

The wrappers themselves use the native `80066188` runner. Their registration
bodies remain explicit function boundaries until implemented. The started
flag is not rolled back when a reached service pauses or fails. A separately
entered prerequisite invocation observes that flag and skips the sequence,
as in the original recursive guard. Reentering the same native invocation is
an error and permanently faults that continuation.

`igb_file_type_registration_arguments` aliases the existing
`IgbFileFactoryServices::file_metadata` optional cell for `80561A04`. It passes
these recovered values to the native registrar:

| Argument | Value |
| --- | --- |
| Name, abstract flag, size | `igIGBFile`, zero, `0x134` |
| Parent registration/getter | `80032950` / `8002F23C` (`igDirectory`) |
| Type getter | `8002F21C` |
| Layout probe | `8002EC9C` |
| Field/default registration | `8002F244` |
| Trailing callback | `8002F724` |
| Extension callbacks | `80030078`, `80034130`, `800344A0`, `8002A6B0`, `80036684`, `8002BF48`, `80029054`, zero terminator |

The layout probe identifies vtable `804724C4`; its stack construction is not
an instance factory. The trailing callback enters `8005068C`, which registers
`blindObjects` through ArkCore `+38` and `8006DC50`; it is not a no-op.

## Composition and remaining work

The function adapter dispatches only the exact represented file callback and
known wrapper identities. The file-factory adapter replaces its
`register_file_type` operation with the actual runner. The generic registrar's
`append_callback` operation composes the actual append invocation. Unknown
functions and other service kinds delegate to the remaining factory or pause
with the reached function address. Factories capture services weakly to avoid
cycles; selected operations retain the services needed to finish.

Fresh original file registration still needs prerequisite registration bodies,
real parent metadata, descriptor constructors and finalization, all sixty-five
file descriptors, and actual field handlers. In particular, inherited
`igDirectory` ObjectRef fields automatically construct an `igDirectoryList`
at `+18` and an `igInfoList` at `+1C`. Zeroing every pointer or preparing an
empty field registry does not satisfy original instance construction.

The permanent tests exercise exact argument identities and aliasing, every
nonzero byte value in the prerequisite gates, callback ordering and live
backing replacement, pending operation capture, malformed physical slots,
nonlinear/wrapping append arithmetic, published failure prefixes, lifecycle
retention and the production adapters. The already-registered metadata test
is explicitly a prepared caller profile; it does not claim a fresh original
registration completed.
