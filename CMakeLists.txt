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
add_compile_options(-Wall -Wno-noexcept-type)

include(FetchContent)
FetchContent_Declare(
    BasicCoro
    GIT_REPOSITORY https://www.github.com/ondra-novak/basic_coro.git
    GIT_TAG 1e7b88f339de6cc7313f14108076c7ab49ee563e
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(BasicCoro)

OPTION(QUARKBOT_NETWORK "Compile network lib (requires openssl)" OFF)
OPTION(QUARKBOT_LEVELDB "Compile leveldb storage (requires leveldb)" OFF)
OPTION(QUARKBOT_TARDIS "Compile tardis importer (requires zlib)" OFF)
if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    OPTION(QUARKBOT_TESTS "Compile tests (require everything)" ON)
else()
    OPTION(QUARKBOT_TESTS "Compile tests (require everything)" OFF)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Default path to install" FORCE)
endif()
add_subdirectory("src")
add_subdirectory("src/strategies/print_events")
add_subdirectory("src/strategies/trending")
add_subdirectory("src/exec")
if (QUARKBOT_TESTS)
enable_testing()
add_subdirectory("src/tests")
endif()

