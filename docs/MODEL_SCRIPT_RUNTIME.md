# Prepared target script execution

Version 0.28 connects explicitly registered embedded Lua chunks to the original
target-Hit context and conditional HP publication. `ModelScriptRuntime` owns one
shared Lua VM, an immutable ordered `ScriptRegistry`, retained target work and
pending invocation frames. This is a prepared combat component; complete player
construction, damage services and the original game loop remain required.

## Ownership and integration

The collision adapter must first bind and prepare the actual target body. Its
`prepare_target_script_work(id)` then exposes a retained `ModelScriptWork` with
an alias to that model's live pose matrix. The two HP wrappers initially contain
zero, as in the original factory. They are distinct objects, and the record is
a separate signed integer initialized to -1. These are not player starting stats.
Actual player preparation must bind the existing GAMERAM HP/MHP objects.

Register the returned work with the script runtime, then supply
`runtime.handle(request)` as `ModelCollisionServices::hit_script`. The script
runtime retains the strong work owner. The event adapter keeps a weak reference
to avoid a model/work/matrix ownership cycle; an expired prepared owner stops
GETRECNO/CHKBULLET instead of silently manufacturing replacement state.
GETRECNO reads the live signed record directly. The collision HP-presence gate
reads the current shared HP reference, including replacement or removal.

The Lua VM may be supplied explicitly to retain an existing Lua heap/globals.
Construction installs the runtime's host dispatchers; another owner must not
replace those dispatchers or execute unrelated work during a pending call.
`initialize` executes explicit synchronous initialization before target calls.
This does not establish the original boot manager registration order.

## Execution and suspension

The target context formats `prefix_targetName.lua` and publishes `event` in the
shared context. The selected top-level chunk executes; `Hit` is not a Lua
function name. Each explicitly registered LuaInfo contributes its first matching
filename. Repeated registrations execute separately in order, in the same VM.
Unknown earlier names and unsupported first matches stop selection rather than
fall through to a later duplicate.

The original selector `80301FA4` initializes status to -1 and overwrites it with
each matched dispatch result. A supported completed chunk maps to status 0 via
the loader/protected runner (`80302AE4`, `80272768`, `80278810`, `80278648`). Lua
return values do not supply that native status. After all registrations, final
status -1 invokes the actual filesystem service, including when no embedded
name matches. Missing filesystem ownership remains explicit. A completed file
service supplies its signed native status, even -1; the target helper itself
does not condition HP publication on that status.

An unavailable native host method pauses the exact Lua CALL. Installing a
service and retrying continues its existing arguments, globals, stack and
instruction cursor. Once a service has been invoked and reports incomplete,
that particular callback owner is retained until completion. Replacing the
default service cannot replay its partial effects through a different owner.
The filesystem callback follows the same ownership rule.

Nested scripts enter through their actual parent host callback and share the
VM, aggregate invocation budgets and model context. When a child pauses, its
parent callback must also report incomplete and resume that same child on retry.
Completed child calls permit later siblings. Nested context is not restored;
the outer HP merge and direction result read the live context left by the child.
Unrelated top-level requests cannot replace a pending invocation. Exceptions,
invalid bytecode and uncertain effects fault the owner and prohibit replay.

## Verified model methods

These methods read the published shared context, with no explicit arguments:

| Lua method | Dispatch ID | Context offset | Helper |
| --- | --- | --- | --- |
| `be:MDLgetRamNo()` | 60 | `+04` | `802ED8C0` |
| `be:MDLgetRecNo()` | 61 | `+14` | `802EE01C` |
| `be:MDLgetBulletKind()` | 70 | `+1C` | `802EDBF8` |
| `be:MDLgetBulletBoxNo()` | 72 | `+20` | `802EDC44` |

Each returns one exact signed 32-bit value as a Lua binary64 number. Dispatch
IDs come from the packed registration stream `804242AC` and descriptor-driven
switch `802EA1FC`, not ordinal positions in the string table.

`be:MDLsetDMGTurn(number)` is dispatch 76, helper `802EDE94`. The original numeric
argument conversion (`8026A434`, case `8026A698`) truncates toward zero; the
helper sets context byte `+30` to one only when the resulting integer equals
one, otherwise zero, and returns no values. This implementation accepts finite
numbers whose truncated values fit int32. Other argument layouts, native type
diagnostics and out-of-range/invalid floating-point behavior remain explicit
boundaries. In particular it does not substitute general Lua truthiness.

All other methods require actual native services. The unchanged original
`hit_p_agumon.lua` can exercise its RAM/kind early returns; the ordinary magic
path additionally clears damage turning and then requires
`be.BLPcalcKoukaMagic`. No damage or status result is fabricated.

Source: USA revision-0 DOL SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. The original Hit chunk is in AFS map
entry 179, LuaInfo 2783, export 237, data index 26, LuaData 1700, buffer 2675.
Original bytes are loaded from the owner's local extraction and never tracked.

The original-data suite binds all four actual Agumon MODELCTRL RAMs. An
independent comparison with official Lua 4.0.1 additionally matches 24
mismatched RAM/kind early returns and four kind-80000 native boundaries: 64
ordered calls, 92 exact rows and 736 instructions, with zero differences.
Twelve deliberate unavailable-service retries preserve the committed prefix.
Kind 80000 stops at PC 58 after 43 instructions and four calls with direction
zero. The reference deliberately errors at its missing BLP service while the
native interpreter retains that CALL; neither supplies a damage result.

## Bounds and remaining work

The runtime admits at most 4,096 qualified work owners. Shared context nesting
is bounded at 32, and the registry/VM enforce their own byte, frame, instruction
and allocation limits. Lua heap budgets remain cumulative and no original
garbage collector is implemented; a retained Hit invocation does not establish
indefinitely running gameplay. Native damage, status, source BHit, sequence
entry, GAMERAM construction and outer controller scheduling remain required.
See [MODEL_SCRIPT_CONTEXT.md](MODEL_SCRIPT_CONTEXT.md),
[SCRIPT_REGISTRY.md](SCRIPT_REGISTRY.md) and [LUA4.md](LUA4.md).
