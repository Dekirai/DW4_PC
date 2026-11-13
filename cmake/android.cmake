# The Android build (docs/ANDROID.md), included by the root CMakeLists.txt
# under if(ANDROID); Gradle (android/app/build.gradle) configures it with the
# NDK's toolchain file, one build per ABI (arm64-v8a for the AYN Thor, x86_64
# for the emulator).
#
# Phase A: the native library "main" is the device probe
# (src/android/probe_main.cpp), not the game yet. SDLActivity loads
# libSDL3.so and then libmain.so and calls SDL_main, so SDL is a shared
# library here (the PC links it statically). The SDL Java sources come from
# the same SDL source tree at build time (android/app/build.gradle), because
# SDLActivity refuses to start when its version and libSDL3.so's differ.
if(CMAKE_VERSION VERSION_LESS 3.24)
  message(FATAL_ERROR "The Android build needs CMake 3.24 or newer (the SDK package cmake;3.31.6)")
endif()
# OFF: only the portable libraries, no SDL (android/ndk-core-check.cmd).
option(DW4_ANDROID_APP "Build the Android app's native library main with SDL3" ON)
if(NOT DW4_ANDROID_APP)
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
include(${CMAKE_CURRENT_LIST_DIR}/third_party.cmake)
FetchContent_MakeAvailable(SDL3)

add_library(main SHARED src/android/probe_main.cpp)
target_compile_definitions(main PRIVATE DW4_PC_VERSION="${PROJECT_VERSION}")
target_compile_options(main PRIVATE -Wall -Wextra)
target_link_libraries(main PRIVATE SDL3::SDL3 GLESv3 EGL android log)
