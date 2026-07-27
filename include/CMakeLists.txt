add_library(quarkbot_sdk INTERFACE)

target_include_directories(quarkbot_sdk INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} ${basiccoro_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR}/..)
add_library(quarkbot::sdk ALIAS quarkbot_sdk)