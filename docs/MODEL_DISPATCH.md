# Live MODELCTRL dispatch and player generation

`ActionEventRuntime::register_model_scene_ram` represents one original
registration occurrence after explicit manager/context preparation. It resolves
the exact loaded info and RAM through `ActorSceneResource::models()`, clears
the four scheduler words in original order, writes the actual manager backlink,
parent and number, then appends MODELCTRL, enabled HIT categories and PLAYER.
Duplicate physical RAM occurrences remain separate ordered registration rows;
their name and index snapshots do not change when live fields change later.
The API does not implement the generic manager/context attachment prelude or
choose a scheduler order. Live manager resolution checks the real backlink.

The same runtime registry owns both these live registrations and the explicit
prepared sequence adapters. A live RAM may have null ONE.work before GENERATE.
`model_work()` freshly resolves that RAM's current ONE. Existing prepared
sequence APIs reject a live registration until its actual execution bundle is
implemented; they never silently substitute a detached prepared work object.

## Retained original capture scopes

The USA base controller at `8030D910` captures RAM, its parent, first named ONE
and ONE.work before invoking player virtual `+C4`. `ModelBaseInvocation` keeps
these captures. The status handler independently ensures the live RAM data list
and resolves ONE/work before classifying even an unknown command. Its canonical
status facade consumes only the captured status field group, with no invented
script-name, RAM-index or matrix reads. Only returned low byte exactly 1 ends
the base branch. A paused status query retains its original work allocation.

GETINFONAME reads the captured parent's live name. GETHP and GETRECNO use live
RAM activity but the pre-status work pointer. Sound/effect appends use that same
captured work's current subcommand wrapper. Captured null work and unavailable
prepared service are separate cases. Checked faults preserve committed effects;
recursive calls poison the invocation even when a callback catches the error.

## GENERATE continuation

The branch at `8030D9CC` activates the actual RAM, clears its scheduler words,
and retains the nullable sequence override before invoking player `+98` through
`ModelPlayerCreate`. It does not replay that prefix when original default tables
or a subsequent native service become available. The player callback performs
the factory, placement, GETPLAYER and startup phases described in
[PLAYER_CONSTRUCTION.md](PLAYER_CONSTRUCTION.md).

On return, the outer branch reloads work from its **original captured ONE**.
This can differ from both the pre-status work and the player's new work. It
performs the conditional max-HP/HP 100 fallback, saves the first child's actual
box reference, clears the root's box, synchronizes heading, and allocates the
recovered default AI owner only when the first-name AIM/AIH lookup finds a row.
First-child access uses physical backing slot zero independently of logical
count. Nullable boxes remain null; no bounds or AI program are invented.

Direct virtual `+8C` creates another base/status invocation with the saved
override, or SQ_WAIT only for a null override. It uses the current messenger
packet without selecting or pushing a replacement. The ordinary sequence
implementation and player `+B8` callback are still required, so normal GENERATE
currently remains suspended at that explicit boundary. Returning from `+98`
does not report full generation complete. The final node-box publication is
implemented behind the direct-entry completion gate and has not yet been
validated through a complete ordinary GENERATE invocation.
The synthetic direct GETHP override does complete and tests that final tail,
including shared source-box retention and separate transformed destination.
Native direct-entry nesting is bounded to 64 levels; this guard does not model
guest stack exhaustion or roll back an already executed generation prefix.

Evidence comes from the supplied USA DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`: base captures `8030D938..8030D980`,
status lookup `803AEBD8..803AEC94`, outer generation `8030D9D8..8030DBFC`,
registration `802F251C`, `803083CC`, and player hook `8034C39C`. Original
disassembly stays in ignored local work files. Synthetic tests exercise owner
replacement, paused status/creation, live routing fields, committed prefixes and
the explicit sequence boundary. This is native recovered behavior, not guest
allocator execution or proof of a complete original game frame.
