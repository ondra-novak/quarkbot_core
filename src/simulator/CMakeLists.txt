cmake_minimum_required(VERSION 3.5)

link_libraries(
     quarkbot_runtime
    ${STANDARD_LIBRARIES}
)


add_executable (simulator
    main.cpp
    sim_account.cpp
    sim_instrument.cpp
    matching.cpp
    replay.cpp
    controller.cpp
    sim_exchange.cpp
    sim_order.cpp
    sim_scheduler.cpp
)


add_dependencies(simulator libjson20_single_header)

