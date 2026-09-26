cmake_minimum_required(VERSION 3.24)
project(DW4_PC VERSION 0.45.0 LANGUAGES C CXX)

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
target_sources(dw4_core PRIVATE src/game_database.cpp src/game_items.cpp src/game_stats.cpp src/game_name.cpp src/game_start.cpp)
target_sources(dw4_core PRIVATE src/model_player_start.cpp)
target_sources(dw4_core PRIVATE src/model_player_startup.cpp src/number_control.cpp src/meter_control.cpp src/model_status.cpp)
target_sources(dw4_core PRIVATE src/model_player_work.cpp)
target_sources(dw4_core PRIVATE src/model_scene.cpp src/model_player_create.cpp src/model_dispatch.cpp src/model_player_sequence.cpp)
target_sources(dw4_core PRIVATE src/model_timer.cpp src/action_dispatch.cpp)
target_sources(dw4_core PRIVATE src/model_pad.cpp src/model_player_input.cpp src/model_status_update.cpp src/model_wait_actual.cpp src/model_motion_actual.cpp src/model_subcommand_update.cpp src/model_frame.cpp)
target_sources(dw4_core PRIVATE src/model_movement_collision.cpp)
target_sources(dw4_core PRIVATE src/model_hit_land_handler.cpp src/hitland_math.cpp)
target_sources(dw4_core PRIVATE src/hitland_query.cpp src/hitland_traversal.cpp src/hitland_intersection.cpp)
target_sources(dw4_core PRIVATE src/actor_geometry.cpp src/hitland_primitives.cpp src/hitland_geometry.cpp src/hitland_geometry_math.cpp src/hitland_node_dispatch.cpp)
target_sources(dw4_core PRIVATE src/matrix_inverse.cpp)
target_sources(dw4_core PRIVATE src/hitland_floor.cpp)
target_sources(dw4_core PRIVATE src/hitland_wall.cpp src/hitland_wall_math.cpp)
target_sources(dw4_core PRIVATE src/game_clock.cpp src/actor_transform.cpp)
target_sources(dw4_core PRIVATE src/application_frame.cpp src/player_input_math.cpp)
target_sources(dw4_core PRIVATE src/viewer.cpp src/serial_scheduler.cpp src/model_manager_frame.cpp src/camera_math.cpp)
target_sources(dw4_core PRIVATE src/system_frame.cpp src/system_stage.cpp src/system_storage.cpp)
target_sources(dw4_core PRIVATE src/context_info.cpp src/loader_directory.cpp src/loader_request.cpp src/native_stage_services.cpp)
target_sources(dw4_core PRIVATE src/igb_stream.cpp src/igb_decode_tables.cpp src/igb_decode.cpp
  src/igb_runtime_types.cpp src/igb_type_mapping.cpp src/igb_runtime_bootstrap.cpp
  src/igb_loading_paths.cpp src/igb_memory_pools.cpp src/igb_dependencies.cpp
  src/loader_globals.cpp src/loader_dependency.cpp src/igb_string_pool.cpp
  src/igb_object_init.cpp src/igb_file_factory.cpp src/igb_file_pools.cpp
  src/igb_type_registration.cpp src/igb_registration_callbacks.cpp src/igb_field_defaults.cpp
  src/igb_bootstrap_object.cpp src/igb_thread_registration.cpp
  src/igb_thread_dependencies.cpp src/igb_thread_defaults.cpp
  src/igb_named_type_registration.cpp src/igb_named_type_defaults.cpp
  src/igb_thread_startup.cpp src/igb_platform_thread_registration.cpp
  src/igb_thread_runtime_init.cpp
  src/igb_semaphore_runtime_init.cpp
  src/igb_platform_semaphore_registration.cpp
  src/igb_gamecube_thread_registration.cpp
  src/igb_gamecube_thread_init.cpp
  src/igb_gamecube_thread_adopt.cpp
  src/igb_thread_manager_insert.cpp
  src/igb_data_list_growth.cpp
  src/igb_allocation_copy.cpp
  src/igb_struct_field_defaults.cpp
  src/igb_struct_member.cpp
  src/igb_data_list_registration.cpp src/igb_data_list_defaults.cpp
  src/igb_field_registry_factory.cpp src/igb_metadata_factory.cpp
  src/igb_metadata_probe.cpp src/igb_field_schema.cpp src/igb_metadata_defaults.cpp
  src/igb_field_values.cpp src/igb_list_registration.cpp src/igb_list_defaults.cpp
  src/igb_field_probe.cpp src/igb_field_type_registration.cpp src/igb_field_type_defaults.cpp src/igb_field_prototype.cpp src/igb_field_init.cpp src/igb_field_string_init.cpp src/igb_field_copy.cpp src/igb_field_clone.cpp src/igb_reflected_clone.cpp src/igb_field_member_copy.cpp
  src/igb_metadata_init.cpp src/igb_meta_enum.cpp src/igb_object_field_construct.cpp
  src/igb_memory_field_construct.cpp src/igb_memory_field_defaults.cpp src/igb_string_field_defaults.cpp
  src/igb_enum_field_defaults.cpp
  src/igb_buffer_copy.cpp src/igb_memory_member_copy.cpp
  src/igb_metadata_operations.cpp src/igb_metadata_registration.cpp src/igb_ark_core_bootstrap.cpp
  src/igb_pointer_list.cpp src/igb_type_list_factory.cpp src/igb_object_registration.cpp
  src/igb_field_operations.cpp src/igb_meta_field_factory.cpp src/igb_object_defaults.cpp
  src/igb_type_publication.cpp src/igb_metadata_layout.cpp)
