cmake_minimum_required(VERSION 3.24)
project(DW4_PC VERSION 0.46.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
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
  src/enbaya.cpp src/motion.cpp src/motion_timer.cpp src/adx.cpp src/axfx.cpp src/axmix.cpp src/save_data.cpp src/card_store.cpp src/save_import.cpp src/sfp.cpp src/sndf.cpp src/sfd.cpp src/ini.cpp src/disc.cpp src/disc_timing.cpp src/card_timing.cpp src/msl_math.cpp)
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
  src/engine/actor.cpp src/engine/collision.cpp src/engine/ui.cpp src/engine/title.cpp src/engine/load_intf.cpp
  src/engine/mat_ctrl.cpp src/engine/generater.cpp src/engine/randomizer.cpp src/engine/combat.cpp src/engine/status.cpp src/engine/save_game.cpp src/engine/save_util.cpp src/engine/save_menu.cpp src/engine/menu_text.cpp src/engine/shadow.cpp src/engine/particles.cpp src/engine/water.cpp src/engine/change_pos.cpp
  src/engine/hud.cpp src/engine/items.cpp src/engine/panel.cpp
  src/engine/subwnd.cpp src/engine/subwnd_folder.cpp src/engine/subwnd_skill.cpp
  src/engine/subwnd_shop.cpp src/engine/save_state.cpp src/engine/port_events.cpp src/engine/online.cpp
  src/engine/online_cameras.cpp src/engine/online_exits.cpp src/engine/online_scenes.cpp src/engine/online_story.cpp
  src/engine/tick_state.cpp src/engine/tick_state_managers.cpp)
# Data Overflow, the survival mode's run engine rules (docs/SURVIVAL.md 13.4):
# the hooks' values and the native run scripts; inert without OverflowRules.
target_sources(dw4_engine PRIVATE src/engine/overflow/rules.cpp src/engine/overflow/player_hit.cpp
  src/engine/overflow/field_start.cpp src/engine/overflow/donors.cpp src/engine/overflow/director.cpp
  src/engine/overflow/spawner.cpp src/engine/overflow/abilities.cpp src/engine/overflow/offers.cpp
  src/engine/overflow/human_bot.cpp)
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
  src/game/online/session.cpp src/game/online/presence.cpp src/game/online/engine_adapter.cpp src/game/online/lobby_api.cpp
  src/game/online/lobby.cpp src/game/online/achievement_api.cpp)
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
  # seamless co-op cameras and exits, docs/SEAMLESS.md 4 and 5.2)
  foreach(suite gekko_math messenger pad online_cameras)
    add_executable(dw4_${suite}_tests tests/${suite}_tests.cpp)
    target_link_libraries(dw4_${suite}_tests PRIVATE dw4_engine)
    add_test(NAME ${suite} COMMAND dw4_${suite}_tests)
  endforeach()
  # Randomizer: seeds, pools, the loading zone table, the save bond (sidecar) and spoiler.
  add_executable(dw4_randomizer_tests tests/randomizer_tests.cpp src/game/randomizer_save.cpp)
  target_link_libraries(dw4_randomizer_tests PRIVATE dw4_engine)
  add_test(NAME randomizer COMMAND dw4_randomizer_tests)
  # Data Overflow's host side (round O1a lane A, docs/SURVIVAL.md 13.2-13.3): the run request, the story's
  # generator across a run engine, the test variables a run keeps out, its cards and setup, the story switch.
  add_executable(dw4_overflow_host_tests tests/overflow_host_tests.cpp src/game/overflow_run.cpp src/game/presence_data.cpp)
  target_link_libraries(dw4_overflow_host_tests PRIVATE dw4_engine)
  add_test(NAME overflow_host COMMAND dw4_overflow_host_tests)
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
  # (The server pins this build carries, for the endpoint check.)
  target_compile_definitions(dw4_online_api_tests PRIVATE "DW4_EXPECTED_PINS=\"${DW4_PINS_JOINED}\"")
endif()

