# Native deferred player-start validation

Version 0.32.0 was validated locally on 2026-09-10 with MSVC 19.51. Original
deferred message packets now retain their owners, signed deadlines, pause shifts
and explicit update continuation. The prepared post-GETPLAYER caller connects
HP binding, exact modifier resets, GAMESTART, BASE_ISUSE, a deferred PLAYERARMS
request and immediate RECALCPLAYER. Four original Agumon slots reach the explicit
later-UI boundary. Actual weapon construction and complete gameplay remain open.

| Check | Evidence |
| --- | --- |
| Release build and package | 71/71 CTest suites passed; local MSVC build without compiler warnings |
| Deferred kernel | 85 synthetic checks: packet identity, typed payloads, repeated preparation, signed/wrapped time, FIFO, growth, callback budget, pause, suspended writes, faults and cleanup/destructor reentry |
| Deferred runtime | 151 synthetic checks: four actual provider identities/indices, missing third provider, nested retained sends, same-ID foreign handles, immediate ownership reservation, frozen clock/count and no replay |
| Original deferred instructions | 61 cases / 1,388 operations / 2,110 exact state and callback rows / 729,885 original instructions; zero differences against the final core |
| Additional instruction evidence | 521 reference-only preparation/pause cases / 50,410 instructions, including all initialized and active byte values; 12 native borrowed-packet guard checks |
| Prepared MODELCTRL caller | 95 synthetic / 127 original-inclusive checks; old rows, exact reset prefixes, live routing/name/clock reads, four original player slots and four queued common05 requests |
| RECALCPLAYER | 259 synthetic / 283 original-inclusive checks; all four RAMs, missing/default/raw modifier bytes, nested row reset, retained stat prefixes, final packet and fault handling |
| Original game/data regression | Game data 8,269; GAMERAM 421; skill selection 499; database 275; database runtime 182; GAMESTART 217; items 157; stats 143; name conversion 461 |
| Original action/script regression | ACTION 6,034,137 checks; target scripts 104; registry 51; HIT bodies 109; collision 80; player events 108; waits 107 |
| Refreshed instruction regressions | 39 magic gates / 2,952 instructions; 43 wrappers plus 4 checked prefixes / 225,904 instructions; 101 stack cases / 7,525 rows / 2,232,011 instructions; 84 ordinary-delivery cases / 2,028 rows / 2,389,520 instructions; zero differences |
| Python and packaged app | 6 verifier tests; 16 app, 10 walking and 9 boot argument checks; actor and walking previews each rendered 120 frames and their captures were visually inspected |

The deferred comparison executes bounded original prepare, schedule, update,
pause, registration, selection, send and buffer-stack instructions. Allocation,
typed-list/string operations, controller hooks and initial pool preparation are
explicit services. It compares all eight represented typed lists, packet identity,
retained object counts, active record order, deadlines and callback traces. It
does not execute the complete original constructor or outer frame loop.

The four-player caller integration consumes actual GETPLAYER rows and original
database exports, DOL names and a prepared font converter. The queued common05
requests use distinct original PLAYERARMS RAM indices. At the exact deadline,
delivery stops at the real missing weapon-manager service. No weapon attachment,
full UI/constructor completion or connected multiplayer success is fabricated.
This caller integration is separate from an independent instruction execution
of the full MODELCTRL constructor, which has not been performed.

Original bytes, probes, logs and captures remain ignored under `work/`. See
[DEFERRED_MESSENGER.md](DEFERRED_MESSENGER.md),
[MODEL_PLAYER_START.md](MODEL_PLAYER_START.md) and [GAME_RAM.md](GAME_RAM.md).

## Previous version 0.31 validation

Version 0.31.0 was validated locally on 2026-09-10 with MSVC 19.51. Native
GAMESTART now connects the original species/base/equipment/stat/common/name
sequence through retained shared database delivery. All four original Agumon
data initializations complete with explicitly prepared database/font services.
Full player construction, other item providers, skill growth and gameplay remain
required.

| Check | Evidence |
| --- | --- |
| Release build and package | 67/67 CTest suites passed without compiler warnings; 6 Python verifier tests passed |
| GAMESTART integration | 183 synthetic / 217 original-inclusive checks; packet precedence, sixteen-row lookup, base-array aliases, nested resets, retained name routes and four actual player starts |
| Initial weapons and queries | 121 synthetic / 157 original-inclusive checks; fresh queries, post-callback records, distinct folder items, shared sockets and append/name/equipped publication order |
| Status and techniques | 127 synthetic / 143 original-inclusive checks; signed wrapping, health refill gates, retained modifiers, six technique groups, experience and full represented equipment tail |
| Name conversion | 35 synthetic / 461 original-inclusive checks; actual 424-pair table, two-pass conversion, shared scratch and distinct nine-unit player name |
| Original equipment/item instructions | 54 cases / 12,228 instructions, zero differences; explicit prepared typed lookup, allocation, list, string and database hooks |
| Original numeric stat instructions | 884 cases / 333,378 instructions, zero differences; includes 56 checked stops above the bounded level table |
| Original outer stat routine | 21 cases / 434,115 instructions / 516 item queries; 31 published fields, ordered techniques, names and query counts match |
| Original name instructions | 516 cases / 2,519,526 instructions, zero differences; all original mapping inputs and four actual default display-name strings included |
| Original selection/base instructions | 311 cases / 183,706 instructions, zero differences; 192 base-array cases, 79 species cases and 40 packet-cardinality cases |
| Existing game/script integration | Game data 8,269 checks, GAMERAM 421, skill selection 499, databases 275, DB runtime 182, target scripts 104, registry 51, HIT bodies 109, collision 80, events 108 and waits 107 |
| Original ACTION regression | 6,034,137 checks across 183 actions, 369 commands, 4,157 poses and 240 four-instance sequence poses |
| Original messenger/wrapper regression | 101 stack cases / 7,525 rows, 84 delivery cases / 2,028 rows, 43 wrappers plus 4 checked prefixes and 39 magic gates; zero differences against the refreshed core |
| Packaged app regression | 16 app, 10 walking and 9 boot argument tests; actor and walking previews each render 120 frames, with both captures visually inspected |

The original-data GAMESTART cases register all twelve DB infos from the actual
selected source, then initialize four separate player rows. Each obtains weapon
records 266/275/274, the actual initial technique and a name converted using the
supplied executable's table. P2ESKILLUP then uses the populated Waza and reaches
its remaining probability/growth boundary. The separate existing Hit tests retain
their fresh factory-row setup; no skill increment or combat success is invented.

Instruction comparisons execute bounded original routines with explicitly
prepared context, allocation, typed-list, string, database and item services.
The stat comparison executes the represented outer routine; font construction,
whole MODELCTRL creation, original scheduling and a physical-console playthrough
are not established. Original bytes, probes, traces and captures remain ignored
under `work/`. See [GAME_START.md](GAME_START.md), [GAME_ITEMS.md](GAME_ITEMS.md),
[GAME_STATS.md](GAME_STATS.md) and [GAME_NAME.md](GAME_NAME.md).

## Previous version 0.30 validation

Version 0.30.0 was validated locally on 2026-09-10 with MSVC 19.51. Original
database graphs now retain typed ownership through actual DATABASE info/RAM
registration. GAMERAM selects a weapon or technique skill before its original
ceiling comparison, retaining pcTech delivery through nested callbacks. Full
selected-player initialization, growth and gameplay remain required.

| Check | Evidence |
| --- | --- |
| Release build and package | 63/63 CTest suites passed without compiler warnings; 6 Python verifier tests passed |
| Database decoding | 157 synthetic / 275 original-inclusive checks; six groups, signed widths, first names, physical aliases, row bounds and actual pcTech/shinka data |
| DATABASE runtime | 126 synthetic / 182 original-inclusive checks; all 12 original DB infos/RAMs, final alias parents, null-to-empty registration names, captured fallback, raw activity and atomic failure |
| GAMERAM skill phase | 467 synthetic / 499 original-inclusive checks; signed ceilings, four source rows, first Waza record, actual pcTech SKILL lookup, retained cache/query and nested player reset |
| Shared ownership regression | 4,151 identity/provider checks; 8,181 synthetic / 8,269 original-inclusive game-data checks; unnamed Waza items remain distinct from disks and named wrappers |
| Original database helpers | 107 native comparisons, including 49 checked host stops, plus 15 guest-only raw-index cases; 8,529 original instructions, zero differences |
| Original database registration/delivery | 48 cases, 787,966 original instructions; final alias parents, ordinal rows, null/empty names, gates and captured GETDATABASE result identity match |
| Original skill selector/ceiling | 63 cases, including 3 explicit missing-Waza boundaries, match across 6,406 original instructions |
| Existing game/script integration | GAMERAM 421 checks, target scripts 104, registry 51, HIT bodies 109, collision 80, player events 108 and player waits 107 |
| Original ACTION regression | 6,034,137 checks across 183 actions, 369 commands, 4,157 poses and 240 four-instance sequence poses |
| Original messenger/wrapper regression | 101 stack cases / 7,525 rows, 84 delivery cases / 2,028 rows, 43 wrapper cases plus 4 checked prefixes and 39 magic-gate cases; zero differences |
| Packaged app regression | App, walking and boot argument checks passed; actor and walking previews each rendered 120 frames and their captures were visually inspected |

The original-data skill cases explicitly prepare Waza items and skill values
to exercise recovered helpers against pcTech reference 1592. They do not
execute GAMESTART or its equipment/stat calculation. The unchanged original
Hit integration still reaches PC 63 after 48 instructions and five logical
calls; its fresh generated WAZALIST is empty, so technique selection remains
an explicit prerequisite. No starting HP or growth effect is fabricated.

Instruction probes execute bounded original routines with explicit prepared
allocation, list, type, context and recipient services. Checked schema/index
stops and missing native services are reported separately from original
unchecked memory behavior. No complete console execution, player constructor
or playable combat claim follows from these comparisons. Original bytes,
probes, traces and screenshots remain ignored under `work/`.

See [DATABASE.md](DATABASE.md), [DATABASE_RUNTIME.md](DATABASE_RUNTIME.md) and
[GAME_RAM.md](GAME_RAM.md) for implemented behavior and remaining prerequisites.

