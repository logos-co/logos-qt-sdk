cmake_minimum_required(VERSION 3.14)
project(LogosQtSdkTests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(GTest REQUIRED)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core RemoteObjects)

enable_testing()
include(GoogleTest)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../cpp ${CMAKE_CURRENT_BINARY_DIR}/logos_qt_sdk)

add_subdirectory(qt-sdk)
