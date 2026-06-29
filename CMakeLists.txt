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

option(quarkbot_DEVELOPMENT_MODE "(Quarkbot) Include development targets" ${PROJECT_IS_TOP_LEVEL})

include(FetchContent)
FetchContent_Declare(
    BasicCoro
    GIT_REPOSITORY https://www.github.com/ondra-novak/basic_coro.git
    GIT_TAG 749ffcc918f61d3a99e96d0b045dc1ac3379201a
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(BasicCoro)

OPTION(QUARKBOT_NETWORK "Compile network lib (requires openssl)" OFF)
OPTION(QUARKBOT_LEVELDB "Compile leveldb storage (requires leveldb)" OFF)
OPTION(QUARKBOT_TARDIS "Compile tardis importer (requires zlib)" OFF)
OPTION(QUARKBOT_TESTS "Compile tests (require everything)" ${quarkbot_DEVELOPMENT_MODE})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Default path to install" FORCE)
endif()
add_subdirectory("src")
if (quarkbot_DEVELOPMENT_MODE)
add_subdirectory("src/strategies/print_events")
add_subdirectory("src/strategies/trending")
add_subdirectory("src/exec")
endif()
if (QUARKBOT_TESTS)
enable_testing()
add_subdirectory("src/tests")
endif()

