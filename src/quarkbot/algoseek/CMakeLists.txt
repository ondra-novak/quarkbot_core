find_package(ZLIB REQUIRED)

target_sources(quarkbot_impl PRIVATE
        algoseek_spec.hpp
        algoseek_data_source.cpp
        algoseek_data_source.hpp)
target_link_libraries(quarkbot_impl PUBLIC ZLIB::ZLIB)
