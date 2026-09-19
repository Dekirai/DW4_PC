# Native game clock

`NativeGameClock` reconstructs the original timer reached by the HITLAND
transform visitor. It keeps the original integer lifecycle and replaces the
platform time source with actual PC monotonic nanoseconds. A freshly constructed
clock is stopped; startup explicitly calls `start()`. There is no fabricated
constant timestamp or per-frame increment. Construction does not invoke the
timer's platform callback.

## Original binding and startup

Evidence is from the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Addresses are original virtual
addresses used for provenance, not runtime host addresses.

Transform dispatch `801F6ED4` calls visitor virtual `+68` at `801F6F60..70`.
The actual visitor method `801D138C` returns the visitor's own `+10/+14` time
when its bound-time byte `+18` is nonzero. Otherwise it loads the singleton at
`r13-2C04` and calls that object's virtual `+68`. Startup establishes
`r13=80564F00`, so this singleton is `805622FC`. This is distinct from the
MODELCTRL context's `+50/+54` time fields.

`igArkCore::initCore` at `8003DA50..6C` creates the timer through `80023670`,
stores it in this singleton, then calls virtual `+6C`. The factory selection is:

| Registered type | Registration / redirect | Result |
|---|---|---|
| `igTimer` | `80023748`, redirect `800237EC` | Selects `igLongTimer` |
| `igLongTimer` | `8002CD80`, redirect `8002CE88` | Selects `igGamecubeLongTimer` |
| `igGamecubeLongTimer` | `8003B534`, vtable probe `8003B4B4` | Concrete vtable `804703A4` |

The concrete vtable inherits the integer timer operations from `igLongTimer`.
Its virtual `+94` is the hardware sampling method `8007734C`; virtual `+60`
returns a resolution of 20 nanoseconds. The original startup call `+6C` is
start/resume, not reset.

## State and default construction

| Original offset | Reflected field | Native field | Fresh default |
|---|---|---|---|
| `+08` | `_startTime` | `start_time` | 0 |
| `+10` | `_additionalTime` | `additional_time` | 0 |
| `+18` | `_resetTime` | `reset_time` | 0 |
| `+20` | `_isStopped` | `stopped` | 1 |

Metadata `8002CDF4` registers these four fields. Their factory/name/global/offset
tables are `80465288`, `804652CC`, `804652DC` and `804652EC`. The first three
are `igLongMetaField`, vtable `80472100`, with eight-byte size method
`80054BC0`. The stopped field alone receives an explicit default override of
one through `8003EC68` at `8002CE40..44`.

The zero defaults follow the same reflected-field initialization used by the
recovered player factory. Registration `800659C0` attaches and finalizes each
descriptor through `80063BA8/80063F14`. `80063C74` obtains its default buffer
through `80068254` and the built-in pool's calloc operation. The arena and
malloc implementations `800833D4/8008A7A4` ensure fresh zeroed bytes; their
lower allocation paths supply zeros when the outer clear is skipped. The long
field's default setter `80063D04` leaves this buffer unchanged for a null input.
Runtime `800667F4 -> 8006712C -> 80063DD0 -> 80063E50` copies those default
bytes to the object. Concrete timer virtuals `+24/+2C/+34` are the inherited
no-ops `800667B0/800667D0/800667CC`; they do not override these values. These
defaults do not depend on uninitialized heap memory.

This establishes the original built-in factory profile. It does not reconstruct
the complete type registry, allocator lifecycle, core startup or scheduler.
Host `state()` exposes these fields for explicit restoration; it does not
silently start, sample or normalize a restored nonzero stopped byte.

## Integer lifecycle

All three time values are 64-bit words. Native unsigned arithmetic preserves
the original carry/borrow and wraparound without signed overflow in C++.
Any nonzero stopped byte means stopped.

