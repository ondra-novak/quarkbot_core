

add_library(quarkbot_network)
target_sources(  quarkbot_network PUBLIC
         FILE_SET HEADERS  
         BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../.."
         FILES base64.hpp
            crack_url.hpp
            http_parser.hpp
            http_status_exception.hpp
            rest_client.hpp
            rest.hpp
            socket.hpp
            sslobjects.hpp
            ssl_stream.hpp
            stream_concept.hpp
            string_utils.hpp
            ws.hpp
            ws_parser.hpp
            ws_stream.hpp
)

target_sources(quarkbot_network PRIVATE 
        rest.cpp
        socket.cpp
        sslobject.cpp
        ssl_stream.cpp
        ws.cpp
)

target_link_libraries(quarkbot_network PRIVATE ssl crypto)
