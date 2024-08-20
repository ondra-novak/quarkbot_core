cmake_minimum_required(VERSION 3.5)



add_executable (simulator
    main.cpp
    sim_account.cpp
    sim_instrument.cpp     
    matching.cpp
    replay.cpp
    controller.cpp
    sim_exchange.cpp
)

add_dependencies(trading_api_common libjson20_single_header)
add_dependencies(trading_api_common trading_api_single_header)

