# DW4_PC on Android (AYN Thor)

The Android port runs the same source tree as the PC version: one CMake root,
one version number, one protocol. `android/` is only a Gradle wrapper whose
native build is the repository's own `CMakeLists.txt` (its `if(ANDROID)` path,
`cmake/android.cmake`). The target device is the AYN Thor (Base/Pro/Max:
Snapdragon 8 Gen 2, Adreno 740, Android 13); other devices are not supported
officially.

Status: **phase B done** (13.11.2026): the game itself runs on the emulator
(SwiftShader and the host GPU) up to the title screen with sound, from the
first start's file picker on, with the F1 menu by controller, Back key and
touch and HOME and back; the PC build is unchanged in its checks ("Phase B"
below, with what is left for phase C). Phase A (toolchain, the Gradle build
offline, the device probe) is done. Phase B's APK reached the title on the
owner's Thor (13.11.2026). main 12f6a1a8 (the display languages with the
live switch, the remaster's Graphics options and texture streaming) is merged
in (14.11.2026, "The main merge of 14.11.2026" below). Phase C's player
side (branch feat-android-cux, 14.11.2026): immersive full screen, the
menus at the display's scale, a menu button for touch, the AYN Thor's
controller family, the lifecycle (settings saved, no catch-up, low
memory), the display's frame rate, the system keyboard for text fields
and the save round trip PC / emulator / PC ("Phase C: the player's side on
Android" below; the Thor was locked, its in-game checks are open).

## Decisions this port follows

- The Thor's lower screen shows online panels (instance list, friends, lobby
  status) and a status mirror (party, HP/SP) switchable by tabs, the F1 menu by
  touch on demand, and "off" as a setting. Every one of these also exists on
  the PC as an overlay or F1 panel; there is no Android-only UI.
- Touch serves menus only (both screens); gameplay uses the controller.
- Simulation math: the original's bit-exact MSL routines on every platform (a
  later phase). The det reference is re-based once and must then match on PC,
  emulator and Thor.
- Distribution: an APK sideloaded, signed with the project's own release key
  (phase F; later a download link through DW4Online). Application id
  `org.dw4pc.game`, minSdk 33 (Android 13).
- Storage: `Android/data/org.dw4pc.game/files` (with import/export); the disc
  is chosen once and copied into the app's storage.
- One version from CMake for both platforms (below).
- The USB controller readers (GameCube adapter, Switch 2 Pro) on Android are
  an optional later phase.
- No new `#ifdef _WIN32` without an Android branch or an explicit "Windows
  only" note.

## Toolchain (phase A, 13.11.2026)

| Part | Version | Note |
| --- | --- | --- |
| Android SDK | `%LOCALAPPDATA%\Android\Sdk` | `ANDROID_HOME` is not set; `android/local.properties` names it |
| NDK | `ndk;30.0.16248370` (r30) | clang 21.0.0, `Android (16134705, based on r574158c)`, libc++ `_LIBCPP_VERSION` 210000 |
| CMake | `cmake;3.31.6` (SDK package) | with Ninja 1.12.1 |
| JDK | Oracle JDK 17.0.12 | `JAVA_HOME=C:\Program Files\Java\jdk-17` |
| Gradle | 8.9 (wrapper in `android/gradle/wrapper`) | always through `android/gradlew-dw4.cmd`: DW4_PC's own Gradle user home `%USERPROFILE%\.gradle-dw4` ("Gradle user home") |
| Android Gradle Plugin | 8.7.3 | needs Gradle 8.9 or newer |
| Platform / build tools | `platforms;android-35`, `build-tools;34.0.0` | compileSdk and targetSdk 35 |
| Emulator | 36.6.11, WHPX | |
| Emulator image | `system-images;android-33;default;x86_64` rev 2 | AOSP with SystemUI, GLES up to 3.1 |
| SDL | 3.4.16 (`fa2c02bb`, `cmake/third_party.cmake`) | `libSDL3.so` on Android, static on the PC; a local copy must have the pinned version (Building, 2.) |

libc++ 21 has `std::from_chars` for `float` and `double` (the code uses it in
`src/lua4.cpp`, `src/engine/engine.cpp` and `src/game/online/json.cpp`): a
test built for arm64-v8a and x86_64 links with `-static-libstdc++` and parses
decimal and hexadecimal floats correctly on the x86_64 emulator.

ABIs: `arm64-v8a` (the Thor) and `x86_64` (the emulator), `c++_static`.

## Layout

| Path | Contents |
| --- | --- |
| `cmake/third_party.cmake` | The FetchContent sources (SDL3, imgui, pl_mpeg, stb, zstd) for the PC and the Android build: one list; checks a local SDL copy against the pinned version |
| `cmake/android.cmake` | The Android path of the root `CMakeLists.txt`: SDL3 shared, the PC's target `dw4_game` as `libmain.so`, the floating-point flags |
| `src/game/app_paths.hpp` | `app_root()` and `private_root()`: the game's folders on each platform |
| `src/game/platform_android.cpp` | Android only: stdout and stderr into logcat (tag `DW4`) |
| `src/android/probe_main.cpp` | Phase A's device probe (`-DDW4_ANDROID_PROBE=ON` builds it as `libmain.so` instead of the game) |
| `android/` | Gradle wrapper project: `settings.gradle`, `build.gradle`, `app/build.gradle`, manifest, resources, `DW4Activity.java` |
| `android/gradlew-dw4.cmd`, `android/gradlew-dw4.sh` | The Gradle entry points: `gradlew` in DW4_PC's own Gradle user home ("Gradle user home") |
| `android/ndk-core-check.cmd` | Compiles `dw4_core`, `dw4_engine`, `dw4_online` with the NDK for both ABIs |
| `android/gradle/cache-retention.init.gradle` | The cache retention (365 days) that `gradlew-dw4` copies into its Gradle user home's `init.d` before every run |
| `tools/android/gles_check.cpp` | A plain `adb shell` executable (EGL pbuffer, no app) that runs the renderer's OpenGL ES paths of the Graphics options and the texture streaming on a device ("GLES checks without the app") |

SDL's Java classes (`org.libsdl.app.*`) are never kept in the repository: the
Gradle task `copySdlJava` copies them at build time from the same SDL source
tree that `libSDL3.so` is built from (`<deps>/sdl3-src/android-project/app/
src/main/java`), because `SDLActivity` refuses to start when its version and
the native library's differ. `DW4Activity` extends `SDLActivity`; it loads
`SDL3` and then `main`.

The manifest: `INTERNET` and `VIBRATE`, no camera, microphone or storage
permissions, `allowBackup="false"`, `hasFragileUserData="true"` (uninstalling
asks whether to keep the data), OpenGL ES 3.0 required, landscape.

## Version

`project(DW4_PC VERSION MAJOR.MINOR.PATCH)` in the root `CMakeLists.txt` is the
`versionName`; `app/build.gradle` reads it. The `versionCode` is

    MAJOR * 10000000 + MINOR * 10000 + PATCH * 100 + BUILD

with `BUILD` 0..99 (`-Pdw4.build=N`, default 0) for a rebuild of the same
version. 0.46.0 is 460000. Any change of the version happens in
`CMakeLists.txt` only.

## Building

Prerequisites:

1. The SDK packages above (`sdkmanager "ndk;30.0.16248370" "cmake;3.31.6"`).
2. The third-party sources offline: the PC build's FetchContent copies in
   `build/Release/_deps` (build the PC version once). A worktree or another
   checkout names the directory in `android/local.properties`:

       sdk.dir=C\:/Users/<you>/AppData/Local/Android/Sdk
       dw4.depsDir=C\:/Users/<you>/source/repos/DW4_PC/build/Release/_deps

   Nothing in that directory is ever written. Its `sdl3-src` must be the
   pinned SDL: `cmake/third_party.cmake` keeps the pin's version next to it
   (`DW4_SDL3_VERSION`, 3.4.16) and stops the configure when the copy's
   `include/SDL3/SDL_version.h` names another version, because a local copy
   bypasses the pin's `URL_HASH`. (A different commit of the same version is
   not caught; the copy from a current PC build is the pinned one.) The SDL
   Java classes come from the same directory, so Java and native SDL always
   match.
3. The Gradle 8.9 distribution, the Android Gradle Plugin 8.7.3 and its
   dependencies in DW4_PC's own Gradle user home `%USERPROFILE%\.gradle-dw4`:
   one online run fills it ("Gradle user home" below).

Then, from the repository root (`gradlew-dw4` sets the Gradle user home,
`JAVA_HOME` if it is not set, and the project directory `android`):

    android\gradlew-dw4.cmd --offline --no-daemon assembleDebug

or in Git Bash `sh android/gradlew-dw4.sh --offline --no-daemon assembleDebug`.
`-Pdw4.nativeJobs=N` caps the native build's parallel compile and link jobs
(CMake job pools; unset: Ninja's default), `--max-workers=1` builds the two
ABIs one after the other (both used while another job needs the CPU: at most
N jobs at a time).
Never run `android\gradlew.bat` directly: it uses the shared
`%USERPROFILE%\.gradle` (`settings.gradle` warns when a build runs there).
`android/gradle.properties` sets `android.builder.sdkDownload=false`, so a
missing SDK package stops the build instead of being downloaded.

The APK is `android/app/build/outputs/apk/debug/app-debug.apk`, signed with
the SDK's debug key (`%USERPROFILE%\.android\debug.keystore`, which the build
only reads). Release signing comes in phase F: a key of this project
kept outside the repository (never another project's key); `.gitignore`
excludes `*.jks`, `*.keystore`, `*signing*.properties` and
`*keystore*.properties` (the usual `keystore.properties`).

The wrapper: `gradle-wrapper.jar` is Gradle 8.9's own (SHA-256 `49849512...`,
the same file as `lib/plugins/gradle-wrapper-main-8.9.jar` of the 8.9
distribution), and `gradle-wrapper.properties` carries the distribution's
published SHA-256 (`distributionSha256Sum`), which the wrapper checks when it
downloads the distribution (an installed one is not checked again); the
download into DW4_PC's Gradle user home on 13.11.2026 passed that check.

### Gradle user home

DW4_PC's Gradle runs use a Gradle user home of their own,
`%USERPROFILE%\.gradle-dw4` (`DW4_GRADLE_USER_HOME` names another one), never
the shared `%USERPROFILE%\.gradle`. `android/gradlew-dw4.cmd` and
`android/gradlew-dw4.sh` set `GRADLE_USER_HOME` to it and copy
`android/gradle/cache-retention.init.gradle` into its `init.d` before every
run. Nothing of the shared home is changed (no `init.d` there, no settings).

Why: Gradle 8 cleans the user home it runs in at most once a day, at the end
of a build, and deletes downloaded dependencies and wrapper distributions that
no build there used for 30 days. In the shared home, a DW4_PC build can delete
other projects' caches and theirs can delete DW4_PC's. The retention script
keeps them for 365 days in DW4_PC's home (checked on Gradle 8.9: 365 instead
of 30; created resources keep Gradle's 7 days), so a rarely built Android
project still builds offline. A build that does run in the shared home logs a
warning ("DW4_PC: this build runs in the shared Gradle user home ...").

