# Porting strategy

## Method

1. **Follow the data.** Stage packages list the `be*Info` objects a stage
   needs; `first.lua`, `start.lua` and the other embedded scripts show which
   manager commands the game sends in which order. A missing manager shows up
   in the F1 notes as "runtime for ..." or "no recipient: CATEGORY COMMAND".
2. **Recover the manager from `main.dol`.** Locate its vtable, command table
   and update routine in IDA and rebuild the state machine one to one: the same
   states, sub-steps, waits, messages and data fields. Keep original names
   (categories, commands, sequence names) and cite addresses in comments.
3. **Keep the original contract.** Managers communicate only through the
   messenger; scripts run unmodified; timing uses whole 60 Hz frames.
4. **Verify in the running game.** Hidden smoke tests with scripted input and
   screenshots compare the result against the original screens; regressions of
   earlier flows (title, character select, lobby intro) are rechecked.

Host approximations are allowed only where the original behaviour is not yet
recovered, and they are labelled as provisional (currently the follow camera
of camera infos without box data and the enemy chase fallback for enemies
without AI maps, neither of which the shipped stages use).

## Research tooling

Local, ignored tooling lives in `work/` (never committed): stage and object
dumpers, a Lua 4 disassembler/decompiler for the embedded scripts, DOL table
readers and probes for collision geometry. The notes in `docs/research`
record recovered behaviour with addresses and remain the reference for
reconstruction work.

## Next steps

1. Commander Room route and Ophanimon's mission, then the lobby gate to the
   first area (`stag0001`).
2. Dungeons: enemies (`ENEMY` models), the player's attack sequences and
   hitboxes, damage and HP (`FIGHTCALC`), items and the HUD numbers.
3. Later dungeons and boss fights compared against the original.
4. Name entry (NAMESEL), difficulty selection, memory card saves.
5. Demo cameras, audio and movies.
6. The PC requirements in [ROADMAP.md](ROADMAP.md): settings menu, bindings,
   presentation options, modding and co-op.
