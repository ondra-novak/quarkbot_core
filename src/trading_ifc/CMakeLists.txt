cmake_minimum_required(VERSION 3.5)

file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

file(MAKE_DIRECTORY ${CMAKE_HEADER_OUTPUT_DIRECTORY}/trading_api)

set(OUTPUT "${CMAKE_HEADER_OUTPUT_DIRECTORY}/trading_api/strategy.h"
           "${CMAKE_HEADER_OUTPUT_DIRECTORY}/trading_api/exchange.h"
)

set(FILES_TO_COPY 
        module.h
        csv.h
        weak_object_map.h
        shared_lockable_ptr.h
)
    
set(DESTINATION_DIR "${CMAKE_HEADER_OUTPUT_DIRECTORY}/trading_api")

set(FULL_PATH_FILES_TO_COPY)
foreach(FILE ${FILES_TO_COPY})
    list(APPEND FULL_PATH_FILES_TO_COPY ${CMAKE_CURRENT_SOURCE_DIR}/${FILE})
endforeach()    

add_custom_command(
    OUTPUT "${CMAKE_HEADER_OUTPUT_DIRECTORY}/trading_api/strategy.h"
    COMMAND to_single_header ARGS strategy_api.h ${CMAKE_HEADER_OUTPUT_DIRECTORY}/trading_api/strategy.h
    DEPENDS ${SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating strategy API"
    VERBATIM
)

add_custom_command(
    OUTPUT "${CMAKE_HEADER_OUTPUT_DIRECTORY}/trading_api/exchange.h"
    COMMAND to_single_header ARGS exchange_api.h ${CMAKE_HEADER_OUTPUT_DIRECTORY}/trading_api/exchange.h
    DEPENDS ${SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating exchange API"
    VERBATIM
) 

add_custom_target(trading_api_single_header ALL DEPENDS ${OUTPUT})
add_dependencies(trading_api_single_header to_single_header)
add_custom_command(TARGET trading_api_single_header POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DESTINATION_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${FULL_PATH_FILES_TO_COPY} ${DESTINATION_DIR}
    COMMENT "copying other headers"
)



