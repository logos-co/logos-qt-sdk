cmake_minimum_required(VERSION 3.14)
project(LogosQtSdk)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core RemoteObjects)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core RemoteObjects)

# The protocol layer — transports, consumer core, token manager, the
# abstract LogosProviderObject interface and the lp_* C ABI. This SDK
# layers the Qt developer API (LogosAPI, provider base classes, the
# legacy QObject provider glue) on top of it.
if(NOT DEFINED LOGOS_PROTOCOL_ROOT)
    if(DEFINED ENV{LOGOS_PROTOCOL_ROOT})
        set(LOGOS_PROTOCOL_ROOT "$ENV{LOGOS_PROTOCOL_ROOT}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../logos-protocol/cpp/logos_protocol.h")
        set(LOGOS_PROTOCOL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../logos-protocol")
    endif()
endif()
if(EXISTS "${LOGOS_PROTOCOL_ROOT}/lib/cmake/logos-protocol")
    # Installed protocol package → proper layered link.
    find_package(logos-protocol REQUIRED
        PATHS "${LOGOS_PROTOCOL_ROOT}/lib/cmake/logos-protocol" NO_DEFAULT_PATH)
    set(LP_TARGET logos-protocol::logos_protocol)
elseif(EXISTS "${LOGOS_PROTOCOL_ROOT}/cpp/CMakeLists.txt")
    # Source checkout → build it as a subproject (dev convenience).
    add_subdirectory("${LOGOS_PROTOCOL_ROOT}/cpp"
                     "${CMAKE_BINARY_DIR}/logos-protocol-build")
    set(LP_TARGET logos_protocol)
else()
    message(FATAL_ERROR "logos-protocol not found. Set LOGOS_PROTOCOL_ROOT to an "
                        "installed logos-protocol prefix or a source checkout.")
endif()

# The Qt HOST RUNTIME — LogosAPI, LogosAPIProvider, LogosProviderBase, the
# legacy QObject adapter and the Qt argument decoder. All five translation units
# this SDK used to compile now live in logos-qt-host (logos-plugin-qt), which is
# where a Qt plugin build finds them too. This SDK keeps the public surface —
# `find_package(logos-qt-sdk)` and `logos-qt-sdk::logos_qt_sdk` are unchanged —
# but no longer owns the code, so nothing anywhere ends up with two copies of
# LogosAPI or of TokenManager's singleton.
#
# Unlike the logos-protocol block above there is no "source checkout" branch
# here. It would have to be an add_subdirectory(), and CMake refuses to
# install(EXPORT) a target whose link interface names a target that is not
# itself in an export set — so that branch would turn every build, not just an
# install, into a generate-time error. An installed prefix is what the Nix build
# and every consumer actually use.
if(NOT DEFINED LOGOS_QT_HOST_ROOT)
    if(DEFINED ENV{LOGOS_QT_HOST_ROOT})
        set(LOGOS_QT_HOST_ROOT "$ENV{LOGOS_QT_HOST_ROOT}")
    endif()
endif()
if(EXISTS "${LOGOS_QT_HOST_ROOT}/lib/cmake/logos-qt-host")
    find_package(logos-qt-host REQUIRED
        PATHS "${LOGOS_QT_HOST_ROOT}/lib/cmake/logos-qt-host" NO_DEFAULT_PATH)
else()
    message(FATAL_ERROR "logos-qt-host not found. Set LOGOS_QT_HOST_ROOT to an "
                        "installed logos-qt-host prefix (nix build "
                        "'github:logos-co/logos-plugin-qt#logos-qt-host').")
endif()

# No sources left, so this is an INTERFACE library: a name, a link interface and
# an include directory. Consumers see no difference — the archive they used to
# get from here now arrives through logos-qt-host's imported target — but two
# Windows-only mechanisms DID notice, because both were written against
# `logos-qt-sdk::logos_qt_sdk` being an archive.
#
# Both are gone now. The shared-runtime migration replaced them: logos-protocol
# and logos-plugin-qt ship real shared libraries that own their own symbols, and
# in-process consumers link those instead of emptying archives. liblogos-side
# whole-archive + generated .def went in logos-liblogos#182;
# LogosSharedFromDll.cmake was deleted from logos-basecamp (#348) and
# logos-logoscore-cli (#98). The invariant they protected is now asserted
# directly, by nix/symbol-gate.nix in those repos and in logos-standalone-app.
# See logos-protocol/cpp/logos_shared_api.h for the current scheme.
# ── logos-cpp-sdk, for the HOST capability ───────────────────────────────────
# logos_qt_host_core.h is Qt marshalling over logos::host::LogosCore, so the
# host target needs logos-cpp-sdk's include dir. Probed in both layouts like
# every other root here: an installed prefix exports the target, a source tree
# is added directly.
if(NOT DEFINED LOGOS_CPP_SDK_ROOT)
    if(DEFINED ENV{LOGOS_CPP_SDK_ROOT})
        set(LOGOS_CPP_SDK_ROOT "$ENV{LOGOS_CPP_SDK_ROOT}")
    else()
        set(LOGOS_CPP_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../logos-cpp-sdk")
    endif()
endif()

if(EXISTS "${LOGOS_CPP_SDK_ROOT}/lib/cmake/logos-cpp-sdk")
    find_package(logos-cpp-sdk REQUIRED
        PATHS "${LOGOS_CPP_SDK_ROOT}/lib/cmake/logos-cpp-sdk" NO_DEFAULT_PATH)
    set(LOGOS_CPP_SDK_HOST_TARGET logos-cpp-sdk::logos_host)
    set(LOGOS_CPP_SDK_HOST_INCLUDES "")
elseif(EXISTS "${LOGOS_CPP_SDK_ROOT}/cpp/CMakeLists.txt")
    add_subdirectory("${LOGOS_CPP_SDK_ROOT}/cpp"
                     "${CMAKE_CURRENT_BINARY_DIR}/logos-cpp-sdk-build")
    set(LOGOS_CPP_SDK_HOST_TARGET logos_host)
    set(LOGOS_CPP_SDK_HOST_INCLUDES "")
elseif(EXISTS "${LOGOS_CPP_SDK_ROOT}/include/cpp/logos_host_core.h"
       OR EXISTS "${LOGOS_CPP_SDK_ROOT}/include/logos_host_core.h")
    # Headers-only export layout (logos-cpp-sdk's nix/include.nix), which has no
    # exported CMake target. Carry the include dirs on logos_qt_host_core itself
    # under BUILD_INTERFACE rather than synthesising a target: a synthesised one
    # cannot go in install(EXPORT), and baking an absolute store path into the
    # INSTALL interface would make the package config non-relocatable.
    #
    # Nothing is lost for installed consumers. Anything linking the host
    # capability uses logos::host::LogosCore, so it already depends on
    # logos-cpp-sdk and brings its own include path.
    #
    # nix/include.nix ships the same headers at BOTH include/ and include/cpp/
    # (see the comment there: logos_lp_client.h must sit beside what it
    # includes), so naming both works with either.
    set(LOGOS_CPP_SDK_HOST_TARGET "")
    set(LOGOS_CPP_SDK_HOST_INCLUDES
        "${LOGOS_CPP_SDK_ROOT}/include/cpp"
        "${LOGOS_CPP_SDK_ROOT}/include")
else()
    message(FATAL_ERROR "logos-cpp-sdk not found. Set LOGOS_CPP_SDK_ROOT to an "
                        "installed prefix, a source tree, or a headers export "
                        "(looked for lib/cmake/, cpp/CMakeLists.txt and "
                        "include/cpp/logos_host_core.h under "
                        "${LOGOS_CPP_SDK_ROOT}).")
endif()

# ── Capability targets ──────────────────────────────────────────────────────
#
# Mirrors logos-cpp-sdk's split: host, module-consumer and module-provider are
# three distinct capabilities, so each gets a target and a consumer links only
# what it actually is.
#
# This is not cosmetic. The single `logos_qt_sdk` target below links
# logos-qt-host PUBLIC, so before the split ANY consumer of a seam header also
# linked the entire Qt host runtime. Measured: logos_qt_lp_bridge.h needs only
# Qt Core + the lp ABI, logos_qt_wire.h adds logos-protocol's converters, and
# logos_ui_plugin_context.h includes nothing but <type_traits>. None of the
# three needs LogosAPI. A ui_qml module backend that wants the Qt-typed
# dependency wrapper should not inherit the host runtime to get it.

add_library(logos_qt_common INTERFACE)
target_link_libraries(logos_qt_common INTERFACE
    Qt${QT_VERSION_MAJOR}::Core
)
target_include_directories(logos_qt_common INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# CALLING other modules: the Qt<->lp seam headers that every generated Qt-typed
# dependency wrapper opens with. Deliberately NO logos-qt-host.
add_library(logos_qt_consumer INTERFACE)
target_link_libraries(logos_qt_consumer INTERFACE
    logos_qt_common
    ${LP_TARGET}
)

# IMPLEMENTING a view plugin: logos_ui_plugin_context.h, the ui_qml backend
# seam. Header-only with no dependency beyond the include path.
add_library(logos_qt_provider INTERFACE)
target_link_libraries(logos_qt_provider INTERFACE
    logos_qt_common
)

# STANDING UP a core: Qt marshalling over logos::host::LogosCore. Named
# logos_qt_host_core, NOT logos_qt_host — logos-qt-host::logos_qt_host is
# logos-plugin-qt's Qt PLUGIN RUNTIME, a different capability, and two targets
# one word apart is exactly the confusion this repo has paid for before.
add_library(logos_qt_host_core INTERFACE)
target_link_libraries(logos_qt_host_core INTERFACE
    logos_qt_common
    ${LOGOS_CPP_SDK_HOST_TARGET}
)
foreach(_inc IN LISTS LOGOS_CPP_SDK_HOST_INCLUDES)
    target_include_directories(logos_qt_host_core INTERFACE $<BUILD_INTERFACE:${_inc}>)
endforeach()

# Umbrella. Everything that linked logos-qt-sdk::logos_qt_sdk before the split
# keeps the exact same link interface, INCLUDING logos-qt-host — which the two
# Windows-only mechanisms below depend on. Migrate to the narrow targets when
# touching a consumer; do not change this one's interface.
add_library(logos_qt_sdk INTERFACE)

target_link_libraries(logos_qt_sdk INTERFACE
    logos_qt_consumer
    logos_qt_provider
    logos-qt-host::logos_qt_host
    ${LP_TARGET}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::RemoteObjects
)

target_include_directories(logos_qt_sdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# There are no forwarding headers here any more.
#
# Between the host split and the repoint this package generated a forwarder for
# each of the six names that moved (logos_api.h, logos_api_provider.h,
# logos_provider_object.h, qt_provider_object.h, logos_qt_arg_decode.h,
# core/interface.h) so consumers could keep including them from this prefix
# unchanged. Every consumer now names logos-qt-host directly, so those files
# were two spellings of one header sitting on the same include path — the shape
# that lets a build silently pick the wrong copy. They are deleted, not
# deprecated: the only logos_api.h in a closure is logos-qt-host's.
#
# What this package still installs is what it owns: logos_ui_plugin_context.h,
# and (from the headers derivation, in the source-export layout) logos_qt_wire.h
# and logos_qt_lp_bridge.h. Consumers that also need the host runtime get its
# include directory transitively, through the logos-qt-host::logos_qt_host that
# the link interface above already names.

install(TARGETS logos_qt_sdk logos_qt_common logos_qt_consumer logos_qt_provider logos_qt_host_core
    EXPORT logos-qt-sdkTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(EXPORT logos-qt-sdkTargets
    FILE logos-qt-sdkTargets.cmake
    NAMESPACE logos-qt-sdk::
    DESTINATION lib/cmake/logos-qt-sdk
)

include(CMakePackageConfigHelpers)

# Where logos-qt-host was found when THIS package was built, baked into the
# generated Config as a HINTS fallback.
#
# `logos-qt-sdk::logos_qt_sdk` names logos-qt-host in its link interface, so a
# consumer that imports this package MUST be able to resolve logos-qt-host or
# the import fails outright. Most of them call
# `find_package(logos-qt-sdk PATHS <prefix> NO_DEFAULT_PATH)`, which tells CMake
# nothing about where anything else lives. HINTS is consulted AFTER
# CMAKE_PREFIX_PATH, so a consumer that does know (every Nix build, where
# logos-qt-host is a propagated input of this package) still wins; the hint only
# catches the ones that do not.
set(LOGOS_QT_HOST_HINT "${logos-qt-host_DIR}")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/logos-qt-sdkConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/logos-qt-sdkConfig.cmake"
    INSTALL_DESTINATION lib/cmake/logos-qt-sdk
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/logos-qt-sdkConfigVersion.cmake"
    VERSION 0.1.0
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/logos-qt-sdkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/logos-qt-sdkConfigVersion.cmake"
    DESTINATION lib/cmake/logos-qt-sdk
)

# This SDK's own Qt-typed header, at the flat include root the CMake export
# layout uses. nix/include.nix ships the same file (plus logos_qt_wire.h and
# logos_qt_lp_bridge.h) under include/cpp/ for the source-export layout; the two
# halves are joined in flake.nix and their file sets stay disjoint apart from
# this one deliberate mirror.
install(FILES
    logos_ui_plugin_context.h
    logos_qt_host_core.h
    DESTINATION include
)
