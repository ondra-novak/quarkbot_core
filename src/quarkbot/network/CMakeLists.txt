
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSSL REQUIRED openssl)


target_sources(quarkbot_impl PRIVATE 
        rest.cpp
        socket.cpp
        sslobject.cpp
        ssl_stream.cpp
        ws.cpp
)

target_link_libraries(quarkbot_impl PRIVATE ssl crypto)

