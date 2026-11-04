# Data Overflow: the host side (round O1a, lane A)

What round O1a lane A built of [SURVIVAL.md](SURVIVAL.md) 15.1: the
extraction of 13.3, the run loop and the switch, the generator save /
restore, the static audit, the isolation of the renderer, audio, input,
F-keys, Discord and achievements, the pacing reset and the dev start. Lane B
(the engine hooks) and lane C (the director) build on the interfaces below.

## 1. Files

| File | What |
| --- | --- |
| `src/game/host_view.hpp/.cpp` | The extraction (13.3, T11): `HostView` (window, renderer, sound output, controllers, F1 state, picture state) and the functions both loops call: `host_event`, `toggle_menu`, `read_live_input`, `live_pad_sample`, `mix_audio` (with the host `gain`), `note_stage`, `make_picture`, `present_scene`, `set_menu_navigation`, `new_ui_frame`, `draw_tags`, `render_ui`, `write_screenshot`, `finish_present`, `pace_presents`, and the F1 Settings tab's display, audio, presence, keyboard and controller sections. The statements moved out of `game_main.cpp`'s `run()` in their order; the story loop keeps its old names as references into `HostView`. |
| `src/game/overflow_run.hpp/.cpp` | The run without a window: `RunRequest` and `parse_run_request` (`--overflow-run`), `RunEnd`, `StoryRandomGuard`, `TestEnvironmentScope`, `RunCards`, `prepare_run_engine`, `StorySwitch`, and the `RunDriver` interface. |
| `src/game/overflow_host.hpp/.cpp` | `run_from_story` (13.3 steps 2-4) and the run loop, the run's F1 menu, the fades, `start_refusal`, the F1 Gameplay tab's dev start. |
| `src/game/overflow_driver.cpp` | `make_run_driver`: the host's stand-in until lane C's director plugs in (lane C replaces this file's body). Since the integration (section 7) it gives the run's engine lane B's `OverflowRules` and ends the run on their report. |
| `tests/overflow_host_tests.cpp` | ctest `overflow_host`: the request, the story generator across a run engine's construction and life, the test variables, the cards, the engine setup, the run engine with lane B's rules (H3: the arena's request passes, every other stage change and both resets stay out), the switch. |

## 2. Interfaces for lane C (and B)

**The driver.** A run is driven through `dw4::game::overflow::RunDriver`
(`overflow_run.hpp`, no SDL in the interface). The host calls, per pass of
the run loop:

| Call | When | Lane C does |
| --- | --- | --- |
| `attach(run, request)` | once, after `prepare_run_engine`, before `request_stage(field)` | `Engine::set_overflow_rules` with the run's rule set (the host's `request_stage(field)` is then the one stage request H3 lets through besides `OverflowRules::field_change`), `OverflowRules::director` (the host destroys the engine before the driver, so a director the driver owns outlives every call), the loadout and Plug Lab points |
| `before_step(run, pads)` | before every run step; `pads[0]` is this machine's player 1 composed as in the story (`live_pad_sample`), slots 2-4 disconnected | a test bot (`bot=kite`) replaces `pads[0]`; the run's own menus may read or neutralize it; queue commands (offers, pause, Retire) for this step |
| `after_step(run)` | after every run step | the run's end once its results closed |
| `retire(run, why)` | F1 > Data Overflow > Retire, the window closing (`window_closed`, the program then quits), a test run's limit (`frames=`, else `--smoke-test`'s count) | Retire at the next step: write what was earned, show the results, then report the end |
| `ended()` | every pass | the end without another step (the host stops stepping and fades out) |
| `decorate(run, items)` | after `Engine::render`, before the renderer draws | 2D items into the draw list (the TitleEntry technique, SURVIVAL.md 13.6) |
| `draw_overlay(run, frame)` | inside the ImGui frame after the picture (`OverlayFrame`: window size, the 4:3 screen, F1 open, run steps) | O1a's plain overlay boxes (level-up, pause, results) |
| `draw_menu(run)` | inside F1 > Data Overflow, after the host's Retire | debug lines, run options |
| `status(run)` | every pass (Discord, the F1 tab, the log) | form name, run level, the run clock (frozen while the run is) |

`make_run_driver(const RunRequest&)` in `src/game/overflow_driver.cpp` is
the one factory; lane C replaces it to return its director's driver.
`RunRequest::options` carries every key the host does not know (`tier=`,
`offers=`, ...) from `--overflow-run`.

