# Original meter controller data flow

`MeterProgram` decodes exact exported `beMeterCtrlInfo` records from one retained
`ActorSceneResource`. `MeterRuntime` registers their actual RAMs with the native
messenger and implements the eight original meter commands plus the represented
per-RAM update. This is controller and node data flow. It does not render a HUD
or run the game's global controller scheduler.

Version 0.35 connects these owners to the prepared player-start continuation and
the real MODELCTRL HP provider across four original Agumon slots. Application HUD
drawing and the complete player lifecycle remain required. The interim walking
preview does not yet display these meters.

## Evidence and ownership

The supplied USA `main.dol` SHA-1 is
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.

| Original entry | Recovered behavior |
| --- | --- |
| `802CD708`, `802CD7A4` | Meter-info type and ordered ONE-list field |
| `802CDA24`, `802CDAC0` | ONE-data type and serialized fields |
| `802CD218`, `802CD2B8` | Meter-node type and its long-list/translation fields |
| `802CDBC0`, tables `804D130C`/`804D132C` | Eight command names and ordinals |
| `80307124` | METERCTRL registration through the ordinary messenger |
| `80307150` | Maximum-dependent initialization |
| `803071DC` | Bound-value calculation, smoothing and node sample-list update |
| `803076A0` | Command handler and retained MODELCTRL/GETHP binding |
| `802F2238` | Unconditional-zero structured hook, leaving ordinary fallback dispatch |

An info's field seven is an `igNamedObjectList` of `beMeterCtrlOneData` owners.
The handler selects its entry using the live signed RAM index before looking up
the command. RAM registration writes final parent/index state and resets its
four generic work words, preserving the raw active byte and ordered routing
rows. Physical RAM aliases therefore observe their last registration's parent,
including delivery through an earlier routing key. ONE and node aliases retain
independent shared mutable identities, rather than being copied for each RAM.

ONE fields retain maximum, time scale, smoothing step, target and current values,
two signed 64-bit time bounds, digit count, three raw flags and nullable shared
integer-wrapper bindings. Its actual `beMeterCtrlNode` retains the loaded graph,
mode, translation step and physical `igLongList` identity. Runtime updates mutate
the shared sample-time list used by that node. All original documents and node
owners survive the resource and registration facades.

Serialized lists have checked type, count, capacity and memory width; null object
rows and malformed required owners are rejected. The parser bounds each list to
65,536 entries, aggregate copied list entries to 262,144 and copied names to
8 MiB. Source ownership uses `ActorSceneResource`'s additional graph bounds.
The prepared format is little-endian IGB v6; original files remain external.

## Commands

| Command | Actual effect |
| --- | --- |
| `GETVALUE` | Append target as one float |
| `SETVALUE` | Read float zero, set target and current, mark dirty |
| `ADDVALUE` | Add float zero to target, clamp upper then lower, leave current alone |
| `GETPERCENT` | Append target divided by maximum; this is a ratio, without multiplication by 100 |
| `SETPERCENT` | If no bound value wrapper exists, set target from separately rounded `input * maximum / 100`; any nonzero input whose result is below one becomes one |
| `ADDPERCENT` | Capture maximum, scale float zero by maximum/100 with separate operations, add to target and clamp upper then lower |
| `SETMAXVALUE` | Store maximum first, then execute the maximum-dependent initialization helper |
| `SETMODELHP` | Capture string zero and integer zero, push the actual messenger packet, query MODELCTRL/name/index/GETHP and retain its shared HP/MHP wrappers |

`SETPERCENT` does not clamp above maximum. A negative nonzero percent becomes
one through its original minimum rule. Zero, including signed zero, follows the
actual floating operations. When a bound value wrapper is present, this command
does not even read float zero. `SETPERCENT` and ADD commands do not directly
change current or mark dirty; the update detects their target/current difference.

Ordinary messenger enabled-only filtering reads the live raw RAM byte. There is
no additional fabricated active gate inside these eight command handlers. Direct
`update(ram_id)` represents entry into the actual update virtual method; selecting
which RAMs receive updates remains the original scheduler's responsibility.

## MODELCTRL binding continuation

`SETMODELHP` captures its selected ONE before invoking other controllers. The
nested message is MODELCTRL / supplied name / supplied index / `GETHP`, with no
additional arguments after the original buffer push and selection. The provider
must return actual shared integer-wrapper objects: object zero is HP, object one
is MHP. The separately returned integer HP value does not become the binding.