## Previous version 0.29 validation

Version 0.29.0 was validated locally on 2026-09-10 with MSVC 19.51. Generated
GAMERAM player rows now retain their shared HP objects through actual GETPLAYER
delivery and prepared model binding. Original Hit scripts can advance through
the recovered magic return gates into the actual skill-growth service boundary.
Selected-player initialization, damage/status effects and gameplay remain required.

| Check | Evidence |
| --- | --- |
| Release build and package | 60/60 CTest suites passed without compiler warnings; 6 Python verifier tests passed |
| Native identities/providers | 4,143 checks cover separate native/serialized domains, retained callbacks, aggregate owner bounds, atomic registration failure, typed data-list lifetime and integer-only buffer reset |
| Generated game data | 8,178 synthetic / 8,266 original-inclusive checks; all 77 schema records, eight kinds, four rows, 48 distinct disks, exact defaults and retained row/wrapper identities |
| Model data binding | 59 checks cover shared HP/MHP, exact-zero HP and MP refill, negative values, captured pre-reset rows, typed failures and live script HP merge |
| Lua messenger wrappers | 156 checks cover actual packet order, current-context fields, signed results, Number-to-byte conversion, captured send ownership, nested pauses and terminal missing-result handling |
| GAMERAM integration | 188 synthetic / 421 original-inclusive checks; exact generated registration, GETPLAYER variants, resets, retained owners, signed magic gates and eight original Hit scenarios |
| Original wrapper comparison | 43 scenarios plus four separately labeled checked service prefixes match 47 native rows across 225,904 guest instructions |
| Original magic gate comparison | 39 cases match across 2,952 kernel instructions, covering lookup-before-unequal, signed remainder branches and integer-only return clearing |
| Messenger regression comparisons | 101 stack scenarios / 7,525 exact rows / 2,232,011 instructions and 84 delivery scenarios / 2,028 rows / 2,389,520 instructions; zero mismatches against the refreshed core |
| Original script/collision/event regressions | Target scripts 104 checks; registry 51; HIT bodies 109; collision integration 80; player events 108; player waits 107 |
| Original ACTION regression | 6,034,137 checks across 183 actions, 369 commands, 4,157 poses and 240 four-instance sequence poses |
| Packaged application | 16 app, 10 walking and 9 boot CLI checks passed; actor inspection renders 120 frames / 121 uploads; walking renders 120 frames, 198 steps, 101.333 travel and 102 uploads |
| Visual inspection | Version 0.29 actor and walking captures inspected; pause/restart/focus replay and the walking 1236x600 render target remain intact |

The unchanged entry179 `hit_p_agumon.lua` now reaches BLT at PC 63 after 48
instructions and five logical calls when supplied with the owned GAMERAM and
bridge. Eight cases exercise every actual Agumon RAM with equal and unequal
source RAMs. The pending packet is `P2ESKILLUP`; retries preserve the completed
BLP call and damage-turn clear. Initial generated HP remains zero throughout.
This does not establish healing, skill growth or a playable character.

Instruction comparisons execute bounded original kernels with explicit prepared
list/allocation/controller hooks. Missing native services are checked host stops,
not claims of original asynchronous behavior. The empty BLP result fault stops
before an original unchecked read. Schema tests compare actual records/defaults;
they do not execute the entire original constructor. All original bytes, probes,
traces and captures remain ignored under `work/`.

See [GAME_DATA.md](GAME_DATA.md), [GAME_RAM.md](GAME_RAM.md) and
[MODEL_SCRIPT_MESSENGER.md](MODEL_SCRIPT_MESSENGER.md).

## Prepared target Lua and HP context validation

Version 0.28.0 was validated locally on 2026-09-10 with MSVC 19.51. Selected
original target-Hit chunks now execute with shared model/HP context and retained
Lua continuations. Actual damage/status services, player construction and the
original game loop remain required before playable combat.

| Check | Evidence |
| --- | --- |
| Release build and package | 55/55 CTest suites passed without compiler warnings; 6 Python verifier tests passed |
| Lua interpreter | 416 synthetic / 426 original-inclusive checks; source-linked MSVC AddressSanitizer also passes; retained CALL/TAILCALL, captured values, nested/sibling frames, shared budgets, abandonment and uncertain effects covered |
| Original func/boot comparison | Strict and retained engines match official Lua 4.0.1 in all 3 explicit profiles: 188 ordered host calls and 3 final resource-queue rows; retained execution pauses once before each effect, with zero differences |
| Shared target context | 81 checks cover qualified work, distinct default HP wrappers, direct signed record, live matrix alias, conditional current-wrapper HP merge, nested overwrite and strict continuation ownership |
| Ordered script registry | 44 synthetic / 51 original-inclusive checks; exact export/registration occurrences, first-name barriers, owned original Hit bytes and aggregate bounds |
| Target script execution | 88 synthetic / 104 original-inclusive checks; repeated chunks, native status versus Lua returns, signed getters, finite setter conversion, retained native/filesystem services, nested shared VM/context, four actual Agumon RAM early returns and explicit ordinary combat boundary |
| Original Hit comparison | 28 cases match official Lua 4.0.1: 24 RAM/kind early returns and four kind-80000 boundaries, 64 ordered calls, 92 exact rows and 736 executed instructions; 12 unavailable-service retries preserve the prefix |
| Collision/script adapter | 45 checks cover live GETRECNO/HP-presence work, owner expiry and matrix lifetime; real CHKBULLET/record/context continuation completes through an explicit synthetic filesystem effect, with no repeated record lookup or committed matrix write |
| Original collision/events/waits | HIT body 109 checks; collision integration 80; player events 108; waits 107, retaining all four Set129/TALKWF/WCHECK paths and the missing HITPLAYERTALK boundary |
| Original ACTION regression | 6,034,137 checks across 183 actions, 369 commands, 4,157 poses and 240 four-instance sequence poses |
| Packaged application | 16 app, 10 walking and 9 boot CLI checks passed; actor inspection renders 120 frames / 121 uploads; walking replay renders 120 frames, 198 steps, 101.333 travel and 102 uploads |
| Visual inspection | Version 0.28 actor and walking captures inspected; walking retains pause/restart/focus-loss replay and the 1236x600 render target |

The original Hit chunk is loaded unchanged from map AFS entry 179, LuaInfo
2783/export 237/data index 26. Four canonical original MODELCTRL RAM bindings
qualify the integration cases. Ordinary kind 80000 executes the verified
damage-turn clear and stops at `be.BLPcalcKoukaMagic`, PC 58, after 43
instructions and four logical calls. No magic, damage or status result is
supplied. The independent reference uses only opcode-count/PC instrumentation;
its deliberate test error at that unavailable method corresponds to the native
retained service boundary, not a claimed identical error mechanism.

Explicit fixture HP values and callbacks are not original player construction.
Lua allocation limits remain cumulative without an original garbage collector.
These checks establish the prepared component and bounded script paths, not a
complete combat or connected multiplayer session. Original bytes, reference
drivers, traces and app captures remain ignored under `work/`. See
[MODEL_SCRIPT_RUNTIME.md](MODEL_SCRIPT_RUNTIME.md),
[MODEL_SCRIPT_CONTEXT.md](MODEL_SCRIPT_CONTEXT.md), [LUA4.md](LUA4.md) and
[SCRIPT_REGISTRY.md](SCRIPT_REGISTRY.md).

## Native target collision validation

Version 0.27.0 was validated locally on 2026-09-10 with MSVC 19.51. Prepared
HITBOX queries now reach target body caches, first overlap and retained nested
record/hit-script operations. Actual target Lua/HP/global context, damage and
original player construction remain required before playable combat.

| Check | Evidence |
| --- | --- |
| Release build and package | 51/51 CTest suites passed without compiler warnings; 6 Python verifier tests passed |
| Authored HIT body/cache | 88 synthetic / 109 original-inclusive checks; four Agumon RAMs, ordered boxes/aliases, raw category bytes, zero-time factory bounds, timestamp reuse and independent mutable caches |
| Target collision kernel | 162 checks cover lazy gates, first/equality overlap, deferred argument reads, qualified endpoints, record counts, nested buffer/script suspension, captured positions, live pose changes and uncertain effects |
| Prepared collision integration | 67 synthetic / 80 original-inclusive checks; source HITBOX to target to BHit, nested target cleanup/LUACALL retry, canonical RAM rejection and retained GETRECNO; four original Agumon HITENEMYBULLET recipients produce four checked misses |
| Target box arithmetic | 231 public math checks; 10,048 original-instruction cases, 44,137 exact output words and 2,544,172 instructions, zero mismatches against the final production core |
| Source box regression | 4,599 original-instruction cases, 22,728 exact finite output words and 2,222,379 instructions, zero mismatches; includes 693 original-authored vector cases |
| Euler numeric correction | 111 synthetic / 133 original-inclusive motion checks; forced hit heading uses NI flushing for trig conversion and Euler arithmetic while preserving live scalar position words |
| Euler instruction comparison | Final-core runner matches 4,932 cases, 78,624 exact output words and 647,129 instructions with a shared host-trig boundary; the existing 9,000 turn/tick cases also retain zero mismatches |
| Messenger stack | 161 checks; final-core comparison matches 101 scenarios, 7,525 exact buffer/selection/owner rows and 2,232,011 instructions |
| Messenger delivery regression | Final-core comparison matches 84 scenarios, 2,028 callback/result rows and 2,389,520 instructions |
| Original ACTION/events/waits | 6,034,137 ACTION checks across 183 actions and 4,157 poses; player events 108 checks; player waits 107 checks, preserving all four Set129/TALKWF/WCHECK paths and the missing HITPLAYERTALK boundary |
| Packaged application | 16 app and 10 walking CLI suites pass; actor inspection renders 120 frames / 121 uploads; walking replay renders 120 frames, 198 steps, 101.333 travel and 102 uploads |
| Visual inspection | Version 0.27 actor and walking captures inspected; walking retains pause/restart/focus-loss replay and the 1236x600 render target |

