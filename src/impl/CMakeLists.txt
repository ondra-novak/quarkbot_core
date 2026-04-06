

add_library (quarkbot_impl  
    backtest_executor.cpp
    simexecutor.cpp
    simexchange.cpp
    siminstrument.cpp
    simaccount.cpp
    simtradableinstrument.cpp
    backtest.cpp
)

target_link_libraries(quarkbot_impl BasicCoro::basic_coro)