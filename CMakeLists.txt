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
  src/enbaya.cpp src/motion.cpp src/motion_timer.cpp src/adx.cpp src/axfx.cpp src/save_data.cpp src/sfp.cpp src/sfd.cpp src/ini.cpp src/disc.cpp src/disc_timing.cpp src/card_timing.cpp)
target_include_directories(dw4_core PUBLIC include)
# Recovered scalar operations round separately; explicit std::fma remains
# fused. Preserve IEEE zero-frame infinity/NaN behavior.
if(MSVC)
  set_source_files_properties(src/motion.cpp src/motion_timer.cpp src/animation_math.cpp PROPERTIES COMPILE_OPTIONS /fp:strict)
  target_compile_options(dw4_core PRIVATE /W4 /permissive- /utf-8)
  add_compile_options(/utf-8)
else()
  set_source_files_properties(src/motion.cpp src/motion_timer.cpp src/animation_math.cpp PROPERTIES COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off")
  target_compile_options(dw4_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Native engine runtime: reconstructed original managers, scene graph and
# stage flow on top of dw4_core. No SDL/ImGui dependency.
add_library(dw4_engine src/engine/math.cpp src/engine/igb_view.cpp src/engine/scene_graph.cpp
  src/engine/engine.cpp src/engine/lua_host.cpp src/engine/managers_core.cpp src/engine/model_ctrl.cpp
  src/engine/actor.cpp src/engine/collision.cpp src/engine/ui.cpp src/engine/title.cpp src/engine/load_intf.cpp
  src/engine/mat_ctrl.cpp src/engine/generater.cpp src/engine/combat.cpp src/engine/status.cpp src/engine/save_game.cpp src/engine/save_util.cpp src/engine/save_menu.cpp src/engine/menu_text.cpp src/engine/shadow.cpp src/engine/particles.cpp src/engine/water.cpp src/engine/change_pos.cpp
  src/engine/hud.cpp src/engine/items.cpp src/engine/panel.cpp
  src/engine/subwnd.cpp src/engine/subwnd_folder.cpp src/engine/subwnd_skill.cpp
  src/engine/subwnd_shop.cpp src/engine/save_state.cpp)
target_link_libraries(dw4_engine PUBLIC dw4_core)
if(MSVC)
  target_compile_options(dw4_engine PRIVATE /W3 /permissive- /utf-8)
endif()

# Development tools: IGB inspection and logical resource catalog.
add_executable(dw4_inspect tools/dw4_inspect.cpp)
target_link_libraries(dw4_inspect PRIVATE dw4_core)
add_executable(dw4_catalog tools/dw4_catalog.cpp)
target_link_libraries(dw4_catalog PRIVATE dw4_core)

if(BUILD_TESTING)
  foreach(suite assets igb igb_document resources packages scripts lua4 pad textures meshes skinning animation_math enbaya adx save_data sfp sfd ini disc)
    add_executable(dw4_${suite}_tests tests/${suite}_tests.cpp)
    target_link_libraries(dw4_${suite}_tests PRIVATE dw4_core)
    add_test(NAME ${suite} COMMAND dw4_${suite}_tests)
  endforeach()
  add_executable(dw4_scene_tests tests/scene_tests.cpp)
  target_link_libraries(dw4_scene_tests PRIVATE dw4_engine)
  add_test(NAME scene COMMAND dw4_scene_tests)
  add_executable(dw4_interpolation_tests tests/interpolation_tests.cpp src/game/interpolation.cpp)
  target_link_libraries(dw4_interpolation_tests PRIVATE dw4_engine)
  add_test(NAME interpolation COMMAND dw4_interpolation_tests)
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
  add_executable(dw4_game src/game/game_main.cpp src/game/gl_renderer.cpp src/game/movie_decoder.cpp src/game/settings.cpp src/game/controls.cpp src/game/interpolation.cpp src/game/texture_pack.cpp src/game/rvz.cpp src/game/netplay.cpp src/game/mods.cpp src/game/test_path.cpp src/game/campaign.cpp)
  target_include_directories(dw4_game PRIVATE ${pl_mpeg_src_SOURCE_DIR} ${stb_src_SOURCE_DIR})
  target_compile_definitions(dw4_game PRIVATE DW4_PC_VERSION="${PROJECT_VERSION}")
  target_link_libraries(dw4_game PRIVATE dw4_engine imgui zstd_decoder SDL3::SDL3 OpenGL::GL)
  if(WIN32)
    target_link_libraries(dw4_game PRIVATE ws2_32)
  endif()
  if(BUILD_TESTING)
    # Keyboard / controller bindings -> GameCube pad input, settings.ini migration (SDL, no device or window).
    add_executable(dw4_controls_tests tests/controls_tests.cpp src/game/controls.cpp src/game/settings.cpp)
    target_link_libraries(dw4_controls_tests PRIVATE dw4_core SDL3::SDL3)
    add_test(NAME controls COMMAND dw4_controls_tests)
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
