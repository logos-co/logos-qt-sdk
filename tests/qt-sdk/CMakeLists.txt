find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

# logos-cpp-generator (a build-time tool from logos-cpp-sdk) generates the
# provider-dispatch fixture exercised by test_provider_dispatch.
find_program(LOGOS_CPP_GENERATOR logos-cpp-generator REQUIRED)

set(SAMPLE_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample_provider.h)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(GENERATED_DISPATCH ${GENERATED_DIR}/logos_provider_dispatch.cpp)

file(MAKE_DIRECTORY ${GENERATED_DIR})

add_custom_command(
    OUTPUT ${GENERATED_DISPATCH}
    COMMAND ${LOGOS_CPP_GENERATOR} --provider-header ${SAMPLE_HEADER} --output-dir ${GENERATED_DIR}
    DEPENDS ${SAMPLE_HEADER}
    COMMENT "Generating provider dispatch for sample_provider"
)

add_executable(qt_sdk_tests
    test_main.cpp
    test_logos_result.cpp
    test_logos_mock_setup.cpp
    test_logos_api_client_mock.cpp
    test_logos_api.cpp
    test_logos_api_client_overloads.cpp
    test_logos_provider_base.cpp
    test_module_proxy.cpp
    test_auth_token_enforcement.cpp
    test_inform_module_token_auth.cpp
    test_logos_api_provider.cpp
    test_local_transport_integration.cpp
    test_worker_thread_ipc.cpp
    test_event_system.cpp
    test_async_calls.cpp
    test_provider_dispatch.cpp
    test_std_string_overloads.cpp
    fixtures/sample_provider.cpp
    ${GENERATED_DISPATCH}
)

target_include_directories(qt_sdk_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
    ${GENERATED_DIR}
)

target_link_libraries(qt_sdk_tests PRIVATE
    logos_qt_sdk
    GTest::gtest
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::RemoteObjects
    Qt${QT_VERSION_MAJOR}::Test
)

gtest_discover_tests(qt_sdk_tests)
