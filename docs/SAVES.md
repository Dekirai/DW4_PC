# Saves (memory card) reconstruction

Status: saving in the save room and calling a saved character at the
character select run (`src/engine/save_menu.cpp`, `src/engine/load_intf.cpp`),
with the name entry (NAMESEL) and the difficulty dialog (DIFFSELECT). Addresses are GDJEB2 (USA).

## Flow

- Loading happens at the character select (LOADCTRL2 / LOADINTF2 panels, one
  per controller). Each player loads one character record from a memory card.
- Saving happens in the save room: the lobby NPC script `saveshop.lua`
  (stagML12) changes to `stagIS12`, a UI-only stage whose SAVECTRL /
  SAVEINTF_ALL managers write each joined player's record, then returns to
  stagML12 (PLAYERPOINT 2) or to the title (stagis10) when nobody saved.
- `stagIS12/start.lua` first copies story flags into the flag folder for the
  chosen difficulty (FLGset 518.., 530.., 542..), plays `RM_NSHOP`.
- A character called from the title starts in stagML11 (PLAYERPOINT 0) after
  `stagIS10/gameStart.lua`: COUNTER 2 = 0, the temporary flags cleared (23-34,
  36-66, 71-73, 84-85, 100-319, 328, 336-391, 409, 415-425, 457-468,
  475-500, 514-517, 562-566, 571-575, 593-594, 598-610: switches, levers,
  dungeon and lobby event flags), the key items set again from flags 518..
  (difficulty 0; 530.. / 542..) and the story flags from their copies 700..
  (phases 1-6, 19-20, bosses 67-70, 83, 86, 89-99, 320-327, 329-333, 335, 49,
  383, 82, 334). A game loaded after the stag0301 lever (flag 56) finds it
  pulled back; the key items it opened the way to stay.
- The story-run driver saves this way at its checkpoints and segment runs
  call such saves at the title (`src/game/campaign.cpp` "Checkpoints").

## Managers

| Name | Class | Update | Commands | Notes |
| --- | --- | --- | --- | --- |
| SAVEUTILITY | beSaveUtil | - | 80323D20 | memory card API front end; one request at a time |
| SAVECTRL | beNDMWSaveCtrl | 803788A8 | 80379184 | mirrors LOADCTRL2 (80368050 / 803689A8) |
| SAVEINTF_ALL | beNDMWSaveIntf | 80379790 | 8037D1DC | per-player panel state machine (~2300 lines) |

SAVECTRL states: 1 start the utility (McUtilCtrl 80367900 / 80367980:
SAVEUTILITY GENERATE I<4>, ISRDY); 2 open `j_chrselback`, SAVEINTF_ALL
GENERATE, wait until COMMONINTF +48 reports the panels done, close the panels,
fade, SETSELECT_DATA, NINZU = joined players (BKCHIPSTAGE cleared for the
others); 3 end the utility (80367A18 / 80367A60) and NEXTSTAGE.

SAVEUTILITY commands (table 804CF848, results in the beSvDeliver object):
GENERATE (I<connect max>), REMOVE, REQUEST_FILEMAKE / FILEFIND / FORMAT /
USE_CHECK / READ_MEDIA / WRITE_MEDIA / GETFREESIZE (I<slot>[, object]),
GETDELIVER, ISBUSY (deliver +44 == 0), GETRESULT (+24), ISRDY, CHECKMEM
(+32, +36, +42, +43), GETOPERATE (+24). API classes beSv*Api (vtables
804E0BEC..804E0F1C: +5C start, +60 update). Read result codes (80321670):
5 / 7 / 11 / 12 card missing or wrong device, 3 other error, 4 file missing,
8 both data copies failed their check.

