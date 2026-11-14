# Language packs next to the program (docs/L10N.md), run after dw4_game
# builds: data/lang/<code>/ (language.ini, textures.tsv: committed) and a
# translator's local/lang/<code>/game.tsv (game-derived: never committed,
# never installed) into <program folder>/lang/<code>/.
#   cmake -DSOURCE=<repo> -DTARGET=<program folder> -P copy_lang.cmake
foreach(base "${SOURCE}/data/lang" "${SOURCE}/local/lang")
  if(NOT IS_DIRECTORY "${base}")
    continue()
  endif()
  file(GLOB codes RELATIVE "${base}" "${base}/*")
  foreach(code IN LISTS codes)
    if(NOT IS_DIRECTORY "${base}/${code}")
      continue()
    endif()
    file(GLOB files "${base}/${code}/*.ini" "${base}/${code}/*.tsv" "${base}/${code}/*.po")
    foreach(file IN LISTS files)
      get_filename_component(name "${file}" NAME)
      file(MAKE_DIRECTORY "${TARGET}/lang/${code}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${file}" "${TARGET}/lang/${code}/${name}")
    endforeach()
  endforeach()
endforeach()
