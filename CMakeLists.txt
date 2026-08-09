cmake_minimum_required(VERSION 3.28)
project (trading_ifc)
set (CMAKE_CXX_STANDARD 23)
if (MSVC)
	add_compile_options(/W4 /EHsc /DNOMINMAX /permissive-   )
	set(STANDARD_LIBRARIES "")
else()
	add_compile_options(-Wall -Wextra -Wpedantic -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion -Wnull-dereference -Wdouble-promotion -Wformat=2)
	set(STANDARD_LIBRARIES "pthread")    
endif()

option(quarkbot_DEVELOPMENT_MODE "(Quarkbot) Include development targets" ${PROJECT_IS_TOP_LEVEL})

include(FetchContent)
FetchContent_Declare(
    BasicCoro
    GIT_REPOSITORY https://www.github.com/ondra-novak/basic_coro.git
    GIT_TAG 52c6e68132f02b70b07d9d68711fe48843a41430
    EXCLUDE_FROM_ALL
    SOURCE_SUBDIR nocompile
)
FetchContent_MakeAvailable(BasicCoro)

OPTION(QUARKBOT_NETWORK "Compile network lib (requires openssl)" OFF)
OPTION(QUARKBOT_LEVELDB "Compile leveldb storage (requires leveldb)" OFF)
OPTION(QUARKBOT_TESTS "Compile tests (require everything)" ${quarkbot_DEVELOPMENT_MODE})

configure_file(include/quarkbot/compile_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/quarkbot_compile_config.h)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/)

find_package(ZLIB REQUIRED)

add_subdirectory("include")
add_subdirectory("src/quarkbot")



if (quarkbot_DEVELOPMENT_MODE)
add_subdirectory("src/strategies/print_events")
add_subdirectory("src/strategies/trending")
add_subdirectory("src/exec")
endif()
if (QUARKBOT_TESTS)
enable_testing()
add_subdirectory("src/tests")
endif()