SAVEINTF helpers shared with LOADINTF2: COMMDLCTRL (8035BE8C..8035C73C),
MESWIN (80363064..8036317C), SELSLOT (8035AE68 / 80358BC0 / 803599AC /
8035ADB0: slot window `j_slotbase`, `j_memoryiconwin`, `j_slotcursor`,
`j_slotmark01/02`), DATASELINTF (80353A30 / 80353AD8 / 80354168 / 80354880 /
80355870 / 80355AEC), DEGISTATECTRL (8035299C / 803529EC / 80353394).
Message ids are `(player << 16) | 0x5xx` (e.g. 0x502, 0x50B, 0x50D, 0x50E,
0x510, 0x51C, 0x51D, 0x51F, 0x527, 0x529, 0x530) and `0x1xxx..0x3xxx` for the
card states.

## Memory card states and access time

SAVEUTILITY's update (80323538) runs one beSv*Api object per request
(`src/card_timing.cpp`, `src/engine/save_util.cpp`). Every request mounts the
card (8031FC40 / 8031FCF0, one step per frame: CARDProbeEx, CARDMountAsync,
CARDGetResultCode until the mount is done, CARDCheck), works on the file and
unmounts (8031FF98). The asynchronous CARD calls end after the EXI transfers
Dolphin times (EXI_DeviceMemoryCard.cpp): reads 926 cycles a byte in 512-byte
segments, page programs 4937 cycles a byte for 128 bytes plus 5000 cycles,
erases 5000 cycles; a sector write (erase + 64 pages) is 5.03 fields, the mount
(five 8 KiB system blocks) 4.68 fields. Requests end in these updates
(Dolphin captures match frame for frame: boot check window, Call Digimon,
save room, full and missing cards):

| request | updates | |
| --- | --- | --- |
| USE_CHECK | 9 | mount |
| FILEFIND | 11 / 12 | mount, CARDOpen; a missing file ends mounted |
| GETFREESIZE | 12 | mount, CARDFreeBlocks |
| READ_MEDIA | 29 | mount, CARDOpen, CARDGetStatus, both 40 KiB copies |
| WRITE_MEDIA | 92 | mount, both copies (6 sectors each), header (2 sectors), CARDSetStatus |
| FILEMAKE | 103 | mount, CARDCreate (2 sectors), copies, header, CARDSetStatus |
| FORMAT | 37 | mount, CARDFormat (5 sectors) |
| no card | 4 / 5 | three CARDProbeEx failures: 12 |

WRITE_MEDIA and FILEMAKE end inside the menus' 3 s minimum (SaveDataTime /
MakeDataTime). Their synchronous CARDSetStatus blocks the original's game loop
for about 5 fields (Dolphin repeats the picture for 6 fields); the windows run
on the world clock, so the following events keep their time and the port does
not stall. beSvStartApi / beSvEndApi take two updates: ISRDY turns true in the
second update after GENERATE and false in the third after REMOVE.

The card in a slot (Engine::card_setting, `DW4_CARD_A` / `DW4_CARD_B` or
settings.ini `[data] card_a` / `card_b`): `none` (CARDProbeEx: no card, 12),
`unformatted` (the mount and CARDCheck report it broken, 7, until a FORMAT),
`inserted`, and the card's blocks (default 2043, Dolphin's GCI folder card).
Free blocks are the card's blocks minus the GCI header block counts (+0x38)
of the card's files (a GCI folder: every `*.gci` in the slot's folder; a
single save file: that file), free files 127 minus their number, so a folder
holding a 2043-block GCI is a full card (GETFREESIZE 9). In automatic mode
slot A always holds a card and slot B when its folder (or save file) exists.

The logo (803B18BC) checks slot A, then B. A broken card asks to format it
(sub 50: texts 92204/92205 + 92030, question 92031 with the cursor on no,
FORMAT, 92202 / 92203). When no slot can hold the save, state 1 reports the
worst problem of slot A, else B (flags 8 no room: 92010 / 92011, 0x40 no
file entry: 92211, 0x20 wrong device: 92209, 4 error: 92020 / 92021, 0x10
broken: 92206 / 92207, none: 92000 "Memory Card not found." / 92001) and asks
"Yes, continue without saving. / No, use another Memory Card."; yes shows
the logos (after 92210 "To manage the contents of your Memory Card, use the
Memory Card Screen." when the card was full), no checks again (92100).

## Where the save lives

The card requests only ever touch a slot's DW4 file (the 64-byte directory
entry and the 90112-byte file, "GCI bytes") and the space the card's files
take. `save::CardStore` (`include/dw4/card_store.hpp`, `src/card_store.cpp`)
keeps them in one of two ways; `Engine::card_store` says which, and
SAVEUTILITY's reads, writes and space checks go through it
(`src/engine/save_util.cpp`):

