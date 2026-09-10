# Original selected-player initialization

Version 0.31 implements the original `GAMESTART` command on the prepared
`GameRamRuntime`. It connects species selection, saved base arrays, initial
weapon ownership, status calculation, player-count publication and both name
routes. Local original-data integration completes this command for all four
Agumon player rows using the twelve database infos from the supplied resource.
This is selected-player data initialization; the original MODELCTRL constructor,
weapon actors, startup menus, frame scheduler and playable combat remain required.

## Packet routing and retained calls

The dispatcher reads signed integer zero and string slot zero before checking
cardinality. Exactly one object selects the opaque-object name route, including
a null object and regardless of extra strings. Otherwise exactly two strings
select the conversion route. Other counts return success without accessing a
player row. Missing mandatory inputs are checked errors even on that return path.

Selected work retains its parsed inputs and actual owners across nested message
delivery. It uses the shared messenger and the same dispatch on retry. A completed
phase is never replayed. Exceptions fault the retained operation; wrong-owner or
recursive entry is rejected. Incomplete native service results preserve the
original synchronous call and do not claim an original asynchronous scheduler.

`GameDatabaseCache` retains the manager's lazy shinka and pcTech owners.
`GameDatabaseQuery` sends `DATABASE/name/0/GETDATABASE` and reads current object
zero after delivery completes. It retains that actual database object. Missing,
null or incorrectly typed results are checked errors. Item operations deliberately
use a fresh query for every call; no private argument buffer is introduced.

## Original publication order

1. Capture the current common list and selected player row. Search sixteen
   shinka `MODEL_NAME` rows in order and use the first match. A null input cannot
   match a returned name. A missing name selects species zero only after all
   sixteen required reads. An empty input matching a null database cell remains
   an explicit unverified string-identity boundary; actual authored model names
   in the selected USA resource are nonempty.
2. Enter `GameBaseToCurrent` on the then-current player row. Resolve LEVEL and
   publish DIGIMONNO before a possible shinka query. After it completes, publish
   DIGIMONNAME, BASE_LEVEL and BASE_EXP, then copy the species' nine BASE_SKILL
   and eleven BASE_UPCNT values. Source accesses precede the original destination
   setter's out-of-range skip. Invalid source access is a checked error. RAM zero
   assigns the BASE_FLAG, BASE_FLAGFOLDER and BASE_COUNTER inner arrays to the
   common wrappers, preserving shared array identity.
3. Enter device-folder initialization, then power-socket initialization as
   separate calls with their own current-row captures. Three distinct initial
   weapons become shared folder/socket entries. Socket append precedes each
   MODEL query; its name append precedes the equipped-byte write. See
   [GAME_ITEMS.md](GAME_ITEMS.md).
4. Run the complete represented status routine with modifiers enabled: health,
   five stats, four resistances, six technique groups, NEXTEXP and the equipment
   tail. Each database interruption retains prior publications. Unsupported
   dynamic item types still require their own original providers. See
   [GAME_STATS.md](GAME_STATS.md).
5. Set ISUSE and the selected BASE_ISUSE byte on the first captured player row.
   Count nonzero ISUSE bytes across all four current rows and publish NINZU on
   the first captured common list. A nested reset can make the retained row
   different from the live row used by that count.
6. Capture the current player row again for PLAYERNAME. The object route clears
   that row's existing inner list and appends the same captured object, preserving
   its type and identity. It does not deserialize a save or cast the payload to
   a particular name type. The string route resolves the context's prepared
   font owner, converts the retained bytes, makes a distinct nine-unit Unicode
   object and only then replaces the list contents. Missing font service leaves
   all completed common effects committed and the old name list intact.

`set_name_services` supplies the actual context font resolver. A mapping table
alone is insufficient to establish the original font and its mutable scratch
owner. The name row is captured before calling the resolver, so a reset inside
that callback does not retarget name publication. Conversion uses original
16-bit units and copies at most eight before a zero terminator. See
[GAME_NAME.md](GAME_NAME.md).

## Reference and verification

Reference: supplied USA GameCube `sys/main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.

| Routine | Original address |
| --- | --- |
| GAMESTART dispatcher branch | `80371A38` |
| Species lookup | `8036DA28` |
| BASE2CURRENT | `8036DBA8` |
| Common GAMESTART body | `8036EB0C` |
| String-name route | `8036EC3C` |
| Opaque-object route | `8036EFF4` |

The asset-free `game_start` suite covers packet precedence, mandatory reads,
sixteen-row lookup, shared base arrays, nested resets, append-before-query
suspension, common publication before missing font, pre-font name-row capture,
opaque payload identity and invalid database results. The optional extraction
argument additionally runs all four original Agumon starts through real DATABASE
registration. Each receives weapon records 266/275/274, actual status values,
the initial Waza record zero and a converted name. Existing P2ESKILLUP then
reaches the explicit remaining probability/growth service using the populated
Waza. It does not fabricate a skill increment or damage result.

Original instruction probes and their explicitly prepared helper boundaries
are summarized in [VALIDATION.md](VALIDATION.md). Original tables, instructions,
probes and screenshots remain outside tracked source.