The original-data checks prepare explicit live owners and matrices; they do not
initialize a complete game session or connected multiplayer. Synthetic scripts
exercise continuation contracts, not original damage behavior. Instruction
comparisons have bounded prepared helper/trig profiles and do not establish
physical-console execution. Original assets, probes and captures stay ignored.
See [MODEL_COLLISION.md](MODEL_COLLISION.md), [MODEL_HIT_BODY.md](MODEL_HIT_BODY.md),
[COLLISION_MATH.md](COLLISION_MATH.md) and [MESSENGER.md](MESSENGER.md).

## Native HITBOX and collision dispatch validation

Version 0.26.0 was validated locally on 2026-09-10 with MSVC 19.51. Timed
HITBOX work now reaches real collision/script service boundaries through the
prepared player runtime. Target body collision, damage, full Lua context and
original player construction/entry remain required for playable combat.

| Check | Evidence |
| --- | --- |
| Release build and package | 48/48 CTest suites passed; no compiler warnings; 6 Python verifier tests passed |
| HITBOX lifecycle | 65 synthetic checks; 2,106 original-instruction cases match across 1,690,336 instructions at 577 addresses; existing sound/effect suite retains 67 passing checks |
| Query-box arithmetic | 92 synthetic checks; 4,599 instruction cases, 22,728 exact finite output words and 2,222,379 instructions, zero mismatches; includes 693 cases using all 231 original Set vectors under three explicit matrices |
| Prepared HITBOX integration | 216 checks cover typed retained boxes, three recipient categories, attachment/source identities, live post-debug matrix and post-query script reads, registration/manager/Lua pauses and callback ownership |
| Sequence and control | 230/1,114 synthetic/original-inclusive sequence checks; 86/140 control checks; all 352 Agumon HITBOX and 62 MUTEKI occurrences decoded; LUACALL pending/fault and post-cleanup field resets covered |
| Full archive SEQ scan | All 9,060 SEQ programs, 80,621 sequence occurrences and 447,274 commands across 2,273 IGB files decode with zero rejections |
| Wrapper Lua continuation | Prepared event runtime passes 275 checks, including incomplete/uncertain immediate Lua, nested-call preflight and retained messenger delivery without recipient replay |
| Messenger regression | 109 synthetic checks; rebuilt original comparison matches 84 scenarios, 2,722 operations, 2,389,520 instructions and 2,028 callback/result rows |
| Original four-instance boundary | Player waits pass 107 checks; all four actual SQ_ATCK1 labels append Set129, start TALKWF and reach WCHECK516. An eligible check then retains the missing HITPLAYERTALK registration boundary |
| ACTION/event regressions | 6,034,137 ACTION checks across 183 actions, 369 commands, 4,157 poses and 240 sequence poses; original player events pass 108 checks |
| Packaged app | 16 app and 10 walking argument suites pass; actor inspection renders 120 frames/121 pose uploads; walking replay renders 120 frames, 198 steps, 101.333 travel and 102 pose uploads |
| Visual inspection | Version 0.26.0 actor and walking captures inspected on an RTX 5070 Ti; walking retains pause/restart/focus-loss replay and its 1236x600 target |

The first Set129 is a talk-category query, not evidence of attack damage.
Synthetic managers exercise dispatch and continuation contracts; they do not
implement the game's target collision or Lua behavior. Original-instruction
comparisons use explicit bounded helper profiles and do not establish physical
Gekko execution. The app captures show the prepared viewer and provisional
walking path. Original assets, probes and captures remain ignored. See
[MODEL_HITBOX.md](MODEL_HITBOX.md), [COLLISION_MATH.md](COLLISION_MATH.md) and
[ACTION_EVENT_RUNTIME.md](ACTION_EVENT_RUNTIME.md).

## Native model wait and live ACTION query validation

Version 0.25.0 was validated locally on 2026-09-10 with MSVC 19.51. Prepared
model waits now use registered live ACTION work and separately selected original
pad slots. Full original player creation, HITBOX/collision dispatch and outer
sequence-entry/status effects remain required for gameplay.

| Check | Evidence |
| --- | --- |
| Release build and package | 45/45 CTest suites passed; no compiler warnings; 6 Python verifier tests passed |
| Model wait kernel | 73 synthetic checks cover common channels, null/empty names, per-pass suspension, live callback mutations, signed attack latches, reentrancy and uncertain effects |
| Original wait comparison | 1,835 cases match the original instruction model: 165,190 instructions at 416 unique addresses; prepared ONE/pad/messenger/string/command/sequence helper boundaries are explicit |
| WCHECK decoder and runner | 150 synthetic checks; 610 with original data, including all 259 occurrences and seven shared records; null-to-empty assignment, retained cursor, scalar preservation and resumable ACTION dispatch covered |
| Live ACTION query and state identity | 62 synthetic checks; 78 with original data across four actual RAMs; live results, missing selected work, ownership rejection, pending events, all mutation stamps and stable read-only getters covered |
| Original query comparison | 56 scenarios, 2,336 instructions, zero mismatches; 48 defined results and 8 explicit missing-work boundaries; actual boolean append and retained-work decrement execute with prepared capacity/reference ownership |
| Prepared player integration | 79 synthetic checks; 99 original-inclusive; separate pad slots, first-result query semantics, retained time/shared random state, same-owner retry, callback-owned message suspension, stale runtime rejection and Atck02 live destination/cursor covered |
| Original attack boundary | All four weapon-selected SQ_ATCK1 instances stop at the required unsupported HITBOX command; four explicitly prepared real KATCA3 actions then reach their own motion waits and return live incomplete ACTION results |
| ACTION/event regressions | 6,034,137 ACTION checks pass across 183 actions, 369 commands, 4,157 poses and 240 sequence poses; original four-player event integration passes 108 checks |
| Packaged app | 16 app and 10 walking argument suites pass; actor inspection renders 120 frames/121 pose uploads; walking replay renders 120 frames with 198 steps, 101.333 travel and 102 pose uploads |
| Visual inspection | Version 0.25.0 actor and walking captures inspected on an RTX 5070 Ti; walking retains pause/restart/focus-loss replay and its 1236x600 target |

The original-data test deliberately retains the HITBOX boundary instead of
skipping it to claim a complete attack sequence. Its separate KATCA3 setup does
not initialize original player work or collision services. Synthetic outer
sequence callbacks exercise continuation ordering only. Instruction comparisons
do not establish physical Gekko execution or complete external-manager behavior;
the app captures show the existing prepared viewer and provisional walking path.
Original assets, probes and captures remain ignored. See
[MODEL_WAIT.md](MODEL_WAIT.md) and [ACTION_EVENT_RUNTIME.md](ACTION_EVENT_RUNTIME.md).

## Native player event routing validation

Version 0.24.1 was validated locally on 2026-09-10 with MSVC 19.51. Explicit
MODELCTRL/ACTION graph bindings now connect native event consumers to owned
sound/effect queues. This is prepared component integration; complete original
player construction, audio/effect managers and outer gameplay scheduling remain
required.

| Check | Evidence |
| --- | --- |
| Release build and package | 42/42 CTest suites passed, no production compiler warnings; 6 Python verifier tests passed |
| Prepared event runtime | 243 synthetic checks cover cross-source routing, aliases, repeated parent assignment, ordered queues, nested suspension, protected shared arguments, externally visible pre/post-cleanup selection phases, local-jump continuation, source collisions and atomic registration bounds |
| Prepared player binding | 57 synthetic checks; 105 with original data, covering all four RAMs and three explicit LOD branches |
| Consumer tables | 68 synthetic checks; 5,960 with original Se and all 58 map Starter exports: 78 Se model rows/2,173 actions, 958 Starter model occurrences/5,887 actions, 1,338 exact Data2 occurrences |
| Messenger | 102 synthetic checks; independent original-instruction comparison covers 84 scenarios, 2,722 operations, 2,389,520 instructions and 2,028 exact callback/result rows, with zero mismatches |
| Subcommand lifetime | 67 synthetic checks; independent original-instruction comparison covers 1,005 lifecycle cases and 2,018,236 instructions, with zero mismatches |
| Subcommand/system timing | 3,008 scalar-addition cases and 3,171 system-time cases cover signed tiny results, two-stage conversion, timestamp wrap and uncapped frame deltas; 621,329 instructions, zero mismatches, 2 expected finite-input overflow rejections |
| Four original player instances | 63 actual model/consumer registrations; original weapon RUN/WAIT sequences route four independent footstep queues through delay, retained start and cleanup; 24 actor-cache poses and 32,904 deformed vertices |
| Original attack event | KATCA3 dispatch reaches its motion wait and emits actual PC_SUBURI02 sound plus x_100armk01/SQ_ATCK01 effect; missing find/generate/erase services resume without repeating completed work |
| ACTION/sequence regressions | All 182 ordinary Agumon actions plus NDSP, 369 commands, 4,157 poses/5,699,247 vertices and 240 four-instance sequence poses/329,040 vertices pass; original model sequence suite passes 258 checks |
| CLI and packaged app | 16 app and 10 walking argument suites pass; 120 actor frames/121 pose uploads and the 120-frame walking replay pass |
| Visual inspection | Version 0.24.1 actor and version 0.24 walking screenshots inspected; walking retains 198 steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads, pause/restart/focus-loss replay and its 1236x600 target |

Instruction comparisons run independent models of the inspected original
instructions with explicitly supplied allocation/list/string/controller service
boundaries. They do not validate those hooked original helper bodies or physical
Gekko execution. Original-data integration runs production native decoders and
owners; test-manager sound/effect handles do not establish audible output or
rendered effect actors. The screenshots show the existing separate inspection
and provisional walking paths. Original assets, instruction probes and captures
remain ignored. See [ACTION_EVENT_RUNTIME.md](ACTION_EVENT_RUNTIME.md),
[MESSENGER.md](MESSENGER.md) and [MODEL_SUBCOMMANDS.md](MODEL_SUBCOMMANDS.md).

## Native actor cache rendering validation

Version 0.23 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. Original actor cache capture and single-influence CPU
deformation now feed an explicit prepared draw. Its camera and fit remain host
inspection state; complete appearance traversal and gameplay remain pending.