**The run's engine** (`prepare_run_engine`, before `attach`): a second
`Engine` on a copy of the story's mounted resources (mods included); cards
on an empty throwaway folder with both slots `CardSetting::none`; the cheats
off except fast loading (`DW4_OVERFLOW_TEST_CHEATS=1` keeps the test cheats);
port events off; `GameSystem::direct_start`, `players[0].digimon = form`, slot
0 joined alone, `ninzu 1`; the system generator reseeded with the run's seed.
The host never steps the story engine meanwhile and never touches it.

**The run loop** (`overflow_host.cpp`) is the story loop's pass with the run's
engine: events (`host_event`; F5 / F9 answer "Not in the simulator"), live
input (nothing while F1 is open), pacing (test runs one step a pass, else the
59.94 Hz step, at most four a pass; F1 pauses the run interactively; nothing
steps once it is over), the steps (the scene traversed once after each, as in
the story), `mix_audio`, name tags, Discord ("Data Overflow" / "<form> Lv <n> -
<mm:ss>"), `note_stage`, `make_picture` + `decorate`, `present_scene`, the
ImGui frame with `draw_overlay`, the run's F1 menu (Data Overflow tab with
Retire, the Settings sections) and the fade, `render_ui`, the run's
screenshots (`shots=`), `finish_present`, `pace_presents`. Lane B's H3 keeps
the console reset (B + X + START) off in an engine with the run's rules; the
loop still ends a run as retired if its engine reboots (a driver that gave
its engine no rules).

**The switch** (`StorySwitch`, owned by the story loop): `request()` (F1 >
Gameplay > Data Overflow (test) > Start a run, or `--overflow-run at=F` once
the story may hand over), 30 story steps with neutral live pads while the
picture and the sound fade to black, `due()` at the top of a pass, then
`run_from_story`, then `returned()`: the pacing starts over, the pad waits for
released buttons, the picture and the sound fade back in over 30 story steps.

**Command line.** `--overflow-run KEY=VALUE ...` (tokens up to the next
`--option`): `form=p_agumon field=stagbs02 seed=N bot=NAME at=F frames=N
shots=A,B,...` plus the director's keys. Without `at=` the run starts before
the story's first step and a test run ends with it (T6's headless runs:
`--overflow-run form=p_agumon seed=1 bot=kite --smoke-test 90000`); with `at=F`
it starts after story frame F once the story has loaded a stage. Output lines:
`overflow: run <request> at story frame <n> (<stage>)` and `overflow: run
<end> after <steps> steps (<form> Lv <n>), run hash <net fingerprint>`;
`DW4_TRACE_OVERFLOW=1` adds the run hash every 600 steps.

## 3. Isolation as built (13.2)

| What could leak | How it is kept out |
| --- | --- |
| the system generator | `StoryRandomGuard` copies `fight::generator()` before the run's engine is made and puts it back after the engine is destroyed (before the story's next step); the run reseeds it with its own seed |
| the engine's test variables | `TestEnvironmentScope`: DW4_DEVICES, DW4_BITS, DW4_XDATA, DW4_DISKS, DW4_FLAG_FOLDER, DW4_FORMS, DW4_LEVEL and DW4_CARD_RECORD leave the process environment for the run's life; DW4_GOD / DW4_ONE_HIT / DW4_INFINITE_MP (the Engine constructor) are undone by `prepare_run_engine`; everything else a test run sets is applied by game_main to the story engine only (DW4_PLAYERS, DW4_DIFFICULTY, DW4_ENTRY, DW4_COUNTERS, DW4_GAMEPLAY, DW4_RANDOMIZER, the online test switches). `DW4_OVERFLOW_TEST_CHEATS=1` keeps them (listed in `online_forbidden_env`) |
| story saves and cards | `RunCards`: an empty folder below the temporary folder, both slots empty, removed when the run is over; the mode writes nothing else (O1a has no meta file) |
| achievements, the online client | the run's engine never collects port events, and the story loop's tracker only sees story steps |
| renderer, smooth motion, name tags, films | at both switches: the picture state, the renderer's texture cache and kept picture, the interpolator and the labels start over |
| audio | one engine is mixed at a time; the playback queue is cleared at both switches; the host gain fades (at 1 the mix is unchanged) |
| input | the run gets this machine's live pads only (slot 1); after F1's Start and after the return the pad waits for released buttons |
| F-keys | F5 / F9: refused in a run; F2 (free camera): story only; F1: the run's own menu with the settings sections only; F11 as everywhere |
| Discord | the run's own activity; the story's comes back with its next pass |
| online | the dev start is refused in direct sessions, rooms, the online screens and with online rules; `--overflow-run` refuses the online options; nothing online is reachable during a run (and `Engine::set_online` throws while two engines live) |
| pacing | the story loop's accumulator and present clocks start over after the return: the story does not catch up the run's time |

