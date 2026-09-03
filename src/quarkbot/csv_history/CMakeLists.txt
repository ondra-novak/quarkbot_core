find_package(ZLIB REQUIRED)

target_sources(quarkbot_impl PRIVATE
        history_csv_source.cpp
        history_csv_source.hpp
        formats.hpp
        csv_source.hpp)
target_link_libraries(quarkbot_impl  PUBLIC ZLIB::ZLIB )