| Check | Evidence |
| --- | --- |
| Release build and package | 36/36 CTest suites passed; no production compiler warnings; 6 Python verifier tests passed |
| CLI validation | 16 app option suites and 10 walking option suites passed without opening a graphics context |
| New core coverage | 41 actor skin checks and 463 animation math checks passed; source/RAM/actor/binding identity, permuted and repeated palette entries, private/shared caches, retained frames, malformed graphs and finite-result rejection covered |
| Original actor skin integration | 36 captures across four RAMs and all three appearances, 24,024 deformed vertices, 24,989 checks passed |
| Quaternion instruction comparison | 16,578 cases, 239,440 exact finite words, 1,553 finite-input overflow and 60 invalid-input rejections; all 10,250 supplied-data rotations unchanged; zero mismatches |
| CPU skin-loop instruction comparison | 1,612 cases, 7,327 positions and 226,991 executed guest instructions; 18,894 exact finite XYZ words and 1,029 finite-input overflow/NaN rejections; zero mismatches |
| Prepared GPU rendering | Exact image comparison against separately specified clip-space geometry verifies raw coordinates, noncommuting root/projection, position updates/reset, API separation, invalid-input rejection and GL state preservation |
| Packaged actor inspection | High-detail body in RAM 0, low-detail body in RAM 1 and solid shadow geometry in RAM 2 each render 8 frames; RAM 3 high-detail run playback renders 120 frames with 121 pose uploads |
| Original traversal and ACTION regression | 720 traversals and 18,000 cached bone queries pass; all 182 ordinary actions plus NDSP, 369 commands, 4,157 poses and 240 four-instance sequence poses pass |
| Packaged walking regression | 120 frames, 198 steps, 101.333 travel, 46 idle/152 run steps and 102 pose uploads; pause/restart/focus-loss replay passed |
| Visual inspection | Actor body/shadow/playback, overview and walking screenshots inspected; version 0.23 and 8x MSAA visible; body targets 1290x554 and walking target 1236x600 |

Math comparisons use the independently implemented original instruction model
and startup NI=1/RN=00 profile, including the documented inferred extreme fused
tininess boundary. They were rerun against helpers linked to the final core
implementation. They do not establish physical Gekko/GX parity. Original-data
integration exercises production decoding and caches; synthetic GPU checks
establish transform order, not original lighting, material or raster output.
The shadow geometry is inspected in Solid mode because its full original
effective material is unsupported. Original data, probes and images stay ignored.
See [ACTOR_SKIN.md](ACTOR_SKIN.md) for exact source and execution boundaries.

## Native actor hierarchy and traversal validation

Version 0.22 was validated locally on 2026-09-09 with MSVC 19.51. The portable
core now owns shared bone/joint caches, computes the original ordinary actor
hierarchy and exposes cached named bone queries. Complete renderer traversal,
event consumers and original gameplay integration remain pending.

| Check | Evidence |
| --- | --- |
| Release build and package | 35/35 CTest suites and 6 Python verifier tests passed; no production compiler warnings |
| New synthetic coverage | 248 animation math checks, 46 hierarchy checks and 50 traversal checks passed; actor lifecycle coverage now contains 61 checks |
| Original actor traversal | Four Agumon instances, three appearances, 720 traversals and 18,000 cached bone queries; freeze, interrupted actions, direct-pose invalidation and NDSP/reactivation included |
| Original graph validation | 484 hierarchy checks and 548,694 traversal checks passed using original Agumon data |
| Matrix instruction comparison | 7,415 cases, 101,168 exact finite output words, 996 finite-input overflow rejections and 96 invalid-input rejections; zero mismatches |
| Hierarchy instruction comparison | 64 bounded cases match all 12,976 output float words; helpers, root exclusion, joint-index permutation and retained root are covered |
| Original ACTION regression | All 182 ordinary actions complete, deactivate and reactivate; 369 commands, 4,157 poses / 5,699,247 finite vertices, plus 240 sequence poses / 329,040 finite vertices |
| Packaged walking regression | 120 frames, 198 steps, 101.333 travel, 46 idle/152 run steps and 102 pose uploads; pause/restart/focus-loss replay passed |
| Packaged visual inspection | Overview and walking captures inspected; version 0.22 visible, original Agumon/map render at 1236x600 with 8x MSAA on an RTX 5070 Ti |

The matrix comparison decodes original paired-single words with exact integer
binary32 arithmetic and the startup NI=1/RN=00 profile. Its 1,271 finite
subnormal/underflow cases include an inferred extreme fused tininess boundary;
they are not physical-console measurements or a claim of universal Dolphin
equality. See [ANIMATION_HIERARCHY.md](ANIMATION_HIERARCHY.md) for primary sources,
the inference and the unchanged numeric scope of older math helpers.

The hierarchy probe executes original integer control flow with explicit
bone-count, copy and matrix services. Original-data tests exercise production
decoding and runtime state; the broadcast sink records events. The packaged
preview retains its prior pose/controller path. These checks do not establish
original rendered-frame parity, event playback, connected multiplayer or the
complete game. Original data, probes and captures remain ignored.

## Native ACTION actor lifetime validation

Version 0.21 was validated locally on 2026-09-09 with MSVC 19.51. The portable
core now owns prepared ACTION actor buffers and executes activation, NDSP and
reactivation. Original actor traversal and messenger consumers remain pending.

| Check | Evidence |
| --- | --- |
| Release build and package | 33/33 CTest suites and 6 Python verifier tests passed; no production compiler warnings |
| Asset-free lifecycle coverage | 124 ACTION checks, 60 actor graph/lifetime checks and 86 combiner checks passed |
| Original actor graph | All four Agumon RAMs decode three actors each; each actor allocates 25 bone and 20 joint matrices, releases them and reactivates |
| Original ACTION regression | All 182 ordinary actions complete their command/effect phases, then deactivate and reactivate; NDSP bypasses its null list |
| Original pose regression | 369 commands, 4,157 action poses / 5,699,247 finite vertices, plus four weapon sequences producing 240 poses / 329,040 finite vertices |
| Original lifecycle instruction probe | 14 bounded activation/NDSP scenarios pass, including the first/middle allocated-actor barrier, attribute cleanup loops and frozen-node retention |
| Native buffer comparison | 13 original/native actor buffer transitions match all 208 compared allocation-prefix, link-selection, storage-presence and identity fields |
| Packaged walking regression | 120 frames, 198 simulation steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads; pause/restart/focus-loss replay passed |
| Packaged visual inspection | Overview and walking captures inspected; version 0.21 visible, original Agumon/map render at 1236x600 with 8x MSAA on an RTX 5070 Ti |

The lifecycle instruction probe executes original activation/cleanup branches
and identity initialization with prepared objects. Allocation, destruction,
metadata lookup, combiner operations, timer deletion and list-capacity services
are explicit hooks; their bodies are not independently validated by this probe.
The 208-field native comparison covers actor buffers only, excluding guest
pointers/refcounts, node/work/attribute/timer fields and those hook bodies.

The original-data tests exercise production decoding and runtime methods with a
recording event sink. Identity actor buffers are not evaluated bone caches.
These results do not establish event-consumer execution, actor traversal,
rendered fade/flash effects, connected multiplayer or a complete original game.
See [ACTION_ACTORS.md](ACTION_ACTORS.md) for ownership and remaining boundaries.
Generated probes, original data and captures remain ignored.

## Native ACTION2 validation

Version 0.20 was validated locally on 2026-09-09 with MSVC 19.51. Prepared
ACTION2 programs now execute motion, joint, fade, wait and flash effects. Full
actor construction, event consumers and renderer integration remain pending.

| Check | Evidence |
| --- | --- |
| Release build and package | 32/32 CTest suites and 6 Python verifier tests passed; no compiler warnings |
| ACTION fixtures | 91 asset-free checks cover typed/bounded graphs, first/same-action dispatch, cursor and wait behavior, timer phase/freeze, raw/packed color, shared attribute cleanup, required/failed/reentrant services and source/info/RAM ownership |
| Original ACTION integration | 183 Agumon definitions decode; all 182 ordinary actions complete their command and motion/fade/flash phases; NDSP remains an explicit outer actor boundary |
| Original action poses | 369 command occurrences, 4,157 complete poses and 5,699,247 finite vertex positions |
| Original sequence-to-action integration | Four independent weapon sequence instances apply their real ACTION lists into 240 poses / 329,040 finite vertices, including interrupted transitions |
| Independent instruction comparison | 20,000 node, timer-speed, joint, random-wait, fade and flash operations match all compared numeric/state fields at explicit prepared-object and service boundaries |
| Packaged walking regression | 120-frame replay passes with 198 steps, 101.333 travel, 46 idle/152 run steps and 102 pose uploads; pause/restart/focus-loss checks pass |
| Packaged visual inspection | Overview and walking captures inspected; version 0.20 visible, original map/Agumon render at 1236x600 with 8x MSAA on an RTX 5070 Ti |

The instruction probe executes original arithmetic and state branches, including
color conversion with original initialized bounds. It supplies prepared objects,
list removal and timer/RNG service results; those external bodies, the complete
dispatcher and actor allocation are outside the independent comparison. The
original-data tests use production codec/pose evaluation and a recording
broadcast sink. They establish neither event-consumer execution nor rendered
fade/flash behavior, physical multiplayer or original gameplay-frame parity.
See [ACTIONS.md](ACTIONS.md) for exact ownership, numerical and integration limits.
Original bytes, probes and captures stay ignored; distributed fixtures are synthetic.

## Native animation combiner validation

Version 0.19 was validated locally on 2026-09-09 with MSVC 19.51. The portable
core now retains prepared combiner states and cached-pose snapshots and applies
explicit MOTIONSET effects. Full ACTION/actor integration remains pending.