## 4. Static audit (13.2)

Every writable data symbol of `dw4_engine` and `dw4_core` (the `.data`,
`.bss` and `.tls` sections of their objects, read with `dumpbin /symbols`,
216 entries without the C++ library's own) falls into one of these groups:

| Group | Symbols | Effect on the story across a run |
| --- | --- | --- |
| shared simulation state | `combat.cpp rng()::generator` (`fight::generator`) | the one: `StoryRandomGuard` |
| counters without simulation meaning | `GeometrySerial::next::counter` (renderer cache keys), `ModelCtrlManager::next_rams_generation` (host bots' caches), `Lua4Vm` identity (ownership checks), `Engine::next_manager_type_id` and the `manager_type_id<T>` ids (stable), `detail::g_live_engines` / `g_online_engines` (two while a run lives), `g_mxcsr_repairs` (diagnostics) | none: not in a fingerprint, a snapshot or a decision |
| caches of the game data | `randomizer::zone_dialog_text::cache` (by stage and point) | none: both engines read the same data and mods |
| scratch buffers | the `CollisionWorld` query vectors (`thread_local`, cleared per query) | none |
| values fixed from the environment at first use | the DW4_TRACE_* switches, DW4_FIXED_RANDOM, DW4_CLOCK_FILE, DW4_DISC_CLOCK, DW4_AX_PHASE, DW4_AX_COMPRESSOR, DW4_AUDIO_STREAMS_CHANNELS, DW4_AUDIO_DUMP_STREAMS, DW4_RANDOMIZER_SWEEP, DW4_CARD_RECORD's flag | constant for the process; the research tools among them (DW4_FIXED_RANDOM, DW4_CLOCK_FILE, DW4_DISC_CLOCK, DW4_AX_PHASE) act on every engine alike, are forbidden online and are never set in play |
| trace-only state | `run_script::logged`, `SoundManager::command::last`, `PanelManager::update::traced`, `NameSelect::update::last`, `advance_motion::traced`, `ride_step::traced`, `run_commands::dumped` | only with their DW4_TRACE_* / DW4_DUMP_SEQ set, output only |
| constant tables | the rest (`static const` tables with dynamic initialisation, arrays of `const char*`: names, columns, sequences, the audio compressor ramps and resampler) | none |

The host's own statics (`src/game`) act on the story loop only (the bots of
input scripts, the F1 Saves tab); the run loop calls none of them. T3 checks
the outcome: every story frame's fingerprint, net fingerprint (the generator,
the models' turns, the sound's random value, the session values) and the
canonical snapshot are equal with and without a run.

## 5. Tests and results (main 75c2436 + this lane)

Scripts live in the worktree's ignored `build/t` (`det.sh`, `shots.sh`,
`cmp_shots.sh`, `t3.sh`, `t4.sh`) and `build/st` (`st_all.sh`).