| Store | Slot A / B | Used for |
| --- | --- | --- |
| single file | `saves/savedata.dat` / `saves/savedata_b.dat` (settings.ini `[data] save` / `save_b`, relative to the program) | the players' default |
| GCI folder | `<root>/Card A` / `<root>/Card B`, the save as `B2-GDJE-NDMWSaveData.gci`, other `*.gci` count as the card's other files | `--card-dir`, `DW4_CARD_DIR`, test runs (`<program>/saves/GC/USA`), `[data] cards` (e.g. Dolphin's `GC/USA`, live), the online session copy |

`src/game/save_store.cpp` picks the store of a run in this order:
`--card-dir`, `DW4_CARD_DIR`, `--save-file FILE` (single file, slot B
`<name>_b<ext>`), a test run (`--smoke-test`) without them (GCI folders at
`<program>/saves/GC/USA`, as before the single file existed, so the
campaign tools, `det.sh` and the Dolphin comparisons are unchanged),
`[data] cards`, else the single files of `[data] save` / `save_b`.

- A single save file holds the GCI bytes unchanged (the same bytes the
  folder store writes as `B2-GDJE-NDMWSaveData.gci`; a headless save room
  run writes byte-identical files in both stores). Its slot holds a card in
  automatic mode once the file exists; `[data] card_b = inserted` puts a card
  in slot B before that.
- Writes: `<file>.tmp` first, then a rename over the file. The single file
  store first copies the old file to `<file>.bak` (every write); the GCI
  folder store keeps no backup (the original folder behaviour).
- Migration: in the players' default, the first start copies
  `<program>/saves/GC/USA/Card A|B/B2-GDJE-NDMWSaveData.gci` (where older
  builds saved) to `savedata.dat` / `savedata_b.dat` when the target does not
  exist and the GCI holds a data copy that passes its check. The old file is
  never deleted. `[data] save_migrated = true` records that the copy was
  done, so a save file deleted later does not come back. A copy that failed
  (an I/O error: the folder cannot be made, the disk is full) leaves the flag
  unset, so the next start tries again; a damaged old file (neither data copy
  passes its check) is not copied and not tried again. Both are told to the
  player at that start (a notice at the top right) and in the F1 Saves tab,
  which keeps naming a damaged old save while its slot has no save of its
  own. With `--save-file`, `DW4_MIGRATE_FROM=<GCI root>` runs the same copy
  (tests).
- Online sessions share only the DW4 save of each owner's slot A
  (`session_card`): the session copy (a GCI folder in the temp directory)
  gets nothing else, and files of other names from a peer are not written.
  At the end the owner's save goes back through the owner's store (with the
  `.bak`), only when the session changed it. Before, every file of the card
  folder went to every peer (a `[data] cards` folder pointing at Dolphin's
  `GC/USA` shared all GameCube saves of the player).
- Save states, a console reboot and the end of an online session keep the
  store and the slot settings of the run.

### Other PC files beside the save

The PC version keeps its own files next to the save and never inside it (the
save stays the original GCI bytes):

| File | What it holds |
| --- | --- |
| `randomizer.ini`, `randomizer_<UNIQUE>_spoiler.txt` | the randomizer's bond per character ("Randomizer bond" below), beside the save |
| `saves/profile.dat` (next to the program) | the local achievements and their counters (text, written through `.tmp` with a `.bak`); offline unlocks stay on this PC |
| `saves/states/state<N>.dw4state` | save states (snapshot at the stage load plus the input since) |
| `saves/backups/` (beside the save) | the copy taken before a GC save import, and before a cloud backup download replaces the save |
| `saves/online/<account>/online_card.dat` (next to the program) | an online room's memory card A: a cache of the account's online character (the server is the truth), with `online_card.state` (the revision it was built from, play time, a save the server has not confirmed yet) and `backups/` (a cache replaced after a conflict) |
| `saves/online/<account>.token` | a remembered online login (DPAPI, this Windows user only) |

