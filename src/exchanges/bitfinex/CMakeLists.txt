add_library(quarkbot_bitfinex 
exchange.cpp
    instrument_map.hpp
    instrument_map.cpp
    instrument.cpp
    public_stream.hpp
    public_stream.cpp
    stream_manager.cpp
    stream_helpers.cpp
    account.hpp
    account.cpp
    signer.cpp)
target_link_libraries(quarkbot_bitfinex PRIVATE quarkbot_network  quarkbot_impl quarkbot_ifc)