| Test | How | Result |
| --- | --- | --- |
| T11 | `det.sh` and hidden screenshots before and after the extraction (title, dng101b1 4:3 and 1600x900 widescreen, the lobby with name tags from the research card, the new game's lobby, F1 Settings, F1 Cheats) | det 4004f056 / 396cb219 twice; every scene picture identical; F1 Settings differs only in its live frame-time graph (as two runs of main do); after the feature commit F1's tab bar has the new Gameplay tab |
| T1 | `det.sh` | 4004f056 / 396cb219, twice |
| T3 | `t3.sh`: the research card's lobby (`t3_lobby.txt`, walking before and after), A with a run (Agumon, `bot=circle`, `at=4000`: it starts at story frame 4113 once the lobby has loaded; 60 s or until defeated) and B without, `DW4_TRACE_FINGERPRINT=1 DW4_TRACE_NET=1` | all 12,200 story lines (fingerprint with generator, net fingerprint, canonical snapshot) equal; port events and achievements equal; also with `DW4_GOD=1 DW4_OVERFLOW_TEST_CHEATS=1` (a full 3,600-step run), and with the run started during the lobby's load (an earlier build) |
| T4 | `t4.sh`: `DW4_STATE_TEST` at 3990 (before the run: pass 2 replays without it), 4600 and 5200 (after it: the input log spans the run), through `DW4_STATE_FILE` | identical after loading, all three |
| T5 | `t3.sh`: the card folder hashed before and after, the `DW4_PROFILE` file of A and B | unchanged / equal; no throwaway card folder left |
| hidden run | `--overflow-run ... shots=` with `--screenshot-frames` around it | the story fades out, the run loads stagBS02 and plays, the story fades back in and goes on |
| run pacing | the same request headless and with `DW4_DEV_PRESENT=144 DW4_DEV_SMOOTH=1` (the interactive pacing at a virtual 144 Hz with smooth motion) | the same run hash: the run loop's presents do not change the run |
| ctest | `build/ctest.cmd` | 45 / 45 (main's 44 + `overflow_host`) |
| st_all | `build/st/st_all.sh` | 17 / 17 identical after loading |
| r25c (T2 without the NPC, which is O1b's) | `work/campaign/segments.sh r25c 4` | 10 / 10 segments reached, 1,748,011 frames, every save "same story" |

## 6. Open for lanes B and C

- (Done by the integration, section 7: the stand-in gives the run's engine
  lane B's rules, so the Violet Ring starts natively without Belphemon.)
- `RunStatus::seconds` of the stand-in counts every run step; the director's
  run clock stops while the run is frozen.
- The run's 2D overlay is ImGui in O1a; `decorate` is there for the game-font
  boxes (SimUi, O1b).
- O3's field changes: `note_stage` already starts the run's pictures over on
  every stage load of the run's engine; the kept picture of the field being
  left stays until the next field draws, as in the story.

## 7. Integration with lane B (branch dref-o1a)

Main 19c2eae + `feat-o1a-hooks` (lane B) + `feat-o1a-host` (lane A), merge
order B, A (SURVIVAL.md 15.1). No textual conflicts: main had moved on by
docs only, and `CMakeLists.txt` merged by itself (lane B's engine sources,
probe and tests beside lane A's game sources and test). The interfaces made
to fit:

| Seam | As integrated |
| --- | --- |
| rules into the run's engine | the stand-in driver's `attach` calls `Engine::set_overflow_rules` with `OverflowRules(RuleSet::run)` (lane B's initial values, no director) before the host's `request_stage(field)`: the Violet Ring starts natively without Belphemon, `hit_p_<form>.lua` is the run's player hit rule, H3 keeps every other stage change and both resets out |
| the run's end | the stand-in reads `OverflowRules::ended()`: a stage exit ends the run at once (`retired`), a defeat or the game over three seconds later (`defeated`, the knock-down plays first); the host's own checks (the field left, 0 HP, a reboot) stay as a safety net |
| the director's lifetime | `run_from_story` destroys the run's engine before the driver, so `OverflowRules::director` (not owned) stays valid for every hook call |
| tracing | `DW4_TRACE_OVERFLOW=1` prints the rule set at the start and the rules' tallies at the end (steps, steps without control, hits taken, misses, clamps, highest hit share, end reason) |
| statics | lane B adds no mutable static (its new statics are constant name tables), so the audit of section 4 stands |

Without a director nothing spawns on the Violet Ring and its music is off
(the native start's BGMSTOP; the phase music is the director's): the dev
start shows an empty arena to walk in until lane C.

Results with the integrated build (commit b1ef13d, Release, all hidden):

| Test | Result |
| --- | --- |
| build | rel with tests, no new warnings (main's C4996 and one C4457) |
| ctest | 47 / 47 (main's 44 + `overflow_host` 68 checks, `overflow_rules` 1,064, `overflow_scripts` 45) |
| T1 det.sh | 4004f056 / 396cb219, two runs of two |
| T2 r25c | 10 / 10 segments, 1,748,011 frames, every segment's frames as lane B's run, every save "same story" (without the NPC, which is O1b's) |
| T3 | `t3.sh` with a run on the Violet Ring (Agumon, `bot=circle`, native start, 3,600 steps) and with one on staggf01 (Veemon, its own first.lua and enemies, the run's hit rule: one hit of 28.1 % of MaxHP, 222 steps without control in its intro): 12,200 story lines equal, 105 port events and achievements equal |
| T4 | `t4.sh`: 3990, 4600, 5200 identical after loading |
| T5 | the card folder unchanged, A and B equal; the achievement profiles equal apart from the unlock times (wall-clock seconds); no throwaway card folder left |
| T10 | `overflow_scripts`: (a) dng101b1 3,600 of 3,600 and staggf01 3,000 of 3,000 steps equal; (b) the Violet Ring's native start |
| T11 | `shots.sh`: every scene (title / new game, dng101b1 4:3 and 1600x900, the lobby with name tags, the new game's lobby) identical to main; F1 Cheats identical to lane A's build (the Gameplay tab); F1 Settings differs only in the frame-time graph, as two runs of main do |
| run hash | staggf01, `seed=7`, 2,400 steps: the same hash twice headless and with `DW4_DEV_PRESENT=144 DW4_DEV_SMOOTH=1` drawn |
| hidden run with pictures | the Violet Ring with the HUD after the native start, Agumon walking its circle, no boss |
| st_all | 17 / 17 identical after loading |
