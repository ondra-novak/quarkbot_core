add_executable(print_events_backtest print_events.cpp)
target_link_libraries(print_events_backtest PRIVATE print_events_strategy quarkbot::impl quarkbot::sdk)
#add_executable(trending_fast_test trending_fast_test.cpp)
#target_link_libraries(trending_fast_test PRIVATE trending_strategy quarkbot::impl quarkbot::sdk)


add_executable(minute2replay minute2replay.cpp)
add_executable(mean_reversion_trending mean_reversion_trending.cpp)
target_link_libraries(mean_reversion_trending PRIVATE  quarkbot::impl quarkbot::sdk)