| Check | Evidence |
| --- | --- |
| Release build and package | 31/31 CTest suites and 6 Python verifier tests passed |
| Combiner fixtures | 73 asset-free checks cover priority/retirement, interrupted snapshots, new flags, same-time cache, motion phases, system/node timestamp separation, four owned clocks, source sharing and malformed prepared graphs |
| Matrix and animation graph fixtures | 151 math checks and 69 graph checks passed, including independent synthetic snapshot expectations and exact duration/priority metadata |
| Original clip regression | All 82 Agumon clips pass the existing sample, five-mode timing, complete pose and layer checks; 5,728 total checks including the asset-free fixtures |
| Original combiner integration | All four prepared Agumon combiners load; eight original idle/run MOTIONSET records drive 240 complete poses with 329,040 finite vertex positions and four interrupted transitions |
| Independent state-update instruction oracle | 10,000 updates in 500 programs / 17,692 commands match at explicit prepared-object and list/pool service boundaries; NaN payload excluded |
| Independent matrix instruction oracle | 30,556 conversions / 305,224 float fields match; nonfinite guest outputs require native rejection |
| Original square-root rounding probe | 25,166,839 positive float cases match the original estimate plus three double Newton steps; exhaustive mantissas for both exponent parities, all float subnormals and normal exponent boundaries |
| Packaged walking regression | 120-frame replay passes with 198 steps, 101.333 travel and 102 pose uploads; pause, restart and focus-loss checks pass |
| Packaged visual inspection | Overview and walking captures inspected on an RTX 5070 Ti; version 0.19 visible, original map/Agumon render at 1236x600 with 8x MSAA |

The state probe executes the original update/retirement-marking instructions.
It supplies prepared creation fields, empty named-transition lookup, signed
division, replace-index rebuilding and list/pool removal services. Those service
bodies and the complete actor are outside that comparison. Original combined
poses use production channel/skin evaluation; they are not independent codec
validation or original gameplay-frame comparisons. See
[ANIMATION_COMBINER.md](ANIMATION_COMBINER.md) for the implementation profile,
numeric evidence and remaining boundaries. Original files and local probes stay
ignored; published tests contain synthetic data only.

## Native animation timing and layer validation

Version 0.18 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. The portable core implements recovered numeric animation
clocks, source wrapping and ordered layer arithmetic. Complete ACTION/combiner
state management remains pending; the preview retains its provisional controller.

| Check | Evidence |
| --- | --- |
| Release build and package | Passed without compiler warnings; 30/30 CTest suites and 6 Python verifier tests passed |
| Animation timing fixtures | 62 asset-free checks cover signed/unsigned coarse clocks, original metadata cache defaults, shared caches, speed-origin continuity, saturation, blend rounding, exceptional durations, endpoint branches and runtime modes |
| Animation graph and layer fixtures | 66 asset-free checks, including timed sampling, cache publication on success, layer order, original quaternion behavior, fused cancellation and invalid inputs |
| Original Agumon integration | 5,659 additional checks pass for all 82 clips; five modes at six local-time fractions add 2,460 complete timed poses and finite layer evaluations |
| Independent timing instruction oracle | 30,000 operations in 1,000 shared-cache programs match at prepared-object and duration-service boundaries; NaN payloads excluded |
| Independent layer instruction oracle | 10,000 tuples / 70,000 float32 quaternion and translation components match; half use decoded original rotations, half synthetic rotations |
| Packaged walking regression | 120-frame SDL replay passes with 198 steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads and final (-292,40,431); pause/restart/focus-loss checks pass |
| Packaged visual inspection | 1280x720 overview and walking captures inspected; version 0.18 visible, original Agumon/map render in the 1236x600 walking target with 8x MSAA |

The instruction probes cover numeric operations and bound channel mixing, not
the complete ACTION state machine, source construction, snapshots, events or
actor lifecycle. Timed original-data checks use the production codec and pose
path; they are not independent codec or original gameplay-frame comparisons.
Original bytes, local probes and captures remain ignored. See
[ANIMATION_TIMING.md](ANIMATION_TIMING.md) for ownership and exact boundaries.

## Native model sequence validation

Version 0.17 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. The portable command runner retains original model sequence
state, weapon sections, supported control/motion effects and required ACTION
requests. Complete outer player callbacks and ACTION execution remain pending;
the walking preview continues using its provisional host controller.

| Check | Evidence |
| --- | --- |
| Release build and package | Passed without compiler warnings; 29/29 CTest suites and 6 Python verifier tests passed |
| Sequence fixtures | 65 asset-free checks cover original list slots, aliases, retained state, label caches/barriers, command-name precedence, nested jumps, movement stop rules, failed/reentrant services and malformed/bounded input |
| Original Agumon sequence integration | 193 additional checks pass across both entry groups, all four slots and all four weapon sections; original lists request the matching WAIT/RUN/WAIT actions and travel approximately 8.275 local units at explicit cached delta 1 |
| Complete SEQ corpus decode | 9,060 programs, 80,621 sequence occurrences and 447,274 command occurrences in 2,273 archive IGBs; zero rejections; unsupported runtime commands are retained, not executed |
| Independent command instruction oracle | 6,000 bounded command programs match at the supplied list/string/channel allocation and ACTION service boundaries; NaN sign/payload excluded |
| Independent label instruction oracle | 3,000 programs with 24,539 set/lookup pairs match original label tables and cached offsets; missing original labels produce an explicit native guard |
| Packaged walking regression | 120-frame SDL replay passes with 198 steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads and final (-292,40,431); pause/restart/focus-loss checks pass |
| Packaged visual inspection | 1280x720 overview and walking captures inspected; version 0.17 visible, Agumon and original map render in the 1236x600 walking target with 8x MSAA |

The original-data fixture supplies a recording ACTION service to verify requests;
it does not execute the animation manager, full sequence entry or player creation.
The instruction probes use synthetic prepared work and supplied external service
boundaries. They do not establish allocator behavior, full callbacks, original
gameplay or physical multiplayer. Original bytes/probes/captures remain ignored.
See [MODEL_SEQUENCE.md](MODEL_SEQUENCE.md) for exact scope and source addresses.

## Native model control and preparation validation

Version 0.16 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. Original CTRL/PDMOVE effects, movement preparation, player
turn rates and post-input sequence requests now run in the portable core.
Full original player construction, input and sequence/action execution remain
pending. The walking preview retains its provisional host controller.

| Check | Evidence |
| --- | --- |
| Release build and package | Passed without compiler warnings; 28/28 CTest suites and 6 Python verifier tests passed; installed executable matches the build by SHA-256 |
| Model control fixtures | 64 asset-free checks: original rates, FMA signed-zero heading, timer/position-lock preparation, exact CTRL/PDMOVE fields and names, byte/phase branches, request priority, independent state, dependency guards and atomic failures |
| Complete CTRL/PDMOVE scan | All 668 records in 2,273 recognized archive IGBs decode: 526 CTRL and 142 PDMOVE; zero rejections |
| Independent control instruction oracle | 10,000 cases, 2,500 each for CTRL, PDMOVE, post-input control and MOVE; zero mismatches at the supported service boundaries |
| Original command/placement integration | 49 additional checks pass using Agumon Move 130 / Stop 1540, CTRL 6499 / 622 and all eight PLAYERPOINT 115 matrices; explicit command effects request WAIT/WALK and travel approximately 8.275 local units over 18 Move / six Stop updates at cached delta 1 |
| Packaged walking regression | 120-frame SDL replay passed: unchanged 198 host steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads and final (-292,40,431); pause/restart/focus-loss checks passed |
| Packaged visual inspection | 1280x720 overview and walking captures inspected; version 0.16 visible; Agumon and original map render in the 1236x600 walking target with 8x MSAA |

The oracle executes a bounded instruction profile with synthetic named objects
and preallocated channel/list capacity. Host atan2, timer creation and sequence
dispatch are supplied boundaries. It does not establish original libm parity,
allocator behavior, full sequence execution or gameplay equivalence. The optional
data checks explicitly apply commands at stored matrices; they are not original
player creation, a scheduler run or physical multiplayer. Original bytes,
instruction probes and captures remain ignored. See [MODEL_CONTROL.md](MODEL_CONTROL.md).

## Native model turning and movement validation

Version 0.15 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. It adds the recovered model turn interpolation and combined
rotation/translation order to the portable core. The walking preview still uses
its provisional host controller; original player preparation and dispatch remain
required before that core can drive the original game.

| Check | Evidence |
| --- | --- |
| Release build and package | Passed; 27/27 CTest suites and 6 Python verifier tests passed |
| Model motion fixtures | 65 asset-free checks: shortest-arc ties, signed steps, exact-zero/force branches, timer side effects, Euler basis and position, negative zero, update order, independent ownership and atomic failure |
| Accumulated angles | Accelerated wrapping matches a separate literal repeated-float loop across both signs and exponent boundaries through magnitude 2^27; stagnant original loops stop explicitly |
| Independent instruction oracle | 9,000 cases: 6,000 standalone turns and 3,000 combined ticks; all 8,434 published states/matrices match by bits, with 566 original non-finite poses rejected at the native boundary and zero mismatches |
| Original Agumon commands and placements | 22 additional checks pass using Move 130 / Stop 1540 and all four PLAYERPOINT 115/group-0 matrices; 18 Move and six Stop updates at explicit cached delta 1 travel approximately 8.275 units along each source basis and stop |
| Packaged walking regression | 120-frame SDL replay passed: unchanged 198 host steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads and final (-292,40,431); pause/restart/focus-loss checks passed |
| Packaged visual inspection | 1280x720 overview and walking frames inspected; version 0.15 is visible, and the walking target is 1236x600 with driver-supplied 8x MSAA |

The instruction oracle shares host double sine/cosine and IEEE float FMA with
the native boundary. Its bitwise matches establish surrounding arithmetic and
control flow, not equivalence to the original PPC math library. NaN payloads,
FPSCR exception flags and guest subnormal modes remain outside this profile.
The original-data fixture exercises four stored placements, not original spawn
initialization, physical multiplayer or the game's scheduler. Original data,
instruction probes and captures remain ignored. See [MODEL_MOTION.md](MODEL_MOTION.md)
for source addresses, supported effects and remaining integration work.

## Native motion and timer validation

Version 0.14 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. The original motion arithmetic and timer are portable core
implementations; the walking preview still uses its provisional host controller.

