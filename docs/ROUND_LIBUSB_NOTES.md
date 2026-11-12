# Round "libusb": the Switch 2 Pro Controller over USB

Requested by the owner after round "pads" (docs/ROUND_PADS_NOTES.md): the
Switch 2 Pro Controller (057e:2069) becomes playable over a USB cable. The
owner approved libusb (LGPL-2.1) as a separately shipped `libusb-1.0.dll`
beside the game. Branch `feat-libusb`, built on main `9612167d`.

## 1. What SDL 3.4.16 needs (read in its sources)

| Where | What it does |
| --- | --- |
| `cmake/sdlchecks.cmake` `CheckHIDAPI`, `cmake/FindLibUSB.cmake` | With `SDL_HIDAPI_LIBUSB` (on by default) SDL looks for `libusb.h` (API version at least `0x01000102`) and a libusb library, checks that `libusb_interrupt_event_handler` compiles, and with `SDL_HIDAPI_LIBUSB_SHARED` (on: `SDL_DEPS_SHARED`) reads the DLL name out of the import library (`dumpbin -headers`, "DLL name"). Result: `HAVE_LIBUSB` and `SDL_LIBUSB_DYNAMIC "libusb-1.0.dll"` in `SDL_build_config.h`, nothing linked. Before this round neither was found on this PC (`LibUSB_LIBRARY-NOTFOUND`), so SDL had no libusb. |
| `src/misc/SDL_libusb.c` `SDL_InitLibUSB` | Loads `libusb-1.0.dll` with `SDL_LoadObject` (Windows: `LoadLibraryW` by name, the program's folder first) and 35 functions (`libusb_init` ... `libusb_error_name`); one missing and SDL uses none of it. |
| `src/hidapi/SDL_hidapi.c` `SDL_hid_init` | `SDL_HINT_HIDAPI_LIBUSB` (default 1) loads libusb; when it cannot ("Couldn't load libusb", a debug message) the Windows HID backend still counts as a success, so SDL's HID layer and every controller on it work as before. |
| `SDL_hidapi.c` `SDL_libusb_required`, `RequiresLibUSB`, `SDL_HIDAPI_ShouldIgnoreDevice` | Five devices are reached only through libusb: the GameCube adapter 057e:0337, the Switch 2 GameCube controller 057e:2073, the Switch 2 Joy-Con 057e:2066 / 2067 and the Switch 2 Pro Controller 057e:2069. SDL's Windows HID path skips them always; its libusb path takes only them while `SDL_HINT_HIDAPI_LIBUSB_WHITELIST` is on (the default when a platform backend exists, as on Windows), and skips the adapter when `SDL_HINT_HIDAPI_LIBUSB_GAMECUBE` is 0. |
| `src/hidapi/libusb/hid.c` | libusb's enumeration lists the HID-class interfaces (and Xbox ones): of the Switch 2 Pro Controller its interface 0. SDL opens it with `libusb_open` and gives the driver the handle (`SDL_PROP_HIDAPI_LIBUSB_DEVICE_HANDLE_POINTER`). |
| `src/joystick/hidapi/SDL_hidapi_switch2.c` `HIDAPI_DriverSwitch2_InitUSB` | Takes that handle, finds the bulk OUT / IN endpoints of **interface 1**, claims interface 1 (`libusb_claim_interface`), reads serial number and calibration from the controller's flash, sends the ten start commands (feature masks, report format, "start output") over bulk; input reports then arrive on interface 0. Over Bluetooth: "Nintendo Switch2 controllers not supported over Bluetooth". It names the pad "Nintendo Switch Pro Controller". |
| `src/joystick/SDL_gamepad.c` `SDL_CreateMappingForHIDAPIGamepad` | The Switch 2 Pro's mapping, positional (`hint:!SDL_GAMECONTROLLER_USE_BUTTON_LABELS`): SOUTH B, EAST A, WEST Y, NORTH X, BACK Minus, GUIDE Home, START Plus, the sticks' clicks, L / R shoulders, ZL / ZR triggers, MISC1 Capture, MISC2 C, RIGHT_PADDLE1 GR, LEFT_PADDLE1 GL. |
| `src/joystick/hidapi/SDL_hidapi_gamecube.c` | Reads the adapter 057e:0337 through libusb when libusb has it. |

### Windows drivers

- **Switch 2 Pro Controller.** A composite device: interface 0 is HID
  (Windows' own HID driver; libusb reads it through its HID backend, nothing
  to change), interface 1 a vendor interface (class 0xFF) with the bulk
  endpoints 0x02 OUT and 0x82 IN. libusb on Windows claims an interface only
  with the WinUSB driver on it (or libusbK / libusb0), and an interface of a
  composite device only when that interface's node names a device interface
  GUID in its registry key (`DeviceInterfaceGUIDs` / `DeviceInterfaceGUID`):
  libusb 1.0.30 `os/windows_winusb.c` collects those GUIDs in its GEN pass
  (`get_guid`), and only the EXT passes that enumerate them call
  `set_composite_interface`; without a GUID interface 1 stays unsupported
  and `libusb_claim_interface(1)` fails, so SDL makes no pad.
  A descriptor dump of the controller as the USB device emulator VIIPER
  reproduces it (`device/ns2pro/descriptor.go`) carries Microsoft OS
  descriptors with the compatible id `WINUSB` and a device interface GUID
  for interface 1, so Windows **should** load WinUSB there by itself with a
  GUID libusb can use; reports of wired use on Windows without an install
  (Steam, November 2025) agree. **Not verified with the device**: whether the
  real controller's descriptors match VIIPER's is the owner's hand test. The
  game says what Windows has (section 3): no driver on interface 1, WinUSB
  there without a GUID, another driver, no interface 1 node at all, each with
  its remedy (Zadig, the entry with USB ID `057E 2069 01`, WinUSB: Zadig's
  INF writes a GUID; never the `00` interface or the whole device). (The SDL
  forum thread cited in the first version of these notes is about macOS,
  where none of this applies; it is no evidence for Windows.)
- **Switch 2 Pro Controller over Bluetooth.** SDL 3.4.16 does not drive
  Switch 2 controllers over Bluetooth ("not supported over Bluetooth"). The
  controller uses Bluetooth LE, and per reports it offers Windows no input
  device that way. The game counts it as connected over Bluetooth only by an
  HID node of it (`HID\{...}_VID&0002057E_PID&2069` classic,
  `HID\{...}_Dev_VID&02057E_PID&2069` LE): Windows has those while a device
  is connected as an input device, whereas its pairing nodes (`BTHENUM`,
  `BTHLE`, `BTHLEDevice`) stay listed while it is paired, also when it is off,
  and would show the notice at every start. A pairing alone goes to the log
  only, with every node's hardware id and driver (what Windows lists for it
  on the owner's PC is not known yet).
- **GameCube adapter, Wii U mode (WUP-028).** Unchanged: the WinUSB driver
  on it with Zadig, as for Dolphin (the port's reader opens it through the
  device interface GUID of Zadig's INF; SDL's libusb path would need WinUSB
  just the same).

## 2. What the build does

- `CMakeLists.txt`: `FetchContent_Declare(libusb_src)` from the official
  release archive `libusb-1.0.30.tar.bz2` of github.com/libusb/libusb (tag
  `v1.0.30`, 17 May 2026), SHA-256
  `fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf`; its
  OpenPGP signature (`.asc`, the release manager's key from the repository's
  `KEYS`) was checked when the pin was set. Option `DW4_LIBUSB` (on).
- SDL's own detection runs unchanged: at configure time `lib.exe` makes an
  import library from libusb's export list (`libusb/libusb-1.0.def`,
  `build/<dir>/libusb-import/`), `LibUSB_INCLUDE_PATH` / `LibUSB_LIBRARY`
  point there, and SDL concludes `HAVE_LIBUSB` and `SDL_LIBUSB_DYNAMIC
  "libusb-1.0.dll"` (its summary: `SDL_HIDAPI_LIBUSB ON`,
  `SDL_HIDAPI_LIBUSB_SHARED ON`).
