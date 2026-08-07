# qt-generator tests.
#
# Two jobs, and the first matters more than the second:
#
#  1. BACK-COMPAT. plain_module.lidl has no optional anywhere, and its goldens
#     were produced by the generator as it stood BEFORE optionality. Any diff
#     means a change leaked into a path that was supposed to be untouched.
#     The goldens are a review artifact, not a snapshot to refresh on red.
#
#  2. The optional paths generate, and an optional RETURN is refused with its
#     reason on every backend that could otherwise emit one.
#
# These drive the installed generator binary rather than linking its internals,
# because the binary is what module-builder actually invokes.

find_program(LOGOS_QT_GENERATOR logos-qt-generator REQUIRED)

set(_fixtures ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
set(_goldens  ${CMAKE_CURRENT_SOURCE_DIR}/goldens)
set(_driver   ${CMAKE_CURRENT_SOURCE_DIR}/run_generator.cmake)
set(_work     ${CMAKE_CURRENT_BINARY_DIR}/work)

function(_qtgen_test name mode backend lidl)
  cmake_parse_arguments(A "" "GOLDEN_DIR;EXPECT_TEXT" "" ${ARGN})
  set(_cmd ${CMAKE_COMMAND}
      -DGENERATOR=${LOGOS_QT_GENERATOR}
      -DLIDL=${_fixtures}/${lidl}
      -DBACKEND=${backend}
      -DMODE=${mode}
      -DOUT_DIR=${_work}/${name})
  if(A_GOLDEN_DIR)
    list(APPEND _cmd -DGOLDEN_DIR=${_goldens}/${A_GOLDEN_DIR})
  endif()
  if(A_EXPECT_TEXT)
    list(APPEND _cmd -DEXPECT_TEXT=${A_EXPECT_TEXT})
  endif()
  list(APPEND _cmd -P ${_driver})
  add_test(NAME qtgen.${name} COMMAND ${_cmd})
endfunction()

# --- 1. back-compat: no optionals => byte-identical output -------------------
_qtgen_test(plain_qt       golden qt       plain_module.lidl GOLDEN_DIR plain_qt)
_qtgen_test(plain_consumer golden consumer plain_module.lidl GOLDEN_DIR plain_consumer)

# --- 2. the optional paths generate at all -----------------------------------
_qtgen_test(optional_qt       generate qt       optional_module.lidl)
_qtgen_test(optional_consumer generate consumer optional_module.lidl)

# --- 3. an optional RETURN is refused, with its reason ------------------------
# Asserting the REASON, not just the failure: a bare non-zero exit would also be
# satisfied by an unrelated parse error, which would quietly retire the tripwire.
_qtgen_test(refuse_return_qt       refuse qt       refuses_optional_return.lidl
            EXPECT_TEXT "optional RETURN")
_qtgen_test(refuse_return_consumer refuse consumer refuses_optional_return.lidl
            EXPECT_TEXT "optional RETURN")