| Check | Evidence |
| --- | --- |
| Release build and package | Passed; 26/26 CTest suites, 6 verifier tests, 9 boot CLI, 11 app CLI and 10 play CLI tests passed |
| Motion fixtures | 168 checks: six modes, preserved/impulse speed, strict crossings, expiry before clamp, exact-zero sentinel, signed deltas, valid zero-frame infinity/NaN, malformed schema and atomic input failure |
| Channels, matrix and timer | Independent owned channels, shared cached delta, initial cached frame 1, coarse clock/float rounding/wrap, signed scale and elapsed time, saturation, strict expiry, preserved restart fields, local scale/rotation and explicit fused translation |
| Complete original command scan | All 3,796 MOVE/MOVEEX records in 2,273 recognized archive IGBs decoded; 197 MOVEEX and 131 zero-frame records; zero rejections |
| Independent scalar instruction oracle | 18,000 setups and 54,000 ticks agree with a bounded interpreter of the supplied PPC routines, including expiry/stop, overflow and unordered comparisons; finite/infinite results checked by bits, NaN payloads excluded |
| Agumon original commands | All 16 MOVE records decode; original Move 130 reaches speed 0.7 and travels approximately 6.65 over 18 delta-1 updates; Stop 1540 travels approximately 1.625 over six updates and becomes inactive |
| Packaged walking regression | 120-frame SDL replay passed: unchanged 198 steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads and final (-292,40,431); pause/restart/focus-loss checks passed |
| Packaged visual inspection | 1280x720 overview and walking frames inspected, driver supplied 8x MSAA; version 0.14 and existing preview scope remain visible |

Original instructions/data and local oracle probes remain ignored. These checks
establish the supported arithmetic/data profile, not a full original gameplay
comparison. NaN payloads, FP exception flags and guest subnormal modes are not
modeled. Original actor/sequence state, turn interpolation, collision response,
timer ownership and scheduling are required before integrating this core into
the original game. See [MOTION.md](MOTION.md).

## Native initialization and pad validation

Version 0.13 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. This increment reconstructs preliminary loading, supported
native callback effects and original physical-pad state. It does not complete
the original startup, audio backend or multiplayer game.

| Check | Evidence |
| --- | --- |
| Release build and package | Passed; 25/25 CTest suites, 6 verifier tests, 9 boot CLI and 11 app CLI tests passed |
| Native controller fixtures | 50 checks: exact exports/schema, owned state, source-qualified aliases, captured recipient keys, preserved RAM fields, ordered unknown-handler stop, atomic malformed steps and budgets |
| Startup loading fixtures | 81 checks including the prior aggregate path and new preliminary-only reader order, descriptor retention, empty `/s` subset, repeated loads, partial ownership, malformed routes and byte caps |
| Original preliminary trace | Only `ndmw.afs[80]` descriptor and `[178]` member requested; one retained file, 222,408 bytes and four exports; no ordinary/common read |
| Original callback effects | Preliminary Static info 499 / RAM 496 registers `STATICINFO / dng101b1 / 0`, then Sound info 3 stops the prefix; separate explicit Switch inspection initializes info 3471 / RAM 3443 without changing its target |
| Packaged preliminary CLI | Reports preserved native RAM/recipient state and returns the expected exit 3 at `beSoundInfo`; func/boot do not execute |
| Original pad fixtures | 2,130 checks: radius and angular boundaries, strict/inclusive comparisons, chord edges, independent stick/button repeat timers, four physical slots, freeze/disconnect/reconnect, invalid-input atomicity and raw GC conversion |
| Independent pad conversion oracle | Every 65,536 raw button mask agrees with interpreted original conversion instructions; all 131,072 signed-byte stick pairs across main/C-stick agree with the SDK branch formulas |
| Aggregate inspection regression | One-player profile retains nine files, 4,006,731 bytes, 454 exports, one Lua info, 17 held scenes and 436 aggregate deferred infos; actual Lua state remains alive |
| Startup UI | Finite packaged 1280x720 run passed; inspected screenshot shows explicit preliminary/audio bypass scope and readable owned-file results |
| Walking regression | Packaged 120-frame replay passed with unchanged 198 simulation steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads, final (-292,40,431), and pause/restart/focus-loss checks |

The native pad library has no device enumeration or original player-join state;
four independent synthetic slots do not establish physical multiplayer gameplay.
Host libm results at angular boundaries are not claimed bitwise identical to the
original PPC math library. Preliminary callback dependencies are explicit and do
not establish the original complete manager registry or sound-transfer services.
The existing aggregate inspection queue spans phases and must not be replayed as
the original later registration queue. See [CONTROLLERS.md](CONTROLLERS.md),
[PLAYER_MOVEMENT_RESEARCH.md](PLAYER_MOVEMENT_RESEARCH.md) and
[AUDIO_STARTUP_RESEARCH.md](AUDIO_STARTUP_RESEARCH.md).

## Native walking preview validation

Version 0.12 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. This establishes an interactive native original-map walking
state, not completion of the original game or its startup sequence.

| Check | Evidence |
| --- | --- |
| Release build | Native executable, portable core and package built successfully |
| Asset-free tests | 23/23 CTest suites; 6 verifier, 8 inspect CLI, 5 catalog CLI, 8 boot CLI, 11 app CLI and 10 play CLI tests passed |
| Walking solver | 683 synthetic checks: slopes, shared edges, tiny gaps, stepped/stacked floors, bounded hierarchy, swept wall radius, sliding, overhead walls, malformed input and equal travel at 30/60/144 Hz |
| Collision reader | 40 synthetic checks plus 5 original checks; 102 original floor triangles and 228 original wall/door triangles preserve their source coordinates and stored inner switch selection |
| Original assets | Map package dng101b1 member 1 / BG02, player model 1386, original entry 115/group 0/player 0, waitS/runD clips; 236 textured GPU batches and six images |
| SDL keyboard replay | 120-frame run: 198 host simulation steps, 101.333 units of total travel, 46 idle and 152 running steps, 102 pose uploads; final position (-292,40,431) after restart and further movement |
| Pause, restart, focus loss | Production replay asserts frozen position/animation, cleared held inputs and exact grounded spawn/facing/idle restoration; all three checks passed |
| Original-data lifecycle | 37 local harness checks passed: movement in all four directions, stable map batches/textures, pause, held-key clearing, restart, return to workbench, fresh start, remount, missing-resource failure and recovery |
| Rendering | 1280x720, 1920x1080 and 3840x2160 windows passed the same replay with identical simulation results; scene targets 1236x600, 1876x960 and 3796x2040; driver supplied 0/8/8 MSAA samples |
| Visibility and GPU state | Original GPU suite plus partial-occlusion silhouette, unchanged visible texture, no self-highlighting, invalid batch rejection, resize, MSAA and state restoration passed |
| Visual inspection | App-rendered walking, spawn, pause and overview frames inspected; original Agumon is visible in the corridor, with gold hidden-surface indication when needed |

The 120 Hz solver, speed, body dimensions and follow camera are provisional
native behavior. These finite runs are not a sustained FPS benchmark. Physical
Xbox/PlayStation/GameCube device compatibility, original collision parity,
enemies, combat, doors, audio, progression, saves and multiplayer remain open.
Original-data captures and harnesses are ignored local files; CI needs no game
assets. [PLAY_PREVIEW.md](PLAY_PREVIEW.md) describes launch and controls.

## Earlier viewer and startup validation

Local validation completed 2026-09-08 on Windows x64, MSVC 19.51, NVIDIA
GeForce RTX 5070 Ti. This records version 0.11 owned startup loading and routing,
not gameplay compatibility or a finished port. Earlier asset, animation,
placement and renderer measurements are retained from their respective
milestones. Retained Lua runtime ownership, ordered common-file loading,
source-qualified info routing and startup UI/CLI lifecycle are the new
acceptance work.

