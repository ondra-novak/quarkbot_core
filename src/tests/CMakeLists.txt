set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/)



set(BASIC_TESTS
    compile_test.cpp
    decimal_test.cpp
    pubsub.cpp
    strategy_publisher.cpp
    mem_storage_test.cpp
    tardis_source_test.cpp
    algoseek_source_test.cpp
    merged_source_test.cpp
    serializer.cpp
    signals.cpp
    ta.cpp
    websocket_binance.cpp
    logger.cpp
    hub.cpp
    timer.cpp
    csv_report.cpp
    config.cpp
    tagset.cpp
    strategy_fragment.cpp
    replay_csv.cpp
    persistent.cpp
    json_number.cpp
)



foreach (testFile ${BASIC_TESTS})
    string(REGEX MATCH "([^\/]+$)" filename ${testFile})
    string(REGEX MATCH "[^.]*" executable_name test_${filename})
    add_executable(${executable_name} ${testFile})
    target_link_libraries(${executable_name} PRIVATE quarkbot::sdk quarkbot::backtest)
    target_compile_definitions(${executable_name} PRIVATE
        MODULE_PATH="${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        TEST_DATA_PATH="${CMAKE_CURRENT_LIST_DIR}/data"
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

# --- LevelDB storage test ---
# QUARKBOT_LEVELDB stays OFF even under QUARKBOT_TESTS (it needs libleveldb-dev),
# so this test is registered only when the component is actually built.
if (QUARKBOT_LEVELDB)
    add_executable(test_leveldb_storage_test leveldb_storage_test.cpp)
    target_link_libraries(test_leveldb_storage_test PRIVATE quarkbot::sdk quarkbot::impl ${LEVELDB_LIBRARIES})
    add_test(NAME "tests/leveldb_storage_test" COMMAND test_leveldb_storage_test)
endif()

