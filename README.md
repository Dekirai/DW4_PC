# Digimon World 4 PC

A native Windows port of **Digimon World 4** (GameCube, USA). It plays the game
from your own disc image, without an emulator, and adds widescreen, high frame
rates, modern controllers, fast loading, GameCube save import and an online
world.

**[Download the latest release](https://github.com/Dekirai/DW4_PC/releases)** ·
[Player guide](docs/PLAYER_GUIDE.md) ·
[Troubleshooting](#troubleshooting-and-faq) ·
[Report a problem](#reporting-problems) ·
[Building from source](#building-from-source)

> [!IMPORTANT]
> You need your own copy of the game: the USA disc (game ID `GDJEB2`). This
> project contains no game data and does not help you get it.

**Contents:** [Status](#status) · [Highlights](#highlights) ·
[What you need](#what-you-need) ·
[Download, install and update](#download-install-and-update) ·
[First start](#first-start) · [Playing online](#playing-online) ·
[Troubleshooting](#troubleshooting-and-faq) ·
[Reporting problems](#reporting-problems) ·
[Building from source](#building-from-source) ·
[Documentation](#documentation) · [Legal](#legal) ·
[Credits](#credits-and-third-party-software)

## Status

The game is playable from a new game to the ending, alone or with up to four
players on one PC. Automated story runs play it through (also without any
cheats), and scenes are compared frame by frame with the original running in
Dolphin. It is **not yet a complete 1:1 reconstruction**: smaller parts of the
original engine are still missing, so you may meet differences or bugs (please
[report them](#reporting-problems)). Online play has its own
[known limits](docs/PLAYER_GUIDE.md#known-limits); developers find the plan in
the [roadmap](docs/ROADMAP.md).

Windows only for now. An Android version is in development (tested on the AYN
Thor handheld); it is not released yet and has no date.

## Highlights

- **The whole game, rebuilt as native PC code:** the story, every area and
  dungeon, the quests, the six boss stages and the ending, with the Normal,
  Hard and Very Hard difficulties unlocked as in the original.
- **Local co-op for 1-4 players**, one controller each (player 1 can also use
  the keyboard).
- **PC presentation:** borderless fullscreen, widescreen, a resizable window,
  anti-aliasing, smooth motion on high refresh rate screens (the game still
  runs at 60 updates per second), better texture filtering, sharpening and
  sharper shadows. One click for the *Original* or a *Modernized* look.
- **Fast loading** (optional): the loading between stages drops from 7-15
  seconds to about a quarter of a second (the fades and scenes stay).
- **Your saves:** the save file is byte-identical to Dolphin's, GameCube saves
  (Dolphin, GCMM, Swiss, GameShark, Action Replay / MaxDrive, memory card
  images) import in the menu, and offline play has four save state slots.
- **Controllers:** Xbox, DualSense, DualShock 4, Switch Pro, Switch 2 Pro
  (USB), Wii U Pro and original GameCube controllers through an adapter, each
  with matching button pictures; every control can be rebound.
- **Online:** an online world on the project's own server, up to four players
  per instance; also direct play over LAN.
- **Optional extras:** a randomizer (enemies, item drops, dungeon entrances;
  online a setting of the instance's host), rebalancing and cheats (not in the
  online world), 72 achievements, Discord Rich Presence, name tags, texture
  packs, content mods, and *Data Overflow*, a survival mode that is still a
  work in progress.

## What you need

| Requirement | Details |
| --- | --- |
| **Game** | Digimon World 4, USA disc (`GDJEB2`, revision 0), from a disc you own: an `.iso` / `.gcm` image, a Dolphin `.rvz` image or a folder extracted with Dolphin. Disc images of other regions or revisions are refused; an extracted folder must come from the USA disc as well. |
| **System** | 64-bit Windows (developed and tested on Windows 11). Nothing else to install: no Visual C++ runtime, no .NET. |
| **Graphics** | A graphics card and driver with OpenGL 3.3. |
| **Disk space** | About 15 MB. An `.rvz` image is converted once into a full disc image next to the program: about 1.4 GB more. |
| **Online play** | An internet connection, a free account (made in the game) and a CPU with AVX2 and FMA3: most Intel Core CPUs from 2013 (4th generation) on and AMD CPUs from 2015 on, every Ryzen included; some Pentium, Celeron and Atom models lack it. After you log in, the game says so if your PC cannot play online. Offline play has no CPU requirement. |

## Download, install and update

1. Open the **[Releases page](https://github.com/Dekirai/DW4_PC/releases)**
   and, under *Assets* of the newest release, download
   `DW4_PC-<version>-windows-x64.zip`. Not *Source code*: that is the code,
   not the game.
2. Extract it into a folder of its own that you can write to, for example
   `C:\Games\DW4_PC`. Not into `C:\Program Files`: the game keeps its
   settings, saves and logs in its own folder.
3. Start **`DW4_PC.exe`**. Windows may show *Windows protected your PC*: the
   program is not code-signed. Choose *More info* > *Run anyway*.

### Updating

Extract the new zip into the same folder and replace the files. Your
`settings.ini`, `saves\` and `mods\` are not in the zip and stay. When the
online server needs a newer version, the game says *Update required*.

### Uninstalling

Back up `saves\`, then delete the folder.

## First start

**Game data.** The first start asks for a *Disc image...* (`.iso`, `.gcm` or
`.rvz`) or an *Extracted folder...* (Dolphin: right-click the game >
*Properties > Filesystem > Extract Entire Disc*), checks it and remembers it
in `settings.ini`. An `.rvz` is converted once into `data\<name>.iso` next to
the program; the game then uses that ISO. WIA, CISO and NKit images must be
converted to ISO or RVZ in Dolphin first (right-click > *Convert File...*).

**A console window** with the program's log opens next to the game: leave it
open while you play and close the game through its own window.

**Controls.** Keyboard and controllers work right away; F1 > *Controls*
rebinds everything (up to three inputs per control).

| Game button | Keyboard | Controller |
| --- | --- | --- |
| Walk (main stick) | W A S D | Left stick |
| D-pad (menus, quick panel cursor) | Arrow keys | D-pad |
| C-stick (quick panel cursor) | Keypad 8 4 2 6 | Right stick |
| A (talk, confirm) | Enter, Space, K | South (A on Nintendo controllers) |
| B (cancel) | Backspace, L | East (B on Nintendo controllers) |
| X (jump) | J | West |
| Y | I | North |
| START | Esc, Tab | Start |
| L (disk panel) | Q | Left trigger or left shoulder |
| R (technique panel) | E | Right trigger |
| Z | Z | Right shoulder |

**F1** (or the controller's View, Back, Share, Create or − button) opens the
menu, **F11** switches fullscreen, **F5** / **F9** save and load save state 1.
Player 1 plays on the keyboard and the first controller, controllers 2-4 are
players 2-4. Holding B, X and START on a controller for more than three
seconds resets the game, as on the GameCube.

**The F1 menu** changes settings while you play and saves them in
`settings.ini`. F1 > *Settings* > *Preset*: *Original* is the GameCube's look
and timing; *Modernized* turns on widescreen, smooth motion, anti-aliasing,
fast loading and the sharper graphics options and skips the intro.

**Saves.** The game saves as on the GameCube, in the save room onto the
memory card in slot A or B. Slot A is `saves\savedata.dat` next to the
program (slot B `saves\savedata_b.dat`, the previous version is kept as
`.bak`). It holds exactly the bytes of Dolphin's `B2-GDJE-NDMWSaveData.gci`,
so renaming it moves a save between the two. To bring a GameCube save over,
use **F1 > Saves** (`.gci`, `.gcs`, `.sav`, card images `.raw` / `.gcp`, GCI
folders): you see each saved character (save box) first, and your current
save is backed up.

All settings, controllers, saves and features in detail:
**[player guide](docs/PLAYER_GUIDE.md)**.

## Playing online

The title screen has a third entry, **Online**. The released game already
knows the project's server: nothing to set up, no port to open (all traffic
goes through the server).

- **Account:** register in the game (or on the server's website). Keep the
  **recovery code** you are shown once: it is the only way back in if you
  forget the password. An e-mail address is optional; password resets by
  e-mail are not offered yet.
- **Online character:** it starts from zero and is saved on the server. Your
  local save is not used online, and nothing comes back into it.
- **The world:** *Play* takes you to your own lobby on your own story. Leaving
  it puts you into an *instance* of that area: a running one you may join, or
  a new one you host (*Public*, *Friends* or *Private*). Up to four players
  share an instance; it plays its host's story, and everyone keeps their own
  EXP, items and Bits.
- **Playing with a friend:** add each other as friends on the server's
  website, then take a gate into the area where your friend plays: the game
  lists their instance when you arrive (unless it is *Private*). Or take the
  same gate at about the same time. Joining a friend from a list and parties come later
  ([known limits](docs/PLAYER_GUIDE.md#known-limits)).
- **The server's website** (its address is in the notes of each release on
  the [Releases page](https://github.com/Dekirai/DW4_PC/releases)) keeps your
  friends list, blocked players, profile and online achievements, and has a
  privacy page about what the server stores. Friends are added there; the
  game has no friends list of its own yet.
- **Cloud backup:** F1 > Saves > *Cloud backup* keeps a copy of your local
  save on the server.
- When the server needs a newer game version, the game says *Update
  required*: download the newest release.

**LAN / direct play** needs no server and no account: start
`DW4_PC.exe --host --players 2` on one PC and
`DW4_PC.exe --join <host's IP address>` on the others, up to four PCs (type
the command in a Command Prompt opened in the game folder, or add the options
to a shortcut's target). The host allows the program in the Windows Firewall
and, for guests outside the local network, opens port 52040 on the router.

More: [player guide, online play](docs/PLAYER_GUIDE.md#online-play).

## Troubleshooting and FAQ

<details>
<summary><b>"This disc is ..., not Digimon World 4 USA" or "not a GameCube disc image"</b></summary>

Only the USA disc (`GDJEB2`, revision 0) works. Convert WIA, CISO and NKit
images to ISO or RVZ in Dolphin (right-click > *Convert File...*); an RVZ
with bzip2 or LZMA compression must be converted again with Zstandard
(Dolphin's default) or to ISO. If the path in the message is where your image
used to be, see the next question.
</details>

<details>
<summary><b>"This is not a GameCube disc image the runtime can read" after I moved or deleted my disc image</b></summary>

The game remembers the path from the first start. Correct `path=` under
`[data]` in `settings.ini` next to the program, or delete that line: the game
then asks for the game data again. The same happens after moving an extracted
folder. With an `.rvz` the game uses the converted `data\<name>.iso`, so do
not delete that file.
</details>

<details>
<summary><b>"Windows protected your PC" or my antivirus blocks DW4_PC.exe</b></summary>

The program is not code-signed, so Windows does not know it yet. Choose
*More info* > *Run anyway*. Download the game only from the
[Releases page](https://github.com/Dekirai/DW4_PC/releases); if your
antivirus program still quarantines that file, [report it](#reporting-problems).
</details>

<details>
<summary><b>The game does not start: an error box about OpenGL or the window</b></summary>

The game needs OpenGL 3.3: install the current driver for your graphics card.
</details>

<details>
<summary><b>My controller does nothing</b></summary>

F1 > *Controls* lists the controllers the game sees and says what is wrong,
for example a GameCube adapter in Wii U mode without its WinUSB driver
(install it with Zadig, as for Dolphin) or another program such as Steam
holding a Switch 2 Pro Controller (which plays only over a USB-C data cable).
Each start writes `saves\controllers.log`; attach it to your
[report](#reporting-problems). Details:
[controllers](docs/PLAYER_GUIDE.md#controllers-and-button-pictures).
</details>

<details>
<summary><b>Online: "does not answer", "maintenance", "Update required", "cannot play online"</b></summary>

- *The server does not answer*: check your connection, then *Try again*.
- *The server is in maintenance*: try again later.
- *Update required*: download the newest release.
- *This PC cannot play online*: the CPU lacks AVX2 / FMA3, which online play
  needs so that every PC computes alike. Offline play works.
- *Online play is not available in this build yet*: a build made from the
  source code (without the server's certificate pins); use a release.

When online play lags, attach `saves\online.log` to your
[report](#reporting-problems) (F1 > *Online* has *Copy path* and *Open
folder*; it holds no password or token). Forgot the password? Use *Forgot the
password?* with your recovery code.
</details>

<details>
<summary><b>How do I reset all settings?</b></summary>

Close the game and delete `settings.ini` next to the program. The next start
uses the defaults and asks for the game data again; your saves stay.
</details>

## Reporting problems

Bugs and questions go to the
**[Issues page](https://github.com/Dekirai/DW4_PC/issues)**. Say what you did
and what happened, and attach the log that fits: `saves\controllers.log` for
controllers, `saves\online.log` for online play, or the last lines of the
console window. Never post your password or recovery code, and no game files.

## Building from source

Windows x64 with Visual Studio (*Desktop development with C++*); the first
configure downloads the pinned libraries.

```bat
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Jobs 4
```

This builds `build\Release` and runs the tests; `-Package` installs a
ready-to-copy folder into `dist\DW4_PC`. Self-built games play offline and
over LAN but cannot connect to the online server. Everything else:
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

**Contributing:** read [DEVELOPMENT.md](docs/DEVELOPMENT.md) first; the
project's rules (English everywhere, no game data in Git, no GitHub Actions,
local validation) are in [AGENTS.md](AGENTS.md).

## Documentation

| Document | For |
| --- | --- |
| [PLAYER_GUIDE.md](docs/PLAYER_GUIDE.md) | Players: settings, controls, controllers, saves, randomizer, achievements, online, mods, texture packs, command line |
| [DEVELOPMENT.md](docs/DEVELOPMENT.md) | Developers: how the port works, building, tests, test runs, the online server |
| [ROADMAP.md](docs/ROADMAP.md) | For developers: the acceptance gates and the plan |
| [SAVES.md](docs/SAVES.md) | The save files and the GameCube save import in depth |
| [TEXTURES.md](docs/TEXTURES.md) | Textures, graphics options, the texture pack format |
| [SURVIVAL.md](docs/SURVIVAL.md) | Data Overflow, the survival mode |
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | The engine's design |

## Legal

- **License:** the project's own code is MIT licensed ([LICENSE](LICENSE)).
  Adapted code and the libraries the program uses keep their own licenses
  ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). The MIT license covers
  this project's code only, not Digimon World 4 or its content.
- **No game data:** this repository and its releases contain no game data and
  no part of the original program (no executable bytes, no disassembly); the
  engine is new C++ source written to behave like the original. You supply
  your own copy of the game. Please do not share disc images, extracted game
  files, texture dumps or texture packs made from the game.
- **Not affiliated:** this is an unofficial fan project, not affiliated with,
  endorsed or sponsored by Bandai Namco, Toei Animation, Nintendo or any other
  rights holder. Digimon and Digimon World 4 are trademarks of their
  respective owners. Other product names (GameCube, Wii U, Nintendo Switch,
  Xbox, PlayStation, DualSense, DualShock, Discord, Dolphin, AYN Thor) belong
  to their owners and are named only to describe compatibility.

## Credits and third-party software

Built with [SDL](https://github.com/libsdl-org/SDL),
[Dear ImGui](https://github.com/ocornut/imgui),
[pl_mpeg](https://github.com/phoboslab/pl_mpeg),
[stb](https://github.com/nothings/stb) and
[Zstandard](https://github.com/facebook/zstd); the animation decoder adapts
[enbrip](https://github.com/korenkonder/enbrip) and the Switch 2 Pro
Controller support adapts SDL's own driver. Research references include
[ivanno4317/dw4-gc](https://github.com/ivanno4317/dw4-gc), mateon1's IGB notes
and Dolphin's GameCube format code. Pins, licenses and provenance:
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md); the license texts ship in
`licenses\` next to the program.
