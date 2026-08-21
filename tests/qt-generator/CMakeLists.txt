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
#     reason on every backend that could otherwise emit one. Since `--backend
#     qt` was retired, `consumer` is the only such backend: it is the only one
#     that can emit a CALLER for a method whose empty answer would be
#     indistinguishable from a failed call.
#
# These drive the installed generator binary rather than linking its internals,
# because the binary is what module-builder actually invokes.

find_program(LOGOS_QT_GENERATOR logos-qt-generator REQUIRED)

set(_fixtures ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
set(_goldens  ${CMAKE_CURRENT_SOURCE_DIR}/goldens)
set(_driver   ${CMAKE_CURRENT_SOURCE_DIR}/run_generator.cmake)
set(_work     ${CMAKE_CURRENT_BINARY_DIR}/work)

function(_qtgen_test name mode backend lidl)
  cmake_parse_arguments(A "" "GOLDEN_DIR;EXPECT_TEXT" "EXTRA_ARGS;REQUIRE_TEXT;FORBID_TEXT" ${ARGN})
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
  # Multi-value options travel as ONE -D per option, entries joined by '|'.
  #
  # NOT by ';', which is what a CMake list already is: `add_test(COMMAND
  # ${_cmd})` expands _cmd, and an ELEMENT that itself contains ';' is spliced
  # into further arguments at that point — so `-DEXTRA_ARGS=--binding;origin`
  # reaches the driver as two argv entries, `-DEXTRA_ARGS=--binding` and a
  # stray `origin`. Measured: every --binding test silently ran with no
  # --binding flag at all, and the one asserting a REFUSAL was the only reason
  # it did not pass. '|' appears in none of these needles and survives the
  # round trip intact; the driver splits it back.
  foreach(_opt EXTRA_ARGS REQUIRE_TEXT FORBID_TEXT)
    if(A_${_opt})
      string(REPLACE ";" "|" _joined "${A_${_opt}}")
      list(APPEND _cmd "-D${_opt}=${_joined}")
    endif()
  endforeach()
  list(APPEND _cmd -P ${_driver})
  add_test(NAME qtgen.${name} COMMAND ${_cmd})
endfunction()

# --- 1. back-compat: no optionals => byte-identical output -------------------
_qtgen_test(plain_consumer golden consumer plain_module.lidl GOLDEN_DIR plain_consumer)

# --- 2. the optional paths generate at all -----------------------------------
_qtgen_test(optional_consumer generate consumer optional_module.lidl)

# --- 3. an optional RETURN is refused, with its reason ------------------------
# Asserting the REASON, not just the failure: a bare non-zero exit would also be
# satisfied by an unrelated parse error, which would quietly retire the tripwire.
_qtgen_test(refuse_return_consumer refuse consumer refuses_optional_return.lidl
            EXPECT_TEXT "optional RETURN")

# --- 3b. the LogosAPI-FREE consumer binding (--binding origin) ----------------
#
# Same contract, same public signatures, same goldens discipline — the only
# difference is how the wrapper reaches a transport. `--bind` picks the call
# TARGET; `--binding` picks the call ORIGIN.
#
# The golden is the full record. The REQUIRE/FORBID tests below it name the
# three properties the change exists for, so that each fails with its own
# sentence rather than as one hunk of a diff nobody reads:
#
#   1. no LogosAPI anywhere in the emitted wrapper — the thing that makes this
#      surface reachable from a cdylib module, which has no such object;
#   2. the origin is the constructor's own parameter, threaded verbatim;
#   3. the emitted wrapper never reaches for forTarget, the one factory that
#      DERIVES an origin (from `api->moduleName()`). A wrapper that derived its
#      origin from a LogosAPI it was handed would call out under the LENDER's
#      identity, carrying the lender's capabilities — measured once already, and
#      invisible to the transport, which sees a well-formed call from a module
#      that really does have that name.
_qtgen_test(plain_consumer_origin golden consumer plain_module.lidl
            GOLDEN_DIR plain_consumer_origin
            EXTRA_ARGS --binding origin)

_qtgen_test(origin_consumer_holds_no_logosapi generate consumer plain_module.lidl
            EXTRA_ARGS --binding origin
            FORBID_TEXT LogosAPI)

_qtgen_test(origin_consumer_asserts_its_own_origin generate consumer plain_module.lidl
            EXTRA_ARGS --binding origin
            REQUIRE_TEXT "PlainModule::PlainModule(const QString& origin)"
                         "LpBridge::forOrigin(origin, QStringLiteral(\"plain_module\"))"
            FORBID_TEXT forTarget "moduleName()")

# The BOUND flavour: target chosen at runtime, origin still asserted. Both are
# ctor parameters, and the emitted body must pass BOTH through untouched — a
# bound wrapper that kept the origin but re-derived the target (or vice versa)
# would type-check perfectly.
_qtgen_test(bound_origin_consumer_takes_origin_and_target generate consumer plain_module.lidl
            EXTRA_ARGS --bind bound --binding origin
            REQUIRE_TEXT "PlainModule::PlainModule(const QString& origin, const QString& target)"
                         "LpBridge::forOrigin(origin, target)"
            FORBID_TEXT LogosAPI forTarget)

# A misspelt flavour is REFUSED, not defaulted. Defaulting `--binding orgin`
# back to the LogosAPI form would emit a wrapper whose constructor the
# origin-bound umbrella cannot call, and the diagnostic would arrive as a
# constructor mismatch inside generated code.
_qtgen_test(refuse_unknown_binding refuse consumer plain_module.lidl
            EXTRA_ARGS --binding orgin
            EXPECT_TEXT "Unknown --binding")

# --- 3c. the provider-rejection detector matches a CLOSED SET of codes --------
#
# The emitted detector folds a provider REFUSAL into logos::CallError instead of
# letting the return decode erase it into a default. It matched the single
# literal "dispatch_failed" until providers were found to be emitting
# "invalid_args" for an arity error with nobody detecting it — measured as
# `logosctl call test_basic_module isPositive` (argument missing) exiting 0 with
# status "ok" and the refusal object as its result.
#
# Named REQUIRE/FORBID rather than left to the golden: the golden says "this
# exact text" and gets refreshed as a unit, and the property that matters here
# is that all three codes are present and the OLD single-literal compare is
# gone. `unknown_method` is asserted even though no provider emits it — that
# readiness is the reason the detectors were widened before the providers.
_qtgen_test(consumer_rejection_detector_matches_closed_code_set
            generate consumer plain_module.lidl
            REQUIRE_TEXT "_code != \"dispatch_failed\""
                         "_code != \"invalid_args\""
                         "_code != \"unknown_method\""
            FORBID_TEXT "code->get<std::string>() != \"dispatch_failed\"")

# The NEGATIVE half, stated as absences a golden refresh cannot quietly restore.
# Widening one literal into a set is one edit away from an OPEN match on any
# {code, message, origin} object, which would hand every method that legitimately
# returns a three-string map to the error channel. The three shape guards are
# what keep it closed, so require them by name.
_qtgen_test(consumer_rejection_detector_stays_narrow
            generate consumer plain_module.lidl
            REQUIRE_TEXT "if (!v.is_object() || v.size() != 3) return false;"
                         "if (!code->is_string() || !message->is_string() || !origin->is_string()) return false;"
                         "if (code == v.end() || message == v.end() || origin == v.end()) return false;")

# --- 4. two consumer wrappers COMPILE in one translation unit -----------------
#
# The tests above run the generator and read its text; none of them ever hands
# the result to a compiler, and none of them generates more than one contract.
# Both gaps met in the same defect: the umbrella (`logos_sdk.cpp`) `#include`s
# every dependency's wrapper .cpp, so a two-dependency consumer compiles two
# wrappers into ONE TU, and the file-scope rejection helper was redefined. See
# fixtures/umbrella_tu.cpp.
#
# A library target rather than a ctest case: there is nothing to run, the claim
# is that it compiles. It fails the BUILD, which is the earliest and loudest
# place to fail.
set(_tu_dir ${CMAKE_CURRENT_BINARY_DIR}/umbrella_tu)
set(_tu_generated
    ${_tu_dir}/plain_module_api.cpp    ${_tu_dir}/plain_module_api.h
    ${_tu_dir}/optional_module_api.cpp ${_tu_dir}/optional_module_api.h)

add_custom_command(
    OUTPUT ${_tu_generated}
    COMMAND ${LOGOS_QT_GENERATOR} --lidl ${_fixtures}/plain_module.lidl
            --backend consumer --module plain_module --output-dir ${_tu_dir}
    COMMAND ${LOGOS_QT_GENERATOR} --lidl ${_fixtures}/optional_module.lidl
            --backend consumer --module optional_module --output-dir ${_tu_dir}
    DEPENDS ${_fixtures}/plain_module.lidl ${_fixtures}/optional_module.lidl
    COMMENT "Generating two consumer wrappers for the single-TU compile check"
)
add_custom_target(qtgen_umbrella_sources DEPENDS ${_tu_generated})

# STATIC and never linked — the wrappers reference the SDK's bridge symbols,
# which only a real module binary resolves. Compiling is the whole test.
add_library(qtgen_umbrella_tu STATIC ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/umbrella_tu.cpp)
add_dependencies(qtgen_umbrella_tu qtgen_umbrella_sources)
target_include_directories(qtgen_umbrella_tu PRIVATE ${_tu_dir})
target_link_libraries(qtgen_umbrella_tu PRIVATE
    logos_qt_sdk
    Qt${QT_VERSION_MAJOR}::Core
)

# --- 5. the origin-bound surface COMPILES WITH NO LogosAPI -------------------
#
# The single most important target in this file, and the one the whole
# `--binding origin` change exists to make possible. Everything above reads the
# generator's TEXT; this hands both halves of the surface to a compiler:
#
#   * `fixtures/origin_umbrella/logos_sdk.h` — the umbrella, emitted by
#     logos-cpp-sdk's logos-cpp-generator (`--api-style qt --binding origin`)
#     and CHECKED IN rather than generated here. See the README beside it: this
#     repo's test derivation takes logos-cpp-generator from its LOCKED
#     logos-cpp-sdk input, so generating it live would tie a green
#     `nix flake check` of this repo to a revision of another one.
#   * the two wrappers below, emitted live by the generator under test, one
#     Static (a name-baked dependency) and one Bound (a runtime-bound
#     interface).
#
# The two are written by different tools in different repos and have to agree on
# a constructor signature; compiling them together is the only thing that checks
# it. A STATIC library and never linked, for the same reason as the target
# above: the wrappers reference the SDK's bridge symbols, which only a real
# module binary resolves. Compiling is the whole test.
set(_origin_dir ${CMAKE_CURRENT_BINARY_DIR}/origin_umbrella_tu)
set(_origin_generated
    ${_origin_dir}/plain_module_api.cpp    ${_origin_dir}/plain_module_api.h
    ${_origin_dir}/optional_module_api.cpp ${_origin_dir}/optional_module_api.h)

add_custom_command(
    OUTPUT ${_origin_generated}
    COMMAND ${LOGOS_QT_GENERATOR} --lidl ${_fixtures}/plain_module.lidl
            --backend consumer --module plain_module
            --bind static --binding origin --output-dir ${_origin_dir}
    COMMAND ${LOGOS_QT_GENERATOR} --lidl ${_fixtures}/optional_module.lidl
            --backend consumer --module optional_module
            --bind bound --binding origin --output-dir ${_origin_dir}
    DEPENDS ${_fixtures}/plain_module.lidl ${_fixtures}/optional_module.lidl
    COMMENT "Generating the LogosAPI-free consumer wrappers for the origin-bound compile check"
)
add_custom_target(qtgen_origin_umbrella_sources DEPENDS ${_origin_generated})

add_library(qtgen_origin_umbrella_tu STATIC
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/origin_umbrella_tu.cpp)
add_dependencies(qtgen_origin_umbrella_tu qtgen_origin_umbrella_sources)
# The generated wrappers first, then the checked-in umbrella beside them — the
# umbrella `#include`s the wrapper headers by name, so both directories have to
# be on the path.
target_include_directories(qtgen_origin_umbrella_tu PRIVATE
    ${_origin_dir}
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/origin_umbrella
)
target_link_libraries(qtgen_origin_umbrella_tu PRIVATE
    logos_qt_sdk
    Qt${QT_VERSION_MAJOR}::Core
)

# --- 6. the widened types round-trip REAL VALUES through the emitted code -----
#
# Everything above reads the generator's TEXT or hands it to a compiler. Neither
# catches the failure this backend's type mapping actually has, because it is
# not a compile error and not a diff: `QVariant::fromValue(QList<qulonglong>)`
# compiles, produces a QVariant, and serialises to JSON **null** — logos-
# protocol's qvariantToNlohmann matches a CLOSED userType() set that
# QList<qulonglong> is not in. The other direction is just as quiet:
# qvariant_cast back from a QVariantList yields an EMPTY list. A wrapper built
# that way passes every assertion in this file and loses the payload of every
# typed call.
#
# So this one EXECUTES the generated code. `roundtrip_tu.cpp` #includes the
# generated .cpp (the umbrella_tu trick, which is what makes the file-local
# record codecs reachable) and pushes boundary values through them: 2^64-1,
# bytes with an embedded NUL and a high byte, empty vs present optionals,
# nested containers, and the mistyped elements that must be refused rather than
# coerced.
#
# It LINKS, unlike the two compile-only targets above — the generated method
# bodies call the bridge, so this also proves the widened wrappers resolve.
set(_rt_dir ${CMAKE_CURRENT_BINARY_DIR}/roundtrip_tu)
set(_rt_generated ${_rt_dir}/widened_module_api.cpp ${_rt_dir}/widened_module_api.h)

add_custom_command(
    OUTPUT ${_rt_generated}
    COMMAND ${LOGOS_QT_GENERATOR} --lidl ${_fixtures}/widened_module.lidl
            --backend consumer --module widened_module --output-dir ${_rt_dir}
    DEPENDS ${_fixtures}/widened_module.lidl
    COMMENT "Generating the widened-types consumer wrapper for the round-trip test"
)
add_custom_target(qtgen_roundtrip_sources DEPENDS ${_rt_generated})

add_executable(qtgen_roundtrip_tests ${CMAKE_CURRENT_SOURCE_DIR}/roundtrip_tu.cpp)
add_dependencies(qtgen_roundtrip_tests qtgen_roundtrip_sources)
target_include_directories(qtgen_roundtrip_tests PRIVATE ${_rt_dir})
target_link_libraries(qtgen_roundtrip_tests PRIVATE
    logos_qt_sdk
    GTest::gtest
    GTest::gtest_main
    Qt${QT_VERSION_MAJOR}::Core
)
gtest_discover_tests(qtgen_roundtrip_tests)

# --- 4. --backend ui: the view plugin's host-facing surface -------------------
#
# This backend had NO test at all, which is how it came to be the one generated
# plugin class that did not publish the teardown hook. ui-host reaches
# aboutToUnload() BY NAME through the meta-object; a plugin class that does not
# declare it has no such meta-method, invokeMethod returns false, and the host
# moves on silently. So dropping this emission again would not fail a build, or
# a load, or a call -- every view would simply, and permanently, lose its chance
# to finish. Nothing else in this repo would notice.
set(_ui_fixtures ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui)

add_test(NAME qtgen.ui_plugin_surface COMMAND ${CMAKE_COMMAND}
    -DGENERATOR=${LOGOS_QT_GENERATOR}
    -DMETADATA=${_ui_fixtures}/metadata.json
    -DREP=${_ui_fixtures}/Ticker.rep
    -DOUT_DIR=${_work}/ui_plugin_surface
    # The three properties the host depends on, each named so it fails with its
    # own sentence:
    #   * the hook is INVOKABLE and returns int -- the host reads it back with
    #     Q_RETURN_ARG(int) and must not need the SDK enum to resolve it;
    #   * the completion signal exists -- a view that answers Asynchronous with
    #     no way to say it is done gets refused the wait outright;
    #   * the emission is QUEUED -- the backend may finish on any thread, and the
    #     host is waiting on the plugin's.
    "-DREQUIRE_TEXT=Q_INVOKABLE int aboutToUnload();|void unloadFinished();|Q_SIGNALS:|maybeUiPluginAboutToUnload|Qt::QueuedConnection"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_ui_generator.cmake)
