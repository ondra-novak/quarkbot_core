find_package(ZLIB REQUIRED)

add_library(quarkbot_impl)
target_sources(quarkbot_impl PUBLIC
         FILE_SET HEADERS  
         BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.." FILES
            backtest.hpp
            merged_data_source.hpp
            report_fills_to_csv.hpp
            simtradableinstrument.hpp
            configured_data_source.hpp
            simaccount.hpp
            simexchange.hpp
            logger.hpp
            tardis_data_source.hpp
            leveldb_storage.hpp
            simexecutor.hpp
            backtest_executor.hpp
            streaming/lock_free_publisher.hpp
            streaming/orderbook_state.hpp
            streaming/subscriber.hpp
            streaming/publisher_manager.hpp
            streaming/publisher_base.hpp
            streaming/stream_mapping.hpp
            streaming/queue_event_stream.hpp
            streaming/multipublisher.hpp
            thread_executor.hpp
            mem_storage.hpp
            siminstrument.hpp
            mmbot_data_source.hpp
)

target_sources(  quarkbot_impl PRIVATE 
            merged_data_source.cpp
            thread_executor.cpp
            siminstrument.cpp
            logger.cpp
            tardis_data_source.cpp
            mmbot_data_source.cpp
            leveldb_storage.cpp
            simexecutor.cpp
            simaccount.cpp
            backtest.cpp
            simexchange.cpp
            backtest_executor.cpp
            simtradableinstrument.cpp
            configured_data_source.cpp
)

target_link_libraries(quarkbot_impl
    PRIVATE BasicCoro::basic_coro
    PRIVATE quarkbot_utils
    PRIVATE quarkbot_ifc
    PRIVATE ZLIB::ZLIB
    PRIVATE leveldb
    
)


