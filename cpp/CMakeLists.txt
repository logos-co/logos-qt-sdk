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

set(QT_SDK_SOURCES
    logos_api.cpp
    logos_api.h
    logos_api_provider.cpp
    logos_api_provider.h
    logos_provider_object.cpp
    logos_provider_object.h
    qt_provider_object.cpp
    qt_provider_object.h
)

add_library(logos_qt_sdk STATIC ${QT_SDK_SOURCES})

target_link_libraries(logos_qt_sdk PUBLIC
    ${LP_TARGET}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::RemoteObjects
)

target_include_directories(logos_qt_sdk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(logos_qt_sdk PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

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

# Headers keep their historical names so existing `#include "logos_api.h"`
# lines resolve unchanged once consumers add this SDK's include dir.
install(FILES
    logos_api.h
    logos_api_provider.h
    logos_provider_object.h
    qt_provider_object.h
    DESTINATION include
)

# Legacy Qt plugin interface (PluginInterface / initLogos(LogosAPI*)) —
# installed at include/core/ exactly where logos-cpp-sdk shipped it.
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/interface.h
    DESTINATION include/core
)
