

add_library(quarkbot_impl)
add_library(quarkbot::impl ALIAS quarkbot_impl)
add_library(quarkbot_backtest)
add_library(quarkbot::backtest ALIAS quarkbot_backtest)

target_include_directories(quarkbot_impl INTERFACE "${CMAKE_CURRENT_LIST_DIR}/..")
target_link_libraries(quarkbot_impl PUBLIC quarkbot_sdk)
target_link_libraries(quarkbot_backtest PUBLIC quarkbot_impl)

add_subdirectory("backtest" EXCLUDE_FROM_ALL)
add_subdirectory("common" EXCLUDE_FROM_ALL)

if (QUARKBOT_NETWORK OR QUARKBOT_TESTS)
add_subdirectory("network" EXCLUDE_FROM_ALL)
endif()
if (QUARKBOT_LEVELDB)
add_subdirectory("leveldb" EXCLUDE_FROM_ALL)
endif()
add_subdirectory("tardis" EXCLUDE_FROM_ALL)
add_subdirectory("trth" EXCLUDE_FROM_ALL)


target_include_directories(quarkbot_backtest PRIVATE ${CMAKE_BINARY_DIR}/)
target_include_directories(quarkbot_impl PRIVATE ${CMAKE_BINARY_DIR}/)