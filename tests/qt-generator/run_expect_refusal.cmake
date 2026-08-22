# Script-mode driver for "this invocation must be REFUSED, and say why".
#
# Separate from run_generator.cmake because it asserts the opposite outcome:
# a non-zero exit and a specific sentence on stderr. A retired backend that
# merely exits non-zero is not enough — the whole point of refusing loudly is
# that the message names the binary the work moved to.
#
# Required: CMD, ARGS (';'-separated), REQUIRE_STDERR ('|'-separated needles).
# Optional: EXPECT_FAIL (default 1).

if(NOT DEFINED CMD OR NOT DEFINED ARGS)
  message(FATAL_ERROR "run_expect_refusal.cmake: CMD and ARGS are required")
endif()
if(NOT DEFINED EXPECT_FAIL)
  set(EXPECT_FAIL 1)
endif()

execute_process(
  COMMAND "${CMD}" ${ARGS}
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
)

if(EXPECT_FAIL AND _rc EQUAL 0)
  message(FATAL_ERROR
    "expected a refusal (non-zero exit) but the command SUCCEEDED.\n"
    "A retired backend that still runs is the failure this test exists to catch:\n"
    "it emits something, and whatever it emits is by definition unmaintained.\n"
    "stdout:\n${_out}\nstderr:\n${_err}")
endif()

if(DEFINED REQUIRE_STDERR)
  string(REPLACE "|" ";" _needles "${REQUIRE_STDERR}")
  foreach(_n ${_needles})
    string(FIND "${_err}" "${_n}" _pos)
    if(_pos EQUAL -1)
      message(FATAL_ERROR
        "the refusal did not mention:\n  ${_n}\n"
        "Exiting non-zero is not enough — the message must name what took the "
        "work over, or the caller gets a parse error instead of the fix.\n"
        "stderr was:\n${_err}")
    endif()
  endforeach()
endif()
