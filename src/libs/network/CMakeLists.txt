

add_library(quarkbot_network ssl_stream.cpp sslobject.cpp ws.cpp socket.cpp
    rest.cpp)
target_link_libraries(quarkbot_network PRIVATE ssl crypto)
