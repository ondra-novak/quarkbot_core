cmake_minimum_required(VERSION 3.5)



add_executable (simulator
    main.cpp
    sim_account.cpp
    sim_instrument.cpp     
    matching.cpp
    replay.cpp
    controller.cpp
    sim_exchange.cpp
    sim_order.cpp
)

add_dependencies(simulator libjson20_single_header)

