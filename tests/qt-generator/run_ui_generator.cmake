# Script-mode driver for `--backend ui`.
#
# Separate from run_generator.cmake because the UI backend takes a different
# input shape entirely: a metadata.json plus a .rep (the view contract), not a
# .lidl. Folding both into one driver would mean a branch on BACKEND in every
# argv line, for two callers.
#
# Required: GENERATOR, METADATA, REP, OUT_DIR.
# Optional: REQUIRE_TEXT / FORBID_TEXT, '|'-separated (see run_generator.cmake
#           for why '|' and not ';').

if(NOT DEFINED GENERATOR OR NOT DEFINED METADATA OR NOT DEFINED REP OR NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "run_ui_generator.cmake: GENERATOR, METADATA, REP and OUT_DIR are required")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

execute_process(
  COMMAND "${GENERATOR}" --backend ui
          --metadata "${METADATA}" --rep "${REP}" --output-dir "${OUT_DIR}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "generator failed (rc=${_rc})\n${_out}\n${_err}")
endif()

file(GLOB _files "${OUT_DIR}/*")
if(_files STREQUAL "")
  message(FATAL_ERROR "generator emitted nothing into ${OUT_DIR}")
endif()

set(_all "")
foreach(_f ${_files})
  file(READ "${_f}" _c)
  string(APPEND _all "${_c}")
endforeach()

if(DEFINED REQUIRE_TEXT)
  string(REPLACE "|" ";" _needles "${REQUIRE_TEXT}")
  foreach(_n ${_needles})
    string(FIND "${_all}" "${_n}" _pos)
    if(_pos EQUAL -1)
      message(FATAL_ERROR "generated UI glue is missing required text:\n  ${_n}")
    endif()
  endforeach()
endif()

if(DEFINED FORBID_TEXT)
  string(REPLACE "|" ";" _needles "${FORBID_TEXT}")
  foreach(_n ${_needles})
    string(FIND "${_all}" "${_n}" _pos)
    if(NOT _pos EQUAL -1)
      message(FATAL_ERROR "generated UI glue contains forbidden text:\n  ${_n}")
    endif()
  endforeach()
endif()
