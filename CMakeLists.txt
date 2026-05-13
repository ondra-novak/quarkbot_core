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
    GIT_TAG 6a8203366291f822c1706ed1d0212ec73e80b149
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(BasicCoro)


#Find LevelDB using pkg-config
find_package(PkgConfig REQUIRED)
#pkg_check_modules(LIBSSL REQUIRED openssl)
#pkg_check_modules(LIBWEBSOCKETS REQUIRED libwebsockets)


find_library(LEVELDB_LIBRARIES leveldb)
#if(NOT LEVELDB_LIBRARIES)
  #message(FATAL_ERROR "libleveldb missing (libleveldb-dev)?")
#endif()


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Default path to install" FORCE)
endif()
enable_testing()
include_directories(BEFORE ${CMAKE_CURRENT_LIST_DIR}/src)
add_subdirectory("src/ifc")
add_subdirectory("src/impl")
add_subdirectory("src/strategies/print_events")
add_subdirectory("src/strategies/trending")
add_subdirectory("src/exec")
add_subdirectory("src/tests")
add_subdirectory("src/libs/network")
add_subdirectory("src/exchanges/bitfinex")
