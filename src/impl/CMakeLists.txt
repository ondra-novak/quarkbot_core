

add_library (quarkbot_impl  
    simexecutor.cpp
    simexchange.cpp
    siminstrument.cpp
    simaccount.cpp
    simtradableinstrument.cpp
)

target_link_libraries(quarkbot_impl BasicCoro::basic_coro)