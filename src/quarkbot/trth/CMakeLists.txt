find_package(ZLIB REQUIRED)

target_sources(quarkbot_impl PRIVATE
        trth_raw_source.cpp
        trth_raw_source.hpp
        trth_event_source.cpp)
target_link_libraries(quarkbot_impl  PUBLIC ZLIB::ZLIB )

