cmake_minimum_required(VERSION 3.5)

add_library(trending_strategy
    trending_core.cpp
)

target_link_libraries(trending_strategy PRIVATE quarkbot_impl)