if(MSVC)
  set_source_files_properties(src/matrix_inverse.cpp src/hitland_floor.cpp src/hitland_wall.cpp src/hitland_wall_math.cpp src/player_input_math.cpp src/camera_math.cpp src/game_clock.cpp src/igb_decode.cpp PROPERTIES COMPILE_OPTIONS /fp:strict)
else()
  set_source_files_properties(src/matrix_inverse.cpp src/hitland_floor.cpp src/hitland_wall.cpp src/hitland_wall_math.cpp src/player_input_math.cpp src/camera_math.cpp src/game_clock.cpp src/igb_decode.cpp PROPERTIES COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off")
endif()
target_sources(dw4_core PRIVATE src/player_point.cpp src/model_placement.cpp)
target_sources(dw4_core PRIVATE src/actor_scene.cpp src/weapon_resource.cpp src/bone_select.cpp src/weapon_runtime.cpp)
target_sources(dw4_core PRIVATE src/scene_traversal.cpp)
# Recovered scalar operations round separately; explicit std::fma in local
# translation remains fused. Preserve IEEE zero-frame infinity/NaN behavior.
if(MSVC)
  set_source_files_properties(src/model_control.cpp src/model_timer.cpp src/hitland_math.cpp src/hitland_query.cpp src/hitland_traversal.cpp src/hitland_intersection.cpp src/hitland_geometry_math.cpp src/hitland_node_dispatch.cpp PROPERTIES COMPILE_OPTIONS /fp:strict)
  set_source_files_properties(src/motion.cpp src/motion_timer.cpp src/model_motion.cpp src/animation_math.cpp src/animation_timing.cpp src/animations.cpp src/actions.cpp src/model_subcommands.cpp src/action_event_tables.cpp src/collision_math.cpp src/model_hit_body.cpp src/model_collision.cpp src/model_player_startup.cpp src/number_control.cpp src/meter_control.cpp src/model_placement.cpp PROPERTIES COMPILE_OPTIONS /fp:strict)
else()
  set_source_files_properties(src/model_control.cpp src/model_timer.cpp src/hitland_math.cpp src/hitland_query.cpp src/hitland_traversal.cpp src/hitland_intersection.cpp src/hitland_geometry_math.cpp src/hitland_node_dispatch.cpp PROPERTIES COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off")
  set_source_files_properties(src/motion.cpp src/motion_timer.cpp src/model_motion.cpp src/animation_math.cpp src/animation_timing.cpp src/animations.cpp src/actions.cpp src/model_subcommands.cpp src/action_event_tables.cpp src/collision_math.cpp src/model_hit_body.cpp src/model_collision.cpp src/model_player_startup.cpp src/number_control.cpp src/meter_control.cpp src/model_placement.cpp PROPERTIES COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off")
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
  foreach(suite igb_reflected_clone igb_field_member_copy igb_buffer_copy igb_memory_member_copy)
    add_executable(dw4_${suite}_tests tests/${suite}_tests.cpp)
    target_link_libraries(dw4_${suite}_tests PRIVATE dw4_core)
    add_test(NAME ${suite} COMMAND dw4_${suite}_tests)
  endforeach()
  foreach(suite assets igb igb_document resources packages scripts lua4 startup startup_loading controllers pad motion model_motion model_control model_sequence textures meshes materials scenes player_placement player_visual skinning animation_math animation_timing enbaya animations animation_combiner actions action_actors animation_hierarchy animation_traversal actor_skin camera settings walking walking_collision player_runtime_binding model_subcommands messenger action_event_tables action_event_runtime player_events action_queries model_wait player_waits collision_math model_hitbox model_hitbox_runtime model_hit_body model_collision model_collision_runtime script_registry model_script_context model_script_runtime action_event_script_work controller_identity)
    add_executable(dw4_${suite}_tests tests/${suite}_tests.cpp)
    target_link_libraries(dw4_${suite}_tests PRIVATE dw4_core)
    add_test(NAME ${suite} COMMAND dw4_${suite}_tests)
  endforeach()
  foreach(suite model_script_messenger game_data game_ram model_game_data database database_runtime game_ram_skill game_items game_stats game_name game_start game_ram_recalc messenger_deferred messenger_deferred_runtime model_player_start model_player_startup model_player_work model_scene model_player_create model_dispatch model_timer model_motion_timer model_control_timer model_sequence_actual model_player_sequence model_sequence_entry action_dispatch model_control_actual model_player_input model_status_update model_wait_actual model_frame number_control meter_control model_status weapon_resource bone_select model_player_queries actor_scene weapon_runtime scene_traversal player_point model_placement player_spawn)
    add_executable(dw4_${suite}_tests tests/${suite}_tests.cpp)
    target_link_libraries(dw4_${suite}_tests PRIVATE dw4_core)
    add_test(NAME ${suite} COMMAND dw4_${suite}_tests)
  endforeach()
  add_executable(dw4_model_motion_actual_tests tests/model_motion_actual_tests.cpp)
  target_link_libraries(dw4_model_motion_actual_tests PRIVATE dw4_core)
  add_test(NAME model_motion_actual COMMAND dw4_model_motion_actual_tests)
  add_executable(dw4_model_subcommand_update_tests tests/model_subcommand_update_tests.cpp)
  target_link_libraries(dw4_model_subcommand_update_tests PRIVATE dw4_core)
  add_test(NAME model_subcommand_update COMMAND dw4_model_subcommand_update_tests)
  add_executable(dw4_model_movement_collision_tests tests/model_movement_collision_tests.cpp)
  target_link_libraries(dw4_model_movement_collision_tests PRIVATE dw4_core)
  add_test(NAME model_movement_collision COMMAND dw4_model_movement_collision_tests)
  add_executable(dw4_model_hit_land_handler_tests tests/model_hit_land_handler_tests.cpp)
  target_link_libraries(dw4_model_hit_land_handler_tests PRIVATE dw4_core)
  add_test(NAME model_hit_land_handler COMMAND dw4_model_hit_land_handler_tests)
  add_executable(dw4_hitland_math_tests tests/hitland_math_tests.cpp)
  target_link_libraries(dw4_hitland_math_tests PRIVATE dw4_core)
  add_test(NAME hitland_math COMMAND dw4_hitland_math_tests)
  add_executable(dw4_model_hit_land_scene_tests tests/model_hit_land_scene_tests.cpp)
  target_link_libraries(dw4_model_hit_land_scene_tests PRIVATE dw4_core)
  add_test(NAME model_hit_land_scene COMMAND dw4_model_hit_land_scene_tests)
  add_executable(dw4_hitland_query_tests tests/hitland_query_tests.cpp)
  target_link_libraries(dw4_hitland_query_tests PRIVATE dw4_core)
  add_test(NAME hitland_query COMMAND dw4_hitland_query_tests)
  add_executable(dw4_hitland_traversal_tests tests/hitland_traversal_tests.cpp)
  target_link_libraries(dw4_hitland_traversal_tests PRIVATE dw4_core)
  add_test(NAME hitland_traversal COMMAND dw4_hitland_traversal_tests)
  add_executable(dw4_hitland_intersection_tests tests/hitland_intersection_tests.cpp)
  target_link_libraries(dw4_hitland_intersection_tests PRIVATE dw4_core)
  add_test(NAME hitland_intersection COMMAND dw4_hitland_intersection_tests)
  add_executable(dw4_model_hit_land_geometry_scene_tests tests/model_hit_land_geometry_scene_tests.cpp)
  target_link_libraries(dw4_model_hit_land_geometry_scene_tests PRIVATE dw4_core)
  add_test(NAME model_hit_land_geometry_scene COMMAND dw4_model_hit_land_geometry_scene_tests)
  foreach(suite hitland_geometry hitland_primitives hitland_node_dispatch actor_geometry matrix_inverse hitland_floor hitland_wall game_clock actor_transform application_frame player_input_math viewer serial_scheduler model_manager_frame camera_math system_frame system_stage system_storage context_info loader_directory loader_request native_stage_services igb_stream igb_decode_tables igb_decode igb_runtime_types igb_type_mapping igb_runtime_bootstrap igb_loading_paths igb_memory_pools igb_dependencies loader_globals loader_dependency igb_string_pool igb_object_init igb_file_factory igb_file_pools igb_type_registration igb_registration_callbacks igb_field_defaults igb_bootstrap_object igb_thread_registration igb_field_registry_factory igb_metadata_factory igb_metadata_operations igb_metadata_registration igb_ark_core_bootstrap igb_pointer_list igb_type_list_factory igb_object_registration igb_field_operations igb_meta_field_factory igb_object_defaults igb_type_publication igb_metadata_layout igb_metadata_probe igb_field_schema igb_metadata_defaults igb_field_values igb_list_registration igb_list_defaults igb_field_probe igb_field_type_registration igb_field_type_defaults igb_field_prototype igb_field_init igb_field_string_init igb_field_copy igb_field_clone igb_metadata_init igb_meta_enum igb_object_field_construct igb_memory_field_construct igb_thread_dependencies igb_thread_defaults igb_data_list_registration igb_data_list_defaults igb_memory_field_defaults igb_string_field_defaults igb_enum_field_defaults igb_named_type_registration igb_named_type_defaults igb_thread_startup igb_platform_thread_registration igb_runtime_object igb_thread_runtime_init igb_memory_member_defaults igb_platform_semaphore_registration igb_struct_field_defaults igb_struct_field_runtime igb_struct_member igb_semaphore_runtime_init igb_gamecube_thread_registration igb_gamecube_thread_init igb_gamecube_thread_adopt igb_thread_manager_insert igb_data_list_growth)
    add_executable(dw4_${suite}_tests tests/${suite}_tests.cpp)
    target_link_libraries(dw4_${suite}_tests PRIVATE dw4_core)
    add_test(NAME ${suite} COMMAND dw4_${suite}_tests)
  endforeach()
endif()

if(BUILD_TESTING)
  # The prepared draw adapter owns CPU data only; no graphics context is needed.
  add_executable(dw4_weapon_preview_tests tests/weapon_preview_tests.cpp src/weapon_preview.cpp)
  target_include_directories(dw4_weapon_preview_tests PRIVATE src)
  target_link_libraries(dw4_weapon_preview_tests PRIVATE dw4_core)
  add_test(NAME weapon_preview COMMAND dw4_weapon_preview_tests)
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
  add_executable(dw4_pc src/app.cpp src/mesh_renderer.cpp src/weapon_preview.cpp)
  target_compile_definitions(dw4_pc PRIVATE DW4_PC_VERSION="${PROJECT_VERSION}")
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
