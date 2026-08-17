

add_library(print_events_strategy
    print_events.hpp
)

target_link_libraries(print_events_strategy PUBLIC quarkbot::impl)