- Target `libusb_dll`: the sources of the libusb project's own MSVC DLL
  project (`msvc/libusb_dll.vcxproj`: core, descriptor, hotplug, io,
  strerror, sync, the Windows events / threads / common / UsbDk / WinUSB
  backends, `msvc/config.h`, the `.def` and the version resource; no Windows
  hotplug, as in its default configurations), with this build's static C
  runtime. `libusb-1.0.dll` (about 320 KB) imports only `KERNEL32.dll`;
  `dw4_game.exe` does not import it (`dumpbin /dependents`). The build puts
  it and `libusb-1.0-COPYING.txt` beside `dw4_game.exe`; `cmake --install`
  (`scripts\build.ps1 -Package`) beside `DW4_PC.exe`, the license also as
  `licenses/libusb.txt`.
- The DLL's corresponding source (LGPL-2.1 section 4): `cmake --install`
  puts the unchanged release archive into the package as
  `licenses/libusb-1.0.30.tar.bz2` (656 KB), so the source travels with the
  DLL. Configure takes it from FetchContent's download
  (`_deps/libusb_src-subbuild/libusb_src-populate-prefix/src`), from beside
  the sources (`<deps>/libusb-1.0.30.tar.bz2`) or from
  `<build>/libusb-source`, each checked against the pinned SHA-256, and
  otherwise downloads it once from the same URL (connected builds only).
  Without it the build and the tests still work, but `cmake --install` stops
  with a message saying where to put it.
