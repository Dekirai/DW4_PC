# DW4_PC on Android (AYN Thor)

The Android port runs the same source tree as the PC version: one CMake root,
one version number, one protocol. `android/` is only a Gradle wrapper whose
native build is the repository's own `CMakeLists.txt` (its `if(ANDROID)` path,
`cmake/android.cmake`). The target device is the AYN Thor (Base/Pro/Max:
Snapdragon 8 Gen 2, Adreno 740, Android 13); other devices are not supported
officially.

Status: **phase A** (toolchain and an empty app). The app's native library is
still the device probe `src/android/probe_main.cpp`, not the game; the game
moves to Android in phase B.

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
| Gradle | 8.9 (wrapper in `android/gradle/wrapper`) | |
| Android Gradle Plugin | 8.7.3 | needs Gradle 8.9 or newer |
| Platform / build tools | `platforms;android-35`, `build-tools;34.0.0` | compileSdk and targetSdk 35 |
| Emulator | 36.6.11, WHPX | |
| Emulator image | `system-images;android-33;default;x86_64` rev 2 | AOSP with SystemUI, GLES up to 3.1 |
| SDL | 3.4.16 (`fa2c02bb`, `cmake/third_party.cmake`) | `libSDL3.so` on Android, static on the PC |

libc++ 21 has `std::from_chars` for `float` and `double` (the code uses it in
`src/lua4.cpp`, `src/engine/engine.cpp` and `src/game/online/json.cpp`): a
test built for arm64-v8a and x86_64 links with `-static-libstdc++` and parses
decimal and hexadecimal floats correctly on the x86_64 emulator.

ABIs: `arm64-v8a` (the Thor) and `x86_64` (the emulator), `c++_static`.

## Layout

| Path | Contents |
| --- | --- |
| `cmake/third_party.cmake` | The FetchContent sources (SDL3, imgui, pl_mpeg, stb, zstd) for the PC and the Android build: one list |
| `cmake/android.cmake` | The Android path of the root `CMakeLists.txt`: SDL3 shared, the library `main` |
| `src/android/probe_main.cpp` | Phase A's device probe (the library `main`) |
| `android/` | Gradle wrapper project: `settings.gradle`, `build.gradle`, `app/build.gradle`, manifest, resources, `DW4Activity.java` |
| `android/ndk-core-check.cmd` | Compiles `dw4_core`, `dw4_engine`, `dw4_online` with the NDK for both ABIs |

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

   Nothing in that directory is ever written.
3. The Android Gradle Plugin 8.7.3 and its dependencies in the Gradle cache
   (`%USERPROFILE%\.gradle\caches\modules-2`).

Then:

    set JAVA_HOME=C:\Program Files\Java\jdk-17
    android\gradlew.bat -p android --offline --no-daemon assembleDebug

