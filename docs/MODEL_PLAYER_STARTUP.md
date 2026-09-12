# Original prepared player-start continuation

`ModelPlayerStartup` extends `ModelPlayerStart` through the return of the original
post-GETPLAYER caller, `8034D990..8034DF3C`. It owns the captured model work and
returned player row, the existing data/weapon prefix, five UI sends and the
map-dependent status tail. This is a prepared entry after a real GETPLAYER
result; it does not implement the earlier constructor, register an entire player
automatically, render a HUD or run the original game frame. The base GENERATE
handler also has required effects after this player virtual callback returns;
those are not supplied by completing this continuation.

The source is the owner's USA GameCube `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original bytes, instruction probes and
captures remain external or ignored. The implementation and fixtures are English.

## Ordered messages and numeric behavior

After the existing RECALCPLAYER returns, each selection clears the real packet
and reads the then-current RAM field. The caller finds the named field on its
captured row at that point, even if GAMERAM has since replaced its current row.

| Original interval | Recipient and command | Argument |
| --- | --- | --- |
| `8034DCF8..8034DD30` | NUMBERCTRL/lvnm/SETVALUE | Current LEVEL integer |
| `8034DD34..8034DDB4` | METERCTRL/hpmt/SETPERCENT | `100.0f * float(HP) / float(MHP)`, separately rounded operations |
| `8034DDB8..8034DDF0` | NUMBERCTRL/hpnm/SETVALUE | Fresh HP integer after the meter callback |
| `8034DDF4..8034DE5C` | METERCTRL/mpmt/SETPERCENT | Wrapped signed `MP * 100`, signed integer division by MMP, then float conversion |
| `8034DE60..8034DE98` | NUMBERCTRL/mpnm/SETVALUE | Fresh MP integer after the meter callback |

HP reads MHP before HP; MP reads MP before MMP. HP 7/13 retains a fraction, while
MP 7/13 sends 53.0. The caller adds no percentage clamp. Exceptional signed
division and nonfinite messenger float appends are explicit checked boundaries;
the port does not fabricate a denominator or claim original invalid-input trap
semantics. Real receivers apply their own original rules afterward.

`NumberControlRuntime` changes the actual loaded digit mappings and shared UV
storage before publishing the new number work. `MeterRuntime` changes its actual
ONE value and later node sample-time storage; SETMODELHP can additionally bind
the exact shared wrappers from MODELCTRL GETHP. These are reusable runtime
owners, not a separately calculated display report. Scene submission for the
HUD and the original update scheduler are still required.

## Map and status ownership

At `8034DEAC`, the continuation reads the actual prepared manager/context map
pointer through `ModelPlayerMapReader` and retains that string allocation.
Replacing the live field during SETMETAL does not retarget the captured name.
The reader is required only at this phase; its absence suspends without replaying
the completed sends. A null pointer is a checked failure.

`read_model_player_map_rules` reads five metal-map pointers at `804E7C80` and four
main-lobby pointers at `804E7C94`. It validates initialized DOL data ranges and
bounded terminated strings. The exact byte names remain in the owner's files;
synthetic callers supply explicit ordered arrays.

A metal-name match sends PLAYER/null-name/live-RAM/SETMETAL without arguments.
The real virtual+C4 handler initializes its status lists when required, writes
slot seven and binds the current GAMERAM DOWNDATA payload through its own
retained nested GETPLAYER. This later queried row is distinct from the startup
caller's earlier captured row. After that send returns, the same captured map
is compared with the lobby list. A match writes captured ONE+125 to byte one;
a nonmatch preserves its previous value. Metadata identifies that field as
`_isMainLobby`. Its earlier allocator default is not yet established, so prepared
work represents an unknown initial byte until explicitly supplied or written.

## Suspension and verification scope

Each actual messenger dispatch stays alive through missing recipient services.
Completed recipient prefixes and callback packet mutations survive; retries do
not repeat selection or recalculate arguments. Wrong-owner and recursive-entry
preflight checks do not corrupt the valid continuation. An uncertain execution
fault preserves its committed prefix and permanently rejects replay. Completion
is idempotent and means this represented caller returned.

Synthetic tests cover ordered paused recipients, live RAM changes, old-row
retention, scalar changes between callbacks, captured map/ONE replacement,
nullable map failures, case-sensitive map matches, numeric failure prefixes and
malformed DOL pointer/string ranges. Independent comparisons execute the original
caller instructions with explicitly represented recipient services, including
separate pause/resume checks. They do not prove the recipient bodies by themselves;
NUMBERCTRL, METERCTRL and status have their own comparisons.

The local original-data integration prepares four distinct Agumon MODELCTRL and
ACTION owners, original databases and HUD member 179. Actual native GAMESTART,
NUMBERCTRL, METERCTRL, MODELCTRL GETHP and PLAYER SETMETAL run in one messenger.
It verifies shared HP/meter owners, number UV mutations, two metal branches and
one main-lobby branch, followed by four deferred weapon generations and eight
replacements. The font, map context, entry state and outer update calls remain
explicitly prepared. See [VALIDATION.md](VALIDATION.md) for current counts and
[ORIGINAL_BASELINE.md](ORIGINAL_BASELINE.md) for the full remaining goal.