- Local disconnected builds (`FETCHCONTENT_FULLY_DISCONNECTED`): the sources
  are at `build/Release/_deps/libusb_src-src` of the main checkout (the
  archive beside them); a build that names SDL's sources but not libusb's
  finds them there, so the existing build folders keep configuring, and a
  build folder populated before (`<FETCHCONTENT_BASE_DIR>/libusb_src-src`)
  keeps its own. Otherwise a disconnected build needs
  `FETCHCONTENT_SOURCE_DIR_LIBUSB_SRC` (a fresh clone without network cannot
  get libusb). `-DDW4_LIBUSB=OFF` builds without libusb (the notices then say
  so).

## 3. The game

`src/game/pad_usb.*` (new):

- `controller_hint_values`: `SDL_HIDAPI_LIBUSB` 1 in play and 0 in test runs
  (no controller of this PC takes part in them; `DW4_TEST_LIBUSB=1` lets a
  hidden test run use it), `SDL_HIDAPI_LIBUSB_WHITELIST` 1 (SDL's Windows
  default, stated: every other controller keeps Windows' drivers),
  `SDL_HIDAPI_LIBUSB_GAMECUBE` 0 (section 4), and the hints round "pads"
  set before.
- `probe_libusb`: looks for `libusb-1.0.dll` as SDL does (`SDL_LoadObject`),
  checks SDL's 35 functions, reads `libusb_get_version`, lets it go. The
  state (loaded / missing / incompatible / off / not built) goes to the
  console (`controllers: libusb: ...`), F1 > Controls and the controller log.
- The Switch 2 Pro Controller's notice: Windows' device list (the adapter
  reader's look, `gc_adapter.cpp`, now also on a cable or over Bluetooth as
  an input device, and the driver service and device interface GUID of
  interface 1's node `USB\VID_057E&PID_2069&MI_01`) against what SDL opened
  (a pad 057e:2069 with SDL's HIDAPI GUID: its Switch 2 driver started it).
  Playable; libusb-1.0.dll missing / unusable / not in this build; over
  Bluetooth only; no driver on interface 1 ("Windows blocks it": the Zadig
  steps); WinUSB there without a device interface GUID (also "Windows blocks
  it": Zadig again, its INF writes one); another driver there; no interface 1
  node at all (a driver on the whole controller: uninstall it in Device
  Manager); WinUSB with its GUID there but not opened (another program holds
  it: Steam, an emulator). A notice counts after three seconds (SDL opens the
  controller a moment after Windows lists it); F1 > Controls shows its text,
  play shows a short notice once.
- `saves/controllers.log` (beside the game, new at every start, not in test
  runs; `DW4_CONTROLLER_LOG=<file>` in one): the version, SDL's version,
  libusb's state, whether SDL has libusb-1.0.dll in use after it started,
  every pad that comes and goes (name, ids, family, SDL backend, device
  path), what Windows lists of the Switch 2 Pro Controller (every node with
  its hardware id and driver: `windows node: ...`) and the adapter, the
  notices, and SDL's own input messages (devices it adds, "Couldn't load
  libusb", the Switch 2 driver's flash warnings; they no longer reach the
  console, errors still do).

## 4. The GameCube adapter: one owner

With libusb, SDL's GameCube driver would open 057e:0337 too (its libusb path
takes it by default), next to the port's own WinUSB reader
(`gc_adapter.cpp`); WinUSB opens a device once, so the two would race and
the background release for Dolphin would no longer hold. Decision: the
port's reader stays the only owner, with or without the DLL:
`SDL_HIDAPI_LIBUSB_GAMECUBE=0` makes SDL's libusb path skip it, and SDL's
Windows HID path skips it anyway (`RequiresLibUSB`). Evidence through SDL's
own code: `tests/pad_usb_tests.cpp` calls SDL's
`SDL_HIDAPI_ShouldIgnoreDevice` after `SDL_hid_init` with the game's hints:
the adapter is ignored by both of SDL's paths, the Switch 2 Pro Controller
taken by libusb only, a DualSense or a Switch Pro by Windows' HID only; with
SDL's default for the adapter hint its libusb path would take the adapter.
The reader keeps letting the adapter go while the game is in the background,
so Dolphin can take it. (SDL's GameCube driver would add rumble; the port
uses none.)

## 5. The Switch 2 Pro Controller end to end

- Family by 057e:2069 (round "pads"); SDL's own driver names it "Nintendo
  Switch Pro Controller" with the Switch Pro's type, so only the id tells.
- Read by label by default (`nintendo_layout=label`): A is the game's A, B
  B, X X, Y Y, Plus START, L and ZL the game's L, ZR its R, R its Z (the
  defaults, as on the Switch Pro); Minus opens and closes F1 (the menus'
  back key), the menus confirm with A.
- Its extra buttons play nothing by default (the shared `[gamepad]` table
  has no default for paddles or MISC2, and an Xbox Elite's paddles share
  those SDL buttons): C (MISC2), GL (LEFT_PADDLE1), GR (RIGHT_PADDLE1),
  Capture, Home. A press still counts as the player's input (the prompts
  switch to the controller). Bound in F1 > Controls they play the bound
  button and are named and drawn C, GL, GR.
