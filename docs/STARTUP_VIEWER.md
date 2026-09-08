# Native boot-script execution

Version 0.11 executes the original `func.lua` and `boot.lua` chunks through a
bounded native Lua 4 interpreter, then loads the requested common files. The
first chunk installs helper functions; the second builds the common-resource
queue using explicit inspection state.
This is actual bytecode execution with native host bindings, without executing
PowerPC code or substituting a hand-written copy of the original script.

The resulting queue is resolved against the original catalog, loaded into owned
IGB documents and routed into the original info lists. The live Lua state is
retained. Gameplay controller initialization remains pending. `start.lua`, `first.lua`,
player generation, movement, combat and the original game camera do not run.

## Native application

Open **Startup**, enter a logical package descriptor and select **Run startup
loading**. Expand **Inspection inputs** to configure difficulty, camera mode,
joined player slots, model names and up to three weapon names per player.
An empty weapon field represents a nil query result. Changing any visible input
discards the previous report; Run executes a fresh VM using the current inputs.

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'PATH\Extracted' `
  --boot-package dng101b1.igb --boot-player-0 p_agumon `
  --boot-weapon-0-0 w_sword01 --boot-weapon-0-1 w_gun01
```

The **Resources** table preserves the original ordered request names. Hover a request to see
the source member, prototype and instruction that appended it. Hover a script
row for its info reference and script-list index. A completed boot report always
shows the next native barrier, so execution of two scripts cannot be confused
with completion of the game startup sequence. **Loaded files** lists retained
documents and their byte/export counts; **Info routing** exposes the final
deferred order, immediate Lua route, held scenes and late-scene tail. These
are loader lists, not proof that manager callbacks initialized the objects.

The scene inspector remains independent. Its explicit map activation and
animated visual placement are still available and are not silently coupled to
boot inspection. No player model is selected from the generated queue without
the intervening native manager initialization work.

## Command-line tool

`dw4_boot` runs the same portable implementation without a graphics context:

```powershell
.\dist\DW4_PC\dw4_boot.exe 'PATH\Extracted' --package dng101b1.igb
.\dist\DW4_PC\dw4_boot.exe 'PATH\Extracted' --package dng101b1.igb `
  --boot-difficulty 2 --boot-camera 1 --boot-player-0 p_agumon
```

The options `--boot-player-N NAME` and `--boot-weapon-N-S NAME` use a single
player digit N=0–3 and a weapon slot S=0–2. Names are bounded ASCII identifiers,
not host paths. Difficulty accepts 0–2; camera accepts 0–1. Defaults are explicit
inspection values: difficulty 0, camera 0, no joined players and nil weapons.
They do not import or infer a save, inventory or new-game state. Original USA
country, area and pack-only getters return constant 1 and cannot be overridden
through these state options.

The CLI returns 0 when both scheduled chunks complete, all initial/requested
files decode and info routing completes, 3 for failed execution/loading/routing, and 1
for malformed arguments or failure to mount the data. In the application,
`--smoke-test 5 --screenshot work/boot.bmp` runs finite hidden UI frames and
requires a successful report. `--max-entry-mib` belongs to the asset viewers and
cannot be combined with boot execution; boot uses the fixed limits below.

## Verified execution semantics

Initial package members are read in preliminary `/s` order before ordinary
members, preserving relative order within each stage. Their exported Lua infos
become the explicit lookup order. For a scheduled filename, the first match
within each info list is selected, then matching continues with later infos.
Malformed first matches never fall through to a later valid duplicate. External
filesystem fallback is unsupported. One VM retains globals and closures across
the func phase and boot phase; a failed call poisons that VM and stops the run.

The native bindings implement:

- `CFLclear`: clear the queued names.
- `CFLsetName`: append unless an exact case-sensitive name already exists;
  preserve the first request and its provenance.
- `CFLgetFolderName`: return the active descriptor's inherited name.
- Joined-player, model, weapon, difficulty and camera queries: read explicit
  inspection inputs with checked argument counts and indices.
- USA country/area/pack-only queries: retain the original literal return value 1.
- The boot script's `gsub(name, "p_", "q_")` profile: perform bounded literal
  substitution and return the replacement count. General Lua patterns and
  replacement callbacks remain unsupported.

The resolver lowercases only ASCII A–Z in the requested name, then compares
against exact catalog keys. Queue deduplication occurs before that folding, so
two differently cased requests remain separate even if they resolve to the same
entry. Resolution uses a stable catalog index and clears old locations/success
before validation. It does not change the general catalog reader's exact lookup.

For `dng101b1`, the tested profiles match an independent official Lua 4.0.1
interpreter for every ordered host-call name/argument and final queue entry:

| Explicit state | Boot instructions | Host calls | Unique requests |
| --- | ---: | ---: | ---: |
| No joined players, difficulty 0, camera 0 | 505 | 105 | 94 |
| Slot 0 Agumon, sword01 and gun01, same difficulty/camera | 310 | 34 | 7 |
| Three joined players with shared weapon requests | 432 | 49 | 9 |

Each profile first executes 35 func instructions and makes no func host calls.
The three-player branch follows the original script's `p_` to `q_` model-name
substitution. It does not constitute three-player gameplay.

## Bounds and next phase

Boot reads at most 32 initial package members. Initial and common files together
are capped at 128 documents, 64 MiB per file and 256 MiB combined. It executes
at most 64 chunks with 32,768 instructions per chunk, plus
the Lua parser/VM budgets in [LUA4.md](LUA4.md). Host callbacks are capped at
32,768 per run. Queue limits are 4,096 distinct requests, 4 MiB of request and
provenance text and 8 MiB of cumulative path scanning, including duplicates and
repeated clears. No file, OS or network Lua functions are installed.

The common-resource queue is now loaded and its exports are retained in the
original routing order. The next phase must implement the original loading-screen
state, the deferred managers' actual callbacks and required layer/text state,
then execute supported calls reached by `start.lua`.
Unknown operations must remain explicit failures. See
[STARTUP_HOST_RESEARCH.md](STARTUP_HOST_RESEARCH.md),
[STARTUP_RESEARCH.md](STARTUP_RESEARCH.md), [SCRIPTS.md](SCRIPTS.md) and
[STARTUP_LOADING.md](STARTUP_LOADING.md) and [VALIDATION.md](VALIDATION.md).
