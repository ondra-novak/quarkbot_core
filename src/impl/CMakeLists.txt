find_package(ZLIB REQUIRED)

add_library(quarkbot_impl
    mmbot_data_source.cpp
    backtest_executor.cpp
    simexecutor.cpp
    simexchange.cpp
    siminstrument.cpp
    simaccount.cpp
    simtradableinstrument.cpp
    backtest.cpp
    configured_data_source.cpp
    tardis_data_source.cpp
    merged_data_source.cpp
)

target_link_libraries(quarkbot_impl
    PUBLIC BasicCoro::basic_coro
    PRIVATE ZLIB::ZLIB
)
