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
of the `*.gci` files in the slot's folder, free files 127 minus their number,
so a folder holding a 2043-block GCI is a full card (GETFREESIZE 9).

The logo (803B18BC) checks slot A, then B. A broken card asks to format it
(sub 50: texts 92204/92205 + 92030, question 92031 with the cursor on no,
FORMAT, 92202 / 92203). When no slot can hold the save, state 1 reports the
worst problem of slot A, else B (flags 8 no room: 92010 / 92011, 0x40 no
file entry: 92211, 0x20 wrong device: 92209, 4 error: 92020 / 92021, 0x10
broken: 92206 / 92207, none: 92000 "Memory Card not found." / 92001) and asks
"Yes, continue without saving. / No, use another Memory Card."; yes shows
the logos (after 92210 "To manage the contents of your Memory Card, use the
Memory Card Screen." when the card was full), no checks again (92100).

## GameCube file

beSaveUtilInfo (`stagis12/separate/*_pack.igb`) holds beSvPlatDataGC: file
`NDMWSaveData`, 90112 bytes (11 blocks of 8192): an 8192-byte header with the
3680-byte banner, 1632-byte icon and the comment `DigimonWorld4` /
`SaveData`, then two redundant 40960-byte copies of the game data at 0x2000
and 0xC000 (a copy whose check fails is skipped).

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