The APK is `android/app/build/outputs/apk/debug/app-debug.apk`, signed with
the SDK's debug key. Release signing comes in phase F: a key of this project
kept outside the repository (never another project's key); `.gitignore`
excludes `*.jks`, `*.keystore` and `*signing*.properties`.

**Gradle offline, state 13.11.2026:** the Gradle cache does not hold AGP
8.7.3's jars, so `--offline` stops at the plugin classpath (124 artifacts:
`com.android.tools.build:gradle:8.7.3`, `builder`, `bundletool`, `aapt2-proto`,
`com.android.tools:sdk-common`/`sdklib`/`repository`/`ddmlib` 31.7.3, netty,
grpc, guava, bouncycastle and others; the execution adds at least
`com.android.tools.build:aapt2:8.7.3-12006047:windows` and the D8/R8 jar).
Gradle 8 deletes cached dependencies that were not used for 30 days; the
cache filled in September 2026 was removed this way at the first Gradle 8.9
run of phase A. One online Gradle run (owner's approval) refills it; after
that the build runs offline again, as long as some Android build uses the
cache at least once a month.

Phase A therefore checked the Gradle side in parts: the native libraries came
from the same root CMake configuration Gradle uses (NDK toolchain file,
`ANDROID_PLATFORM=android-33`, `ANDROID_STL=c++_static`, `FETCHCONTENT_SOURCE_
DIR_SDL3`), the version code and the SDL Java copy from `app/build.gradle`'s
own Groovy code on Gradle 8.9, and the test APK was assembled with the SDK's
`aapt2`, `javac`, `d8`, `zipalign` and `apksigner` (debug key) from exactly
the manifest, resources and Java sources in `android/app`.

### NDK compile check

    android\ndk-core-check.cmd [build dir]

configures the root `CMakeLists.txt` with the NDK for arm64-v8a and x86_64
(`-DDW4_ANDROID_APP=OFF`: no SDL) and builds `dw4_core`, `dw4_engine` and
`dw4_online` with `ninja -k 0`, so every compile error shows.

## The emulator

AVD `dw4thor33` (its own; the AVD `emusync33` belongs to another project and
is never touched): API 33 `default` x86_64 image, upper screen 1920x1080 at
400 dpi, landscape, 4 GB RAM, hardware keyboard, no audio, no camera, and the
Thor's lower screen as a second display 1240x1080 at 330 dpi.

    emulator -avd dw4thor33 -no-window -no-audio -gpu swiftshader_indirect ^
             -no-snapshot -no-boot-anim -port 5580 -multidisplay 1,1240,1080,330,0

- `-multidisplay` takes one comma-separated argument (`index,width,height,
  dpi,flag`); with spaces the emulator refuses the command line.
- Flag 0 already gives a presentation display: DisplayManager reports display
  2 "Emulator 2D Display" 1240x1080 with `FLAG_PRESENTATION` (flags 0xc8:
  PRESENTATION, SHOULD_SHOW_SYSTEM_DECORATIONS, TRUSTED), state ON, listed in
  `DISPLAY_CATEGORY_PRESENTATION`. It is a virtual display: `screencap -d`
  cannot capture it.
- `-gpu swiftshader_indirect` and `-gpu host` both work without a window;
  `host` reaches the NVIDIA GPU (GLES 3.1). The very first boot of the fresh
  AVD with `-gpu host` and `-multidisplay` ended (exit 127) right after the
  app started; two later boots (`host` alone, `host` with `-multidisplay`)
  ran the app and `screencap` without a problem.
- Stop it with `adb -s emulator-5580 emu kill`.

Install, start, look:

    adb -s emulator-5580 install -r app-debug.apk
    adb -s emulator-5580 shell am start -W -n org.dw4pc.game/.DW4Activity
    adb -s emulator-5580 shell am start -n org.dw4pc.game/.DW4Activity --es dw4args "--seconds 10"
    adb -s emulator-5580 exec-out screencap -p > probe.png
    adb -s emulator-5580 logcat -d -s DW4probe

The intent extra `dw4args` becomes the program's arguments
(`DW4Activity.getArguments`); phase C uses it for the det runs.

## The device probe (phase A's app)

`src/android/probe_main.cpp` opens an OpenGL ES 3.0 context through SDL,
clears to `#2060C0` and draws a `#F0B020` triangle with a `#version 300 es`
shader from a vertex buffer, and logs to logcat (tag `DW4probe`):

- SDL's compiled and linked version, the Android SDK version, the ABI, clang
  and libc++;
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
  events, the lifecycle events, frame times every 5 s, and the first frame's
  centre and corner pixel.

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

Findings for phase B:

- `SDL_GetBasePath` is `./` and `SDL_GetPrefPath` the internal
  `/data/data/org.dw4pc.game/files/`; `SDL_GetAndroidExternalStoragePath` is
  `/storage/emulated/0/Android/data/org.dw4pc.game/files` (read and write).
  The game needs one central `app_root()`.
- The lifecycle events (`WILL_ENTER_BACKGROUND`, `DID_ENTER_BACKGROUND`,
  `LOW_MEMORY` on HOME, `WILL_ENTER_FOREGROUND`, `DID_ENTER_FOREGROUND`)
  reached an `SDL_AddEventWatch` callback, never `SDL_PollEvent`; the game's
  lifecycle handling goes into such a watch.
- The emulator's GLES translator draws nothing without an enabled vertex
  attribute array (an attribute-less `gl_VertexID` triangle stayed
  invisible); the renderer always draws from buffers anyway.
- 256 vertex uniform vectors (the GLES minimum, SwiftShader) are less than the
  renderer's bone palette needs today (about 295): the palette goes into a
  uniform buffer.
- No depth clamp and no BC formats on either GPU; ASTC LDR everywhere.
- The emulator's keyboard appears as a gamepad ("qwerty2", 0x0627:0x0001);
  BACK arrives as the key "AC Back".

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
