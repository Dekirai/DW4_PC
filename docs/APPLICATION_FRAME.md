# Application time and outer frame entry

`ApplicationFrameInvocation` reconstructs the original `igInsightCore` update
at `8028AF84`. It samples the application's own timer, publishes the original
time fields, then invokes the dispatcher and scheduler in that order. Both
downstream services remain explicit requirements. This entry does not substitute
a loop over player slots for the original manager scheduler.

`ApplicationFrameState` derives from `ModelSceneContext`, so a manager can share
this exact context owner and read the newly published `world_time` through its
existing RAM timer helpers. `publish_system_frame_time` separately reconstructs
the reached `beSystem` timing leaf that produces `frame_time_f`. It belongs at
the actual system-manager update site, not at every player or subcommand call.

## Original owners and initialization

Evidence comes from the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Addresses identify original
instructions and metadata; the native module does not execute guest code.

`igInsightCore` is registered at `802862C8`, with metadata `80286368` and vtable
`804CBE58`. Its reflected timing fields are:

| Offset | Original field | Represented field |
|---|---|---|
| `+30` | `_timer` | `timer` |
| `+38/+3C` | `_currentTime` | `current_time` |
| `+40/+44` | `_frameTime` | `frame_time` |
| `+48` | `_currentToWorldTimeFactor` | `current_to_world_factor` |
| `+50/+54` | `_worldTime` | inherited `world_time` |

The three 64-bit scalar fields and raw factor have zero metadata defaults.
The post-default callback `8028AB54` later writes `0x00040000` to the factor
at `8028ACF0/F4`, after establishing renderer ordering. The native state leaves
the raw scalar defaults at zero. `initialize_application_frame_factor` exposes
only that explicit factor store; it does not claim to perform the surrounding
renderer, receiver or registry initialization.

The `_timer` descriptor is auto-owned: `802864EC..8028650C` supplies the
`igTimer` type and enables its factory. It creates a separate concrete long
timer through the same type redirects documented in [GAME_CLOCK.md](GAME_CLOCK.md).
It is not the global HITLAND timer singleton. The native caller supplies this
distinct timer owner explicitly.

Original application startup `8028AA24` clears frame/current time, visits the
live registered manager list through each manager's virtual `+64`, and only
then starts the application timer through virtual `+6C` at `8028AA94..AAA4`.
The corresponding stop path `8028AAC4` pauses the timer before manager `+68`
calls. These full startup/stop bodies are not implemented by this module.
Constructing a state or initializing the factor does not silently start a
clock or complete manager startup.

## Exact time prefix and call order

The frame retains the application owner. At the original `+30` load it captures
that owner's actual `NativeGameClock`, then invokes its `time()` once. Fields
read after the callback remain live, including the previous current timestamp,
factor and accumulated world time.

1. Subtract previous current time from the sampled value modulo 2^64 and store
   the frame-time low/high words at `+44/+40`.
2. Store sampled current time at `+3C/+38`.
3. Read the signed 32-bit fixed-point factor. Multiply the frame delta by its
   sign extension, retaining only the low 64 product bits. Interpret those bits
   as signed and arithmetic-shift right by 18. Add that increment to world time
   modulo 2^64 and publish `+54/+50`.
4. Select dispatcher `+20`, call its virtual `+5C` at `8028B018..28`.
5. After that call completes, select scheduler `+0C`, call its virtual `+5C`
   at `8028B02C..3C`.

The multiplication must wrap before the shift. Using a wider product and
shifting it before narrowing changes large or negative inputs. No clamp,
fixed timestep, host-render delta or per-player clock read is introduced.
The scale setter `8028B054`, which multiplies a float by 262144 then uses
`fctiwz`, remains a separate unimplemented operation; exposing the raw factor
does not claim equivalent floating setter behavior.

Native service callbacks can suspend. A selected callback is retained across
retries; completed time publication and earlier calls are not repeated. An
absent callback reports its required service. A dispatcher may replace the
scheduler before the latter's original selection point. A new invocation
cannot start another update on an application with a pending frame. Provider
exceptions, recursive execution and abandoning a started incomplete frame
fault the owner while preserving its completed prefix. These are native
continuation contracts, not claims about original hardware exceptions.

## Concrete scheduler and system timing

The abstract `igInsightScheduler` factory redirects through `80285BB4` and
`80284728` to `igSerialScheduler`, registered at `802846AC`. Its concrete
vtable is `804CC018`; virtual `+5C` targets `8028B7B0`. This body copies and
retains the registered manager list into cached storage, captures its count,
then calls each cached manager's virtual `+74` in order. The cache is cleared
afterward. [SerialSchedulerInvocation](SERIAL_SCHEDULER.md) now reconstructs
that body, including its live source/cache reloads and retained pending manager
calls. `make_serial_scheduler_update` binds it to the application entry.
It does not establish the complete game manager registration order or implement
all those managers. An absent scheduler or required manager implementation does
not return successful no-op completion.

`beSystem` vtable `804DFB20` supplies update `80316258` at `+74`. The time leaf
`80316320..64` occurs after reset-button handling and before stage/DVD/freeze
processing. It captures system `+0C` context and its world timestamp, then:

```text
frame_time      = world_time - saved_world_time   // wrapping 64-bit words
frame_time_f    = original_elapsed_frames(signed interpretation of frame_time)
saved_world_time = captured world_time
```

The signed conversion and binary32 division by `16666667.0f` reuse the already
verified `original_elapsed_frames` helper. The system result is uncapped;
negative deltas and wrapped differences retain their original behavior. No
timer sampling occurs in this leaf. It requires the system's actual context
owner and leaves all fields untouched if that owner is missing.

System metadata `802B7838` gives `_svWorldTimeL(+18)` and `_frameTimeL(+20)`
zero defaults. `_frameTimeF(+28)` explicitly defaults to `1.0f`: descriptor
ordinal three is selected at `802B7884`, constant `8041D5B0` is word
`3F800000`, and `8004D4BC` applies it at `802B7894`. The concrete system's
post-default virtuals `+24/+2C/+34` are inherited no-ops. `ModelSceneSystem`
now represents these timing fields and the inherited context owner. The
timing leaf does not bypass or implement the surrounding system update.

## Validation and remaining integration

Permanent tests cover exact fixed-point word results, shared context/system
publication, live field changes during sampling, downstream call order,
pending callbacks, owner replacement, concurrent frame rejection, failure and
abandonment prefixes. Prepared virtual endpoints are labeled as such; they do
not qualify missing game services as implemented.

The ignored raw oracle executes every instruction of `8028AF84` with explicit
prepared timer-return, dispatcher and scheduler endpoints. Its corpus contains
11,296 boundary/random cases, 587,392 original instructions and 52 unique
instruction addresses. The final production-core test executable matches all
11,296 cases with zero mismatches; its permanent suite passes 45 checks, and
the full local build passes 122/122 CTest suites. The oracle verifies the
low/high store order and that both
downstream endpoints see the completed time prefix. The integer decoder's new
rotate-insert/arithmetic-shift handling received an independent review against
the actual instructions. The fixture does not execute the prepared endpoint
bodies; [GAME_CLOCK.md](GAME_CLOCK.md) separately records timer validation.

Complete application startup, dispatcher behavior, manager registration and
serial scheduling, full system processing, renderer timing and repeated
playable gameplay remain integration work. A correct native time producer is
necessary for those paths; it does not make a prepared callback loop a complete
original frame scheduler.
