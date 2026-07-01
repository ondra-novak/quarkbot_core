find_package(ZLIB REQUIRED)

target_sources(quarkbot_impl PRIVATE 
        configured_data_source.cpp 
        merged_data_source.cpp
        mmbot_data_source.cpp
        tardis_data_source.cpp)
target_link_libraries(quarkbot_impl  PUBLIC ZLIB::ZLIB )

add_library(quarkbot::tardis ALIAS quarkbot_impl)