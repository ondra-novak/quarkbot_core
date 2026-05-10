set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/)



set(BASIC_TESTS
    compile_test.cpp
    decimal_test.cpp
    pubsub.cpp
    mem_storage_test.cpp
    instrument_spec_test.cpp
    tardis_source_test.cpp
    merged_source_test.cpp
    order_test.cpp
    serializer.cpp
    signals.cpp
    websocket_binance.cpp
    bitfinex_instruments.cpp
)



foreach (testFile ${BASIC_TESTS})
    string(REGEX MATCH "([^\/]+$)" filename ${testFile})
    string(REGEX MATCH "[^.]*" executable_name test_${filename})
    add_executable(${executable_name} ${testFile})
    target_link_libraries(${executable_name} PRIVATE BasicCoro::basic_coro quarkbot_impl)
    target_compile_definitions(${executable_name} PRIVATE
        MODULE_PATH="${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
    )
    add_test(NAME "tests/${filename}" COMMAND ${executable_name})
endforeach()

target_link_libraries(test_websocket_binance PRIVATE quarkbot_network)
target_link_libraries(test_bitfinex_instruments PRIVATE quarkbot_bitfinex)