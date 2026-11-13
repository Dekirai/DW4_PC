# The Android build (docs/ANDROID.md), included by the root CMakeLists.txt
# under if(ANDROID); Gradle (android/app/build.gradle) configures it with the
# NDK's toolchain file, one build per ABI (arm64-v8a for the AYN Thor, x86_64
# for the emulator), and builds only the target dw4_game.
#
# The game is the PC's own target dw4_game, built as the shared library
# libmain.so: SDLActivity loads libSDL3.so and then libmain.so and calls
# SDL_main (game_main.cpp includes SDL_main.h on Android). SDL is a shared
# library here (the PC links it statically). The SDL Java sources come from
# the same SDL source tree at build time (android/app/build.gradle), because
# SDLActivity refuses to start when its version and libSDL3.so's differ.
# DW4_ANDROID_PROBE=ON builds phase A's device probe (src/android/
# probe_main.cpp) as libmain.so instead.
if(CMAKE_VERSION VERSION_LESS 3.24)
  message(FATAL_ERROR "The Android build needs CMake 3.24 or newer (the SDK package cmake;3.31.6)")
endif()

# Clang on every target of the simulation and the game: no contraction into
# fused multiply-adds (MSVC x64 does not contract either), no fast math, and
# char signed as on x86 (AArch64 makes it unsigned). (The PC's MSVC build
# keeps its own flags; the targets below exist already.)
set(DW4_ANDROID_FP_FLAGS -ffp-contract=off -fno-fast-math -fsigned-char)
foreach(target dw4_core dw4_engine dw4_online)
  target_compile_options(${target} PRIVATE ${DW4_ANDROID_FP_FLAGS})
endforeach()

# OFF: only the portable libraries, no SDL (android/ndk-core-check.cmd).
option(DW4_ANDROID_APP "Build the Android app's native library main with SDL3" ON)
option(DW4_ANDROID_PROBE "Build phase A's device probe as libmain.so instead of the game" OFF)
# The root CMakeLists.txt's tests stay off here.
set(BUILD_TESTING OFF)
if(NOT DW4_ANDROID_APP)
  set(DW4_BUILD_APP OFF)
  return()
endif()

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
# (The Java classes are compiled by Gradle, not packed into an SDL3.jar.)
set(SDL_ANDROID_JAR OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI_LIBUSB OFF CACHE BOOL "" FORCE)

if(DW4_ANDROID_PROBE)
  set(DW4_BUILD_APP OFF)
  include(${CMAKE_CURRENT_LIST_DIR}/third_party.cmake)
  FetchContent_MakeAvailable(SDL3)
  add_library(main SHARED src/android/probe_main.cpp)
  target_compile_definitions(main PRIVATE DW4_PC_VERSION="${PROJECT_VERSION}")
  target_compile_options(main PRIVATE -Wall -Wextra)
  target_link_libraries(main PRIVATE SDL3::SDL3 GLESv3 EGL android log)
  return()
endif()

# The game (the root CMakeLists.txt's DW4_BUILD_APP block).
set(DW4_BUILD_APP ON)
# OpenGL::GL of the PC build is OpenGL ES 3 and EGL here (the renderer's
# GLES path, gl_renderer.cpp; ImGui's OpenGL 3 backend picks ES 3 itself).
if(NOT TARGET OpenGL::GL)
  add_library(OpenGL::GL INTERFACE IMPORTED)
  set_target_properties(OpenGL::GL PROPERTIES INTERFACE_LINK_LIBRARIES "GLESv3;EGL")
endif()

# The root CMakeLists.txt's add_executable(dw4_game ...) line stays the PC's
# (branches that add a game source change only that line): here it makes the
# shared library SDLActivity loads instead. Every other target is unchanged.
macro(add_executable name)
  if("${name}" STREQUAL "dw4_game")
    add_library(${name} SHARED ${ARGN})
  else()
    _add_executable(${name} ${ARGN})
  endif()
endmacro()

# After the root CMakeLists.txt made dw4_game: the name SDLActivity loads,
# the Android parts, the floating-point flags.
function(dw4_android_game)
  set_target_properties(dw4_game PROPERTIES OUTPUT_NAME main)
  target_sources(dw4_game PRIVATE ${PROJECT_SOURCE_DIR}/src/game/platform_android.cpp)
  # (The NDK makes -Wformat-security an error; the F1 menu's wrapped text
  # helpers pass their literal formats through a variadic lambda.)
  target_compile_options(dw4_game PRIVATE ${DW4_ANDROID_FP_FLAGS} -Wno-error=format-security)
  target_link_libraries(dw4_game PRIVATE android log)
endfunction()
cmake_language(DEFER CALL dw4_android_game)