**The one-time online step (done 13.11.2026, owner's approval):** a fresh
Gradle user home holds nothing, so its first `gradlew-dw4` run must be online:

    android\gradlew-dw4.cmd --no-daemon --info --continue assembleDebug

It downloads the Gradle 8.9 distribution (`gradle-8.9-bin.zip`, checked
against `distributionSha256Sum`, unpacked 151 MB; the wrapper deletes the zip
after unpacking) and the Android Gradle Plugin 8.7.3 with everything it needs
to assemble a debug APK: 322 files, 104.5 MB (125 jars, 181 POMs, 16 Gradle
module files; 104 from `dl.google.com`, 218 from `repo.maven.apache.org`),
among them `com.android.tools.build:gradle:8.7.3`, `builder`, `bundletool`,
`aapt2:8.7.3-12006047:windows`, `com.android.tools:sdk-common`/`sdklib`/
`repository`/`ddmlib` 31.7.3, Kotlin stdlib 1.9.20, netty, grpc, guava and
bouncycastle (`--info` logs each one as `Downloading <url>`; the list of
13.11. is `work/android_research/gradle_downloaded_urls.txt`, gitignored).
`--continue` lets independent tasks finish when one fails, so a failing
native build does not leave execution-time artifacts such as `aapt2`
undownloaded. The home then holds 488 MB (most of it the unpacked
distribution and Gradle's transformed jars). From then on every build is
`--offline`.

Checked on 13.11.2026 after the online step: with `android/.gradle`,
`android/build`, `android/app/build` and `android/app/.cxx` deleted,
`android\gradlew-dw4.cmd --offline --no-daemon --info assembleDebug` builds the
APK from nothing (39 tasks, both ABIs' native builds, 4 min 47 s at
below-normal priority) with no `Downloading` line and an unchanged
`modules-2`; `sh android/gradlew-dw4.sh --offline ...` in Git Bash (without
`JAVA_HOME`) finds the same home. The shared `%USERPROFILE%\.gradle` had no
file newer than the start of these runs. The warning "This version only
understands SDK XML versions up to 3 but an SDK XML file of version 4 was
encountered" comes from the newer SDK packages next to AGP 8.7.3 and does not
matter.

**Another project's cache (EmuSaveSync):** before DW4_PC had its own home,
phase A's first Gradle 8.9 run (creating the wrapper, 13.11.2026 06:05) ran in
the shared `%USERPROFILE%\.gradle`. Its cleanup deleted the AGP and AndroidX
jars from September 2026 that EmuSaveSync's Android build had left there
(older than 30 days; any other Gradle run would have done the same). Only
their metadata remained (`caches/modules-2` 2.5 MB); the `gradle-8.9-bin`
distribution in `wrapper/dists` and `caches/build-cache-1` stayed.
EmuSaveSync's next Android build therefore needs network once. DW4_PC never
runs Gradle in the shared home again, so it cannot happen through DW4_PC a
second time; whether the shared home gets a retention script of its own is
EmuSaveSync's owner's choice (the same `cache-retention.init.gradle` in
`%USERPROFILE%\.gradle\init.d` would do it).

### NDK compile check

    android\ndk-core-check.cmd [build dir]

configures the root `CMakeLists.txt` with the NDK for arm64-v8a and x86_64
(`-DDW4_ANDROID_APP=OFF`: no SDL) and builds `dw4_core`, `dw4_engine` and
`dw4_online` with `ninja -k 0`, so every compile error shows. It runs the
builds at below-normal priority and ends with exit code 1 and "errors" when
either ABI fails (it names the ABI), 0 otherwise.

## The emulator

AVD `dw4thor33` (its own; the AVD `emusync33` belongs to another project and
is never touched): API 33 `default` x86_64 image, upper screen 1920x1080 at
400 dpi, landscape, 4 GB RAM, hardware keyboard, no audio, no camera, and the
Thor's lower screen as a second display 1240x1080 at 330 dpi. `hw.ramSize` in
the AVD's `config.ini` is a plain number of MB (`4096`, guest `MemTotal`
4008536 kB): with `4096M` the emulator fell back to its 2048 MB minimum
("Increasing RAM size to 2048MB").

    emulator -avd dw4thor33 -no-window -no-audio -gpu swiftshader_indirect ^
             -no-snapshot -no-boot-anim -port 5580 -multidisplay 1,1240,1080,330,0

- `-multidisplay` takes one comma-separated argument (`index,width,height,
  dpi,flag`); with spaces the emulator refuses the command line.
- Flag 0 means "the emulator's default", not "no flags": the emulator writes
  `hw.display1.flag = 1739` into the AVD's `config.ini`, Android's virtual
  display flags PUBLIC | PRESENTATION | OWN_CONTENT_ONLY | SUPPORTS_TOUCH |
  ROTATES_WITH_CONTENT | SHOULD_SHOW_SYSTEM_DECORATIONS | TRUSTED. Inside the
  guest DisplayManager reports display 2 "Emulator 2D Display" 1240x1080 with
  `Display` flags 0xc8 (PRESENTATION, SHOULD_SHOW_SYSTEM_DECORATIONS,
  TRUSTED), state ON, listed in `DISPLAY_CATEGORY_PRESENTATION`. Phase E
  compares these with what `dumpsys display` reports for the Thor's lower
  screen. On the emulator it is a virtual display: `screencap -d` cannot
  capture it.
- `-gpu swiftshader_indirect` and `-gpu host` both work without a window;
  `host` reaches the NVIDIA GPU (GLES 3.1). The very first boot of the fresh
  AVD with `-gpu host` and `-multidisplay` ended (exit 127) right after the
  app started; two later boots (`host` alone, `host` with `-multidisplay`)
  ran the app and `screencap` without a problem.
- Stop it with `adb -s emulator-5580 emu kill`.

Install, start, look:

    adb -s emulator-5580 install -r android\app\build\outputs\apk\debug\app-debug.apk
    adb -s emulator-5580 shell am start -W -n org.dw4pc.game/.DW4Activity
    adb -s emulator-5580 shell "am start -n org.dw4pc.game/.DW4Activity --es dw4args '--seconds 10'"
    adb -s emulator-5580 exec-out screencap -p > probe.png
    adb -s emulator-5580 logcat -d -s DW4probe

The intent extra `dw4args` becomes the program's arguments
(`DW4Activity.getArguments`, split at white space); phase C uses it for the
det runs. `adb shell` joins its arguments into one shell line on the device,
so the whole `am start` goes in one pair of quotes and the extra's value in
its own inner quotes; `... --es dw4args "--seconds 10"` without them reaches
the app as just `--seconds`. The probe logs what it got (`arguments 2:
[--seconds] [10]`). The launcher activity is exported, so any app on the
device may start it, also with an extra: the game takes `dw4args` only in a
debuggable build (`FLAG_DEBUGGABLE`, every debug APK) and only while the
file `dw4args.allow` is in the app's internal folder, which no other app
can write (since 14.11.2026; a debug APK is what the owner plays on the
Thor until phase F):

    adb shell run-as org.dw4pc.game touch files/dw4args.allow
    adb shell run-as org.dw4pc.game rm files/dw4args.allow

Otherwise it ignores the extra and logs why ("dw4args ignored: not a
debuggable build" / "... no files/dw4args.allow"). Two arguments exist for
such runs: `--mute` (no audio device
this run: silent on a device nobody listens to, settings.ini and the
device's volume untouched) and `--env NAME=VALUE` (an environment variable
set before the run reads any: the test switches such as `DW4_GL_CHECK=1`,
`DW4_LANGUAGE=de` or a test run's Graphics options, which Android cannot
pass otherwise). Both work on the PC too. Checked on the emulator (13.11.2026): the debug APK logs
`arguments 2: [--seconds] [3]` and quits after 3 s; the same APK without
`android:debuggable` logs "dw4args ignored" and `arguments 0: none`.

## The device probe (phase A's app)

`src/android/probe_main.cpp` opens an OpenGL ES 3.0 context through SDL,
clears to `#2060C0` and draws a `#F0B020` triangle with a `#version 300 es`
shader from a vertex buffer, and logs to logcat (tag `DW4probe`):

- its arguments (`dw4args`), SDL's compiled and linked version, the Android
  SDK version, the ABI, clang and libc++;
- `SDL_GetBasePath`, `SDL_GetPrefPath`, the internal, external and cache
  paths, the external storage state;
- `GL_VERSION`, `GL_RENDERER`, the GLSL version, the uniform limits
  (`GL_MAX_VERTEX_UNIFORM_VECTORS` and others), highp/mediump precision, the
  watched extensions (`GL_EXT_depth_clamp`, ASTC, BPTC/S3TC/RGTC, `KHR_debug`,
  ...), every extension and every compressed texture format, EGL version and
  extensions;
- the displays from Android's DisplayManager with their flags, state, mode,
  dpi and the activity's own display (`DW4Activity.displayReport` through
  JNI), and display events (`DisplayListener`);
- game controllers SDL sees (name, vendor, product), key, button and touch
  events, the lifecycle events with the thread they arrive on, frame times
  every 5 s, and the first frame's centre and corner pixel.

### Results on the emulator (x86_64, 13.11.2026)

| | `-gpu swiftshader_indirect` | `-gpu host` |
| --- | --- | --- |
| `GL_VERSION` | OpenGL ES 3.0 (SwiftShader 4.0.0.1) | OpenGL ES 3.1 (4.5.0 NVIDIA 621.01) |
| `GL_RENDERER` | Android Emulator OpenGL ES Translator (Google SwiftShader) | ... (NVIDIA GeForce RTX 5070 Ti) |
| GLSL | ES 3.00 | ES 3.10 |
| `GL_MAX_VERTEX_UNIFORM_VECTORS` | 256 | 1024 |
| `GL_MAX_UNIFORM_BLOCK_SIZE` | 16384 | 65536 |
| fragment highp / mediump | 23 / 23 bit | 23 / 10 bit |
| `GL_EXT_depth_clamp` | no | no |
| ASTC (`KHR_..._astc_ldr`) | yes (28 formats) | yes (28 formats) |
| BC (BPTC, S3TC, RGTC) | no | no |
| ETC2/EAC | 10 formats | 10 formats |
| Frame time | 16.67 ms (60 Hz vsync) | 16.67 ms |

The screencap shows exactly the two test colours (the triangle 12.6 % of
the picture, the expected 12.5 %); after HOME and a restart (`LaunchState
HOT`) the picture is the same. The Thor's own numbers (Adreno 740) come from
the same probe once it may be installed there.

These first results came from a test APK assembled by hand with the SDK's
`aapt2`, `javac`, `d8`, `zipalign` and `apksigner` while Gradle could not yet
build offline. The Gradle-built debug APK (13.11.2026, `--offline` from a
clean build directory; 7.4 MB, `lib/arm64-v8a` and `lib/x86_64` each with
`libSDL3.so` and `libmain.so`, versionCode 460000, versionName 0.46.0,
`DEBUGGABLE`, signed with the SDK's debug key) gives the same: installed
fresh on `dw4thor33` (`-gpu swiftshader_indirect`, second display), Android
picks `primaryCpuAbi=x86_64`; it starts cold in 487 ms, the screencap has
exactly `#2060C0` 87.5 % and `#F0B020` 12.5 %, the same after HOME and a
restart (`HOT`); the log shows SDL compiled and linked 3.4.16, clang 21.0.0,
the same paths, GL limits and display list (display 2 1240x1080, flags
0xc8), all five lifecycle events on the SDL main thread, 16.65-16.97 ms per
frame, and `dw4args '--seconds 3'` arrives as `arguments 2: [--seconds] [3]`
and quits after 3.0 s.

Findings for phase B:

- `SDL_GetBasePath` is `./` and `SDL_GetPrefPath` the internal
  `/data/data/org.dw4pc.game/files/`; `SDL_GetAndroidExternalStoragePath` is
  `/storage/emulated/0/Android/data/org.dw4pc.game/files` (read and write).
  The game needs one central `app_root()`.
- The lifecycle events (`WILL_ENTER_BACKGROUND`, `DID_ENTER_BACKGROUND`,
  `LOW_MEMORY` on HOME, `WILL_ENTER_FOREGROUND`, `DID_ENTER_FOREGROUND`)
  reached an `SDL_AddEventWatch` callback, never `SDL_PollEvent`; the game's
  lifecycle handling goes into such a watch. The watch runs on the SDL
  thread: SDL 3.4.16 queues the events from the Java side and delivers them
  in `Android_PumpEvents` (also while it blocks in a pause) through
  `SDL_SendAppEvent`, which only calls the event watchers. The probe logs
  the thread: all five events arrived on the thread that runs `SDL_main`.
- The emulator's GLES translator draws nothing without an enabled vertex
  attribute array (an attribute-less `gl_VertexID` triangle stayed
  invisible); the renderer always draws from buffers anyway.
- 256 vertex uniform vectors (the GLES minimum, SwiftShader) are less than the
  renderer's bone palette needs today (about 295): the palette goes into a
  uniform buffer.
- No depth clamp and no BC formats on either GPU; ASTC LDR everywhere.
- The emulator's keyboard appears as a gamepad ("qwerty2", 0x0627:0x0001);
  BACK arrives as the key "AC Back".

## Phase B: the game on Android (13.11.2026)

### Build

- `cmake/android.cmake` turns the root `CMakeLists.txt`'s own game target
  `dw4_game` into the shared library `libmain.so` (`OUTPUT_NAME main`):
  SDL3 shared, `OpenGL::GL` an interface target on `GLESv3` and `EGL`, no
  tests (`BUILD_TESTING` off), the Android parts added after the target
  exists (`cmake_language(DEFER)`: `platform_android.cpp`, `android`, `log`).
  `game_main.cpp` includes `SDL_main.h` on Android only, so `main` is the
  `SDL_main` SDLActivity calls. Gradle builds only `dw4_game`
  (`targets 'dw4_game'`), with every third-party source from the PC build's
  `_deps` (`FETCHCONTENT_SOURCE_DIR_*`).
- The root `CMakeLists.txt` changes only under `if(ANDROID)` / `NOT ANDROID`:
  SDL static on the PC only, `find_package(OpenGL)` on the PC only, the
  `DW4_PC.exe` install rule on the PC only. The Windows libraries were
  already under `if(WIN32)`. Its `add_executable(dw4_game ...)` line is the
  PC's own, unchanged (branches that add a game source change only that
  line): `cmake/android.cmake` overrides `add_executable` for the one target
  `dw4_game`, which becomes `add_library(dw4_game SHARED ...)` there; every
  other target goes to the built-in command (`_add_executable`).
- Clang flags on `dw4_core`, `dw4_engine`, `dw4_online` and `dw4_game`:
  `-ffp-contract=off -fno-fast-math -fsigned-char` (no fused multiply-adds
  as MSVC x64, `char` signed as on x86). `dw4_game` also gets
  `-Wno-error=format-security` (the NDK's default error; the F1 menu's
  wrapped text helpers pass literal formats through a variadic lambda).
- Data Overflow builds unchanged (it reaches the paths through
  `program_dir()`, which is `app_root()` now).

### Floating-point environment

`FpEnvGuard` (`src/engine/online_detail.hpp`, `online.cpp`) replaces the
x86-only MXCSR guard at every simulation entry (`MxcsrGuard` stays as its
name for the call sites): MXCSR 0x1F80 on x86 / x86-64 as before; on
AArch64 FPCR with RMode round to nearest, FZ, FIZ, DN, AH, NEP and every
trap enable off, put back on exit. `Engine::mxcsr_repairs()` counts either;
`tests/online_tests.cpp` checks FPCR on AArch64 the way it checks MXCSR on
x86. Online play stays refused on Android (`online_cpu_refusal`) until the
simulation math is the MSL routines everywhere (a later phase).

### Paths and logs

`app_paths.hpp`: `app_root()` is the PC's program folder
(`SDL_GetBasePath`) and on Android the app's external files folder
`/storage/emulated/0/Android/data/org.dw4pc.game/files` (settings.ini,
saves/, mods/, data/). There is no fallback to the internal folder (it would
be a new first start without the player's settings, saves and disc copy):
when the external storage cannot be written at start-up, the game stops with
a message (`app_root_problem()`). `private_root()` is the same on the PC and the
internal `/data/data/org.dw4pc.game/files` on Android: the remembered online
login (`saves/online`), `online.log`, `controllers.log`. Every former
`SDL_GetBasePath` caller (`game_main.cpp`, `settings.cpp`) goes through
them; the paths are decoded as UTF-8 everywhere now (before, some callers
took SDL's UTF-8 text as the ANSI code page), and the start-up line
`data: PATH` prints the path as UTF-8 (`path::string()` throws on MSVC for
characters outside the ANSI code page; the game has no UTF-8 code page
manifest). On Android stdout and stderr go
to logcat line by line (tag `DW4`), so the game's own lines and the error
texts before their message boxes are there. stdout's lines are `I/DW4`;
stderr carries the engine's trace (`[67] stage stagis08 section ...`) and
SDL's echo of its own log next to the real problems, so its lines are
`I/DW4` too unless their words name a problem: `W/DW4` for "error",
"fail", "cannot", "could not", "unable", "warning", "refused", "invalid",
"missing", "not found", "unsupported", "corrupt", `E/DW4` for "fatal",
"crash", "abort", "exception" and the reason the game stops (`dw4_game:
...`, before its message box); `logcat *:W` shows the problems alone.

### The one window

`open_game_window()` opens the game's window with its OpenGL context, ImGui
and fonts once; whoever needs it first opens it and the game keeps it (on
Android SDL has exactly one window):

- the RVZ conversion's progress (before: its own SDL window and renderer),
- the LAN lobby of `--host` / `--join` (before: its own window "Digimon World
  4 PC - Online"); a test run draws it only with `DW4_RENDER=1`, hidden,
- Android's first-start import.

`status_frame()` draws one ImGui panel (lines, a progress bar, Cancel) in
it. Checked on the PC: a hidden LAN session (`--host` and `--join` on
127.0.0.1, `DW4_RENDER=1`, 120 frames) logs "the lobby drew in the game
window" on both, starts both players and ends with the same fingerprint.

### The first start and the disc

- The PC is unchanged: a message box (disc image or extracted folder), the
  file or folder dialog, an RVZ converted once into `data/`.
- Android: the message box offers the disc image only (SDL has no folder
  picker there), then `SDL_ShowOpenFileDialog` with the filter `*` (with
  `iso;gcm;rvz` Android's picker would hide `.rvz` / `.gcm`, which have no
  MIME type). The picker returns a `content://` URI; `import_disc` reads it
  with `SDL_IOFromFile` (SDL opens content URIs through the
  `ContentResolver`). Before anything is copied it reads the first 0x80
  bytes and stops on a file that is no GameCube ISO / RVZ (the magic at
  0x1C; an RVZ's disc header at 0x58) or not GDJEB2 disc 0 revision 0, and
  when the copy (plus an RVZ's converted ISO, plus 64 MB) does not fit into
  the free space. A provider that streams through a pipe (no seek back to 0,
  no size, e.g. a cloud provider: SDL makes such a descriptor a `FILE*`) gets
  the head written first and the rest after it; the progress then uses the
  RVZ's own size or a whole GameCube disc (1,459,978,240 bytes). It copies
  it into `data/dw4.iso` of the app's folder
  (progress and Cancel in the game window; an RVZ image is copied to
  `data/dw4.rvz.part`, converted with the PC's converter, and the copy
  removed), checks it like the PC (`check_game_data`: GDJEB2 revision 0,
  `ndmw.igb`; an unusable copy is removed again) and saves the path in
  settings.ini.
- Debug builds take `--import-disc PATH` (through the `dw4args` extra) for a
  test of the same import from a plain path, without the picker.
- Every start logs the game data's identity, `data: PATH, data_hash
  0123456789abcdef` (the value LAN and online peers compare).

### Renderer on OpenGL ES 3.0

`gl_renderer.cpp` keeps one set of shader sources (written for OpenGL 3.3
core) and one code path, with these differences on a GLES context
(`gl_is_es()`, the `GL_VERSION` string):

- `compile()` swaps the `#version 330 core` line for `#version 300 es` with
  `precision highp float; precision highp int; precision highp sampler2D;`
  (the alpha test compares 8-bit values; mediump is 10 bits on the NVIDIA
  host GPU). ImGui's backend gets `#version 300 es`.
- The skinning palette (64 matrices) is a 4 x 64 `RGBA32F` texture on unit 2
  that the vertex shader reads with `texelFetch` (exact floats), on the PC
  too. Neither plain uniforms (about 295 vectors, GLES guarantees 256) nor
  a uniform block works on the emulator: SwiftShader counts a block's
  members against the 256 vertex uniform vectors ("Vertex shader active
  uniforms exceed GL_MAX_VERTEX_UNIFORM_VECTORS (256)").
- Depth clamping: `GL_DEPTH_CLAMP` where the context has it (OpenGL 3.3, or
  `GL_EXT_depth_clamp` on GLES); otherwise the vertex shader clamps
  `gl_Position.z` into `[-w, w]` (`u_depth_clamp`), which only the
  orthographic 2D layers use. The per-vertex clamp equals GL_DEPTH_CLAMP's
  per-pixel one only for triangles wholly on one side of the near and of
  the far plane (orthographic: depth is linear across the triangle); a
  triangle that crosses one gets a different depth inside. That this never
  happens is an assumption that `DW4_CHECK_DEPTH_CLAMP=1` checks on any
  platform (a CPU check, no picture change): it counts the orthographic
  triangles that cross a plane and logs `renderer: depth clamp check: N of
  M orthographic triangles cross the near or far plane` at 1, 10, 100, ...
  (silent while none does) and the total when the renderer ends. On the PC
  the route `newgame_dng101b1` (6000 frames, rendered hidden with
  `DW4_RENDER=1`: the new game's menus and talk, the HUD, the dungeon) ends
  with `0 of 3442592 orthographic triangles crossed the near or far plane`
  (13.11.2026). The renderer logs which clamp it uses.
- `keep_picture`: GLES copies no multisampled framebuffer into a texture;
  the window's picture is blitted (resolved) into the kept texture's
  framebuffer. The PC keeps `glCopyTexSubImage2D`. Android asks for an
  RGBA8 surface (the blit needs the texture's format).
- No `GL_MULTISAMPLE` switches on GLES (multisampling follows the surface).
- The overhead name tags' depth probes (`read_depths`): ES 3.0 reads no
  depth buffer, so the window's depth around the probes is blitted into a
  depth texture of the window's own format and fetched there (since phase
  C, "Phase C leftovers on the player's side").
- `GL_SRC_ALPHA_SATURATE` is a source factor only on GLES; as a destination
  (never seen; GX has no such factor) it becomes `ONE` with one log line.
- The full-screen strips (movie, kept picture, sharpening) draw with
  attribute 0 enabled on a small buffer (the emulator's translator draws
  nothing without one).
- A failed shader link names the driver's reason.
- With main's texture streaming and Graphics options (merged 13.11.2026,
  main bb95abb5): the Sharpening option copies the window's picture; on
  GLES the multisampled window is resolved first by a blit (the same
  rectangle on both sides, as GLES wants it) into a window-sized texture
  and copied from there (`Impl::copy_window_es`), and `GL_DEPTH_CLAMP` is
  only switched where the context has it. Texture packs: BC7 needs OpenGL
  4.2 or `GL_ARB_texture_compression_bptc`, on GLES
  `GL_EXT_texture_compression_bptc`; BC1 / BC3
  `GL_EXT_texture_compression_s3tc` (`caps_`). A GPU without them (the
  emulator's) gets the pack's DDS files decoded to RGBA8 on the loading
  thread (the path `DW4_TEXTURE_CPU_DECODE=1` tests on the PC); the Thor's
  Adreno 740 has all three and takes them compressed (gles_check,
  14.11.2026; phase B's note that Adreno had neither was wrong). Pack
  textures go up into immutable storage (`glTexStorage2D`, core in GLES
  3.0) in bands of 256 KiB, as on the PC (before 14.11.2026 GLES uploaded
  each level whole). Anisotropic filtering follows
  `GL_EXT_texture_filter_anisotropic` (the menu offers only the levels the
  GPU has, none without it); sampler objects and the LOD bias in the
  shader are GLES 3.0 already.
- `DW4_GL_CHECK=1` (any platform; on Android `--env DW4_GL_CHECK=1` in
  `dw4args`): the GL errors raised in every presented frame and before
  the texture uploads (which clear the error flags to tell a refused
  upload) are logged (`renderer: GL error 0x... <where> (frame N)`, the
  first 20) and counted (`renderer: GL error check: N errors in M checks`
  when the renderer ends).
- `reset_context()` (a lost context) also forgets the samplers, the game's
  and the pack's textures, refused uploads, the upload in progress and the
  sharpening objects, and starts the loading thread again with the same
  configuration; the pack's textures come again as they are drawn
  (provoked on the emulator since phase C: `DW4_TEST_CONTEXT_LOSS`, "Phase
  C leftovers on the player's side").

### Lifecycle and input

- An `SDL_AddEventWatch` logs `lifecycle: entering the background / in the
  background / low memory / entering the foreground / in the foreground /
  terminating` (SDL hands these only to watches). In the background the game
  loop stands in `SDL_PollEvent` (SDL's `ANDROID_BLOCK_ON_PAUSE`) and SDL
  pauses the sound output.
- `SDL_EVENT_RENDER_DEVICE_RESET` (SDL made a new EGL context after the old
  one was lost): ImGui's OpenGL backend is shut down and started again, the
  renderer forgets every object name and makes its objects again
  (`GlRenderer::reset_context`), the prompt pictures are uploaded again. An
  Android-only event watch takes SDL's new context handle into the game
  window (SDL replaces it internally; run() destroys the live one at the
  end), and `status_frame` (the first start's import, the LAN lobby; no
  renderer yet) rebuilds ImGui's GL objects itself.
- A failed GL context closes the window again (`open_game_window`), so a
  later call does not hand out a window without a context.
- Android's Back key is trapped (`SDL_HINT_ANDROID_TRAP_BACK_BUTTON`) and
  opens and closes the F1 menu (`SDLK_AC_BACK`), as the controller's View /
  Back button does (the Thor's own controller through SDL's Android gamepad
  backend). A touch in the window's top right corner (x >= 90 %, y <= 12 %)
  toggles it too, unless it lands on an open menu's window (ImGui's hit
  test, `FindHoveredWindowEx`: then the tap is ImGui's); ImGui takes touches
  as SDL's mouse events, so the menus work by touch. Both are Android-only
  (`SDL_PLATFORM_ANDROID`): on the PC a keyboard's or mouse's browser Back
  key stays a bindable key and a Windows touch screen's tap stays a click.
  Every toggle logs `menu: open|closed (input)`.

### Results on the emulator (x86_64, `-gpu swiftshader_indirect`)

- First start from a clean app (`pm clear`): the message box, "Disc
  image..." opens Android's picker (DocumentsUI, logged `first start: the
  file picker`); a copy of the disc image pushed to `/sdcard/Download`
  (never the user's folder itself) picked there arrives as
  `content://com.android.providers.downloads.documents/document/msf%3A...`,
  1459978240 bytes, copied in about 25 s with the progress in the game
  window, checked, saved.
- `data: /storage/emulated/0/Android/data/org.dw4pc.game/files/data/dw4.iso,
  data_hash 1c693d45198dd6c1`: the same value the PC logs for the same
  image and for the Dolphin extraction.
- The renderer: `OpenGL ES 3.0 (OpenGL ES 3.0 SwiftShader 4.0.0.1) on
  Android Emulator OpenGL ES Translator (Google SwiftShader), depth clamp in
  the vertex shader`. The logos, the opening movie and the title screen
  (`stagis10`) draw; about 25-40 ms per frame on SwiftShader.
- Sound: `dumpsys audio` lists the app's AAudio player (usage MEDIA) as
  `started`; `dumpsys media.audio_flinger` shows its track active on the
  speaker output (48000 Hz, stereo, 16 bit, no underruns) with the output's
  frames written growing.
- The F1 menu: by touch (the corner), by Android's Back key
  (`input keyevent KEYCODE_BACK`), and by a controller: a virtual Xbox 360
  pad made through `/dev/uinput` (root on the emulator image;
  `work/android_research/emulator/vpad.c`) appears as "Xbox 360 Controller"
  045e:028e in SDL's Android gamepad backend, and its View button opens and
  closes the menu. A touch switches the menu's tabs.
- HOME and back (`LaunchState HOT`): the lifecycle lines in order, the game
  goes on drawing (no black picture, no new context needed).
- The emulator's own keyboard ("qwerty2") counts as a controller for SDL
  but has no View / Back button in its gamepad mapping; `input gamepad
  keyevent` comes from the virtual keyboard device, not a controller.

### Results on the emulator (`-gpu host`, the Gradle APK)

The debug APK from `gradlew-dw4 --offline assembleDebug` (versionCode
460000, versionName 0.46.0, `DEBUGGABLE`, `primaryCpuAbi=x86_64`), installed
over the earlier one (the imported disc and settings.ini stayed):
`renderer: OpenGL ES 3.1 (4.5.0 NVIDIA 621.01) on Android Emulator OpenGL ES
Translator (NVIDIA GeForce RTX 5070 Ti/PCIe/SSE2), depth clamp in the vertex
shader`, the same `data_hash 1c693d45198dd6c1`, the title screen, the
AAudio player `started`, the menu by touch and by the virtual pad's View
button, HOME and back `HOT` with the lifecycle lines. While the PC built at
the same time, the emulator's own System UI showed "isn't responding"
dialogs (not the game's).

### The PC after phase B

- `build/Clean` from scratch (MSVC, Ninja, Release) and `ctest`: 66 of 66.
- `work/dref/checks/det.sh` twice: `4004f056` / `396cb219` both runs, both
  times.
- The save state suite (a private copy of `st_all.sh`): 17 of 17 identical
  after loading.
- Screenshots of `newgame_dng101b1.txt` at frames 1500, 4300 and 5000
  (the title, the dungeon with the skinned partner and the HUD) are
  pixel-identical to main `463c7a5b`'s.
- The same disc logs the same `data_hash` as on Android, as an image and
  as the Dolphin extraction.

### The phase B review fixes (13.11.2026)

The review's findings are fixed on feat-android, with main bb95abb5
merged in (the texture streaming and the Graphics options; see the
renderer section). Checked:

- The PC: `build/Clean` from scratch and `ctest` 76 of 76 (main brought
  the texture and graphics tests); `det.sh` twice on an exe copy,
  `4004f056` / `396cb219` in both runs both times; the depth clamp check
  on `newgame_dng101b1` (renderer section): 0 crossing triangles.
- `git merge-tree` of feat-android with main, int-i1b and feat-remaster:
  no conflict (CMake keeps main's `add_executable(dw4_game ...)` line).
  With feat-l10n: exactly the five conflicts main itself has with it
  (CMakeLists.txt, gl_renderer.cpp/.hpp, texture_pack.cpp/.hpp: main's
  texture streaming against l10n's texture swaps); feat-android before
  the main merge (e934cd39) had none with it.
- The emulator (SwiftShader, the Gradle APK built offline, both ABIs with
  the new code): the title with the same `data_hash 1c693d45198dd6c1`;
  a tap in the top right corner opens and a second one closes the menu;
  with the menu's window dragged into the corner a tap there goes to
  ImGui (no toggle); the Back key opens and closes it.
- The import: `--import-disc` of a 1 MB file of zeros stops at once with
  "The chosen file is not a GameCube disc image (ISO) or RVZ image." (no
  copy). The import through a FIFO (a writer `cat`s the ISO into it: no
  seek, no size, as a streaming provider) copies 1,459,978,240 bytes in
  about 16 s, passes `check_game_data` and starts with the same
  `data_hash`; the title follows.
- The Sharpening option on GLES (`sharpening=high` in settings.ini): the
  title draws with it on SwiftShader and on `-gpu host` (NVIDIA through
  the translator), no GL error or crash in logcat; the logo's edge
  strength rises by a third against the unsharpened title. Neither
  emulator surface is multisampled (the menu says "now 0x" for msaa=4),
  so the multisampled resolve of `copy_window_es` and of `keep_picture`
  waits for the Thor.
- Home and back to the app: the lifecycle lines in order, the game draws
  on.

### Open for phase C (and later)

- Depth read-back for the name tags on GLES; a debug walk through every
  menu with `DW4_GL_CHECK=1` (the title, the new game, the dungeon, the F1
  menu and the Graphics options are checked, 14.11.2026). (Both done in
  phase C, "Phase C leftovers on the player's side".)
- ImGui's size on the Thor's 400 dpi screen (the menus are drawn at 1x),
  fullscreen without the status bar (immersive mode), a visible menu button
  for touch. (Done in phase C's UX lane: "Phase C: the player's side on
  Android".)
- Pausing the sound and saving settings when going to the background;
  `LOW_MEMORY` (the texture cache). (Done, the same section.)
- A context loss is handled but was not provoked on the emulator (nor the
  texture pack streaming after one). (Done in phase C, the same section.)
- On the Thor, in the game itself: a multisampled surface (msaa=4) with
  the kept picture and the Sharpening option, a texture pack (BC7 / BC1 /
  BC3 compressed on Adreno), the language switch (gles_check ran the same
  GL calls without the app, 14.11.2026); a full external storage (the
  import's space check) and an unmounted one (`app_root_problem`), neither
  provoked on the emulator. (Both provoked on the emulator in phase C, the
  same section; the rest needs the Thor in the game.)
- Measure on the Thor (correct, possibly expensive on a tiler):
  `keep_picture` blits the whole (with msaa=4 multisampled) window every
  frame, an extra resolve in the middle of the frame after which ImGui
  loads the tiles again (alternatives: keep only when a stall is likely,
  or every few frames); the skinning palette goes up with
  `glTexSubImage2D` per skinned item into the one texture earlier draws of
  the frame still read, which drivers answer by copying the texture
  (alternative: all of the frame's palettes in one larger texture, one
  upload, a row offset per draw). The Sharpening option adds a second
  resolve and copy on GLES. gles_check's figures on the Thor (1920 x 1080,
  4x multisampled, 60 rounds, each with `glFinish`): the kept picture's
  blit 0.53 ms, the sharpening's resolve, copy and pass 2.42 ms (without
  multisampling 0.49 / 1.10 ms); measured with the screen locked, so the
  GPU's clock may have been low.
- Texture packs on a GPU without BC formats: CPU-decoded RGBA8 needs 4x
  (BC7) to 8x (BC1) the memory of the compressed files (not the Thor's
  case: Adreno 740 takes BC7, BC1 and BC3, and ASTC LDR / HDR).
- The debug APK's native code is built without optimization (Gradle's
  Debug build type: `CMAKE_BUILD_TYPE=Debug`, no `-O`); performance on the
  Thor is measured with an optimized build (a release build type or
  `RelWithDebInfo` for the native part). (The profile build type since
  phase C: "Phase C leftovers on the player's side".)
- The F1 menu shows PC-only entries on Android too (Fullscreen (F11), the
  window size buttons, "Dump textures"); they go or change with phase C's
  menu work. (Fullscreen, the window sizes and the WinUSB GameCube adapter
  are PC only now; "Dump textures" stays, not checked on Android.)
- The RVZ import was not run on the emulator (no RVZ of the game); it uses
  the PC's converter on the copied file.

## The main merge of 14.11.2026 (main 12f6a1a8)

main 12f6a1a8 brought feat-l10n (game text catalogs laid out at display
time, the live language switch in F1 > Settings > Language, the German
pictures, the composed glyph cells of the font atlas, the l10n tools and
lint; docs/L10N.md) and the remaster tools' review rounds; the texture
streaming and the Graphics options were merged before (main bb95abb5).
The merge commit had no conflict: l10n's renderer changes are CPU side
(texture records keyed by the USA texture's hash and the language
picture's, the composed glyph cells painted into the font atlas's pixels
before an upload, a pack's font00 decoded to RGBA8 for it) and sit next to
phase B's GLES paths. The changes after it, for Android:

- **Language packs in the APK.** The PC build copies `data/lang/<code>/`
  next to the program (`cmake/copy_lang.cmake`, now PC only); Android has
  no program folder, so Gradle (`copyLangAssets`) puts the committed pack
  files (`language.ini`, `textures.tsv`; never a file named `game.*`) into
  the APK's assets `lang/<code>/`, and the game copies them into `lang/` of
  its folder (`Android/data/org.dw4pc.game/files/lang`) at every start
  where they differ (`install_bundled_language_packs`,
  `platform_android.cpp`; logged as `language: N language pack files from
  the APK into ...`). Without a `game.tsv` the German pack is "pictures
  only" (two of its three pictures come from the player's own disc; the
  travel dialog's is drawn from game.tsv's text). The game text catalog
  (`game.tsv`) is derived from the game's script: it is never committed and
  never in an APK; a player puts their own into `files/lang/de/`.
- **Putting a game.tsv on the owner's Thor (a local step).** `adb push`
  into `Android/data/org.dw4pc.game/files` creates folders owned by the
  shell (`drwxrws--- shell ext_data_rw`), which the app (no member of
  `ext_data_rw`) cannot enter: the folder must be the app's own. So:

      adb push local\lang\de\game.tsv /data/local/tmp/dw4c/game.tsv
      adb shell "run-as org.dw4pc.game mkdir -p /storage/emulated/0/Android/data/org.dw4pc.game/files/lang/de"
      adb shell "cat /data/local/tmp/dw4c/game.tsv | run-as org.dw4pc.game sh -c 'cat > /storage/emulated/0/Android/data/org.dw4pc.game/files/lang/de/game.tsv'"
      adb shell rm -rf /data/local/tmp/dw4c

  (a debug build only: `run-as` needs `debuggable`). A file pushed into a
  folder the app made is readable (`-rw-r--r-- shell`), as on the
  emulator. A game.tsv copied in while the game runs is read when the
  language is chosen again; the menu's entry follows it (before, it kept
  saying "pictures only" until the next start; checked on the PC the other
  way round: a game.tsv removed after the start, the switch at frame 3000,
  the entry then says "(pictures only, no game.tsv)").
- **The renderer on GLES**: immutable storage for pack textures and BC7
  through `GL_EXT_texture_compression_bptc` (renderer section above),
  `DW4_GL_CHECK=1`.
- **Test aids**: `--mute`, `--env NAME=VALUE` (the emulator section),
  `DW4_MENU_SECTION=Graphics` (the F1 Settings tab scrolled to its Graphics
  section, for menu screenshots); a test run's F1 menu now shows the
  Graphics options the renderer uses (the environment's), not settings.ini's
  (test runs never save settings.ini).
- `-Pdw4.nativeJobs=N` for Gradle (Building).

What the Graphics options and the languages do on GLES:

| Feature | OpenGL ES 3.0 (emulator, SwiftShader) | OpenGL ES 3.2 (Thor, Adreno 740) |
| --- | --- | --- |
| Texture quality, streaming | BC7 / BC1 decoded to RGBA8 on the loading thread, uploaded in bands | BC7 / BC1 / BC3 uploaded compressed in bands |
| Texture filtering | Bilinear and Trilinear only (no `GL_EXT_texture_filter_anisotropic`): the menu offers no anisotropic level and says "This GPU offers no anisotropic filtering"; a settings.ini value of `anisotropic16` shows and samples as Trilinear | up to Anisotropic 16x (maximum 16) |
| Mip LOD bias | the shader's bias, as on the PC | the same |
| Sharpening | the window resolved by a blit, copied, the pass drawn (`copy_window_es`) | the same, with a 4x multisampled window |
| Shadow quality | RGBA8 colour attachment up to 1024 x 1024 | the same (max texture size 16384) |
| Texture memory | the same budget | the same |
| Language | packs from the APK; German text and the composed glyph cells (ä ö ü ß „ “) | the same GL calls (gles_check); the game run is still open (below) |

Checked on 14.11.2026:

- `git merge-tree` of feat-android's head with int-i1b 446595aa,
  feat-remaster 150f42a2 and main / feat-l10n 12f6a1a8: no conflict.
- **The PC** (`build/Clean`, MSVC, after the merge and every change):
  `ctest` 78 of 78 (main brought the i18n and l10n lint tests);
  `det.sh` twice on an exe copy, `4004f056` / `396cb219` in both runs both
  times; the save state suite 17 of 17. Hidden UI runs (test runs, no
  window shown, no sound, `DW4_GL_CHECK=1`, the new game route
  `newgame_dng101b1`): the F1 Settings tab open while `DW4_L10N_SWITCH=
  2400:de,3900:en` switches the language (the Language entry shows
  "Deutsch (Fan-Übersetzung)" and the game text behind it is German, then
  English again); German from the start (the name entry's "Gib den Namen
  deines Digimon ein. Drücke zum Abschluss START." with ü); the Graphics
  section (`DW4_MENU_SECTION=Graphics`) with Anisotropic 16x, Sharpening
  High, Shadow quality Ultra and a synthetic texture pack (below) in the
  dungeon; 0 GL errors in every run.
- **The emulator** (`dw4thor33`, SwiftShader, OpenGL ES 3.0, the debug APK
  built offline at idle priority with `-Pdw4.nativeJobs=6`): installed over
  the earlier APK (the disc copy stayed), started with `dw4args '--mute
  --env DW4_GL_CHECK=1'`: `language: 2 language pack files from the APK
  into .../files/lang`, the title; the Back key opens F1, the Language
  entry offers "Deutsch (Fan-Übersetzung) (pictures only, no game.tsv)";
  chosen by touch: `language: de: no game.tsv ... 2 of 3 pictures ready`;
  with a game.tsv pushed into the app's folder and German chosen again:
  `game.tsv 4515 texts, 0 rejected ..., 3 of 3 pictures ready`; back to
  English, settings.ini `language=en`; logcat without errors. Two test
  runs through `dw4args` (`--smoke-test`, the route's input script and a
  card folder in the app's folder, `--screenshot` BMPs pulled with adb):
  German from the start shows the same German name entry as the PC; the
  Graphics run (`DW4_TEXTURE_PACK` = the synthetic pack, anisotropic16,
  sharpening high, shadow quality ultra, `DW4_MSAA=4`) draws the pack's
  checkerboards in the dungeon, the menu shows Trilinear and the note that
  the GPU has no anisotropic filtering; `renderer: GL error check: 0
  errors` in 4300 and 5023 checks.
- **The synthetic texture pack** (`work/android_research/synth_pack.py`,
  local): format 2, eight textures of the title and the dungeon by their
  hashes and sizes only (from a dump run's file names; the dumped pictures
  were deleted), each a checkerboard of 4 x 4 texel blocks in two colours
  per mip level (BC7 mode 6, BC1 and RGBA8 DDS files, scale 4 down to 1):
  no game pixels, so it may go onto a device.
- **The Thor**: `adb install -r` of the new debug APK (the imported
  `files/data/dw4.iso` and settings.ini stayed, settings.ini byte for byte).
  Its lock screen is secure (a PIN or pattern; `KeyguardServiceDelegate
  secure=true`), so the game was not started there (no UI check while the
  owner is away): the title, F1 by the Back key and the language switch in
  the game on the Thor are still open. Instead gles_check ran by `adb
  shell` (below), and the owner's game.tsv was put into the app's folder
  as the local step above (`files/lang/de/game.tsv`, the app's own; the
  game adds `language.ini` and `textures.tsv` at its next start), so F1 >
  Settings > Language offers German with its text.

### GLES checks without the app (gles_check)

`tools/android/gles_check.cpp` is a plain executable for `adb shell`: an
EGL pbuffer of 1920 x 1080 (4x multisampled by default) with an OpenGL ES 3
context, no Activity and no SDL, so it runs while the device is locked. It
reads the renderer's shader sources from a copy of `src/game/
gl_renderer.cpp`, compiles them with the renderer's GLES header and runs the
GL calls of the Graphics options and the texture streaming as the renderer
makes them, `glGetError` after every step (the file's header lists them).
Build and run (the folder `/data/local/tmp/dw4c` is removed afterwards):

    %NDK%\toolchains\llvm\prebuilt\windows-x86_64\bin\clang++ --target=aarch64-linux-android33 -std=c++20 -O1 -s ^
        -static-libstdc++ tools\android\gles_check.cpp -lEGL -lGLESv3 -o gles_check
    adb push gles_check src\game\gl_renderer.cpp /data/local/tmp/dw4c/
    adb shell "chmod 755 /data/local/tmp/dw4c/gles_check && /data/local/tmp/dw4c/gles_check /data/local/tmp/dw4c/gl_renderer.cpp 4"

On the owner's Thor (14.11.2026): `OpenGL ES 3.2 V@0676.53 ... Adreno (TM)
740`, GLSL ES 3.20, max texture size 16384, max samples 4, 256 vertex
uniform vectors (the GLES minimum: the palette texture was the right
choice), 118 extensions, 78 compressed formats; anisotropic filtering (max
16), BPTC, S3TC, ASTC LDR and HDR, `GL_EXT_depth_clamp`, `KHR_debug`. Every
program links (the scene 32 active uniforms, the movie, the kept picture,
the sharpening); immutable RGBA8 storage 2048 x 2048 with 12 levels filled
in 93 bands (21.3 MiB in 22 ms) and BC7 / BC1 / BC3 in 30 / 20 / 30 bands;
a draw through a trilinear sampler with 16x anisotropy; the RGBA32F
palette; the shadow map at 128, 512 and 1024 texels complete, drawn and
read back; the multisampled window resolved by a blit, copied, sharpened,
and blitted into the kept picture; `GL_DEPTH_CLAMP_EXT` on and off:
**PASSED, 0 failures** (with and without multisampling).

## Phase C: the player's side on Android (UX lane, 14.11.2026)

Branch `feat-android-cux` (from feat-android 0dc24e67). The owner's Thor was
locked with a PIN or pattern the whole time (`KeyguardServiceDelegate
secure=true`), so the game itself was not started there: everything below
that runs the game was checked on the emulator (an AVD of this lane's own,
`dw4cux33`: the Thor's 1920 x 1080 at 369 dpi, a navigation bar, no
hardware keyboard, 2 GB, `-gpu host`; `dw4thor33` belongs to the
determinism lane) and on the PC; on the Thor ran only `adb shell` reads and
gles_check.

### Full screen (immersive)

- The Android window opens with `SDL_WINDOW_FULLSCREEN` (SDL's immersive
  window style) and `DW4Activity` hides the status and navigation bars
  itself (`WindowInsetsController.hide(systemBars)`, transient bars by a
  swipe, `LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS`, `setDecorFitsSystemWindows
  (false)`), again on every focus gain, resume, configuration change and
  input device added, removed or changed (a controller reconnect, the
  Thor's controller style switched), and logs the result 0.6 s later:
  `window: 1920x1080, status bar hidden, navigation bar hidden, display 0
  at 60.00 Hz` (logcat tag `DW4`).
- F11, "Fullscreen (F11)" and the window size buttons are PC only.
- Emulator: hidden bars at the start (the emulator shows Android's
  one-time "Viewing full screen" note; the owner's Thor has it confirmed
  for good, `immersive_mode_confirmations=confirmed`, so it shows none), after F1 by the
  button, after HOME and back, after the app switcher and back, and after
  a virtual pad (uinput) came and went; the first start's disc dialog is a
  system dialog and shows the bars while it is open.

### The menus' size

- `[window] ui_scale` (percent; `auto`, the default, is not written into a
  file that has none): auto is the display's scale on Android
  (`SDL_GetWindowDisplayScale`, Android's `densityDpi / 160`: 369 dpi is
  **231 %**) and 1x on the PC, whose look stays as it was. A set percent
  (100-400) counts on both. F1 > Settings > Display > "Menu size": Automatic
  (N %), 100 ... 300 %.
- At a scale above 1: ImGui's style sizes scaled from the dark style
  (`ScaleAllSizes`), the font size (`FontScaleDpi`), ImGui's scalable font
  instead of its 13 px pixel font, the F1 window as large as the display
  allows (620 x 560 at 1x scaled, centred, scrolling inside), the notices'
  places, the first start's screen, the frame time graph and the fixed
  widths of the saves and cloud backup fields. The online screens over the
  title keep sizing themselves by the window's height like the game's own
  text (22 px at 1009 lines, 24 px on the Thor); their PushFont sizes are
  taken before the menus' scale.
- PC (`DW4_UI_SCALE=percent` shows a scale in a test run): at 1x the game's
  pictures are byte for byte the same as before (title, name entry,
  dungeon, Xbox prompts: same md5); the F1 menu's pictures differ only
  where the new "Menu size" line and the changed controllers note stand
  (and below, moved down; the scrollbar), the lines above them
  pixel-identical. The Thor's look on the PC (1920 x 1080, 231 %,
  `build/phc/ui_thor/`): every tab readable and inside the window.

### The menu button for touch

- Android only (`DW4_TOUCH_UI=1` draws it in a test run, also on the PC;
  other test runs leave it out of their screenshots): three lines on
  a dark rounded square, 44 px at 1x (102 px on the Thor), in the middle of
  the bar right of the 4:3 picture, so never over the HUD (which stays on
  the 4:3 picture, also in widescreen); in a window too narrow for that, in
  the top right corner.
- Shown for 4 s after any touch (and at the start), always while F1 is
  open, then it fades out in 0.5 s. A tap on it (or in the top right
  corner as before) opens and closes F1. Touch still works only on menus.
- The button is drawn over everything, so a tap on it is always the
  button's (`menu: closed (the touch menu button)`), also where the F1
  window reaches under it (a menu size of 300 % on the Thor's 1920 x 1080:
  the window spans x 30-1890, the button x 1734-1866; before, ImGui took
  that tap). A tap elsewhere in the top right corner is ImGui's when it
  lands on an open menu.

### The AYN Thor's controller

What the device reports (read only, 14.11.2026): the controller is USB HID
`2020:0111` "Odin Controller" (`Vendor_2020_Product_0111.kl`: `BTN_SOUTH`
`BUTTON_A`, `BTN_EAST` `BUTTON_B`, `BTN_NORTH` (0x133) `BUTTON_X`,
`BTN_WEST` (0x134) `BUTTON_Y`, `BTN_TL2` / `BTN_TR2` `BUTTON_L2` / `R2`,
`BTN_SELECT`, `BTN_START`, `BTN_MODE`, `KEY_BACK` `BACK`, `KEY_HOME`,
`KEY_APPSELECT`; axes X, Y, Z, RZ, GAS 0..32767, BRAKE, HAT). The key
layout is fixed, so the face buttons' codes change in the controller
itself. Its settings: `persist.sys.gamepad.type` **0** while the quick
settings tile "Controller style" says **Standard**; `trigger_input_mode`
0 with the tile "L2/R2 mode" **Analog**; "Smooth Display" (peak refresh
rate) 60; `global_gamepad_to_mouse_mode` 0 (the stick mouse off). Reviews
and AYN's settings: face buttons printed in the Nintendo layout (A right,
B bottom, X top, Y left; the Super Famicom colours A red, B yellow, X blue,
Y green), shoulders L1 / R1, analog triggers L2 / R2, Start and Select
above the lower screen; "Style: Xbox or Standard", "L2/R2 mode: Analog,
Digital or Both". A tester's report for the Odin's styles fits Standard =
the printed letters' codes and Xbox = positions.

- `PadFamily::thor` ("AYN Thor", settings id `thor`): 2020:0111 or a name
  with "Odin Controller". It reads like the Nintendo pads: by label (the
  default `nintendo_layout`) the printed A is the game's A and the menus'
  confirm, by position the bottom button is.
- `[gamepad] thor_style` = auto (the device's setting, read every second,
  so the quick settings tile takes effect while the game runs; Standard
  when it cannot be read), standard or xbox (F1 > Controls, "AYN Thor
  controller style", shown when a Thor pad is connected). Standard: SDL's
  SOUTH is the printed A (no swap by label); Xbox: SOUTH is the bottom
  button (swapped by label, as a Switch Pro). `persist.sys.gamepad.type` 1
  is taken as Xbox; only 0 = Standard was seen on the device.
- Pictures: round dark buttons with the letters in their colours, L1 / R1,
  L2 / R2, and START / SELECT as pills with the words (the printed icons
  of these two were not found: a look at the device decides; one table
  entry in `pad_family.cpp`).
- L2 / R2: SDL's Android backend makes the GAS / BRAKE axes the triggers
  (the generic mapping `lefttrigger:a4,righttrigger:a5`) and the L2 / R2
  keys joystick buttons 15 and 16 outside the gamepad mapping, so in
  "Both" a pull counts once already; in "Digital" (keys only) the triggers
  did nothing. The keys are now folded into the trigger axes
  (`with_digital_triggers`, `read_pad`): one pull in every mode, and a
  binding captured from them binds the trigger.
- The "ODIN Station Virtual Mouse" (the same USB id, `Sources: MOUSE`) is
  no joystick for SDL and takes no player slot; with the stick mouse off it
  sends nothing.
- Android's Back and Home keys no longer switch the prompts to the
  keyboard (on the emulator `input keyevent BACK` did; the Thor's Back
  button is the controller's).
- The Thor's controller has a Back button (`KEY_BACK`) and a Select button
  (`BTN_SELECT`); SDL's Android backend makes both its gamepad button
  BACK, so the Back button opened F1 only while no binding used BACK.
  `DW4Activity.dispatchKeyEvent` hands `KEYCODE_BACK` from the Thor's
  controller (2020:0111 or "Odin Controller", a gamepad source) to SDL as
  Android's Back key: F1 always (`menu: open (the Back key)`), whatever
  the bindings; Select stays the pad's BACK button (F1 while unbound, the
  prompts' SELECT pill). Other controllers are unchanged.

### Lifecycle

- Going to the background saves changed settings (`lifecycle: settings
  saved`); the game loop stands in `SDL_PollEvent` (the game pauses: SDL
  blocks on pause) and SDL pauses the AAudio output. Coming back the loop
  starts its clock again (`lifecycle: the game goes on`; before, up to four
  steps caught up). The PC does not pause on focus loss, nor does it now.
- Another window in front while the game stays on the screen (the
  notification shade, the quick settings with the Thor's "Controller style"
  tile, a dialog): SDLActivity pauses nothing then (Android 7+ multi-window:
  only `SDL_EVENT_WINDOW_FOCUS_LOST`), and the controller's input goes to
  that window. The game pauses meanwhile like the F1 menu's pause and goes
  quiet (`focus: another window is in front: the game pauses` / `focus:
  back: the game goes on`), never in a LAN or online session (their steps
  are every member's) or a test run. `[menu] pause_unfocused` (default
  true, written only when false) and F1 > Settings "Pause the game while
  another window is in front (notifications, quick settings)" (Android
  only) switch it off; whether a touch on the Thor's lower screen takes the
  focus (AYN's Focus Mode) is phase E0's question.
- `DW4Activity.onTrimMemory` passes only real memory warnings on, the
  `TRIM_MEMORY_RUNNING_*` levels (5, 10, 15: memory is short while the game
  runs). `TRIM_MEMORY_UI_HIDDEN` (20, every HOME) and the background levels
  `BACKGROUND`, `MODERATE`, `COMPLETE` (40, 60, 80) are logged and dropped:
  in the background the game loop stands in SDL's pause, so they would act
  only after coming back (letting go of the textures it is about to draw)
  and never while the low memory killer decides. The passed ones arrive as
  `SDL_EVENT_LOW_MEMORY`, on which the renderer lets go of the hashed
  textures no picture drew in the last two frames
  (`GlRenderer::trim_textures`, as the memory budget would).
- Emulator: HOME (`entering the background`, `settings saved` after F1 >
  VSync was switched, settings.ini `vsync=false`, `trim memory level 20
  ... not passed on`), back (`in the foreground`, `the game goes on`, the
  surface's frame rate asked again, bars hidden), `am send-trim-memory ...
  RUNNING_LOW` (`trim memory level 10 (passed on as low memory)`, `low
  memory: 0 textures (0 KiB) let go`: no pack loaded). The app switcher
  (Launcher3's live tile) neither paused the app nor took its focus there.
- HOME inside a long pass: SDL releases the EGL surface after waiting at
  most 0.5 s for the game's thread to reach its pause, and a stage load
  holds that thread longer (stagis09's load: about 1.1 s in the debug
  APK on the emulator). On the emulator (14.11.2026, HOME 0.4-1.0 s after
  `stage stagis09 section ...`, three of four rounds inside the load)
  `surfaceDestroyed` came while the load ran, the game logged `in the
  background` only after it, and one of the three met the released surface
  at its swap (`E/libEGL eglSwapBuffersWithDamageKHRImpl: error 300d
  (EGL_BAD_SURFACE)`); no crash, and the game drew on after coming back.
  Since then `DW4Activity.GameSurface.surfaceDestroyed` asks for the pause
  itself and waits (at most 3 s) until the game's lifecycle watch has seen
  `DID_ENTER_BACKGROUND` (`nativeGamePaused`, `platform_android.cpp`;
  SDL lets go of the context right after that) before SDL releases the
  surface; a wait of 100 ms or more is logged (`lifecycle: the surface
  waited N ms for the game to reach its pause`).

### Frame pacing

- Every game surface asks for the game's rate: `Surface.setFrameRate(60,
  FRAME_RATE_COMPATIBILITY_FIXED_SOURCE, CHANGE_FRAME_RATE_ALWAYS)` in
  `DW4Activity.GameSurface.surfaceChanged`, and the game passes a frame
  limit above 60 (up to 120) through JNI (`DW4Activity.setFrameRate`).
  SurfaceFlinger on the emulator: the game's layer votes `60.00 Hz
  ExactOrMultiple SeamedAndSeamless`. On the Thor the display runs mode 1
  (60 Hz) under the owner's Smooth Display 60 (`PRIORITY_USER_SETTING_PEAK
  _REFRESH_RATE` max 60); at 120 the vote picks a rate 60 divides.
- `DW4_FRAME_STATS=SECONDS` (any platform; `--env` on Android) logs
  presents per second, the percentiles of the present interval, of the
  pass's work up to the swap, of the swap and of the engine step, and a
  second line with the presents after 0, 1 and 2+ engine steps and how
  often a 0 and a 2+ follow each other (`frames: presents after 0 / 1 / 2+
  steps: ...`): the game steps at 59.94 Hz, a 60 Hz panel repeats a picture
  about every 16.7 s (a lone 0); 0 / 2+ pairs in a row are judder. Emulator
  (`-gpu host`, the debug APK), intro and title: 56.8 / 52.4 presents/s
  over 10 s (the second with a stage load), interval p50 16.7 ms, work p50
  4.4-4.8 ms, step p50 0.12 ms, p99 1-7 ms. On the Thor: open (locked).
- gles_check on the Thor (`gles_check gl_renderer.cpp SAMPLES FRAMES`, a
  frame stand-in of three blended full-screen quads and an overlay quad,
  GPU time by `GL_EXT_disjoint_timer_query`, wall time to `glFinish`;
  medians; the GPU clock rose from 401 to 615 MHz of 680 during the runs):

  | 1920 x 1080 | GPU, 4x MSAA | wall, 4x MSAA | GPU, no MSAA | wall, no MSAA |
  | --- | --- | --- | --- | --- |
  | plain frame | 0.37-0.52 ms | 2.3-3.2 ms | 0.27 ms | 1.0 ms |
  | + kept picture | +0.06-0.11 ms | +0.5-0.8 ms | +0.03 ms | +0.23 ms |
  | + Sharpening | +0.35-0.37 ms | +1.1-1.8 ms | +0.28 ms | +0.39 ms |
  | + both | +0.36-0.50 ms | +1.5-2.4 ms | +0.27 ms | +0.97 ms |

  The kept picture's blit in the middle of a multisampled frame costs the
  GPU about 0.1 ms, the Sharpening option about 0.4 ms (the wall times add
  the flushes a `glFinish` per frame forces): no reason to change a GLES
  default. Thermal (sysfs, read only, after the runs): CPU 33-39 °C, GPU
  30-33 °C, battery 20 °C, thermal status 0 (the thermal HAL reported "not
  ready").

### Text input

- ImGui's SDL backend starts SDL's text input while a field is active;
  without a hardware keyboard SDL shows Android's keyboard and pans the
  picture so the field stays above it (emulator: the Saves tab's cloud
  backup Account field, `mInputShown=true`, `inputType=0x3c001`).
- A password field opens it as a password field's: SDL's
  `TEXT_PASSWORD_HIDDEN` without autocorrection, and the keyboard's input
  restarted (`DW4Activity.restartTextInput`), since it keeps a field's kind
  otherwise (emulator: `inputType=0x20081`).
- Back on the keyboard stops SDL's text input while ImGui's field stays
  active; a tap on that field brings the keyboard back (`text input: the
  system keyboard again`). `DW4_TRACE_IME=1` logs the IME calls.
- The game's own name entry is the original's letter grid (controller);
  the port's on-screen keyboard of the online screens stays for the
  controller.

### Saves between the PC and Android

`DW4_CAMPAIGN_SAVE_NOW=NAME` (test runs with the campaign driver,
`DW4_CAMPAIGN_LOAD=1`: Call Digimon at the title with Card A's character)
makes a save due at once; the driver saves in the save room and keeps the
card as checkpoint NAME. The det route itself never saves, so the PC's save
came from r25c's k2 checkpoint (a PC campaign run), loaded and saved again
in a hidden PC run (`work/cux/rt_pc.sh`, private copies):

| step | where | loaded | saved |
| --- | --- | --- | --- |
| pc1 | PC (hidden test run) | r25c k2: level 7, phase 2, keys 0,1,2 at frame 3197 | frame 6279 |
| emu1 | x86_64 emulator, the debug APK (pc1's GCI in the app's folder, `--card-dir`) | the same at 3197 | 6279 |
| pc2 | PC from emu1's GCI pulled back | the same at 3197 | 6279 |

The three GCI files differ only in bytes 0x2a-0x2b (the header's
modification time). The same file is the player's `saves/savedata.dat`
("the same bytes as Dolphin's B2-GDJE-NDMWSaveData.gci"). The Thor's turn
(arm64) waits for an unlocked device: put the GCI into `files/rt/...`
through `run-as` as the game.tsv above and run the same `dw4args`.

### settings.ini on Android

The same file and defaults as on the PC (`Android/data/org.dw4pc.game/
files/settings.ini`, written on the way to the background and when F1
closes), with these differences:

| key | Android |
| --- | --- |
| `[window] width`, `height`, `fullscreen` | not used: always the whole screen |
| `[window] ui_scale` | auto = the display's scale (231 % on the Thor) |
| `[window] fps_limit` | 0 (the default): 60 Hz asked of the display; above 60: that rate up to 120 |
| `[window] msaa` | 4 (the PC's default; Adreno 740: 4x) |
| `[gamepad] thor_style` | auto (the Thor's Controller style) |
| `[menu] pause_unfocused` | true (written only when false): the game pauses while another window is in front |
| `[gamepad] gc_adapter` | not used (WinUSB, the PC's GameCube adapter reader) |

### Phase C leftovers on the player's side (lane C2, 14.11.2026)

The points of "Phase C status > Left" that need no device, on
`feat-android-cux` (the determinism leftovers are on `feat-android-cdet`,
"Determinism across platforms"). Checked on the emulator (`dw4cux33`,
`-gpu host`, headless) and the PC; the Thor only by `adb shell`
(gles_check), its lock screen still secure.

**Measuring: the profile build type.** `android\gradlew-dw4.cmd --offline
--no-daemon assembleProfile` builds `app-profile.apk`: the debug build
(the SDK's debug key, the same application id, so `adb install -r` puts it
over a debug APK and keeps the app's data; debuggable, so `run-as` and the
`dw4args` test runs work) with the native code built as `RelWithDebInfo`
(`-O2`; Gradle's Debug build type compiles it without optimization). Only
the Java side differs from a release build (CheckJNI; the game makes a few
JNI calls per frame at most). `-Pdw4.abis=x86_64` (or `arm64-v8a`) builds
one ABI, half the native build's time (x86_64's `libmain.so`: 9.9 MB in the
profile APK, 20.3 MB in the debug APK). Frame times on the emulator, debug
against profile APK of this head
(`DW4_FRAME_STATS=5`, `--mute`, the same 30 s from the start: the logos, the
opening movie, the title):

| 5 s period (two runs each) | debug APK | profile APK |
| --- | --- | --- |
| the logos | work p50 2.7-2.8 ms, step p50 0.03-0.10 ms | work p50 2.5-2.7 ms, step p50 0.02-0.04 ms |
| the title stage's load | longest frame 1,123-1,182 ms (47 steps/s) | longest frame 126-138 ms (59 steps/s) |
| the opening movie | work p50 6.8-8.3, p90 15.5-17.7 ms | work p50 5.0-5.3, p90 7.5-8.2 ms |

60 presents/s in every period but the load's (interval p50 16.7 ms; the
emulator's GPU, the NVIDIA card through the translator, sets the logos'
work). The optimized native code cuts the movie's frame work (the MPEG
decoder) by a third at the median and by half at the 90th percentile, and
the load's hitch to a ninth; the step itself is 0.02-0.03 ms. The
emulator's x86_64 cores (WHPX on the PC's CPU) say nothing about the Thor:
its numbers need the owner's unlocked Thor ("Left").

**The name tags' depth read-back on GLES.** `GlRenderer::read_depths`
reads every shown tag's three occlusion probes of a frame in one go. OpenGL
ES 3.0 has no depth read (`glReadPixels` takes no `GL_DEPTH_COMPONENT`), so
there the window's depth inside the probes' bounding rectangle is blitted
into a depth texture of the window's own depth and stencil format (a depth
blit wants both sides equal; `DEPTH24_STENCIL8` on the emulator and the
Thor; a multisampled window is resolved by the blit), one pass of points
(`kDepthProbeVertex` / `kDepthProbeFragment`) fetches it at the probes into
a row of an `R32UI` texture (the depth's float bits) and one `glReadPixels`
takes the row back: the same values the PC's per-pixel `glReadPixels`
gives. The pass keeps the frame's state (viewport, program, vertex array,
texture unit 3, the enables); a context where a step fails logs it once
(`renderer: no depth read-back on this OpenGL ES context (...)`) and keeps
the old behaviour (no tag hides). The PC reads each pixel as before.

- gles_check runs the same calls against known depths (scissored clears of
  0.25, 0.75 and 1; twelve probes as four tags spread over 1801 x 971
  pixels): every probe reads its depth on the emulator (`-gpu host`, no
  multisampled pbuffer there) and on the Thor (Adreno 740, 4x multisampled
  and without), PASSED. One read-back (blit, pass, read, which waits for the
  GPU) takes 0.33 ms median on the Thor at 4x MSAA (0.31 ms without), 0.38
  ms on the emulator.
- The game: `det_walk3` with `DW4_LABELS=all DW4_TRACE_LABELS=10` (the
  player's tag, traced every 10 frames, first 8,000 frames): the PC and the
  emulator mark the tag "behind the scene" at exactly the same frames
  (4350, 4530, 4850, 5100-5120, 6190-6210, 7570-7630, 7860-7870; before,
  GLES marked none), and the pictures at frame 7600 show the faded tag
  behind the same rock (`build/phe/shots/walk3_7600_pc_emu.png` and the
  zoom `walk3_7600_tag_zoom.png` of the worktree, not committed). 0 GL
  errors in 11,719 checks.

**A walk through every F1 page with `DW4_GL_CHECK=1`** (the emulator, test
runs from the start: Settings, Settings scrolled to Graphics, Controls,
Gameplay, Saves, Online, Cheats, Achievements, Debug): 0 GL errors in each
(700 checks per run). The first start's screens: `DW4_GL_CHECK=1` now also
checks the status screens (the copy and conversion of the first start, the
LAN lobby: `status screen: GL error check (SCREEN): N errors in M
frames`); the import of the disc image by `--import-disc` drew 362 frames
of the copy's progress with 0 GL errors (the message box and Android's
picker are system UI). A test run has no online lobby, so its Online tab is
empty; a normal start shows the refusal ("Online play is not available in
this build yet: the server's certificate is not set up."). The offline
menus said "this PC" on Android: "Offline achievements stay on this
device" and the texture pack's "this device uses its own" now
(`kThisMachine`, app_paths.hpp; the online screens' wording is phase D's).

**A lost OpenGL context.** `DW4_TEST_CONTEXT_LOSS=1` (Android, a test
switch through `--env`, refused online) destroys the game's EGL context
while the game is in the background (`eglDestroyContext` when SDL sends
`WILL_ENTER_FOREGROUND`, before it makes the context current again), so
SDL finds it lost, makes a new one and sends `SDL_EVENT_RENDER_DEVICE_RESET`:
the path of a device that loses its context. The emulator, the det route
`newgame_dng101b1` with the synthetic texture pack (`DW4_TEXTURE_PACK`,
format 2: BC7 / BC1 / RGBA8, decoded on the loading thread there), HOME at
24 s (the title) and back after 4 s: `lifecycle: test: the OpenGL context
destroyed in the background`, `lifecycle: the OpenGL context was
recreated`, `the game goes on`; the dungeon's four pack textures stream in
again (`uploads 4 (14.4 MiB)`, as in the run without HOME) and the pictures
at frames 4300, 5000 and 6000 are pixel-identical to that run's; 0 GL
errors. The renderer's GL and depth clamp checks now count across a reset
(one total at the end; before, the lost context's Impl printed its own).

**"Don't keep activities"** (the emulator only: `settings put global
always_finish_activities 1` and a reboot, which Android needs to take it;
deleted and rebooted again afterwards): HOME ends the activity, the game
gets `terminating` and its main returns (`GL error check: 0 errors`); coming
back, SDLActivity starts a new process (`Process org.dw4pc.game has died`,
`Start proc`) and the game starts again from the logos, twice in a row,
without a crash. What was not saved is gone, as in any game Android ends.

**The external storage.**

- Full: a file of 2,500 MB (`fallocate`) left 1,378 MB free;
  `--import-disc` of the disc copy stopped before copying with "There is
  not enough free space for the game data: 1456 MB needed, 1378 MB free."
  (logged as `E/DW4` and shown in the message box). With the file removed
  the same import copied the 1,459,978,240 bytes in 8 s and the game
  started with the same `data_hash 1c693d45198dd6c1`.
- Unmounted: `sm unmount emulated;0` (mounted again afterwards): the start
  stops with `app_root_problem()`'s message ("The game's folder
  (Android/data/org.dw4pc.game/files) cannot be written: the device's
  shared storage is not available. ...").

**Mods, PNG texture packs and offline achievements on Android** (the
player's folders in `Android/data/org.dw4pc.game/files`; folders made by
`adb shell` need `chmod 777`, files `666`, or the app cannot enter them):

- A synthetic mod (`mods/c2synth/mod.ini` and one added text file, no game
  data): `mod: C2 synthetic check 1.0 (1 files)` and the data identity
  follows it (`data_hash 28d995fccac778ee` instead of `1c693d45198dd6c1`,
  as on the PC).
- A synthetic PNG pack of format 1 (`mods/textures/synthpng_<hash>.png`:
  red and white checkerboards at twice the size of the synthetic DDS pack's
  eight textures, `work/c2/png_pack.py`, local) replaces the logo in a
  normal start.
- Offline achievements: the det_walk3 run with `DW4_ACHIEVEMENTS=1` writes
  a profile byte for byte the PC's (186 bytes, md5 d5797351..., in-game
  minutes 4); the F1 Achievements tab of a normal start shows the profile
  in `files/saves/profile.dat`.

**Seen on the way (not changed):** at the start the touch menu button
stands at the top centre for the first seconds (the logos), then in the
bar right of the picture (the reason is not known yet). `adb push` / `adb shell` stdin is not binary safe for `run-as ... cat
> file` (a PNG arrived as 5 bytes): `adb exec-in` is.

### Open after this lane

- On the Thor in the game (it was locked): the immersive window and its
  log line, the menus at 231 %, the touch button, the Thor family's
  prompts and the controller style (also switched while the game runs:
  one getevent capture of the printed A in each style confirms the
  mapping), L2 / R2 in Digital and Both, HOME / app switcher, the keyboard,
  `DW4_FRAME_STATS` in the intro, the title and the det route
  (`newgame_dng101b1`, `--mute`), the save round trip on arm64.
- START / SELECT: the printed icons.
- The online screens on the Thor at their window-height size (24 px text):
  the owner's judgement.

## Compile status (NDK clang 21, phase A)

- `dw4_core`, `dw4_engine`, `dw4_online`: no errors for arm64-v8a and x86_64.
  The only blocker was `src/engine/online.cpp:19`, the unguarded
  `<xmmintrin.h>` of the MXCSR guard (x86 only). It is now included on x86
  only; elsewhere the guard does nothing until phase B's platform-neutral
  `FpEnvGuard` (FPCR on AArch64) replaces it. The PC build is unchanged
  (`tests/online_tests.cpp` guards its MXCSR test the same way).
- Two warnings remain: `src/sndf.cpp:249` (missing field initializer) and
  `src/engine/overflow/director.cpp:678` (switch without every enumerator;
  Data Overflow, paused).
- The game layer (`src/game`, the 48 sources of `dw4_game`) passes
  `-fsyntax-only` for arm64-v8a against the SDL3, imgui, pl_mpeg, stb and zstd
  headers without errors (warnings: `achievements.cpp:499` switch,
  `game_main.cpp:1091` deprecated `u8path`, `game_main.cpp:3935` non-literal
  format string). Linking and running it on GLES is phase B's work: OpenGL
  3.3 core to GLES 3.0 (shader header, uniform buffer for the palette, depth
  clamp in the vertex shader, depth read-back, `keep_picture` through an FBO,
  no `GL_MULTISAMPLE` toggles), the Windows-only libraries behind `WIN32`, the
  paths, the one-window rule (RVZ progress and lobby as ImGui), the
  lifecycle.

## The Thor

Nothing is installed on the Thor without the owner's approval. The first
steps are read-only and their output goes to `work/android_research/thor/`
(gitignored):

    adb devices -l
    adb shell dumpsys display
    adb shell dumpsys input
    adb shell getprop ro.build.fingerprint   (and the other ro.build.*, ro.product.*)

Connecting it:

1. Settings > About phone (or About device): tap "Build number" seven times;
   Android reports that developer options are on.
2. Settings > System > Developer options: turn on "USB debugging". Connect
   the Thor to the PC by USB-C and accept the "Allow USB debugging?" prompt on
   the Thor (with "Always allow from this computer" if wanted).
3. Without a cable: Developer options > "Wireless debugging" (PC and Thor in
   the same Wi-Fi), "Pair device with pairing code", then on the PC
   `adb pair <ip>:<pairing port>` with the six-digit code and
   `adb connect <ip>:<port>` (the port shown on the Wireless debugging page).
4. If Windows shows no ADB device over USB, the Google USB driver
   (`extras;google;usb_driver`) may be needed; wireless debugging works
   without it.

After that (with approval): install the probe APK for the real Adreno 740
limits, then phase E0's focus spike (does a touch on the lower screen take the
controller focus away in AYN's Focus Mode "Auto").

### Read-only results on the user's Thor (13.11.2026)

`adb devices -l`: `product:kalama model:AYN_Thor device:kalama`. Windows
showed the Thor as `USB\VID_18D1&PID_4EE1` (MTP only) until USB debugging was
on; Windows' own driver was enough for ADB, no Google USB driver needed.
Output in `work/android_research/thor/` (gitignored; it holds the serial).

| | |
| --- | --- |
| System | Android 13 (API 33), build `Thor_V1.0.0.377_20260206`, security patch 2024-01-01, locale `de-DE` |
| SoC / GPU | QCS8550 (`kalama`, the Snapdragon 8 Gen 2 family), Adreno; `ro.opengles.version` 196610 = **OpenGL ES 3.2** (+ AEP), Vulkan 1.3 (`vulkan.level=1`) |
| ABIs | `arm64-v8a` (32-bit `armeabi-v7a` too) |
| Frame pacing | `vendor.perf.framepacing.enable=1` |

**Displays** (`dumpsys display`, `cmd display get-displays`):

| id | name | logical size | panel | refresh | flags | touch |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | "Integrierter Bildschirm" (built-in) | 1920 x 1080 (rotation 1) | 1080 x 1920 portrait, 369 dpi, HDR (420 nits) | mode 1 = 60 Hz now, mode 2 = 120 Hz (default mode) | SECURE, TRUSTED | `fts_ts`, viewport INTERNAL |
| **4** | "Screen-2" | 1240 x 1080 (rotation 1) | 1080 x 1240 portrait, 369 dpi | mode 4 = 60 Hz now, mode 3 = 120 Hz (default mode) | SECURE, **PRESENTATION**, TRUSTED | `fts_ts_3`, viewport EXTERNAL |

- The lower screen's display id is **4** on the Thor (2 on the AVD
  `dw4thor33`). Phase E must find it through
  `DisplayManager.getDisplays(DISPLAY_CATEGORY_PRESENTATION)` and the
  `DisplayListener`, never by a fixed id.
- Both panels are portrait panels the system rotates to landscape; the
  logical sizes above are what an app sees.
- Both run at 60 Hz in this state although 120 Hz modes exist; the game runs
  at its own 60 (frame pacing, gate 2) and should ask for the 60 Hz mode
  (`preferredDisplayModeId`) rather than rely on the current one.

**Input devices** (`dumpsys input`):

- **"Odin Controller"**: USB HID `2020:0111` (bus 3), classes KEYBOARD |
  GAMEPAD | JOYSTICK | EXTERNAL, controller number 1, key layout
  `/system/usr/keylayout/Vendor_2020_Product_0111.kl`:
  - buttons: `BUTTON_A` 0x130, `BUTTON_B` 0x131, `BUTTON_C` 0x132,
    `BUTTON_X` 0x133, `BUTTON_Y` 0x134, `BUTTON_Z` 0x135, `L1`/`R1`
    0x136/0x137, `L2`/`R2` 0x138/0x139, `SELECT` 0x13a, `START` 0x13b,
    `MODE` 0x13c, `THUMBL`/`THUMBR` 0x13d/0x13e; `DPAD_UP/DOWN/LEFT/RIGHT`
    0x220-0x223; `HOME` 0x66, `BACK` 0x9e, `APP_SWITCH` 0x244, volume keys.
  - axes reported: `X`/`Y` (left stick), `Z`/`RZ` (right stick), raw
    -32767..32767, flat 15; `GAS`/`BRAKE` (analog triggers, raw 0..32767);
    `HAT_X`/`HAT_Y` (D-pad, -1..1). The layout also names RX/RY/THROTTLE/
    RUDDER/WHEEL, which this controller does not report.
  - So the triggers come both as buttons (L2/R2) and as analog axes; SDL's
    Android joystick path sees a standard gamepad. Phase C added the "AYN
    Thor" pad family (`PadFamily::thor`; "The AYN Thor's controller" in
    phase C's section).
- **"ODIN Station Virtual Mouse"**: the same USB id (version 0x00c8),
  CURSOR_ICON | EXTERNAL: AYN's mouse emulation. The game ignores mouse
  input from it.
- Touch: `fts_ts` (upper screen, display 0) and `fts_ts_3` (lower screen,
  display 4, EXTERNAL viewport).
- `gpio-keys`: volume, HOME, FOCUS, CAMERA; `pmic_pwrkey`, `pmic_resin`,
  `hall_switch` (lid/cover), headset jack.

### Phase B's APK on the Thor (13.11.2026, installed with the owner's approval)

The debug APK of phase B (`org.dw4pc.game` 0.46.0, arm64-v8a) on the real
device:

- First start: the message box and Android's picker; the owner picked the
  image from the SD card (`content://com.android.externalstorage.documents/
  document/<card>%3ARoms%2Fgc%2F...iso`), copied in 88 s into
  `files/data/dw4.iso`, `data_hash 1c693d45198dd6c1` (the PC's value).
- `renderer: OpenGL ES 3.2 V@0676.53 ... on Adreno (TM) 740, depth clamp
  GL_EXT_depth_clamp`: the real extension, so the vertex shader's emulation
  is not used here. Low latency audio is on.
- The intro and the title run without an error in logcat.
- The built-in controller: every button works in the game and in the menus,
  and the Back button opens and closes the menu (`menu: open (the
  controller's View / Back button)`). The title shows the Xbox-style menu
  glyph for START.
- The window is 1920 x 970: the status bar and the navigation bar stay
  visible. Phase C makes the game window immersive full screen.

The GL limits and compressed formats came from gles_check on 14.11.2026
("GLES checks without the app"): ASTC LDR and HDR, BPTC and S3TC, 16x
anisotropy, 256 vertex uniform vectors. Still unknown: the Focus Mode
behaviour of the lower screen's touch (phase E0).

On 14.11.2026 the new debug APK (main 12f6a1a8 merged) was installed with
`adb install -r` (data and settings.ini kept); the lock screen is secure,
so the game was not started while the owner was away ("The main merge of
14.11.2026").
