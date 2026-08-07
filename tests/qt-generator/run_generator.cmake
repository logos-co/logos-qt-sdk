# Script-mode driver for the qt-generator tests.
#
# CTest cannot express "run a tool, then compare a directory of outputs" or
# "run a tool and assert it FAILED with this text" directly, and neither is
# worth a gtest binary that would only shell out anyway. Both are a few lines
# here, and keeping them in CMake means the tests need no compilation and no
# extra link against the generator's internals.
#
# Modes (-DMODE=...):
#   generate  run the generator; fail if it does not exit 0
#   golden    run the generator, then require byte-identical output vs GOLDEN_DIR
#   refuse    run the generator; require a NON-zero exit AND that stderr
#             contains EXPECT_TEXT
#
# Required: GENERATOR, LIDL, BACKEND, OUT_DIR. Golden mode also needs GOLDEN_DIR;
# refuse mode also needs EXPECT_TEXT.

if(NOT DEFINED GENERATOR OR NOT DEFINED LIDL OR NOT DEFINED BACKEND OR NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "run_generator.cmake: GENERATOR, LIDL, BACKEND and OUT_DIR are required")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(_args --lidl "${LIDL}" --backend "${BACKEND}" --output-dir "${OUT_DIR}")
# The qt backend names the impl header it includes; fixed so the emitted
# `#include` is stable across machines and therefore diffable.
if(BACKEND STREQUAL "qt")
  list(APPEND _args --impl-header fixture_impl.h)
endif()

execute_process(
  COMMAND "${GENERATOR}" ${_args}
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
)

if(MODE STREQUAL "refuse")
  if(_rc EQUAL 0)
    message(FATAL_ERROR
      "expected the generator to REFUSE ${LIDL} on --backend ${BACKEND}, but it exited 0.\n"
      "If optional returns are now representable end to end, read "
      "lidlCheckOptionalReturns before changing this test.")
  endif()
  string(FIND "${_err}" "${EXPECT_TEXT}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR
      "the generator refused ${LIDL}, but the message does not explain why.\n"
      "expected to contain: ${EXPECT_TEXT}\ngot: ${_err}")
  endif()
  return()
endif()

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "generator failed on ${LIDL} (--backend ${BACKEND}):\n${_err}")
endif()

if(NOT MODE STREQUAL "golden")
  return()
endif()

# Byte-identity against the checked-in goldens. Compare in BOTH directions so a
# newly-emitted file is a failure too, not a silent addition.
file(GLOB _gold RELATIVE "${GOLDEN_DIR}" "${GOLDEN_DIR}/*")
file(GLOB _got  RELATIVE "${OUT_DIR}"    "${OUT_DIR}/*")
if(NOT _gold STREQUAL _got)
  message(FATAL_ERROR
    "generated file SET changed for ${LIDL} (--backend ${BACKEND}).\n"
    "golden:    ${_gold}\ngenerated: ${_got}")
endif()

foreach(_f IN LISTS _gold)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${GOLDEN_DIR}/${_f}" "${OUT_DIR}/${_f}"
    RESULT_VARIABLE _diff
  )
  if(NOT _diff EQUAL 0)
    execute_process(COMMAND diff -u "${GOLDEN_DIR}/${_f}" "${OUT_DIR}/${_f}"
                    OUTPUT_VARIABLE _d ERROR_VARIABLE _d)
    message(FATAL_ERROR
      "${_f} is no longer byte-identical for a contract with NO optionals.\n"
      "This path was supposed to be untouched — review the diff rather than "
      "refreshing the golden:\n${_d}")
  endif()
endforeach()
