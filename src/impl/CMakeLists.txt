

add_library (quarkbot_impl  
    simexecutor.cpp
    simexchange.cpp
    siminstrument.cpp
)

target_link_libraries(quarkbot_impl BasicCoro::basic_coro)