### Online character and cloud backup (server)

The online play ([DEVELOPMENT.md](DEVELOPMENT.md#online-server-dw4online)) keeps its save on the server
and apart from the local one: an account's **online character** is one
3316-byte character record (the layout below), stored with a revision and
decoded by the server's own port of the record codec (`server/DW4Online`,
golden vectors from `src/save_data.cpp`); it starts from zero and is never
read from or written to `savedata.dat`. The **cloud backup** is a private
copy of the local save, uploaded and downloaded by the player at any time:
the GCI bytes of `savedata.dat` (90176 bytes, or the bare 90112-byte file),
with at least one data copy that passes its check, the latest and five
earlier revisions per account; it is never shown on the website and never
used for achievements.

In the game client (docs/SEAMLESS.md 9): an online room plays from a virtual
memory card, `saves/online/<account>/online_card.dat`, built from the online
character when the room is entered (the record in box 0, card B absent; no
online character: an empty card and the original new game). It goes up to
the server when the save room writes it, at every stage change and when the
member leaves the room; a save made elsewhere meanwhile is never
overwritten (the game says so and loads the server's character). F1 > Saves >
Cloud backup uploads and downloads the local save outside rooms (a download
copies the current save to `backups/` first).

Round 64's design (docs/SEAMLESS.md 9.1) gives an online account the four
boxes of a card and keeps each online character in readable tables on the
server: the record below is composed from them when a room is entered and
decoded into them at every accepted upload, byte for byte.

## Importing GC saves

`dw4::save::load_import` / `parse_import` (`include/dw4/save_import.hpp`,
`src/save_import.cpp`) find the DW4 save (directory entry GDJE / B2 /
`NDMWSaveData`, at least 90112 bytes, a data copy that passes its check) in:

| Container | Recognised by | Layout |
| --- | --- | --- |
| `.gcs` (GameShark, PSO tools) | `GCSAVE` at 0 | 0x110-byte header, entry at 0x110 (its block count is unreliable, often 1), blocks from 0x150 |
| `.sav` (MaxDrive, Action Replay) | `DATELGC_SAVE` at 0 | 0x80-byte header, entry at 0x80 with the bytes of each 16-bit pair at +0x06 and +0x2C..+0x3F swapped, blocks from 0xC0 |
| memory card image (`.raw`, `.gcp`; Dolphin, Nintendont, dumps) | size a multiple of 8192 | block 0 header (+0x22 size in Mbit), directories in blocks 1 / 2 (127 entries, +0x1FFA update counter, +0x1FFC / +0x1FFE checksums over 0..0x1FFB), block maps in 3 / 4 (+0 / +2 checksums over 4..0x1FFF, +4 update counter, +0x0A next block of blocks 5.., 0xFFFF last) |
| `.gci` (Dolphin, GCMM, Swiss) | anything else from 64 + 8192 bytes | entry, then the blocks |
| GCI folder | a folder | `B2-GDJE-NDMWSaveData.gci` in it, `Card A`, `Card B`, then any `*.gci` with the DW4 entry |

The card checksum is the 16-bit sum of the big-endian words and the sum of
their complements (0xFFFF counts as 0). A card image uses the directory and
block map copies whose checksums hold, the higher update counter first (a
16-bit wrapping compare), then the others; the file follows the block map
from the entry's first block, and when no map gives a chain whose data
passes the save's check, the file's blocks in a row.

The preview (`summarize_save`) shows per box: player name, Digimon (form
name), level, bits, story phase (flags 2-6, 86 as
`work/campaign/tools/gcisum.py`) and the forms unlocked. The records hold no
play time or save date (see above); the preview shows the file's time from
its directory entry (`save_time_text`, the console clock, so local time).

`merge_import` builds the new save: the whole file (the source's 90112 bytes
unchanged) or chosen boxes, each copied as its whole 8 KB box buffer from the
source's checked data copy into the target box of the current save's checked
copy (or, without a current save or with both copies broken, the source's
8 KB header and empty boxes), then both copies written with the new
checksum. The directory entry is the one the save room writes (time now).
Before writing, `import_save` copies the current save to
`<save folder>/backups/<name>-YYYYMMDD-HHMMSS<ext>` (a GCI folder store:
`<root>/backups`). A character already called in the running session is not
changed; the game reads the card again at the next Call Digimon or save.

User side: F1 menu, Saves tab (file dialog, preview, a box choice and a
target box per source box, target slot A / B, "Import the chosen boxes" /
"Replace the whole save"; off during online sessions); command line
`--save-info FILE` (preview) and `--import-save FILE[:BOXES] [--import-slot
A|B]` with `BOXES` = `all` (default), `1,3` (same boxes) or `1>2,4>1`
(boxes counted 1-4 as the data select shows them), into the run's store
(`--card-dir`, `--save-file`, else the save files after the migration).
Tests: `tests/save_import_tests.cpp` (synthetic files of every container,
broken directories / block maps, wrapped counters, merges),
`tests/card_store_tests.cpp`, `tests/save_store_tests.cpp`.

PS2 saves (SLUS-20836 / SLES-53405 / SLPS-25453) are not imported yet: no
samples to check the record layout against.

## GameCube file

beSaveUtilInfo (`stagis12/separate/*_pack.igb`) holds beSvPlatDataGC: file
`NDMWSaveData`, 90112 bytes (11 blocks of 8192): an 8192-byte header with the
3680-byte banner, 1632-byte icon and the comment `DigimonWorld4` /
`SaveData`, then two redundant 40960-byte copies of the game data at 0x2000
and 0xC000 (a copy whose check fails is skipped).

The only time the save carries is the card directory entry's (GCI bytes
0x28-0x2B, `save::card_time`): CARDCreate, CARDWrite and CARDSetStatus store
OSGetTime() / (bus clock / 4) there (0x8024B480, 0x8024BBAC, 0x8024C43C; the
SetStatus of the save flow writes last), the seconds since 2000-01-01 on the
console clock, which the IPL keeps in local time. The port writes the PC's
local calendar time counted the same way (a console set to the PC's clock),
not UTC. The game never reads it back (CARDStat.time at the status buffer
+0x24 has no load), and no game code calls OSGetTime: the records hold no
save date or play time (UNIQUE comes from the world clock, 8035EEF0). Dolphin
writes its emulated clock there (a movie's RTC in movie runs).

## Character record (80364AEC, big-endian, 3316 bytes; 4 per copy)

A copy is the checksum word, the version word 4 and the four save boxes, each
an 8192-byte beNDMWDegiData buffer (80345CD8 allocates 0x2000 bytes; the read
80363A64 and the write 803674C0 step by the buffer size), so box n starts at
copy offset 8 + n * 0x2000 and only its first 3316 bytes are used.

| Offset | Field |
| --- | --- |
| 0 | ISUSE (byte) |
| 4 | UNIQUE |
| 8 | DIGIMONNAME (string) |
| 40 | 0xFFFF, then PLAYERNAME as UTF-16 (8 characters) |
| 72 / 76 / 80 / 84 / 88 | LEVEL, HP, MHP, MP, MMP |
| 92 / 96 | XDATA, BIT |
| 100 | DEVICEFOLDER: 36 packed devices, -1 empty |
| 244 | DISKFOLDER: 12 packed disks, -1 empty |
| 292 | CARDLIST bytes |
| 344 | BANKDEVICE: 96 packed devices, -1 empty |
| 728 | BANKBIT |
| 732 | POWERSOCKET: 3 DEVICEFOLDER indices (-1 none) |
| 744 | POWERPOCKET: 5 indices |
| 764 | MAINSLOT index |
| 768 | MAINPOCKET: 5 indices |
| 788 | SUBSLOT index |
| 792 | BASE_FLAG bytes (1024) |
| 1816 | BASE_FLAGFOLDER bytes (12) |
| 1828 | BASE_COUNTER (16 ints) |
| 1892 | BASE_ISUSE bytes (16) |
| 1908 | BASE_LEVEL (16 ints) |
| 1972 | BASE_EXP (16 ints) |
| 2036 | BASE_SKILL (16 x 9 ints) |
| 2612 | BASE_UPCNT (16 x 11 ints), ends at 3316 |

Verified against a real Dolphin save (GCI folder `B2-GDJE-NDMWSaveData.gci`,
64-byte directory entry + the 90112-byte file): each copy starts with the
checksum (sum of all following big-endian words) and the word 4 (the
SAVEUTILITY GENERATE argument, 80322538 -> 80535C1C; check at 8031FB98), and
the records follow back to back from copy offset 8 (unused ones stay zero).
PLAYERNAME is written in full-width forms (U+FF21.. = 'A'..), terminated by
0. A disk word is record | kind << 12 | count << 16.

80364894 then refreshes the slot summary (ISUSE, LEVEL, HP, MHP, MP, MMP,
digimon name, player name, UNIQUE) shown by the data select window.

BASE_FLAG, BASE_FLAGFOLDER and BASE_COUNTER belong to each player. New
player data (8036CE1C, GAMEDATAINIT) has BASE_FLAG[960] = 1 (a constant byte
at 80561411; nothing reads it), so every character the game creates carries
that byte at record offset 1752. BASE2CURRENT (8036DBA8) points the common
FLAG, FLAGFOLDER and COUNTER, which the scripts change, at player 1's arrays;
the other players keep their own, and each record is written from its own
player (80364AEC).

Packed device word (item class vtable +100 / +104): bits 0-9 record, bit 10
equipped, bits 12-15 kind (+8), bits 16-19 rank (+0x18; a disk's count),
bits 20-30 value (+0x14). Weapons and armor1 use every field, armor2 has no
rank, chips only record/flag/kind. This is `ItemDevice::pack`.

## Randomizer bond (PC option)

The randomizer's seed and modes ([PLAYER_GUIDE.md](PLAYER_GUIDE.md#randomizer-rebalancing-and-cheats)) belong to a save, not
only to settings.ini. The save itself stays the original GCI bytes; the bond
is a sidecar next to it (`src/game/randomizer_save.cpp`): `randomizer.ini` in
the folder that holds the save (the folder of the single save file - `saves/`
beside the program for `saves/savedata.dat`, else the folder of `[data] save`
or `--save-file`; the GCI root of `--card-dir` / `DW4_CARD_DIR`; `saves/`
beside the program for a test run's default folders and for a `[data] cards`
folder, so nothing is written into a Dolphin folder) with one section per
character, keyed by the record's UNIQUE (offset 4, drawn from the world clock
when the character is made; `enemies` is off, each or wave, `zones` off or
dungeons, `zone_rule` the rule the loading zone table is drawn with: 2 now,
any arrangement within an area, at least one entrance changed; a bond without it was written in round 48
and keeps rule 1, no entrance keeping its own dungeon; `seed=0` is a save
the F1 menu set to play the original game), and
`randomizer_<UNIQUE>_spoiler.txt` beside it:

```ini
[character_1A2B3C4D]
name=TAI
seed=20261029
drops=true
enemies=each
zones=dungeons
zone_rule=2
```

When player 1's character changes at the character select (a new game or a
called box) the bond of its UNIQUE applies. A new game without one (the
engine's observe-only `new_character` port event names its UNIQUE) is bound
to the current settings when they randomize; a called box without one plays
the original game, whatever the settings say, until the F1 menu's "Bind
the settings below to this save" binds them (a notice says so when the
settings randomize); "Play this save without the randomizer" writes a bond
with seed 0. Test runs bind every character to `DW4_RANDOMIZER` unless
`DW4_RANDOMIZER_BIND=new`. Two characters can share a
UNIQUE only when they were made in the same frame (the same value for every
player made then); they then share the bond, which is the session's anyway.
Online sessions use the host's settings and bind nothing (the online save is
separate).