- Prompts: the Switch 2 Pro pictures (round "pads").

## 6. Checks

| Check | Build | Result |
| --- | --- | --- |
| ctest (build/Clean of the worktree) | feat-libusb | 66 / 66 (new: `pad_usb` with the DLL, `pad_usb_missing`; `pad_detect` with the Switch 2 Pro end to end) |
| pad_usb with the DLL | | the built DLL exports SDL's 35 functions, version 1.0.30.12037; found by name beside the program; SDL's `SDL_hid_init` loads it and `SDL_hid_exit` lets it go; SDL's filter as in section 4; SDL's enumeration of Nintendo's ids through Windows' HID and libusb about 13 ms; test-run hints: SDL never loads it |
| pad_usb_missing | | DLL search limited to System32: missing, SDL's HID layer starts, nothing loaded, the adapter still skipped by SDL, the notice "libusb-1.0.dll is missing", virtual pads (DualSense, DualShock 4, Switch Pro, Switch 2 Pro) classified as before |
| Mutations | | each new test shown to catch its fault: the adapter hint, libusb in test runs, the missing notice, the grace, an export SDL needs, SDL's HIDAPI GUID, interface 1's node, the GR name (8 of 8) |
| det.sh (newgame_dng101b1, 6000 frames), a copy of the exe | feat-libusb | 4004f056 / 396cb219, twice |
| save state suite (st_all.sh), the copy | feat-libusb | 17 / 17 identical after loading with a loaded state |
| Hidden start with the DLL (`DW4_TEST_LIBUSB=1`, a screenshot run: SDL's gamepad subsystem with libusb) | feat-libusb | exit 0; `controllers: libusb: ...libusb-1.0.dll 1.0.30.12037 (Switch 2 Pro Controller over USB)`; the log: SDL has it in use |
| Hidden start without the DLL | feat-libusb | exit 0; `controllers: libusb: libusb-1.0.dll not found: the Switch 2 Pro Controller cannot be used, every other controller works`; the log: SDL's "Couldn't load libusb" |
| Test runs without `DW4_TEST_LIBUSB` | feat-libusb | `controllers: libusb: off in test runs (...libusb-1.0.dll 1.0.30.12037)`, and `(libusb-1.0.dll not found)` without the DLL; exit 0 |
| Player package (`cmake --install`, as `build.ps1 -Package`) | feat-libusb | `libusb-1.0.dll` and `libusb-1.0-COPYING.txt` beside `DW4_PC.exe`, `licenses/libusb.txt` (and, since the review fixes, `licenses/libusb-1.0.30.tar.bz2`, section 8) |
| Configure variants | feat-libusb | a disconnected build without `FETCHCONTENT_SOURCE_DIR_LIBUSB_SRC` finds the sources beside SDL's (SDL: `HAVE_LIBUSB`, `SDL_LIBUSB_DYNAMIC "libusb-1.0.dll"`); `-DDW4_LIBUSB=OFF`: SDL without libusb, `pad_usb` (`--not-built`) passes |
| r25c | - | not run: no input code changed beyond the hints and the driver setup (the binding spaces, the slots and the game's pad are untouched) |

No controller was connected to this PC during the checks (Windows listed no
Nintendo device); nothing was installed and no driver or device setting
changed.

## 7. Open points

- **The hand tests** (owner): DualSense / DualShock 4 (USB, Bluetooth),
  Switch Pro, Switch 2 Pro over USB; in particular whether Windows puts
  WinUSB with a device interface GUID on the Switch 2 Pro's interface 1 by
  itself (F1 > Controls and `saves/controllers.log` say: the log's `windows:`
  line names interface 1's driver and whether it has a GUID).
- **Steam**: with Steam's Switch controller support on, Steam may hold the
  Switch 2 Pro Controller's interface 1 (WinUSB opens a device once): the
  notice says "could not open it"; close Steam or turn that support off.
- **Bluetooth**: SDL 3.4.16 has no Switch 2 Bluetooth support. What Windows
  lists for a Switch 2 Pro Controller paired over Bluetooth is unknown; the
  game names it only when Windows lists it as a Bluetooth input device, and
  the log lists every node it has (`windows node: ...`).
- **Plugging in during play**: SDL starts the Switch 2 Pro Controller on the
  main thread when it finds it (`HIDAPI_JoystickDetect` ->
  `HIDAPI_DriverSwitch2_InitUSB`: seven flash reads and ten bulk commands,
  each with a timeout of 100 ms to 1 s), and every device change now also
  runs a libusb enumeration of all USB devices (about 13 ms here; SDL's own
  and the Wii U Pro check's `SDL_hid_enumerate`). Plugging it in mid-game can
  hold the picture for a moment; not measured with the device.
- **Other Switch 2 controllers**: libusb also lets SDL reach the Switch 2
  Joy-Con (057e:2066 / 2067) and the Nintendo Switch Online GameCube
  controller (057e:2073), which were not usable before; untested, no
  prompts or notices of their own.
- **Merging**: the main checkout's existing build folders find libusb's
  sources and the release archive beside SDL's (section 2);
  `work/port-research/build_*.cmd` need no change. The online client folder
  on the Desktop needs `libusb-1.0.dll`, `libusb-1.0-COPYING.txt` and the
  source archive (`licenses/libusb-1.0.30.tar.bz2`) with the next client
  build.

## 8. Review fixes

| Finding | Fix | Check |
| --- | --- | --- |
| The Bluetooth notice could never appear: only classic Bluetooth's `VID&0002057E` spelling was read, the controller uses Bluetooth LE (`VID&02057E`), and per reports it offers Windows no input device over Bluetooth at all | `hardware_id_usb_ids` reads both spellings (the vendor is the last four of six or eight digits); "over Bluetooth" now means a Bluetooth HID node of it (connected as an input device), while pairing nodes (`BTHENUM`, `BTHLE`, `BTHLEDevice`, listed also while it is off) go to the log only, so a paired controller that is off gives no notice at every start; the log lists every node. README and the hand-test steps no longer promise the notice: over Bluetooth the game may say nothing | `pad_usb`: LE and classic ids, LE HID nodes, malformed ids, a pairing alone (no notice, the log's lines); `pad_family`: an LE id |
| WinUSB on interface 1 without a device interface GUID went undetected and showed "another program may be using it" | `DeviceRecord.interface_guid` of interface 1's node (the same registry values libusb's `get_guid` reads) becomes `UsbFindings.switch2_vendor_guid`; WinUSB without it is the notice `no_interface_guid` ("Windows blocks it", Zadig on `057E 2069 01`, whose INF writes a GUID); "could not open it" only with WinUSB and its GUID; the log's `windows:` line says "with / without a device interface GUID" | `pad_usb`: the GUID of interface 1 only (not of interface 0 or the HID child), the notice and its text |
| No interface 1 node (`UsbDriver::unknown`) gave "could not open it" with the false "(WinUSB on its interface 1)" | Its own notice `no_interface`: Windows lists no interface 1, a driver on the whole controller does this, uninstall it in Device Manager, send the log | `pad_usb`: the notice, the text claims no WinUSB |
| LGPL: the source offer named only the upstream URL | The unchanged release archive goes into the package (`licenses/libusb-1.0.30.tar.bz2`), checked against the pin at configure time; without it `cmake --install` stops (the build does not) | `cmake --install` into a scratch folder: the archive (656112 bytes) beside `libusb.txt`; a configure without it: the warning, and the generated install script carries the stop |
| A populated build folder reconfigured disconnected without `FETCHCONTENT_SOURCE_DIR_SDL3` stopped | The guard also accepts `<FETCHCONTENT_BASE_DIR>/libusb_src-src` | A disconnected configure with only `FETCHCONTENT_BASE_DIR` (its sources populated): SDL with libusb; with the archive in `<build>/libusb-source` the install script takes it |
| The test's head comment said SDL's libusb never enumerates | Corrected: one `SDL_hid_enumerate(057e)` runs through libusb and briefly opens a connected Switch 2 controller to list it | - |
| The docs claimed more than is known | README and section 1: WinUSB on interface 1 "should" come by itself, not verified with the device; the macOS forum thread is no evidence for Windows | - |
| Plugging in mid-game, other Switch 2 controllers | Documented (section 7) and in the hand-test steps; no code change | - |

Also: `update_controllers` copies the device list's findings only after a
new look (they now list nodes), not every frame.

Checks of the review fixes (worktree, exe copies, hidden runs at below-normal
priority, no controller connected):

| Check | Result |
| --- | --- |
| ctest (build/Clean) | 66 / 66; `pad_usb` 148 checks with the DLL |
| Mutations of the new tests | 7 of 7 caught: LE ids not read, a pairing counted as connected, the GUID ignored, the GUID taken from any node, no interface 1 reported as not opened, the no-interface text claiming WinUSB, the no-GUID text blaming Steam |
| det.sh, a copy of the exe | 4004f056 / 396cb219, twice (four runs) |
| save state suite (a private copy of st_all.sh, same scripts and card) | 17 / 17 identical after loading with a loaded state |
| Hidden start with the DLL (`DW4_TEST_LIBUSB=1`, screenshot run) | exit 0; libusb-1.0.dll 1.0.30.12037 in use; the screenshot identical to the first round's |
| Hidden start without the DLL | exit 0; "libusb-1.0.dll not found ..."; SDL's "Couldn't load libusb"; the same screenshot |
| Plain test runs | `controllers: libusb: off in test runs (...)` with and without the DLL; exit 0 |
| Player package | `libusb-1.0.dll`, `libusb-1.0-COPYING.txt` beside `DW4_PC.exe`; `licenses/libusb.txt`, `licenses/libusb-1.0.30.tar.bz2` |
