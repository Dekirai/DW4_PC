cmake_minimum_required(VERSION 3.24)
project(DW4_PC VERSION 0.46.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# No path of the build machine in the binaries: __FILE__ of every target from
# here on, the fetched dependencies' included, is relative
# (cmake/trim_build_paths.cmake).
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/trim_build_paths.cmake)
dw4_trim_build_paths()
option(DW4_BUILD_APP "Build the native game runtime (SDL3/OpenGL/ImGui)" ${WIN32})
# One self-contained EXE: the C++ runtime linked in, so a clean Windows needs
# no Visual C++ redistributable (applies to the dependencies built here too).
if(MSVC)
  option(DW4_STATIC_RUNTIME "Link the C++ runtime statically" ON)
  if(DW4_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
endif()
include(CTest)

# Portable core: original file formats (IGB, packages, textures, meshes),
# Lua 4 bytecode, Enbaya animation, pad and motion primitives.
add_library(dw4_core
  src/assets.cpp src/igb.cpp src/igb_document.cpp src/resources.cpp src/packages.cpp src/scripts.cpp
  src/lua4.cpp src/pad.cpp src/textures.cpp src/meshes.cpp src/skinning.cpp src/animation_math.cpp
  src/enbaya.cpp src/motion.cpp src/motion_timer.cpp src/adx.cpp src/axfx.cpp src/axmix.cpp src/save_data.cpp src/card_store.cpp src/save_import.cpp src/sfp.cpp src/sndf.cpp src/sfd.cpp src/ini.cpp src/disc.cpp src/disc_timing.cpp src/card_timing.cpp src/msl_math.cpp
  src/i18n/game_text.cpp src/i18n/language_pack.cpp)
target_include_directories(dw4_core PUBLIC include)
# Recovered scalar operations round separately; explicit std::fma remains
# fused. Preserve IEEE zero-frame infinity/NaN behavior.
if(MSVC)
  set_source_files_properties(src/motion.cpp src/motion_timer.cpp src/animation_math.cpp src/msl_math.cpp PROPERTIES COMPILE_OPTIONS /fp:strict)
  target_compile_options(dw4_core PRIVATE /W4 /permissive- /utf-8)
  add_compile_options(/utf-8)
else()
  set_source_files_properties(src/motion.cpp src/motion_timer.cpp src/animation_math.cpp src/msl_math.cpp PROPERTIES COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off")
  target_compile_options(dw4_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Native engine runtime: reconstructed original managers, scene graph and
# stage flow on top of dw4_core. No SDL/ImGui dependency.
add_library(dw4_engine src/engine/math.cpp src/engine/gekko_math.cpp src/engine/igb_view.cpp src/engine/scene_graph.cpp
  src/engine/engine.cpp src/engine/lua_host.cpp src/engine/managers_core.cpp src/engine/model_ctrl.cpp
  src/engine/actor.cpp src/engine/collision.cpp src/engine/ui.cpp src/engine/text_l10n.cpp src/engine/title.cpp src/engine/load_intf.cpp
  src/engine/mat_ctrl.cpp src/engine/generater.cpp src/engine/randomizer.cpp src/engine/combat.cpp src/engine/status.cpp src/engine/save_game.cpp src/engine/save_util.cpp src/engine/save_menu.cpp src/engine/menu_text.cpp src/engine/shadow.cpp src/engine/particles.cpp src/engine/water.cpp src/engine/change_pos.cpp
  src/engine/hud.cpp src/engine/items.cpp src/engine/panel.cpp
  src/engine/subwnd.cpp src/engine/subwnd_folder.cpp src/engine/subwnd_skill.cpp
  src/engine/subwnd_shop.cpp src/engine/save_state.cpp src/engine/port_events.cpp src/engine/online.cpp
  src/engine/online_cameras.cpp src/engine/online_exits.cpp src/engine/online_scenes.cpp src/engine/online_story.cpp
  src/engine/online_departure.cpp
  src/engine/tick_state.cpp src/engine/tick_state_managers.cpp)
# Data Overflow, the survival mode's run engine rules (docs/SURVIVAL.md 13.4):
# the hooks' values and the native run scripts; inert without OverflowRules.
target_sources(dw4_engine PRIVATE src/engine/overflow/rules.cpp src/engine/overflow/player_hit.cpp
  src/engine/overflow/field_start.cpp src/engine/overflow/donors.cpp src/engine/overflow/director.cpp
  src/engine/overflow/spawner.cpp src/engine/overflow/abilities.cpp src/engine/overflow/offers.cpp
  src/engine/overflow/human_bot.cpp src/engine/overflow/guardians.cpp src/engine/overflow/gear.cpp)
target_link_libraries(dw4_engine PUBLIC dw4_core)
# The Gekko arithmetic rounds only where the original rounds (no contraction).
if(MSVC)
  target_compile_options(dw4_engine PRIVATE /W3 /permissive- /utf-8)
  set_source_files_properties(src/engine/gekko_math.cpp PROPERTIES COMPILE_OPTIONS /fp:strict)
else()
  set_source_files_properties(src/engine/gekko_math.cpp PROPERTIES COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off")
endif()

# Seamless online co-op client (docs/SEAMLESS.md): the DW4R2 relay protocol,
# pads on the wire, pacing, the WinHTTP worker with certificate pinning, the
# DPAPI token, the session state machine and the online gates. No SDL / ImGui.
# DW4_ONLINE_DEV: a development build may talk to a loopback test server
# (DW4_ONLINE_SERVER / DW4_ONLINE_PIN); release builds never do.
option(DW4_ONLINE_DEV "Online client development build (loopback test server allowed)" OFF)
add_library(dw4_online STATIC
  src/game/online/cpu_gate.cpp src/game/online/env_check.cpp src/game/online/pad_wire.cpp src/game/online/wire.cpp
  src/game/online/pacing.cpp src/game/online/net_stats.cpp src/game/online/net_log.cpp src/game/online/host_priority.cpp src/game/online/server_config.cpp src/game/online/http.cpp src/game/online/token_store.cpp
  src/game/online/json.cpp src/game/online/room_settings.cpp src/game/online/api.cpp src/game/online/transport.cpp
  src/game/online/session.cpp src/game/online/session_go.cpp src/game/online/presence.cpp src/game/online/engine_adapter.cpp src/game/online/lobby_api.cpp
  src/game/online/lobby.cpp src/game/online/achievement_api.cpp src/game/online/title_driver.cpp
  src/game/online/prompt_wire.cpp src/game/online/session_prompt.cpp)
target_include_directories(dw4_online PUBLIC src/game)
target_link_libraries(dw4_online PUBLIC dw4_engine)
if(DW4_ONLINE_DEV)
  target_compile_definitions(dw4_online PUBLIC DW4_ONLINE_DEV=1)
endif()
if(MSVC)
  target_compile_options(dw4_online PRIVATE /W4 /permissive- /utf-8)
  if(DW4_STATIC_RUNTIME)
    target_compile_definitions(dw4_online PRIVATE DW4_CRT_STATIC=1)
  endif()
endif()
if(WIN32)
  target_link_libraries(dw4_online PUBLIC winhttp crypt32 bcrypt ws2_32)
endif()
# The online character (docs/SEAMLESS.md 9): the virtual card, the uploads
# with If-Match, the automatic saves; the cloud backup of the local save.
target_sources(dw4_online PRIVATE src/game/online/character.cpp src/game/online/cloud_save.cpp)
# The SPKI pins of the DW4Online server's certificate (docs/SEAMLESS.md 10,
# server/DW4Online/deploy/DEPLOY.md 6.1), a build step: never a key in the
# repository. DW4_ONLINE_PINS="<in use>,<spare>" or, when it is empty, the
# pins in DW4_ONLINE_PINS_FILE (default local/online_pins.txt, gitignored;
# deploy/print-pins.ps1 -Out writes it: one pin per line, # comments). A pin
# is the base64 SHA-256 of the certificate's SubjectPublicKeyInfo (44
# characters). Without pins the client refuses the server (fail closed);
# DW4_ONLINE_REQUIRE_PINS=ON makes that a configure error (release packages).
set(DW4_ONLINE_PINS "" CACHE STRING "SPKI pins of the DW4Online server: in use[,spare] (DEPLOY.md 6.1)")
set(DW4_ONLINE_PINS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/local/online_pins.txt" CACHE FILEPATH "SPKI pins of the DW4Online server, one per line (DEPLOY.md 6.1)")
option(DW4_ONLINE_REQUIRE_PINS "Fail the configure step without DW4Online server pins (release packages)" OFF)
set(DW4_PIN_LIST "")
set(dw4_pins_source "DW4_ONLINE_PINS")
if(NOT DW4_ONLINE_PINS STREQUAL "")
  string(REPLACE "," ";" DW4_PIN_LIST "${DW4_ONLINE_PINS}")
elseif(EXISTS "${DW4_ONLINE_PINS_FILE}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DW4_ONLINE_PINS_FILE}")
  file(STRINGS "${DW4_ONLINE_PINS_FILE}" dw4_pin_lines)
  foreach(line IN LISTS dw4_pin_lines)
    if(NOT line MATCHES "^[ \t]*#")
      list(APPEND DW4_PIN_LIST "${line}")
    endif()
  endforeach()
  set(dw4_pins_source "${DW4_ONLINE_PINS_FILE}")
endif()
list(TRANSFORM DW4_PIN_LIST STRIP)
list(REMOVE_ITEM DW4_PIN_LIST "")
list(LENGTH DW4_PIN_LIST dw4_pin_count)
if(dw4_pin_count GREATER 2)
  message(FATAL_ERROR "DW4Online server pins: at most two (in use, spare), ${dw4_pin_count} in ${dw4_pins_source}")
endif()
foreach(pin IN LISTS DW4_PIN_LIST)
  string(LENGTH "${pin}" pin_length)
  if(NOT pin MATCHES "^[A-Za-z0-9+/]+=$" OR NOT pin_length EQUAL 44)
    message(FATAL_ERROR "DW4Online server pin '${pin}' (${dw4_pins_source}) is not a base64 SHA-256 (44 characters ending in '=')")
  endif()
endforeach()
if(dw4_pin_count GREATER 0)
  list(GET DW4_PIN_LIST 0 dw4_pin_in_use)
  target_compile_definitions(dw4_online PRIVATE "DW4_SERVER_PIN_IN_USE=\"${dw4_pin_in_use}\"")
  if(dw4_pin_count GREATER 1)
    list(GET DW4_PIN_LIST 1 dw4_pin_spare)
    target_compile_definitions(dw4_online PRIVATE "DW4_SERVER_PIN_SPARE=\"${dw4_pin_spare}\"")
  endif()
  message(STATUS "DW4Online server pins: ${dw4_pin_count} (${dw4_pins_source})")
elseif(DW4_ONLINE_REQUIRE_PINS)
  message(FATAL_ERROR "DW4Online server pins missing: set DW4_ONLINE_PINS or write ${DW4_ONLINE_PINS_FILE} (server/DW4Online/deploy/DEPLOY.md 6.1)")
else()
  message(STATUS "DW4Online server pins: none (the online client refuses the server until they are set, DEPLOY.md 6.1)")
endif()
string(REPLACE ";" "," DW4_PINS_JOINED "${DW4_PIN_LIST}")

# Android (docs/ANDROID.md): android/ is only the Gradle wrapper around this
# file. cmake/android.cmake turns the game below into the app's native
# library libmain.so with SDL3 as libSDL3.so (DW4_BUILD_APP on, no tests;
# Gradle builds only dw4_game). Its DW4_ANDROID_APP=OFF builds only the
# portable libraries above (android/ndk-core-check.cmd).
if(ANDROID)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/android.cmake)
endif()

# Development tools: IGB inspection and logical resource catalog.
add_executable(dw4_inspect tools/dw4_inspect.cpp)
target_link_libraries(dw4_inspect PRIVATE dw4_core)
add_executable(dw4_catalog tools/dw4_catalog.cpp)
target_link_libraries(dw4_catalog PRIVATE dw4_core)
# Data Overflow's O1a spikes (docs/SURVIVAL_O1A.md): a headless run engine probe.
add_executable(dw4_overflow_probe tools/overflow_probe.cpp)
target_link_libraries(dw4_overflow_probe PRIVATE dw4_engine)
if(WIN32)
  target_link_libraries(dw4_overflow_probe PRIVATE psapi)
endif()
# Round I1a's first tasks (docs/SEAMLESS.md 19.25): headless engine probes for
# the world without rooms (settings at a restore, the stage redirect, HOME and
# the game over). A development tool, never part of the game.
add_executable(dw4_world_probe tools/world_probe.cpp)
target_link_libraries(dw4_world_probe PRIVATE dw4_engine dw4_online)
# Translation tools (docs/L10N.md 4): the English corpus for translators and
# the lint of a translated game.tsv with the engine's font layout.
add_library(dw4_l10n_tools STATIC tools/l10n/l10n.cpp)
target_link_libraries(dw4_l10n_tools PUBLIC dw4_engine)
add_executable(dw4_l10n tools/l10n/dw4_l10n.cpp)
target_link_libraries(dw4_l10n PRIVATE dw4_l10n_tools)

if(BUILD_TESTING)
  foreach(suite assets igb igb_document resources packages scripts lua4 textures meshes skinning animation_math enbaya adx save_data save_import card_store sfp sfd ini disc)
    add_executable(dw4_${suite}_tests tests/${suite}_tests.cpp)
    target_link_libraries(dw4_${suite}_tests PRIVATE dw4_core)
    add_test(NAME ${suite} COMMAND dw4_${suite}_tests)
  endforeach()
  add_executable(dw4_scene_tests tests/scene_tests.cpp)
  target_link_libraries(dw4_scene_tests PRIVATE dw4_engine)
  add_test(NAME scene COMMAND dw4_scene_tests)
  # (pad: the PADINPUT redirect of Engine::pads too; online_cameras: the
  # seamless co-op cameras and exits, docs/SEAMLESS.md 4 and 5.2; i18n: the
  # translation catalog, language packs and font additions, docs/L10N.md)
  foreach(suite gekko_math messenger pad online_cameras i18n)
    add_executable(dw4_${suite}_tests tests/${suite}_tests.cpp)
    target_link_libraries(dw4_${suite}_tests PRIVATE dw4_engine)
    add_test(NAME ${suite} COMMAND dw4_${suite}_tests)
  endforeach()
  # The translation lint on synthetic fonts and texts (tools/l10n).
  add_executable(dw4_l10n_lint_tests tests/l10n_lint_tests.cpp)
  target_link_libraries(dw4_l10n_lint_tests PRIVATE dw4_l10n_tools)
  add_test(NAME l10n_lint COMMAND dw4_l10n_lint_tests)
  # Randomizer: seeds, pools, the loading zone table, the save bond (sidecar) and spoiler.
  add_executable(dw4_randomizer_tests tests/randomizer_tests.cpp src/game/randomizer_save.cpp)
  target_link_libraries(dw4_randomizer_tests PRIVATE dw4_engine)
  add_test(NAME randomizer COMMAND dw4_randomizer_tests)
  # Data Overflow's host side (round O1a lane A, docs/SURVIVAL.md 13.2-13.3): the run request, the story's
  # generator across a run engine, the test variables a run keeps out, its cards and setup, the story switch.
  add_executable(dw4_overflow_host_tests tests/overflow_host_tests.cpp src/game/overflow_run.cpp src/game/presence_data.cpp)
  target_link_libraries(dw4_overflow_host_tests PRIVATE dw4_engine)
  add_test(NAME overflow_host COMMAND dw4_overflow_host_tests)
  # Data Overflow's meta file (round O1b lane F, docs/SURVIVAL.md 8; T7 overflow_profile): the
  # format, the .bak and the corruption cases, the Plug Lab, the loadout, Sim Bits and the Data Records.
  add_executable(dw4_overflow_profile_tests tests/overflow_profile_tests.cpp src/game/overflow_profile.cpp)
  target_link_libraries(dw4_overflow_profile_tests PRIVATE dw4_engine)
  add_test(NAME overflow_profile COMMAND dw4_overflow_profile_tests)
  add_executable(dw4_interpolation_tests tests/interpolation_tests.cpp src/game/interpolation.cpp)
  target_link_libraries(dw4_interpolation_tests PRIVATE dw4_engine)
  add_test(NAME interpolation COMMAND dw4_interpolation_tests)
  # Presence features (no SDL / ImGui): the engine's observe-only port events,
  # the Discord IPC framing with a fake discord-ipc pipe server, the area /
  # form tables, the name tags' projection and the achievement rules.
  add_executable(dw4_port_events_tests tests/port_events_tests.cpp)
  target_link_libraries(dw4_port_events_tests PRIVATE dw4_engine)
  add_test(NAME port_events COMMAND dw4_port_events_tests)
  add_executable(dw4_presence_tests tests/presence_tests.cpp src/game/discord_ipc.cpp src/game/discord_presence.cpp src/game/presence_data.cpp)
  target_include_directories(dw4_presence_tests PRIVATE src/game)
  add_test(NAME presence COMMAND dw4_presence_tests "${CMAKE_CURRENT_SOURCE_DIR}/data/online/areas.json")
  add_executable(dw4_labels_tests tests/labels_tests.cpp src/game/overhead_labels.cpp src/game/presence_data.cpp)
  target_include_directories(dw4_labels_tests PRIVATE src/game)
  target_link_libraries(dw4_labels_tests PRIVATE dw4_engine)
  add_test(NAME labels COMMAND dw4_labels_tests)
  # The ghost figures of the hub stages (docs/SEAMLESS.md 12): placement,
  # draw list order, camera fade; with the game data also a form's figure.
  add_executable(dw4_ghosts_tests tests/ghosts_tests.cpp src/game/ghost_figures.cpp src/game/overhead_labels.cpp src/game/presence_data.cpp)
  target_include_directories(dw4_ghosts_tests PRIVATE src/game)
  target_link_libraries(dw4_ghosts_tests PRIVATE dw4_engine)
  add_test(NAME ghosts COMMAND dw4_ghosts_tests "${DW4_TEST_DATA}")
  # Data Overflow's screens (round O1b lane D, docs/SURVIVAL.md 3, 9, 14.1 T9):
  # every string against the game font's characters (with the game data;
  # without, against the glyph list of 9.1), the NPC's rules, the talk, the
  # dialog's state machine, the screens' 2D items and the NPC actor.
  add_executable(dw4_overflow_screens_tests tests/overflow_screens_tests.cpp src/game/overflow_text.cpp src/game/sim_ui.cpp
    src/game/presentation_actor.cpp src/game/overflow_lobby.cpp src/game/overflow_screens.cpp src/game/overflow_run.cpp
    src/game/ghost_figures.cpp src/game/overhead_labels.cpp src/game/presence_data.cpp src/game/overflow_profile.cpp)
  target_include_directories(dw4_overflow_screens_tests PRIVATE src/game)
  target_link_libraries(dw4_overflow_screens_tests PRIVATE dw4_engine)
  add_test(NAME overflow_screens COMMAND dw4_overflow_screens_tests "${DW4_TEST_DATA}")
  # Seamless online co-op, the engine side. With the game data (a disc image
  # or an extracted folder, never in the repository) it also plays membership
  # scenarios in real stages; without, those are skipped.
  set(DW4_TEST_DATA "" CACHE PATH "Game data for the online engine scenarios (optional)")
  add_executable(dw4_online_tests tests/online_tests.cpp)
  target_link_libraries(dw4_online_tests PRIVATE dw4_engine dw4_online) # (dw4_online: the GameEngineLink checks)
  target_compile_definitions(dw4_online_tests PRIVATE DW4_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME online COMMAND dw4_online_tests "${DW4_TEST_DATA}")
  # Round J's tick state (docs/SEAMLESS.md 6.13.3): idempotence, divergence
  # trials and the rebuild from the visit's load in real stages (game data).
  add_executable(dw4_tick_state_tests tests/tick_state_tests.cpp)
  target_link_libraries(dw4_tick_state_tests PRIVATE dw4_engine)
  add_test(NAME tick_state COMMAND dw4_tick_state_tests "${DW4_TEST_DATA}")
  # The online test driver's script language (src/game/online_script.hpp, tools/online_test).
  add_executable(dw4_online_script_tests tests/online_script_tests.cpp src/game/online_script.cpp src/game/test_path.cpp src/game/campaign.cpp)
  target_include_directories(dw4_online_script_tests PRIVATE src/game)
  target_link_libraries(dw4_online_script_tests PRIVATE dw4_engine)
  target_compile_definitions(dw4_online_script_tests PRIVATE DW4_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME online_script COMMAND dw4_online_script_tests)
  add_executable(dw4_achievements_tests tests/achievements_tests.cpp src/game/achievements.cpp)
  target_include_directories(dw4_achievements_tests PRIVATE src/game)
  target_link_libraries(dw4_achievements_tests PRIVATE dw4_engine)
  add_test(NAME achievements COMMAND dw4_achievements_tests "${CMAKE_CURRENT_SOURCE_DIR}/data/online/achievements.json")
  # The online account's achievements (docs/SEAMLESS.md 11) against a fake request queue.
  add_executable(dw4_online_achievements_tests tests/online_achievements_tests.cpp src/game/online_achievements.cpp src/game/achievements.cpp)
  target_include_directories(dw4_online_achievements_tests PRIVATE src/game)
  target_link_libraries(dw4_online_achievements_tests PRIVATE dw4_online)
  add_test(NAME online_achievements COMMAND dw4_online_achievements_tests)
  # Data Overflow (docs/SURVIVAL.md 13.4, 14.1): the run engine's rules
  # without data, and with the game data T10 (the native player hit script
  # and field start against the originals) and the run's hooks.
  add_executable(dw4_overflow_rules_tests tests/overflow_rules_tests.cpp)
  target_link_libraries(dw4_overflow_rules_tests PRIVATE dw4_engine)
  add_test(NAME overflow_rules COMMAND dw4_overflow_rules_tests)
  add_executable(dw4_overflow_scripts_tests tests/overflow_scripts_tests.cpp)
  target_link_libraries(dw4_overflow_scripts_tests PRIVATE dw4_engine)
  add_test(NAME overflow_scripts COMMAND dw4_overflow_scripts_tests "${DW4_TEST_DATA}")
  add_executable(dw4_overflow_director_tests tests/overflow_director_tests.cpp)
  target_link_libraries(dw4_overflow_director_tests PRIVATE dw4_engine)
  add_test(NAME overflow_offers COMMAND dw4_overflow_director_tests offers)
  add_test(NAME overflow_rng COMMAND dw4_overflow_director_tests rng)
  add_test(NAME overflow_guardians COMMAND dw4_overflow_director_tests guardians)
  add_test(NAME overflow_gear COMMAND dw4_overflow_director_tests gear "${DW4_TEST_DATA}")
  add_test(NAME overflow_finale COMMAND dw4_overflow_director_tests finale "${DW4_TEST_DATA}")
  add_test(NAME overflow_spawn COMMAND dw4_overflow_director_tests spawn "${DW4_TEST_DATA}")
  add_test(NAME overflow_gap COMMAND dw4_overflow_director_tests gap "${DW4_TEST_DATA}")
  add_test(NAME overflow_human COMMAND dw4_overflow_director_tests human "${DW4_TEST_DATA}")
  add_test(NAME overflow_poison COMMAND dw4_overflow_director_tests poison "${DW4_TEST_DATA}")
  add_test(NAME overflow_balance COMMAND dw4_overflow_director_tests balance "${DW4_TEST_DATA}")
  # The online client (asset free; the env test reads the sources).
  # (online_story: the story sets and golden vectors of data/online, SEAMLESS.md 8.)
  foreach(suite online_cpu_gate online_env online_wire online_session online_api online_story online_lobby)
    add_executable(dw4_${suite}_tests tests/${suite}_tests.cpp)
    target_link_libraries(dw4_${suite}_tests PRIVATE dw4_online)
    add_test(NAME ${suite} COMMAND dw4_${suite}_tests)
  endforeach()
  target_compile_definitions(dw4_online_env_tests PRIVATE DW4_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_compile_definitions(dw4_online_story_tests PRIVATE DW4_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  # The online character and the cloud backup (asset free, no server).
  add_executable(dw4_online_character_tests tests/online_character_tests.cpp)
  target_link_libraries(dw4_online_character_tests PRIVATE dw4_online)
  add_test(NAME online_character COMMAND dw4_online_character_tests)
  # Round I1a's Play (docs/SEAMLESS.md 19.5.1): the title driver's decisions, and with the game data the real
  # title and character select driven into the lobby.
  add_executable(dw4_online_title_tests tests/online_title_tests.cpp)
  target_link_libraries(dw4_online_title_tests PRIVATE dw4_online)
  add_test(NAME online_title COMMAND dw4_online_title_tests "${DW4_TEST_DATA}")
  # (The server pins this build carries, for the endpoint check.)
  target_compile_definitions(dw4_online_api_tests PRIVATE "DW4_EXPECTED_PINS=\"${DW4_PINS_JOINED}\"")
endif()

if(DW4_BUILD_APP)
  # Exact upstream commits and SHA-256 archives (cmake/third_party.cmake, the
  # same list as the Android build). No prebuilt game binaries.
  if(NOT ANDROID) # (Android: libSDL3.so, cmake/android.cmake)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  endif()
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/third_party.cmake)
  # SDL without libusb (docs/ROUND_SW2WIN_NOTES.md): the port reads the
  # controllers SDL 3.4.16 reaches only through libusb itself, the Switch 2
  # Pro Controller (switch2_pro.cpp: Windows' own HID driver for its input,
  # WinUSB on its interface 1 for the start commands, no driver install) and
  # the GameCube adapter in Wii U mode (gc_adapter.cpp). Set off explicitly,
  # also over a cache of the earlier libusb builds, so SDL takes no libusb
  # found elsewhere on the build machine either.
  set(SDL_HIDAPI_LIBUSB OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(SDL3 imgui_src pl_mpeg_src stb_src zstd_src)
  dw4_check_trimmed_paths(SDL3 imgui_src pl_mpeg_src stb_src zstd_src)
  set(ZSTD_LIB ${zstd_src_SOURCE_DIR}/lib)
  add_library(zstd_decoder STATIC
    ${ZSTD_LIB}/common/debug.c ${ZSTD_LIB}/common/entropy_common.c ${ZSTD_LIB}/common/error_private.c
    ${ZSTD_LIB}/common/fse_decompress.c ${ZSTD_LIB}/common/xxhash.c ${ZSTD_LIB}/common/zstd_common.c
    ${ZSTD_LIB}/decompress/huf_decompress.c ${ZSTD_LIB}/decompress/zstd_ddict.c
    ${ZSTD_LIB}/decompress/zstd_decompress.c ${ZSTD_LIB}/decompress/zstd_decompress_block.c)
  target_include_directories(zstd_decoder PUBLIC ${ZSTD_LIB})
  target_compile_definitions(zstd_decoder PRIVATE ZSTD_DISABLE_ASM)
  # Round J: the checkpoints' compression (docs/SEAMLESS.md 6.13.2), the
  # single-threaded encoder of the same release.
  add_library(zstd_encoder STATIC
    ${ZSTD_LIB}/common/fse_decompress.c ${ZSTD_LIB}/compress/fse_compress.c ${ZSTD_LIB}/compress/hist.c ${ZSTD_LIB}/compress/huf_compress.c
    ${ZSTD_LIB}/compress/zstd_compress.c ${ZSTD_LIB}/compress/zstd_compress_literals.c ${ZSTD_LIB}/compress/zstd_compress_sequences.c
    ${ZSTD_LIB}/compress/zstd_compress_superblock.c ${ZSTD_LIB}/compress/zstd_double_fast.c ${ZSTD_LIB}/compress/zstd_fast.c
    ${ZSTD_LIB}/compress/zstd_lazy.c ${ZSTD_LIB}/compress/zstd_ldm.c ${ZSTD_LIB}/compress/zstd_opt.c)
  target_include_directories(zstd_encoder PUBLIC ${ZSTD_LIB})
  target_compile_definitions(zstd_encoder PRIVATE ZSTD_DISABLE_ASM)
  target_link_libraries(zstd_encoder PUBLIC zstd_decoder)
  if(NOT ANDROID) # (Android: OpenGL::GL is GLESv3 and EGL, cmake/android.cmake)
  find_package(OpenGL REQUIRED)
  endif()
  add_library(imgui STATIC
    ${imgui_src_SOURCE_DIR}/imgui.cpp
    ${imgui_src_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_src_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_src_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_src_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_src_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(imgui PUBLIC ${imgui_src_SOURCE_DIR} ${imgui_src_SOURCE_DIR}/backends)
  target_link_libraries(imgui PUBLIC SDL3::SDL3 OpenGL::GL)
  add_executable(dw4_game src/game/game_main.cpp src/game/gl_renderer.cpp src/game/pad_family.cpp src/game/pad_host.cpp src/game/prompt_layout.cpp src/game/button_art.cpp src/game/button_prompts.cpp src/game/gc_adapter.cpp src/game/pad_usb.cpp src/game/switch2_pro.cpp src/game/movie_decoder.cpp src/game/settings.cpp src/game/graphics_options.cpp src/game/controls.cpp src/game/interpolation.cpp src/game/texture_pack.cpp src/game/texture_codec.cpp src/game/texture_stream.cpp src/game/language.cpp src/game/rvz.cpp src/game/netplay.cpp src/game/save_store.cpp src/game/mods.cpp src/game/test_path.cpp src/game/campaign.cpp src/game/online_script.cpp
    src/game/discord_ipc.cpp src/game/discord_presence.cpp src/game/presence_data.cpp src/game/presence_art.cpp
    src/game/overhead_labels.cpp src/game/overhead_labels_draw.cpp src/game/ghost_figures.cpp src/game/achievements.cpp
    src/game/randomizer_save.cpp src/game/dev_present.cpp src/game/online_run.cpp src/game/online_ui.cpp src/game/online_ui_script.cpp src/game/title_entry.cpp
    src/game/online_achievements.cpp src/game/host_view.cpp src/game/overflow_run.cpp src/game/overflow_host.cpp src/game/overflow_driver.cpp
    src/game/overflow_text.cpp src/game/sim_ui.cpp src/game/presentation_actor.cpp src/game/overflow_lobby.cpp src/game/overflow_screens.cpp
    src/game/online_prompt_ui.cpp)
  target_include_directories(dw4_game PRIVATE ${pl_mpeg_src_SOURCE_DIR} ${stb_src_SOURCE_DIR})
  target_compile_definitions(dw4_game PRIVATE DW4_PC_VERSION="${PROJECT_VERSION}")
  target_link_libraries(dw4_game PRIVATE dw4_engine dw4_online imgui zstd_decoder zstd_encoder SDL3::SDL3 OpenGL::GL)
  # The language packs next to the program (docs/L10N.md): data/lang and a
  # translator's local/lang game.tsv (never installed: game-derived text).
  # (Android has no program folder: the APK carries data/lang as assets,
  # android/app/build.gradle, and never a game.tsv.)
  if(NOT ANDROID)
  add_custom_command(TARGET dw4_game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR} -DTARGET=$<TARGET_FILE_DIR:dw4_game> -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_lang.cmake
    VERBATIM)
  endif()
  # F1 Saves > Cloud backup (the online character's lane, docs/SEAMLESS.md 9).
  target_sources(dw4_game PRIVATE src/game/cloud_backup_panel.cpp)
  # The release game is a Windows program: a double click opens no console
  # window; its lines go to the starter's pipe or console, else to
  # saves/game.log (src/game/program_output.hpp). Development builds
  # (DW4_ONLINE_DEV) keep the console unless DW4_GAME_CONSOLE says otherwise.
  option(DW4_GAME_CONSOLE "dw4_game as a console program (a console window opens with it)" ${DW4_ONLINE_DEV})
  target_sources(dw4_game PRIVATE src/game/program_output.cpp)
  if(WIN32 AND NOT DW4_GAME_CONSOLE)
    set_target_properties(dw4_game PROPERTIES WIN32_EXECUTABLE ON)
    target_compile_definitions(dw4_game PRIVATE DW4_GAME_WINDOWED=1)
    if(MSVC)
      # (main() stays the entry point of the Windows program.)
      target_link_options(dw4_game PRIVATE /ENTRY:mainCRTStartup)
    endif()
  endif()
  # Data Overflow's meta file saves/overflow.dat (round O1b lane F, docs/SURVIVAL.md 8).
  target_sources(dw4_game PRIVATE src/game/overflow_profile.cpp)
  # The second screen's panels (docs/ANDROID.md phase E): the PC's overlay,
  # Android's lower screen (cmake/android.cmake adds its own frame).
  target_sources(dw4_game PRIVATE src/game/screen2.cpp)
  # The F1 menu's one apply layer and option tables (docs/CONSOLE_MENU.md).
  target_sources(dw4_game PRIVATE src/game/menu_choices.cpp src/game/menu_ops.cpp)
  # Android's console F1 menu (docs/CONSOLE_MENU.md; on the PC only in test runs).
  target_sources(dw4_game PRIVATE src/game/console_nav.cpp src/game/console_menu.cpp src/game/console_pages.cpp src/game/console_glyphs.cpp)
  # The menu script and its virtual pad (test runs: PC, emulator, AYN Thor).
  target_sources(dw4_game PRIVATE src/game/menu_script.cpp src/game/menu_script_parse.cpp)
  if(WIN32)
    # (setupapi, winusb: the GameCube adapter in Wii U mode, gc_adapter.cpp;
    # with hid, cfgmgr32 also the Switch 2 Pro Controller, switch2_pro.cpp)
    target_link_libraries(dw4_game PRIVATE ws2_32 setupapi winusb hid cfgmgr32)
  endif()
  # Texture pack authoring (docs/TEXTURES.md): dw4_texpack export writes every
  # texture of the game data once under the renderer's pack hash, which it
  # takes from the renderer's own code (src/game/texture_pack.cpp). A
  # development tool, not part of the release.
  add_executable(dw4_texpack tools/dw4_texpack/main.cpp tools/dw4_texpack/texpack_export.cpp src/game/texture_pack.cpp src/game/texture_codec.cpp)
  target_include_directories(dw4_texpack PRIVATE ${stb_src_SOURCE_DIR})
  target_link_libraries(dw4_texpack PRIVATE dw4_engine)
  # Round I1a's ghost figure measurement (docs/SEAMLESS.md 19.13): a test
  # build's DW4_PROBE_GHOSTS* switches (ghost_figures.hpp, game_main.cpp).
  # Never in a player build.
  option(DW4_GHOST_PROBE "Ghost figure measurement switches (test builds only)" OFF)
  if(DW4_GHOST_PROBE)
    target_compile_definitions(dw4_game PRIVATE DW4_GHOST_PROBE=1)
  endif()
  if(BUILD_TESTING)
    # Keyboard / controller bindings -> GameCube pad input, settings.ini migration (SDL, no device or window).
    add_executable(dw4_controls_tests tests/controls_tests.cpp src/game/controls.cpp src/game/settings.cpp src/game/graphics_options.cpp)
    target_link_libraries(dw4_controls_tests PRIVATE dw4_core SDL3::SDL3)
    add_test(NAME controls COMMAND dw4_controls_tests)
    # The F1 menu's apply layer: every op against the direct assignment the
    # PC's menu made (settings.ini bytes, side effects), the option tables,
    # the choice and stepper arithmetic (docs/CONSOLE_MENU.md).
    add_executable(dw4_menu_ops_tests tests/menu_ops_tests.cpp src/game/menu_ops.cpp src/game/menu_choices.cpp src/game/settings.cpp
      src/game/graphics_options.cpp src/game/controls.cpp src/game/pad_family.cpp src/game/overhead_labels.cpp src/game/presence_data.cpp)
    target_include_directories(dw4_menu_ops_tests PRIVATE src/game)
    target_link_libraries(dw4_menu_ops_tests PRIVATE dw4_engine dw4_core SDL3::SDL3)
    add_test(NAME menu_ops COMMAND dw4_menu_ops_tests)
    # The console F1 menu's navigation model (focus, pages, buttons, touch).
    add_executable(dw4_console_nav_tests tests/console_nav_tests.cpp src/game/console_nav.cpp)
    add_test(NAME console_nav COMMAND dw4_console_nav_tests)
    # The console menu's button pictures (every family, both menu_buttons
    # modes, both platforms), the Back / Select rule, the menu script's parser.
    add_executable(dw4_console_glyph_tests tests/console_glyph_tests.cpp src/game/console_glyphs.cpp src/game/menu_script_parse.cpp
      src/game/pad_family.cpp src/game/button_art.cpp src/game/prompt_layout.cpp src/game/controls.cpp src/game/settings.cpp
      src/game/graphics_options.cpp src/game/gc_adapter.cpp)
    target_link_libraries(dw4_console_glyph_tests PRIVATE dw4_engine dw4_core imgui SDL3::SDL3 OpenGL::GL)
    if(WIN32)
      target_link_libraries(dw4_console_glyph_tests PRIVATE setupapi winusb)
    endif()
    add_test(NAME console_glyphs COMMAND dw4_console_glyph_tests)
    # Controller families: classification, binding spaces, prompt pictures,
    # the prompts in the game's text lines (laid out by the engine's
    # layout_text), settings migration (no device), and detection through
    # SDL's virtual joysticks with every real backend off, through the
    # host's own pad code (docs/ROUND_PADS_NOTES.md).
    add_executable(dw4_pad_family_tests tests/pad_family_tests.cpp src/game/pad_family.cpp src/game/button_art.cpp src/game/prompt_layout.cpp
      src/game/controls.cpp src/game/settings.cpp src/game/graphics_options.cpp src/game/gc_adapter.cpp)
    target_link_libraries(dw4_pad_family_tests PRIVATE dw4_engine dw4_core SDL3::SDL3)
    if(WIN32)
      target_link_libraries(dw4_pad_family_tests PRIVATE setupapi winusb)
    endif()
    add_test(NAME pad_family COMMAND dw4_pad_family_tests)
    add_executable(dw4_pad_detect_tests tests/pad_detect_tests.cpp src/game/pad_family.cpp src/game/pad_host.cpp src/game/controls.cpp
      src/game/settings.cpp src/game/graphics_options.cpp src/game/gc_adapter.cpp)
    target_link_libraries(dw4_pad_detect_tests PRIVATE dw4_core SDL3::SDL3)
    if(WIN32)
      target_link_libraries(dw4_pad_detect_tests PRIVATE setupapi winusb)
    endif()
    add_test(NAME pad_detect COMMAND dw4_pad_detect_tests)
    # Who opens which USB controller (SDL's own device filter with the game's
    # hints: no libusb, the GameCube adapter and the Switch 2 Pro Controller
    # are the port's own readers'), the Switch 2 Pro Controller's notices and
    # the controller log's lines (docs/ROUND_SW2WIN_NOTES.md).
    add_executable(dw4_pad_usb_tests tests/pad_usb_tests.cpp src/game/pad_usb.cpp src/game/pad_family.cpp src/game/pad_host.cpp
      src/game/controls.cpp src/game/settings.cpp src/game/graphics_options.cpp src/game/gc_adapter.cpp src/game/switch2_pro.cpp)
    target_link_libraries(dw4_pad_usb_tests PRIVATE dw4_core SDL3::SDL3)
    if(WIN32)
      target_link_libraries(dw4_pad_usb_tests PRIVATE setupapi winusb hid cfgmgr32)
    endif()
    add_test(NAME pad_usb COMMAND dw4_pad_usb_tests)
    # The Switch 2 Pro Controller reader (docs/ROUND_SW2WIN_NOTES.md): its
    # start commands, replies, calibration and input reports from the
    # documented format, its state machine on a fake transport (open, start,
    # read, unplug, replug, the window in the background), and its SDL
    # virtual pad through the host's own pad code (no device, no window).
    add_executable(dw4_switch2_tests tests/switch2_tests.cpp src/game/switch2_pro.cpp src/game/gc_adapter.cpp src/game/pad_family.cpp src/game/pad_host.cpp
      src/game/controls.cpp src/game/settings.cpp src/game/graphics_options.cpp)
    target_link_libraries(dw4_switch2_tests PRIVATE dw4_core SDL3::SDL3)
    if(WIN32)
      target_link_libraries(dw4_switch2_tests PRIVATE setupapi winusb hid cfgmgr32)
    endif()
    add_test(NAME switch2 COMMAND dw4_switch2_tests)
    # The texture export's pack hashes are the renderer's: synthetic C8 and
    # CMPR images, the exported names read back as replacements; with
    # DW4_TEST_DATA the original documents through the engine's bindings and
    # the hashes the renderer dumped on the dng101b1 route.
    add_executable(dw4_texpack_tests tests/texpack_tests.cpp tools/dw4_texpack/texpack_export.cpp src/game/texture_pack.cpp src/game/texture_codec.cpp)
    target_include_directories(dw4_texpack_tests PRIVATE ${stb_src_SOURCE_DIR})
    target_link_libraries(dw4_texpack_tests PRIVATE dw4_engine)
    add_test(NAME texpack COMMAND dw4_texpack_tests "${DW4_TEST_DATA}")
    # Texture pack format 2 and the Graphics options (docs/TEXTURES.md): the
    # manifest (valid, malformed, newer, unsafe paths), DDS headers, the BC7
    # decoder against DirectXTex's decode of every mode and partition, BC1 /
    # BC3, mip chains, the file choice per quality, the loader, the
    # streaming thread (scan, read-ahead, requests, reconfiguration), the
    # memory budget's evictions and the options' settings.ini round trip.
    add_executable(dw4_graphics_tests tests/graphics_tests.cpp src/game/texture_stream.cpp src/game/texture_pack.cpp src/game/texture_codec.cpp
      src/game/settings.cpp src/game/graphics_options.cpp src/game/controls.cpp)
    target_include_directories(dw4_graphics_tests PRIVATE ${stb_src_SOURCE_DIR})
    target_link_libraries(dw4_graphics_tests PRIVATE dw4_engine dw4_core SDL3::SDL3)
    foreach(part manifest dds codec mips choice loader budget options)
      add_test(NAME graphics_${part} COMMAND dw4_graphics_tests ${part})
    endforeach()
    add_test(NAME graphics_stream COMMAND dw4_graphics_tests stream "${DW4_TEST_DATA}")
    # The run's save store: store choice, migration, imports, online session card.
    add_executable(dw4_save_store_tests tests/save_store_tests.cpp src/game/save_store.cpp src/game/settings.cpp src/game/graphics_options.cpp src/game/controls.cpp)
    target_link_libraries(dw4_save_store_tests PRIVATE dw4_core SDL3::SDL3)
    add_test(NAME save_store COMMAND dw4_save_store_tests)
  endif()
endif()
# The release zip's layout, exactly (docs/DEVELOPMENT.md "Release packages";
# scripts/package_windows.ps1 builds, installs, checks and zips it): the game
# as DW4_PC.exe (dw4_game in the build), dw4_catalog (the mod authors' path
# finder of the player guide), the player docs only (README, docs/PLAYER_GUIDE.md; the
# developer docs stay on GitHub), the licenses, tools/verify_game.py and of
# every committed language pack its language.ini and textures.tsv (never a
# game.tsv: game-derived text, docs/L10N.md 2). No dw4_inspect, no PDB.
# (Android's game is the app's libmain.so in the APK, android/: no DW4_PC.)
if(DW4_BUILD_APP AND NOT ANDROID)
  install(PROGRAMS $<TARGET_FILE:dw4_game> DESTINATION . RENAME DW4_PC${CMAKE_EXECUTABLE_SUFFIX})
endif()
install(TARGETS dw4_catalog RUNTIME DESTINATION .)
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION .)
if(DW4_BUILD_APP)
  install(FILES ${SDL3_SOURCE_DIR}/LICENSE.txt DESTINATION licenses RENAME SDL.txt)
  install(FILES ${imgui_src_SOURCE_DIR}/LICENSE.txt DESTINATION licenses RENAME imgui.txt)
  install(FILES ${stb_src_SOURCE_DIR}/LICENSE DESTINATION licenses RENAME stb.txt)
  install(FILES ${zstd_src_SOURCE_DIR}/LICENSE DESTINATION licenses RENAME zstd.txt)
endif()
install(FILES licenses/enbrip-MIT.txt DESTINATION licenses)
if(DW4_BUILD_APP)
  install(FILES licenses/pl_mpeg-MIT.txt DESTINATION licenses)
endif()
install(FILES docs/PLAYER_GUIDE.md DESTINATION docs)
install(FILES tools/verify_game.py DESTINATION tools)
if(DW4_BUILD_APP)
  install(DIRECTORY data/lang/ DESTINATION lang FILES_MATCHING PATTERN "language.ini" PATTERN "textures.tsv")
endif()