| Check | Result |
| --- | --- |
| Release core and native app build | Passed |
| CTest: existing asset/viewer suites plus scripts, Lua 4, startup and startup loading | 21/21 suites passed in Release |
| Embedded scripts and scheduling | 67 synthetic checks plus 66 original checks passed in Release and fully instrumented AddressSanitizer; exported identity, shared records, first-match shadowing, independent native barriers and aggregate budgets |
| Original embedded scripts | dng101b1 member 1 exports 55 supported script records; func/boot/start/first references and byte sizes match the recovered chains |
| Native Lua 4 | 286 synthetic checks plus 10 original checks passed in Release and fully instrumented AddressSanitizer; typed stack/call/closure/table/vararg behavior, malformed chunks, invalid operands, poison-on-failure and cumulative limits |
| Boot host/resolver/runtime fixtures | 69 checks passed in Release and fully instrumented AddressSanitizer; prior host/resolver checks plus actual string/table/function/number globals after source destruction, move construction/assignment, stable report identity, failed-state rejection, one-attempt execution and resolution after moves |
| Startup loading/routing fixtures | 59 checks passed in Release and fully instrumented AddressSanitizer; bounded callback reads, stage/FIFO order, source-qualified aliases, common Lua chunks not rerun, exact scene rules, valid reference zero, malformed/null branches, cumulative camera/name/export limits, failed-prefix counters and independent loading/routing success flags |
| Original owned startup resources | Zero/one/three profiles retain 96/9/11 files and 14,535,397 / 4,006,731 / 4,747,791 serialized bytes; every document equals its direct AFS payload, including repeated occurrences across profiles |
| Independent original info routing | All 1,617 qualified routing tuples, including the separate late-scene list, match an independent raw-IGB oracle across three profiles; final deferred counts 583/436/446 and held-scene counts 104/17/19; every ordered source/stage/ordinal/location/size also matches |
| Retained original boot runtime | All 17 original helper globals survive source/chunk destruction and runtime moves; the actual production Host path still matches official Lua 4.0.1 for all 188 calls/arguments and 110 queue entries, with unchanged instruction counts |
| Packaged startup CLI | All original ordered queue and qualified Lua/held/deferred entries match the independent oracle for zero/one/three profiles; actual retained Lua state reported after loading |
| Version 0.11 app lifecycle | 32 isolated native checks passed: successful owned-session counts, invalid input/package/unresolved-resource replacement, clear/remount, fresh-run recovery and byte-identical original 236-batch animated-player framebuffer before/after success, failure and result-view rendering |
| Version 0.11 startup UI | Packaged finite 1280×720, 1920×1080 and 3840×2160 runs passed; Resources, Loaded files, Deferred and Late views inspected at 720p; difficulty/camera variants, missing/unresolved resources, slot-1-only state, empty startup and all nine pages return expected results |
| Independent original execution | Native VM and unmodified official Lua 4.0.1 match all 188 ordered host calls/arguments and all 110 total queue entries across zero/one/three-player input profiles |
| Original zero-player boot | func: 35 instructions / 0 calls; boot: 505 instructions / 105 calls; 94 distinct requests, all catalog-resolved |
| Original one-player boot | Slot 0 Agumon with sword01/gun01/nil: boot 310 instructions / 34 calls / 7 requests, all catalog-resolved |
| Original three-player boot | Agumon/Vmon/Dorumon with shared weapons: boot 432 instructions / 49 calls / 9 requests, including original q_ substitution and exact dedup; all resolved |
| Boot CLI syntax | 8 table-driven test methods cover explicit state, malformed/range/duplicate selectors, nil weapon configuration, fixed USA getters, missing values and mixed app sources; no graphics/data required |
| Version 0.10 boot app lifecycle | 26 ignored native checks passed: mixed-source guards, input order, zero/one queues, success replaced by failed rerun, recovery, remount/missing mount and exact existing animated-scene pixels before/after boot success/failure; 9 additional tool CLI cases passed |
| Version 0.10 packaged startup UI | Original zero/one/three-player queues match all reference entries; finite 1280×720, 1920×1080 and 3840×2160 runs passed and screenshots were inspected; difficulty/camera variants, empty startup without data, missing/unresolved resources and slot-1-only state return the expected results |
| Version 0.10 viewer regression | All nine pages and the existing textured, animated player scene passed finite runs with the final packaged executable |
| Python ISO verifier tests | 6 synthetic tests passed |
| Native CLI end-to-end tests | 8 synthetic tests passed |
| Catalog/package CLI arguments | 5 tests passed, including missing/duplicate/conflicting selectors, empty paths, signed/partial/overflow indices and index-zero handling |
| Native app CLI without graphics | 11 test methods with table-driven subcases passed; explicit player selectors, duplicates, ranges, source conflicts, finite clip time and playback prerequisites; runs in Windows app CI |
| Player placement fixtures | 66 synthetic checks passed in Release and fully instrumented AddressSanitizer; exported identity, exact groups/slots, affine rotation/scale/reflection, immutable source, invalid links, overflow and aggregate serialized-name budgets |
| Original entry matrices | Both groups and all four slots in ndmw entry 179 decoded; all 128 matrix components compare bit-for-bit with eight original stored matrices |
| Player visual fixtures | 118 synthetic checks passed in Release and fully instrumented AddressSanitizer; model instance/ONE/LOD/actor/appearance/skin provenance, malformed or ambiguous ownership, attachments, modifiers, visibility and allocation guards |
| Original player visual | 12 additional checks passed in Release and AddressSanitizer: Agumon's three explicit branches and fourth model instance select the expected original geometry |
| Dynamic scene GPU and focus | Release and AddressSanitizer passed isolated actor-batch updates, every static pixel unchanged, UV/sampler retention, occlusion, exact reset, MSAA, stable normalization, source-bounds focus and invalid-input state preservation |
| App lifecycle integration | 37 ignored local harness checks passed: original shadow Solid to rejected Textured to Solid restores exact pixels; failed clip index/time/post-decode placement retains prior clip/time/vertices/pixels; member changes remove player; invalid resource leaves 235 static batches |
| Combined original map and player | BG02 plus high Agumon: 236 textured batches, six images, 31,886 static vertices plus 1,371 player vertices; full entry matrix replaces authored model-node transform |
| Original player native variants | Package and direct-AFS sources; high/low appearance, Solid shadow, both entry groups, slots 0 and 3, waitS/runD and finite playback; unsupported shadow material and invalid resource/model/point/group/branch/clip/time fail explicitly |
| Compact playback and 4K | 1280×720 and 3840×2160 host windows render combined scenes; actual targets 650×243 at MSAA 0 and 3210×1683 at MSAA 8; elevated inspection camera shows group 0, while nearby geometry can obscure group 1 |
| Version 0.9 startup research | Original file-list manager dispatch order func/boot, then start, then first traced; start calls funcPlayerSet; embedded filename lookup and outer ONE-model matrix replacement documented; script execution was added in version 0.10 as recorded above |
| Package synthetic fixtures | 109 checks passed in Release and AddressSanitizer: export/member order, catalog identity, exact `/s` handling, malformed input, forged metadata, NUL-padded repeated names, boundary compatibility and allocation limits |
| Original package integration | 8 checks passed in Release and AddressSanitizer; 97 descriptors / 195 members, including 98 preliminary `/s` members; both dng101b1 members match their AFS bytes exactly |
| Named map export identity | `dng101b1.igb` -> ndmw entry 80 -> member 1 `[0.179]` -> explicit export 7 / scene index 1 / info 3372 / BG02 root 2877 |
| Original switch command evidence | `first.lua` submits `SWITCHCTRL / ALLON`; original dispatcher, controller registration callback and target-mask effect traced; controller 3471 is explicit export 9 and targets root 2877 |
| Switch-controller and scene fixtures | 127 combined scene checks pass in Release and AddressSanitizer, including all-on/off, cross-word and nested masks, unchanged bytes, wrong/exported targets, malformed state and cumulative child-validation budget |
| Original native command replay | ALLON produces 235 instances / 26,352 triangles; ALLOF is complete and empty; every instance path, geometry reference and world transform agrees with explicit child-zero inspection |
| Package/control UI regression | 36 finite native checks rerun on version 0.9 passed: descriptor-only inspection, original commands, source/selector guards, invalid controller/target, empty/no-scene results, all eight pages, font, raw scene, Agumon animation and manual rig |
| Final compact scene controls | 1280×720 screenshot inspected after tab-fitting adjustment: Instances, Details, State and Status visible; ALLON, ALLOF and Stored selection fit |
| Named package at 1080p and 4K | All 235 textured batches / 5 images retained; 1290×603 and 3210×1683 scene targets inside 1920×1080 and 3840×2160 host windows, actual 8-sample MSAA |
| USA DOL identity | SHA-1 matches `e409a88a7379ed1a536f93b0a303a0ce7cd5d877` |
| ISO versus extracted data | Boot header, DOL, filesystem table and all four filesystem files match |
| Native boot/DOL inspection | `GDJEB2`, revision byte 0, 10 loaded sections, entry `0x80005240` |
| Native archive index | 1,382 `area.afs` entries; 973 `ndmw.afs` entries |
| Native IGB metadata integration | 2,274 inputs, zero failures (details in IGB_FORMAT.md) |
| Native IGB object integration | All 2,274 inputs and their object lists passed (details in IGB_OBJECTS.md) |
| Logical resource catalog | 2,080 paths, 2,079 available; archive counts match; 20 logical/direct reads identical |
| Native texture integration | 25,906 base images decoded, zero failures: 22,254 indexed8/RGBA8 and 3,652 BC1 |
| Independent BC1 sample | Original card payload decoded by Pillow's DDS reader matches native RGBA byte-for-byte |
| Texture viewer | Original font atlas and card rendered on the native GPU; screenshots inspected |
| Logical texture loading | `--resource bectext.igb` resolves and displays `ndmw.afs[79]` |
| Invalid explicit texture source | Missing resource reports an error and exits with code 3 in a finite smoke run |
| Native mesh corpus integration | 198,068 geometry instances decoded; 3,793,429 emitted triangles; zero unsupported profiles or failures |
| Original UV corpus | All 198,068 geometries checked: 46,988 valid UV streams, 151,080 absent; zero size or non-finite failures |
| UV wrapping evidence | 48,360 UV pairs on 3,825 geometries lie outside [0,1]; source coordinates are preserved |
| Stored base-texture binding corpus | 3,208 supported bindings; all have valid UVs and decodable bound images; remaining material profiles explicitly unsupported |
| Mesh synthetic fixtures | Triangle lists, grouped strips, winding, degenerates, counts, references, finite values and allocation bounds passed |
| Original topology identification | DOL enum table confirms triangle list 3 and triangle strip 4; all source index/length constraints checked |
| Portable orbit camera | Projection/frustum, orbit, pan, zoom bounds, reset and invalid-input tests passed |
| Native GPU renderer test | Pixel coverage, orbit, wireframe, resize, MSAA resolve, textured colors/UVs/wrapping/filtering/alpha, state restoration and cleanup passed |
| GPU pose updates | Position changes retain texture, UVs, indices, normalization and frame counter; reset reproduces pixels, invalid updates preserve the prior pose |
| Joint overlay projection | Uses the same fixed normalization/camera; fitted center, clipping and unchanged reference projection passed |
| Original skin reference mapping | Agumon geometry 5665 -> selector 5481 -> skin 404 -> database 810 -> skeleton 701; 25 bones, 20 joints, 24 palette entries |
| Original CPU manual pose | All three Agumon geometries reset with zero measured position error; bone 8 Z+30 degrees changes only vertices mapped to its joint descendants |
| Original rig corpus | All 894 `ndmw.afs` IGBs screened: 476 geometries with skin buffers, 290 supported with 222,694 vertices; no supported reset/manual-pose failures |
| Rig reset accuracy | Maximum coordinate error 1.97209e-15 across supported corpus; remaining 186 skinned profiles explicitly unsupported |
| Skinning synthetic fixtures | 153 checks passed, including full helper ancestry, original quaternion signs/norm, noncommuting child rotation, reset and malformed graph/matrix/palette/cache rejection |
| Native manual Agumon pose | Textured high geometry, bone 8 (`LArm01`) Z+45 degrees: 252 changed vertices, maximum displacement 4.47705; inverse-bind reset changes zero vertices |
| Original animation graph and poses | All 82 Agumon clips decoded and evaluated at zero, quarter, three-quarter and endpoint times: 8,200 bone transforms with complete mesh/joint outputs |
| Independent pose composition | 328 high-detail Agumon poses / 449,688 vertices compared with separate float32 composition using the same resolved channels; maximum vertex component difference 2.38419e-6, below 1e-5 |
| Reference codec compatibility | 55,950 quaternion/translation tuples over every half-sample interval and endpoint match the pinned enbrip implementation at nine significant digits; separate original engine interpolation is documented in ENBAYA.md |
| Original interpolation math | 49 synthetic checks; 83,400 quaternion comparisons against a bounded scalar interpreter of the original DOL routine match exactly on MSVC; the comparison does not emulate floating exception/status flags |
| Animation graph fixtures | 50 synthetic checks cover binding identity/ambiguity, masks including bits 31/32, duration, unsupported channels, owned data and original pose guards |
| Bounded Enbaya fixtures | 201 checks passed in Release and AddressSanitizer, covering packed widths, state changes, malformed/truncated streams, allocation limits and both sampling paths |
| Native clip inspection | Original textured waitS/runD at zero and quarter time produce distinct inspected frames; 720p controls fit and 1080p uses actual 8-sample MSAA |
| Native live playback | 32 frames upload 33 evaluated poses and cross the runD endpoint with Loop enabled while retaining texture and UVs |
| Animation error handling | Missing/case-mismatched names, out-of-duration time, unsupported rig and missing source exit 3; malformed, non-finite, negative or mixed manual/animation CLI inputs exit 1 |
| Original 3D sample | `ndmw.afs[179]`, reference 3: 24 vertices and 8 triangles rendered |
| Agumon stored-space preview | `_common/p_agumon.igb`, mesh 2, reference 5665: 1,371 vertices and 1,966 triangles; solid and wireframe screenshots inspected |
| Agumon bound-image verification | Image 5609, `P_agumon01.dds`, 256×256 BC1: all 65,536 texels opaque; independent Pillow DDS decode matches native RGBA exactly |
| Agumon textured GPU preview | Original high-detail mesh, 1,371 UV pairs and bound image rendered; yellow skin, green eye, mouth/teeth and white claws visually inspected |
| Low Agumon textured preview | Mesh 0, image 5597 (`P_agumon02.dds`, 64×64), 570 UV pairs rendered |
| Unsupported original material | Shadow mesh 1 in Textured mode reports its incomplete/ambiguous scope and exits 3; its Solid preview still succeeds |
| Mesh MSAA target | 1920×1080 window, 1290×554 textured offscreen mesh target with rig inspection and actual 8 samples |
| Compact rig layout | 1280×720 window: scrollable left Rig tab, visible manual controls and complete posed model in a 636×211 viewport |
| Manual rig source routing | Logical resource and direct AFS entry both work; low-detail default, high-detail textured/wireframe and combined head-axis deltas render successfully |
| Explicit rig failures | Helper bone 0, out-of-range bone 25, unsupported box and missing resource exit 3; malformed angles and CLI combinations exit 1 |
| Seven UI pages | Finite native smoke runs passed, including the existing texture CLI |
| Textured source routing | Both logical resource and direct AFS entry select the verified original image; missing source exits 3 |
| Invalid explicit mesh source | Missing resource exits 3; `--mesh` without a source exits 1 |
| Native BNR decode | 96×32 RGB5A3 banner displayed correctly |
| Initial five UI pages | Version 0.1 hidden rendering tests passed; overview, graphics and archive screenshots inspected |
| 4K/MSAA context | 3840×2160 framebuffer, actual 8 samples, VSync Off reported by native graphics page |
| Start without game data | Passed; data can be selected later |
| Static scene fixtures | 80 active checks passed in Release and AddressSanitizer: transforms, DAG instances, ordered geometry emission, masks, explicit inspection, malformed data, incomplete diagnostics and limits |
| Instance-path materials | 96 synthetic checks passed in Release and AddressSanitizer, including independently inherited bind/enable scopes and full-path texture-modifier rejection |
| Original BG02 stored state | `ndmw.afs[179]`, scene index 6 / info 3372 / root 2877: stored root mask is zero, so default traversal remains empty |
| Original BG02 explicit inspection | Root child 0: 462 node visits, 219 transforms, 235 instances from 29 geometry attributes; 31,886 placed vertices and 26,352 triangles; complete static traversal |
| Independent scene placement | Separate raw-graph traversal and original instruction arithmetic match all 235 owner/geometry/path tuples; matrix component error 0 and maximum world-position component error 9.5367431640625e-7 |
| Original scene texture coverage | All 235 instances have verified path bindings and correctly sized UVs; 5 unique images decode to 528,384 RGBA bytes |
| Native scene GPU | Shared placement, differently textured batches, opaque occlusion independent of draw order, texture sharing, one MSAA target, state restoration and malformed/budget rejection passed in Release and AddressSanitizer |
| Native BG02 scene | Solid, Wireframe and Textured finite renders passed; 720p controls fit and the 1080p scene target uses actual 8-sample MSAA |
| Native scene at 4K | 3840×2160 host window, 3210×1683 textured scene target, actual 8-sample MSAA, all 235 batches and 5 images retained |
| Eight-page and viewer regression | All eight pages, original font, manual Agumon pose and textured runD inspection passed finite native runs |
| Scene error handling | Empty stored selection, invalid root/child, non-switch override, missing source and unsupported dynamic branch exit 3; invalid/mixed CLI options exit 1 |

