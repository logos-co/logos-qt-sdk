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
# `logos-qt-sdk::logos_qt_sdk` being an archive. See logos-liblogos/src and
# LogosSharedFromDll.cmake in logos-basecamp / logos-logoscore-cli.
add_library(logos_qt_sdk INTERFACE)

target_link_libraries(logos_qt_sdk INTERFACE
    logos-qt-host::logos_qt_host
    ${LP_TARGET}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::RemoteObjects
)

target_include_directories(logos_qt_sdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# The forwarding headers.
#
# Every name this SDK used to ship is still installed under every path it used
# to be installed at, so no consumer changes an include line and no consumer
# changes a layout probe -- logos-module-builder, logos-basecamp and
# logos-test-framework all decide "source checkout or installed prefix?" by
# testing for include/cpp/logos_api.h, and would report "logos-qt-sdk not found"
# while looking straight at the prefix if it disappeared.
#
# They are generated rather than committed because the include they contain is
# an absolute path into logos-qt-host; forward-header.h.in explains why nothing
# relative can work here.
#
# Ask the imported target where its headers are rather than assuming
# <prefix>/include, so a layout change in logos-qt-host fails here at configure
# time instead of producing forwarders that point at nothing.
get_target_property(_qt_host_include_dirs
    logos-qt-host::logos_qt_host INTERFACE_INCLUDE_DIRECTORIES)
if(NOT _qt_host_include_dirs)
    message(FATAL_ERROR
        "logos-qt-host::logos_qt_host declares no INTERFACE_INCLUDE_DIRECTORIES; "
        "cannot generate the forwarding headers.")
endif()
list(GET _qt_host_include_dirs 0 LOGOS_QT_HOST_INCLUDE)

set(LOGOS_QT_SDK_FORWARDED_HEADERS
    logos_api.h
    logos_api_provider.h
    logos_provider_object.h
    qt_provider_object.h
    logos_qt_arg_decode.h
    # The legacy Qt plugin interface (PluginInterface / initLogos(LogosAPI*)),
    # which consumers include as "core/interface.h" -- the subdirectory is part
    # of the name, so it is forwarded under that name too.
    core/interface.h
)

set(_forward_dir "${CMAKE_CURRENT_BINARY_DIR}/forward")
foreach(FORWARD_HEADER IN LISTS LOGOS_QT_SDK_FORWARDED_HEADERS)
    if(NOT EXISTS "${LOGOS_QT_HOST_INCLUDE}/${FORWARD_HEADER}")
        message(FATAL_ERROR
            "logos-qt-host does not ship ${FORWARD_HEADER} at "
            "${LOGOS_QT_HOST_INCLUDE}; a forwarder for it would be a dangling "
            "include that only fails in a consumer's build.")
    endif()
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/forward-header.h.in"
                   "${_forward_dir}/${FORWARD_HEADER}" @ONLY)
endforeach()

install(TARGETS logos_qt_sdk
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
# Consumers must not have to learn about logos-qt-host to keep working — that is
# the whole point of the forwarding step — and most of them call
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

# Headers keep their historical names AND their historical positions, so
# existing `#include "logos_api.h"` / `#include "core/interface.h"` lines
# resolve unchanged whichever of this prefix's include directories a consumer
# put on the search path. The forwarders are installed twice on purpose --
# flat, and again under include/cpp/ where the source-export layout put them --
# because consumers add both directories and probe the second one. Duplicated
# forwarders are harmless: each one names the same absolute file, and that file
# has its own include guard.
#
# logos_ui_plugin_context.h is NOT forwarded: it is this SDK's own header, not
# part of the host runtime that moved.
install(FILES
    ${_forward_dir}/logos_api.h
    ${_forward_dir}/logos_api_provider.h
    ${_forward_dir}/logos_provider_object.h
    ${_forward_dir}/qt_provider_object.h
    ${_forward_dir}/logos_qt_arg_decode.h
    logos_ui_plugin_context.h
    DESTINATION include
)
install(FILES
    ${_forward_dir}/logos_api.h
    ${_forward_dir}/logos_api_provider.h
    ${_forward_dir}/logos_provider_object.h
    ${_forward_dir}/qt_provider_object.h
    ${_forward_dir}/logos_qt_arg_decode.h
    DESTINATION include/cpp
)

# Legacy Qt plugin interface (PluginInterface / initLogos(LogosAPI*)) —
# installed at include/core/ exactly where logos-cpp-sdk shipped it, and a
# forwarder now like the rest.
install(FILES
    ${_forward_dir}/core/interface.h
    DESTINATION include/core
)