if(DW4_BUILD_APP)
  include(FetchContent)
  # Exact upstream commits and SHA-256 archives. No prebuilt game binaries.
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    URL https://codeload.github.com/libsdl-org/SDL/zip/fa2c02bb6e21974a89ea9824bc53c9932abe5f9c
    URL_HASH SHA256=ab7a774d41332dc73e6cad6ed9cb14b91cb1de76497dca116be7080f069a703a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_Declare(imgui_src
    URL https://codeload.github.com/ocornut/imgui/zip/f1cc2ae15e53a861a874c3034aae6798fde194ab
    URL_HASH SHA256=008e1f3398e1c6e4d317ae4a05d2dec597bd360f4f3717b1d96e9edb66dba34f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  # MPEG-1 video of the Sofdec movies (single header, no CMake project).
  FetchContent_Declare(pl_mpeg_src
    URL https://codeload.github.com/phoboslab/pl_mpeg/zip/c871f2be022ece7ef4f64230b4fb8e1fb9eb6023
    URL_HASH SHA256=67dd194f9de4af65937c057cf630e5e189e163a0ed9f489de2fcc75ee2117daf
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  # PNG reading and writing for texture packs and dumps (single headers).
  FetchContent_Declare(stb_src
    URL https://codeload.github.com/nothings/stb/zip/2c980bb59875b0d32144a71867fbdebb2f77cd20
    URL_HASH SHA256=8e59f72b0780690cda64726804269f638a3be77b9d1506ea95f443f7964bccf0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  # Zstandard decoding for RVZ disc images (decoder sources only).
  FetchContent_Declare(zstd_src
    URL https://codeload.github.com/facebook/zstd/zip/refs/tags/v1.5.6
    URL_HASH SHA256=3b1c3b46e416d36931efd34663122d7f51b550c87f74de2d38249516fe7d8be5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(SDL3 imgui_src pl_mpeg_src stb_src zstd_src)
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
  find_package(OpenGL REQUIRED)
  add_library(imgui STATIC
    ${imgui_src_SOURCE_DIR}/imgui.cpp
    ${imgui_src_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_src_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_src_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_src_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_src_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(imgui PUBLIC ${imgui_src_SOURCE_DIR} ${imgui_src_SOURCE_DIR}/backends)
  target_link_libraries(imgui PUBLIC SDL3::SDL3 OpenGL::GL)
  add_executable(dw4_game src/game/game_main.cpp src/game/gl_renderer.cpp src/game/movie_decoder.cpp src/game/settings.cpp src/game/controls.cpp src/game/interpolation.cpp src/game/texture_pack.cpp src/game/rvz.cpp src/game/netplay.cpp src/game/save_store.cpp src/game/mods.cpp src/game/test_path.cpp src/game/campaign.cpp src/game/online_script.cpp
    src/game/discord_ipc.cpp src/game/discord_presence.cpp src/game/presence_data.cpp src/game/presence_art.cpp
    src/game/overhead_labels.cpp src/game/overhead_labels_draw.cpp src/game/ghost_figures.cpp src/game/achievements.cpp
    src/game/randomizer_save.cpp src/game/dev_present.cpp src/game/online_run.cpp src/game/online_ui.cpp src/game/online_ui_script.cpp src/game/title_entry.cpp
    src/game/online_achievements.cpp src/game/host_view.cpp src/game/overflow_run.cpp src/game/overflow_host.cpp src/game/overflow_driver.cpp)
  target_include_directories(dw4_game PRIVATE ${pl_mpeg_src_SOURCE_DIR} ${stb_src_SOURCE_DIR})
  target_compile_definitions(dw4_game PRIVATE DW4_PC_VERSION="${PROJECT_VERSION}")
  target_link_libraries(dw4_game PRIVATE dw4_engine dw4_online imgui zstd_decoder zstd_encoder SDL3::SDL3 OpenGL::GL)
  # F1 Saves > Cloud backup (the online character's lane, docs/SEAMLESS.md 9).
  target_sources(dw4_game PRIVATE src/game/cloud_backup_panel.cpp)
  if(WIN32)
    target_link_libraries(dw4_game PRIVATE ws2_32)
  endif()
  # Round I1a's ghost figure measurement (docs/SEAMLESS.md 19.13): a test
  # build's DW4_PROBE_GHOSTS* switches (ghost_figures.hpp, game_main.cpp).
  # Never in a player build.
  option(DW4_GHOST_PROBE "Ghost figure measurement switches (test builds only)" OFF)
  if(DW4_GHOST_PROBE)
    target_compile_definitions(dw4_game PRIVATE DW4_GHOST_PROBE=1)
  endif()
  if(BUILD_TESTING)
    # Keyboard / controller bindings -> GameCube pad input, settings.ini migration (SDL, no device or window).
    add_executable(dw4_controls_tests tests/controls_tests.cpp src/game/controls.cpp src/game/settings.cpp)
    target_link_libraries(dw4_controls_tests PRIVATE dw4_core SDL3::SDL3)
    add_test(NAME controls COMMAND dw4_controls_tests)
    # The run's save store: store choice, migration, imports, online session card.
    add_executable(dw4_save_store_tests tests/save_store_tests.cpp src/game/save_store.cpp src/game/settings.cpp src/game/controls.cpp)
    target_link_libraries(dw4_save_store_tests PRIVATE dw4_core SDL3::SDL3)
    add_test(NAME save_store COMMAND dw4_save_store_tests)
  endif()
  # The release's single entry point is DW4_PC.exe (dw4_game in the build).
  install(PROGRAMS $<TARGET_FILE:dw4_game> DESTINATION . RENAME DW4_PC${CMAKE_EXECUTABLE_SUFFIX})
  install(FILES ${SDL3_SOURCE_DIR}/LICENSE.txt DESTINATION licenses RENAME SDL.txt)
  install(FILES ${imgui_src_SOURCE_DIR}/LICENSE.txt DESTINATION licenses RENAME imgui.txt)
  install(FILES ${stb_src_SOURCE_DIR}/LICENSE DESTINATION licenses RENAME stb.txt)
  install(FILES ${zstd_src_SOURCE_DIR}/LICENSE DESTINATION licenses RENAME zstd.txt)
endif()
install(TARGETS dw4_inspect dw4_catalog RUNTIME DESTINATION .)
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION .)
install(FILES licenses/enbrip-MIT.txt DESTINATION licenses)
if(DW4_BUILD_APP)
  install(FILES licenses/pl_mpeg-MIT.txt DESTINATION licenses)
endif()
install(DIRECTORY docs DESTINATION . FILES_MATCHING PATTERN "*.md")
install(FILES tools/verify_game.py DESTINATION tools)
