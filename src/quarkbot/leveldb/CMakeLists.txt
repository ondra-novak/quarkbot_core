
#Find LevelDB using pkg-config
find_package(PkgConfig REQUIRED)

find_path(LEVELDB_INCLUDE_DIR leveldb/db.h)
find_library(LEVELDB_LIBRARIES leveldb)
if(NOT LEVELDB_LIBRARIES)
  message(FATAL_ERROR "libleveldb missing (libleveldb-dev)?")
endif()
if(NOT LEVELDB_INCLUDE_DIR)
  message(FATAL_ERROR "LevelDB headers missing (libleveldb-dev)?")
endif()



target_sources(quarkbot_impl PRIVATE leveldb_storage.cpp leveldb_storage.hpp)
target_link_libraries(quarkbot_impl PRIVATE ${LEVELDB_LIBRARIES})
target_include_directories(quarkbot_impl PUBLIC ${LEVELDB_INCLUDE_DIR})