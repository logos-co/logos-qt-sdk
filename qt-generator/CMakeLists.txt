cmake_minimum_required(VERSION 3.14)
project(LogosQtGenerator)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

# logos-lidl: the canonical, language-neutral LIDL frontend (lexer/parser/AST/
# serializer/validator). Both generators link it, so the parsed surface can
# never skew between them.
find_package(logos-lidl REQUIRED)

# The shared C++/Qt codegen BACKEND helpers (Qt type-name mapping + the C++
# impl-header source parser + the compat shim onto logos-lidl's AST), still
# distributed by logos-cpp-sdk under share/lidl-frontend/. Only these — not the
# frontend — are source-shared; the frontend itself comes from logos-lidl above.
if(NOT DEFINED LIDL_FRONTEND_DIR)
    if(DEFINED ENV{LIDL_FRONTEND_DIR})
        set(LIDL_FRONTEND_DIR $ENV{LIDL_FRONTEND_DIR})
    else()
        message(FATAL_ERROR "LIDL_FRONTEND_DIR is not set (point it at logos-cpp-sdk's share/lidl-frontend)")
    endif()
endif()

add_executable(logos-qt-generator
    main.cpp
    lidl_gen_provider.cpp
    lidl_gen_cdylib_glue.cpp
    lidl_gen_ui.cpp
    ${LIDL_FRONTEND_DIR}/impl_header_parser.cpp
    ${LIDL_FRONTEND_DIR}/lidl_emit_common.cpp
)

target_include_directories(logos-qt-generator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIDL_FRONTEND_DIR}
)

target_link_libraries(logos-qt-generator PRIVATE Qt${QT_VERSION_MAJOR}::Core logos-lidl::logos_lidl)

install(TARGETS logos-qt-generator RUNTIME DESTINATION bin)