| Virtual / original entry | Native operation | Ordered effect |
|---|---|---|
| `+68 / 80093630` | `time()` | Stopped: return additional time. Running: sample, then return additional + sample - start. No stores. |
| `+6C / 80093728` | `start()` | Only if stopped: clear stopped, sample, write start. Keep additional and reset. |
| `+70 / 8009377C` | `pause()` | Only if running: sample, add sample - start to additional, set stopped to 1. |
| `+74 / 800937F0` | `reset()` | Copy reset to additional; if running, sample and write start. Preserve stopped. |
| `+78 / 8009384C` | `stop()` | Set stopped to 1, copy reset to additional. No sample or baseline edit. |
| `+7C / 80093868` | `restart()` | Clear stopped, copy reset to additional, sample, write start. |
| `+84 / 80093910` | `set_reset_time()` | Change reset only; current time is unaffected. |
| `+8C / 8009391C` | `reset_time()` | Return reset. |
| `+90 / 800939B4` | `stopped()` | Return the raw stopped byte. |

The running getter and pause operation read live fields after the platform
sample. Reset/restart publish their reset accumulator before sampling, and
start/restart publish their running byte before sampling. The native methods
retain this order, including when a test provider changes fields or fails.
The owner is synchronous and belongs to one thread. Missing, throwing or
recursive providers fault it without rolling back an already visible prefix;
later operations cannot silently retry an incomplete sample. This failure
handling is a native service contract, not an original hardware exception model.

## Platform source and numerical comparison

Original `8007734C` calls `OSGetTime` at `80262284`, reads bus frequency from
`800000F8`, and invokes signed 64-bit division `800A3AF8`. Its exact conversion
is:

```text
divisor = (unsigned_bus_clock >> 2) / 125000
numerator = signed_64_bits((ticks * 8000) modulo 2^64)
nanoseconds = numerator / divisor, truncated toward zero
```

For a 162 MHz bus the divisor is 324. `original_gamecube_clock_nanoseconds`
implements this conversion for validation, including wrapped signed numerators.
It rejects a zero divisor. The PC time source does not use this hardware
conversion or read emulated registers.

`native_monotonic_clock_source()` uses `std::chrono::steady_clock` and one
shared process-local epoch. Every invocation takes a fresh sample and returns
elapsed nanoseconds; separate source objects share that epoch. Actual clock
resolution can permit equal consecutive samples. The timer's explicit
`start()` samples its own baseline, so elapsed game time is independent of
when another host source was created.

The independent instruction oracle executed the actual original lifecycle,
visitor getter, platform conversion, stable timebase read and signed division
on authored input words. It compared native results, sample counts and final
fields. Provider mutation/failure cases were injected at the actual `+94`
entry and are separately identified as service-contract fixtures. Matching
arithmetic for equal supplied inputs does not imply identical PC and console
sample streams, scheduling or timing resolution.

## HITLAND integration and remaining work

`HitLandNodeDispatchServices::game_clock` holds an explicit shared native owner.
The existing `global_clock` callback takes precedence if supplied. An unbound
visitor selects this service only when it reaches the original clock call;
a bound visitor does not sample it. The selected owner and returned time stay
with the retained dispatch while a later node input update is pending. A
completed dispatch does not resample on retry. Absence of a required service
continues to report the original boundary.

The floating-second wrappers (`+5C`, `+64`, `+80`, `+88`), resolution API,
whole core startup, frame pacing, scheduler, suspend policy and global pause
integration remain separate work. The original `+80` setter includes a
floating conversion through `800A4130`; it is not replaced with a guessed
host cast. This clock is a recovered native service, not evidence that the
whole animation or gameplay update pipeline is complete.

The local test suite exercises lifecycle ordering, noncanonical stopped bytes,
modulo arithmetic, failure prefixes, callback field reloads and the real PC
monotonic source without sleeps or a strictly increasing-tick assumption.
The final local build passes 120/120 CTest suites, including 60 clock checks.
The final production-core comparison covers 4,264 lifecycle cases, 4,001
hardware conversions and 12 forced timebase rollovers with zero mismatches.
Transform comparison and original player-frame results are recorded in
[VALIDATION.md](VALIDATION.md).
