
add_subdirectory("ifc" EXCLUDE_FROM_ALL)
add_subdirectory("impl" EXCLUDE_FROM_ALL)
add_subdirectory("utils" EXCLUDE_FROM_ALL)
if (QUARKBOT_NETWORK OR QUARKBOT_TESTS)
add_subdirectory("libs/network" EXCLUDE_FROM_ALL)
endif()
if (QUARKBOT_LEVELDB)
add_subdirectory("libs/leveldb" EXCLUDE_FROM_ALL)
endif()
if (QUARKBOT_TARDIS OR QUARKBOT_TESTS)
add_subdirectory("libs/tardis" EXCLUDE_FROM_ALL)
endif()

