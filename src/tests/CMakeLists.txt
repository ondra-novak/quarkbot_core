set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/)



set(BASIC_TESTS
    compile_test.cpp
    decimal_test.cpp
    pubsub.cpp
    mem_storage_test.cpp
    tardis_source_test.cpp
    merged_source_test.cpp
    serializer.cpp
    signals.cpp
    websocket_binance.cpp
    logger.cpp
    hub.cpp
    timer.cpp
    csv_report.cpp
    config.cpp
    tagset.cpp
)



foreach (testFile ${BASIC_TESTS})
    string(REGEX MATCH "([^\/]+$)" filename ${testFile})
    string(REGEX MATCH "[^.]*" executable_name test_${filename})
    add_executable(${executable_name} ${testFile})
    target_link_libraries(${executable_name} PRIVATE quarkbot::sdk quarkbot::impl)
    target_compile_definitions(${executable_name} PRIVATE
        MODULE_PATH="${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
    )
    add_test(NAME "tests/${filename}" COMMAND ${executable_name})
endforeach()

# --- fixture plugin + test for the shared object module loader (somodule.hpp) ---

add_library(test_somodule_plugin SHARED plugins/test_plugin.cpp plugins/plugin_common.hpp)
target_link_libraries(test_somodule_plugin PRIVATE quarkbot::sdk)

add_executable(test_somodule_test somodule_test.cpp)
target_link_libraries(test_somodule_test PRIVATE quarkbot::sdk quarkbot::impl)
target_compile_definitions(test_somodule_test PRIVATE
    TEST_PLUGIN_PATH="$<TARGET_FILE:test_somodule_plugin>"
)
add_dependencies(test_somodule_test test_somodule_plugin)
add_test(NAME "tests/somodule_test" COMMAND test_somodule_test)

