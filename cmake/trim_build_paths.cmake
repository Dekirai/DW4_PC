# Release privacy: no path of the build machine in the binaries.
#
# __FILE__ in the game's sources and in the dependencies built here (SDL's
# release asserts, among them) expands to the absolute path the compiler was
# given: the checkout, the build folder or a FetchContent folder, with the
# Windows profile folder in it. dw4_trim_build_paths() cuts those prefixes off
# every __FILE__ of the targets created after it, in this directory and below
# (FetchContent's subdirectories included), so the text reads
# "src/engine/x.cpp" or "sdl3-src/src/gpu/SDL_gpu.c". Only the text of
# __FILE__ changes; no code does.
#
# The prefixes: the project's source and binary folders, FETCHCONTENT_BASE_DIR
# when set, and the parent folder of every FETCHCONTENT_SOURCE_DIR_<NAME> (a
# dependency's sources taken from elsewhere keep their folder's name).
# MSVC: /d1trimfile:<prefix>/ (cuts the prefix; case and slash insensitive;
# the first matching one applies, so the longest goes first; the trailing
# forward slash keeps a quoted path from ending in \"). GCC / Clang:
# -fmacro-prefix-map=<prefix>/= (GCC applies the last match, Clang the
# longest: the longest goes last). Call it before the first target.
function(dw4_trim_build_paths)
  set(prefixes "${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}")
  if(DEFINED CACHE{FETCHCONTENT_BASE_DIR} AND NOT "$CACHE{FETCHCONTENT_BASE_DIR}" STREQUAL "")
    list(APPEND prefixes "$CACHE{FETCHCONTENT_BASE_DIR}")
  endif()
  get_cmake_property(cached CACHE_VARIABLES)
  foreach(var IN LISTS cached)
    if(var MATCHES "^FETCHCONTENT_SOURCE_DIR_" AND NOT "$CACHE{${var}}" STREQUAL "")
      set(dir "$CACHE{${var}}")
      cmake_path(ABSOLUTE_PATH dir BASE_DIRECTORY "${PROJECT_BINARY_DIR}" NORMALIZE)
      string(REGEX REPLACE "/+$" "" dir "${dir}")
      cmake_path(GET dir PARENT_PATH parent)
      list(APPEND prefixes "${parent}")
    endif()
  endforeach()
  set(normal "")
  foreach(p IN LISTS prefixes)
    cmake_path(ABSOLUTE_PATH p BASE_DIRECTORY "${PROJECT_BINARY_DIR}" NORMALIZE OUTPUT_VARIABLE abs)
    string(REGEX REPLACE "/+$" "" abs "${abs}")
    if(NOT abs STREQUAL "")
      list(APPEND normal "${abs}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES normal)
  # (Of two prefixes where one contains the other, the shorter sorts first.)
  list(SORT normal COMPARE STRING CASE INSENSITIVE ORDER ASCENDING)
  if(CMAKE_C_COMPILER_ID STREQUAL "MSVC" OR CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    list(REVERSE normal)
    foreach(p IN LISTS normal)
      add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/d1trimfile:${p}/>")
    endforeach()
  else()
    include(CheckCCompilerFlag)
    check_c_compiler_flag("-fmacro-prefix-map=${PROJECT_SOURCE_DIR}/=" DW4_HAVE_MACRO_PREFIX_MAP)
    if(NOT DW4_HAVE_MACRO_PREFIX_MAP)
      message(WARNING "The compiler takes no -fmacro-prefix-map: __FILE__ keeps the build machine's paths")
      return()
    endif()
    foreach(p IN LISTS normal)
      add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-fmacro-prefix-map=${p}/=>")
    endforeach()
  endif()
  set(DW4_TRIMMED_PATHS "${normal}" PARENT_SCOPE)
endfunction()

# After FetchContent_MakeAvailable: every dependency's source folder must lie
# below a trimmed prefix (a warning names one that does not).
function(dw4_check_trimmed_paths)
  foreach(dep IN LISTS ARGN)
    FetchContent_GetProperties(${dep} SOURCE_DIR dir)
    cmake_path(NORMAL_PATH dir)
    set(covered FALSE)
    foreach(p IN LISTS DW4_TRIMMED_PATHS)
      string(LENGTH "${p}/" n)
      string(SUBSTRING "${dir}/" 0 ${n} head)
      string(TOLOWER "${head}" head)
      string(TOLOWER "${p}/" want)
      if(head STREQUAL want)
        set(covered TRUE)
      endif()
    endforeach()
    if(NOT covered)
      message(WARNING "${dep}'s sources (${dir}) lie below no trimmed prefix: its __FILE__ keeps that path")
    endif()
  endforeach()
endfunction()
