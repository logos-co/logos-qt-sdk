find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

add_executable(qt_sdk_tests
    test_main.cpp
    # The HOST capability's Qt marshalling layer. Only the std <-> Qt edge is
    # tested here; the substance lives in logos-cpp-sdk's test_logos_host_core.
    test_qt_host_core.cpp
    # Argument decoding for a Qt-typed provider — moved here with the decoder
    # itself, which logos-protocol hosted but never used.
    test_qt_arg_decode.cpp
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
    # Provider-side ARGUMENT decoding (registry entry Q1) for the QMetaObject
    # provider site, plus the pure logos::qtArgFromVariant<T> converter every
    # generated dispatch is built from. Carries the four typed-array cases the
    # logoscore CLI cannot express as arguments.
    test_provider_arg_validation.cpp
    test_std_string_overloads.cpp
    # The Qt <-> canonical-JSON edge the generated Qt consumer wrapper is built
    # from (logos_qt_wire.h). One round-trip per surface type, so the generator
    # can emit one pair of calls everywhere instead of a table per direction.
    test_qt_wire.cpp
)

target_include_directories(qt_sdk_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(qt_sdk_tests PRIVATE
    logos_qt_sdk
    GTest::gtest
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::RemoteObjects
    Qt${QT_VERSION_MAJOR}::Test
)

gtest_discover_tests(qt_sdk_tests)
