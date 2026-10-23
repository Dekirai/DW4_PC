# Porting strategy

## Method

1. **Follow the data.** Stage packages list the `be*Info` objects a stage
   needs; `first.lua`, `start.lua` and the other embedded scripts show which
   manager commands the game sends in which order. A missing manager shows up
   in the F1 notes as "runtime for ..." or "info type ...". A message no row
   takes is dropped like the original's messenger does (80305A28), so it is
   no note: `DW4_TRACE_NO_RECIPIENT=1` lists them once per stage, and
   `DW4_TRACE_INFOS=1` the infos each stage registers, to tell a stage
   without that info (the original drops the message too) from a manager the
   port lacks.
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

The story path from the boot logos through character select, the lobby, the
areas, dungeons and all six boss stages to the staff roll runs in the native
engine; automated runs (`--smoke-test` with `--input`, `DW4_GOD`,
`DW4_ONE_HIT`) defeat every boss and pass the boss gates. Remaining work:

1. Behaviour checks of the later dungeons against the original (security and
   heal gates, quests), ideally with recorded traces from the original.
2. The PC requirements in [ROADMAP.md](ROADMAP.md): save states, supported
   languages, original/modernized options, content modding, online co-op and
   the release packaging (compressed disc images).
