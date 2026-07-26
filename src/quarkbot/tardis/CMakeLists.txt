

target_sources(quarkbot_impl PRIVATE
        tardis_data_source.cpp)
target_link_libraries(quarkbot_impl  PUBLIC ZLIB::ZLIB )
