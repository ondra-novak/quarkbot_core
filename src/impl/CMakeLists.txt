cmake_minimum_required(VERSION 3.5)


add_library (quarkbot_impl  
    base_order.cpp
    stream_impl.cpp
    thread_worker.cpp
    worker_base.cpp
    scheduler_rt.cpp
    backtest_worker.cpp
    backtest_exchange.cpp
    data_source_base.cpp
    update_orderbook.cpp
)

