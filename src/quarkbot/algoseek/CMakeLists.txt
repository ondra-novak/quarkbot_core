find_package(ZLIB REQUIRED)

target_sources(quarkbot_impl PRIVATE
        algoseek_spec.cpp
        algoseek_spec.hpp)
target_link_libraries(quarkbot_impl PUBLIC ZLIB::ZLIB)
