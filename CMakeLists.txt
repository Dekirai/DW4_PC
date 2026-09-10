cmake_minimum_required(VERSION 3.24)
project(DW4_PC VERSION 0.30.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(DW4_BUILD_APP "Build the native SDL/OpenGL development workbench" ${WIN32})
include(CTest)

add_library(dw4_core src/assets.cpp src/igb.cpp src/igb_document.cpp src/resources.cpp src/packages.cpp src/scripts.cpp src/lua4.cpp src/startup.cpp src/startup_loading.cpp src/controllers.cpp src/pad.cpp src/textures.cpp src/meshes.cpp src/materials.cpp src/scenes.cpp src/scene_controls.cpp src/player_placement.cpp src/player_visual.cpp src/skinning.cpp src/animation_math.cpp src/enbaya.cpp src/animations.cpp src/camera.cpp src/settings.cpp src/walking.cpp src/walking_collision.cpp)
target_include_directories(dw4_core PUBLIC include)
target_sources(dw4_core PRIVATE src/motion.cpp src/motion_timer.cpp src/model_motion.cpp src/model_control.cpp src/model_sequence.cpp src/animation_timing.cpp src/animation_combiner.cpp src/actions.cpp src/action_actors.cpp src/animation_hierarchy.cpp src/actor_skin.cpp)
target_sources(dw4_core PRIVATE src/player_runtime_binding.cpp src/model_subcommands.cpp src/messenger.cpp src/action_event_tables.cpp src/action_event_runtime.cpp src/action_queries.cpp src/model_wait.cpp src/collision_math.cpp)
target_sources(dw4_core PRIVATE src/model_hit_body.cpp src/model_collision.cpp)
target_sources(dw4_core PRIVATE src/script_registry.cpp src/model_script_context.cpp src/model_script_runtime.cpp)
target_sources(dw4_core PRIVATE src/controller_identity.cpp src/game_data.cpp src/game_ram.cpp src/model_game_data.cpp src/model_script_messenger.cpp)
target_sources(dw4_core PRIVATE src/database.cpp src/database_runtime.cpp)
# Recovered scalar operations round separately; explicit std::fma in local
# translation remains fused. Preserve IEEE zero-frame infinity/NaN behavior.
if(MSVC)
  set_source_files_properties(src/motion.cpp src/motion_timer.cpp src/model_motion.cpp src/animation_math.cpp src/animation_timing.cpp src/animations.cpp src/actions.cpp src/model_subcommands.cpp src/action_event_tables.cpp src/collision_math.cpp src/model_hit_body.cpp src/model_collision.cpp PROPERTIES COMPILE_OPTIONS /fp:strict)
else()
  set_source_files_properties(src/motion.cpp src/motion_timer.cpp src/model_motion.cpp src/animation_math.cpp src/animation_timing.cpp src/animations.cpp src/actions.cpp src/model_subcommands.cpp src/action_event_tables.cpp src/collision_math.cpp src/model_hit_body.cpp src/model_collision.cpp PROPERTIES COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off")
endif()
if(MSVC)
  target_compile_options(dw4_core PRIVATE /W4 /permissive- /utf-8)
  add_compile_options(/utf-8)
else()
  target_compile_options(dw4_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(dw4_inspect tools/dw4_inspect.cpp)
target_link_libraries(dw4_inspect PRIVATE dw4_core)
add_executable(dw4_catalog tools/dw4_catalog.cpp)
target_link_libraries(dw4_catalog PRIVATE dw4_core)
add_executable(dw4_boot tools/dw4_boot.cpp)
target_link_libraries(dw4_boot PRIVATE dw4_core)

if(BUILD_TESTING)
  foreach(suite assets igb igb_document resources packages scripts lua4 startup startup_loading controllers pad motion model_motion model_control model_sequence textures meshes materials scenes player_placement player_visual skinning animation_math animation_timing enbaya animations animation_combiner actions action_actors animation_hierarchy animation_traversal actor_skin camera settings walking walking_collision player_runtime_binding model_subcommands messenger action_event_tables action_event_runtime player_events action_queries model_wait player_waits collision_math model_hitbox model_hitbox_runtime model_hit_body model_collision model_collision_runtime script_registry model_script_context model_script_runtime action_event_script_work controller_identity)
    add_executable(dw4_${suite}_tests tests/${suite}_tests.cpp)
    target_link_libraries(dw4_${suite}_tests PRIVATE dw4_core)
    add_test(NAME ${suite} COMMAND dw4_${suite}_tests)
  endforeach()
  foreach(suite model_script_messenger game_data game_ram model_game_data database database_runtime game_ram_skill)
    add_executable(dw4_${suite}_tests tests/${suite}_tests.cpp)
    target_link_libraries(dw4_${suite}_tests PRIVATE dw4_core)
    add_test(NAME ${suite} COMMAND dw4_${suite}_tests)
  endforeach()
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
  FetchContent_MakeAvailable(SDL3 imgui_src)
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
  add_executable(dw4_pc src/app.cpp src/mesh_renderer.cpp)
  target_link_libraries(dw4_pc PRIVATE dw4_core imgui SDL3::SDL3 OpenGL::GL)
  if(BUILD_TESTING)
    # Explicit local GPU check; CI runners need no working graphics/display driver.
    add_executable(dw4_renderer_tests tests/renderer_tests.cpp src/mesh_renderer.cpp)
    target_include_directories(dw4_renderer_tests PRIVATE src)
    target_link_libraries(dw4_renderer_tests PRIVATE dw4_core SDL3::SDL3 OpenGL::GL)
  endif()
  if(WIN32)
    target_link_libraries(dw4_pc PRIVATE shell32)
  endif()
  install(TARGETS dw4_pc RUNTIME DESTINATION .)
  install(FILES ${SDL3_SOURCE_DIR}/LICENSE.txt DESTINATION licenses RENAME SDL.txt)
  install(FILES ${imgui_src_SOURCE_DIR}/LICENSE.txt DESTINATION licenses RENAME imgui.txt)
endif()
install(TARGETS dw4_inspect dw4_catalog dw4_boot RUNTIME DESTINATION .)
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION .)
install(FILES licenses/enbrip-MIT.txt DESTINATION licenses)
install(DIRECTORY docs DESTINATION . FILES_MATCHING PATTERN "*.md")
install(FILES tools/verify_game.py DESTINATION tools)
