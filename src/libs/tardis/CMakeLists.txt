find_package(ZLIB REQUIRED)
add_library(quarkbot_tardis)
target_sources(quarkbot_tardis PUBLIC 
        tardis_data_source.hpp 
        configured_data_source.hpp)
target_sources(quarkbot_tardis PRIVATE 
        configured_data_source.cpp 
        tardis_data_source.cpp)
target_link_libraries(quarkbot_tardis  PUBLIC ZLIB::ZLIB quarkbot_ifc quarkbot_impl)

add_library(quarkbot::tardis ALIAS quarkbot_tardis)