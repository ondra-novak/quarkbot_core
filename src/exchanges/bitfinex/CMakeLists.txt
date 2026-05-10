add_library(quarkbot_bitfinex exchange.cpp
    instrument_map.hpp
    instrument_map.cpp
    instrument.cpp)
target_link_libraries(quarkbot_bitfinex PRIVATE quarkbot_network  quarkbot_impl quarkbot_ifc)