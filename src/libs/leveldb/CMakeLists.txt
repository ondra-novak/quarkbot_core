
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



add_library(quarkbot_leveldb_storage)
target_sources(quarkbot_leveldb_storage PUBLIC leveldb_storage.hpp)
target_sources(quarkbot_leveldb_storage PRIVATE leveldb_storage.cpp)
target_link_libraries(quarkbot_leveldb_storage  PUBLIC ${LEVELDB_LIBRARIES}  quarkbot_ifc quarkbot_impl)

add_library(quarkbot::leveldb-storage ALIAS quarkbot_leveldb_storage)