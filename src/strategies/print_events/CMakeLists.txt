cmake_minimum_required(VERSION 3.5)

add_library(print_events_strategy
    print_events.hpp
)

target_link_libraries(print_events_strategy PRIVATE quarkbot_impl quarkbot_ifc)