The nested dispatch and original selected ONE survive service suspension. A
changed live meter RAM index does not redirect the unfinished binding. On return,
the handler publishes HP first, then obtains and publishes MHP, and only then
pops the packet. A wrong or missing second result preserves the committed HP
binding and unfinished child packet rather than pretending the command rolled
back. Null object results remain nullable bindings. Missing services remain
pending; uncertain callback faults cannot be replayed.

The real MODELCTRL GETHP path at `8030E49C..8030E4FC` returns the live HP integer
and actual HP/MHP wrappers only when its RAM is active and its work exists.
Otherwise it returns integer zero without wrapper objects. That latter packet
does not satisfy SETMODELHP's unchecked original object accesses; the native
implementation exposes a checked boundary.

## Update and node sample times

The first update, when RAM work word zero is zero, runs initialization, sets that
word to one and clears the other three words. Initialization computes the signed
decimal digit count of the truncated maximum, `time_scale = 2,000,000,000 / max`,
and `step = max / 60`, with step's minimum of one, then marks the ONE dirty.

When bound HP exists, the update reads that shared wrapper and the actual MHP
wrapper. It multiplies HP by 100 with 32-bit wrap, performs signed integer
division by MHP, converts the result to float, then applies the same meter
percent-to-target rule. The wrappers remain live when their originating player
row is replaced elsewhere.

The current value moves toward target by at most step per update. Its lower
clamp is tested before the upper clamp, unlike the ADD command ordering. Raw
nonzero integer mode truncates the clamped result; a nonzero value truncated to
zero becomes one. A changed value sets dirty. Dirty processing clears the flag
and the actual node sample list before filling it:

- Meter mode emits 1,000,000,000 at an empty endpoint and 3,000,000,000 at a full
  endpoint. Only current is clamped there; target remains unchanged. Interior
  values multiply current by time scale, truncate through the recovered
  `__cvt_dbl_usll` helper, apply the original signed time bounds, and add
  1,000,000,000 with 64-bit wrap.
- Digit mode truncates current and emits decimal remainders in units-first
  order as `1,000,000,000 + digit * 1,000,000,000`. It stops at digit count or,
  with nonzero trimming enabled, when the remaining integer is zero.

Scalar add, multiply and divide keep separate binary32 rounding and the known
startup NI=1 tiny-result flush. Zero denominators, non-finite scalar results and
unrepresented exceptional signed division/fctiwz results are checked at their
actual stage. Earlier committed field writes remain observable on failure.
No denominator substitution, percentage normalization or rollback is invented.

## Validation and remaining work

Standalone MSVC `/W4` compilation passes without warnings. `meter_control_tests`
passes **46 synthetic checks**, or **73 including original data**. Synthetic
checks exercise commands, signed zero, minimum-one behavior, smoothing, integer
and digit modes, physical RAM/ONE aliases, retained source lifetime, enabled-only
filtering, paused GETHP binding, live HP mutations, malformed resources and
committed failure prefixes, including a missing second GETHP object after the
first shared HP owner has already been published.

Original-data checks read ndmw.afs entry 179: `hpmt` info 31070 and `mpmt` info
31212 each own four RAMs and four ONEs. All eight consume the player-start
SETPERCENT payload and update their actual node sample lists. HP's first current
step is integer one; MP's is the fractional binary32 result of 100/60. These
tests prove controller data flow, not a rendered or correctly positioned HUD.

An independent original-instruction comparison passes **2,326 cases**, including
256 runs on the eight actual original meter owners: **797,659 instructions**,
912 prepared list-growth calls, 41 checked numeric boundaries and zero
differences. The original initialization, update, seven scalar command paths and
`800A4130` time conversion execute against prepared memory. Exact rational
binary32 rounding supplies the NI=1 arithmetic reference. Comparisons cover
percent minimums, signed zero, tiny results, smoothing, raw integer/digit modes,
wrapped HP division, signed time bounds and saturated time conversion.

The oracle prepares command lookup, packet float access and list allocation;
it does not independently execute SETMODELHP's nested recipients, allocation,
scheduling, rendering or physical hardware. Full build/CTest registration and
actual GETHP/startup integration are covered by the version 0.35 checks in
[VALIDATION.md](VALIDATION.md). Global scheduling, meter-node animation/geometry
traversal and original HUD rendering remain required before claiming this
feature in the playable preview.
