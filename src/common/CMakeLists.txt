cmake_minimum_required(VERSION 3.5)


add_library (quarkbot_runtime
    loader.cpp
    config_desc.cpp
    basic_context.cpp
    basic_exchange.cpp
    basic_log.cpp
    basic_mq.cpp
    context_scheduler.cpp
    basic_network.cpp
    websocket_client.cpp
    rest_client.cpp
    leveldb_storage.cpp
    memory_storage.cpp
    basic_config.cpp
    read_line.cpp
    console.cpp
    report_ldjson.cpp
)

add_dependencies(quarkbot_runtime libjson20_single_header)