Run `scripts/build.ps1 -Package` to reproduce the Windows build and CTest.
The additional synthetic checks are:

```powershell
python -m unittest discover -s tests -p 'test_*.py' -v
python tests/cli_tests.py build/Release/dw4_inspect.exe
python tests/catalog_cli_tests.py build/Release/dw4_catalog.exe
python tests/app_cli_tests.py build/Release/dw4_pc.exe
python tests/boot_cli_tests.py build/Release/dw4_boot.exe build/Release/dw4_pc.exe
```

Example finite rendering check (output is local and ignored by Git):

```powershell
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 3 --page graphics --screenshot work/graphics.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource bectext.igb --screenshot work/font.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --archive area.afs --entry 621 --screenshot work/card.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource _common/p_agumon.igb --mesh 2 --screenshot work/agumon.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --screenshot work/agumon-textured.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --rig --bone 8 --rotate-z 45 --screenshot work/agumon-manual-pose.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --animation runD --animation-time 0.2 --screenshot work/agumon-animation.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --archive ndmw.afs --entry 179 --scene-root 6 --scene-inspect-child 0 --scene-mode textured --screenshot work/bg02-scene.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --package dng101b1.igb --package-member 1 --scene-root 1 --scene-switch-control 3471 --scene-switch-command all-on --scene-mode textured --screenshot work/bg02-package.bmp
.\build\Release\dw4_renderer_tests.exe
```

`--smoke-test` deliberately renders finite hidden frames and skips normal frame
limiting. It is not a benchmark or proof of 144 FPS game simulation. The 4K
check used a local settings file requesting 3840×2160 and MSAA 8x. Borderless
desktop mode was implemented but not exercised in the hidden-window test.

No physical controller was connected during validation; Xbox, PlayStation and
GameCube hardware behavior remains unverified. Gameplay scene activation, actor and
animation-combiner behavior, gameplay, audio, saves, online co-op and modding remain
unimplemented. Original clip inspection includes helper transforms; skinning
still requires single influences and identity selector transforms. See
[SKINNING.md](SKINNING.md) and [ANIMATIONS.md](ANIMATIONS.md).
Static scene inspection now reconstructs a bounded export/subtree profile with
original placements and explicit root selection. The inspected BG02 switch is
stored disabled. An explicit original ALLON command can now enable it through
its exported controller, alongside the separate manual-child inspection option.
The original func/boot script order now executes in a bounded native VM and
builds a resolved resource queue. Version 0.11 retains that live VM, loads the
common payloads and reconstructs the original info-routing order. The original
loading-screen gate, active manager callbacks and later layer/text initialization
remain unimplemented. Nested masks and source data remain unchanged. See
[STARTUP_LOADING.md](STARTUP_LOADING.md) and [STARTUP_VIEWER.md](STARTUP_VIEWER.md).
Explicit player visual placement now replaces the verified outer model matrix
and joins original single-clip playback to the scene. It does not execute player
generation, choose startup state, compose weapons/shadows or simulate movement.
The host inspection camera can be occluded by map geometry and does not reproduce
the original camera. See [PLAYER_VIEWER.md](PLAYER_VIEWER.md),
[PLAYER_PLACEMENT_RESEARCH.md](PLAYER_PLACEMENT_RESEARCH.md) and
[STARTUP_RESEARCH.md](STARTUP_RESEARCH.md).
See [PACKAGES.md](PACKAGES.md) and [SCENE_ACTIVATION.md](SCENE_ACTIVATION.md).
The scene renderer applies base
images with inspection shading; original lighting, render passes, culling and
transparent-surface ordering are still outstanding. See [SCENES.md](SCENES.md),
[SCENE_RESEARCH.md](SCENE_RESEARCH.md) and [SCENE_RENDERER.md](SCENE_RENDERER.md).
The mesh viewer displays stored or posed positions with inspection shading or a
verified base-texture binding. Full original materials and comparison against
recorded gameplay frames remain outstanding. The 3,208 supported bindings are a limited profile, not a claim that all
198,068 meshes have fully supported materials. See [MATERIALS.md](MATERIALS.md)
and the corpus diagnostics in [MATERIAL_RESEARCH.md](MATERIAL_RESEARCH.md).
Base texture decoding does not establish final appearance with the original renderer.
No exhaustive visual comparison of the 25,906 image instances was performed.
See [TEXTURES.md](TEXTURES.md) for layout constraints and sample observations.
Big-endian IGB metadata and documents were tested synthetically; all recognized
local game IGB metadata was little-endian. The original data remains outside
the repository.

GitHub Actions independently builds/tests the portable core on Windows/Linux
and builds/packages the Windows app. The workflow does not require game files
and does not perform an original-data or physical-GPU test. The renderer GPU
test is built there but is run explicitly on a machine with an OpenGL driver.
