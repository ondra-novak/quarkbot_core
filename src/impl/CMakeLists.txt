

add_library(quarkbot_impl)
target_sources(quarkbot_impl PUBLIC
         FILE_SET HEADERS  
         BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.." FILES
            ./mem_storage.hpp
            ./streaming/subscriber.hpp
            ./streaming/orderbook_state.hpp
            ./streaming/queue_event_stream.hpp
            ./streaming/closed_bar_lambda.hpp
            ./streaming/stream_mapping.hpp
            ./streaming/multipublisher.hpp
            ./streaming/publisher_base.hpp
            ./streaming/ranged_bar_lambda.hpp
            ./streaming/stream_maps.hpp
            ./streaming/all_market_stream_manager.hpp
            ./streaming/publisher_manager.hpp
            ./streaming/lock_free_publisher.hpp
            ./simtradableinstrument.hpp
            ./mmbot_data_source.hpp
            ./merged_data_source.hpp
            ./thread_executor.hpp
            ./backtest_executor.hpp
            ./simexchange.hpp
            ./backtest.hpp
            ./simexecutor.hpp
            ./siminstrument.hpp
            ./logger.hpp
            ./simaccount.hpp
            ./order_trigger.hpp
            ./simexec_report_csv.hpp
)

target_sources(  quarkbot_impl PRIVATE 
        ./merged_data_source.cpp
        ./simtradableinstrument.cpp
        ./thread_executor.cpp
        ./siminstrument.cpp
        ./backtest.cpp
        ./backtest_executor.cpp
        ./mmbot_data_source.cpp
        ./simaccount.cpp
        ./order_trigger.cpp
        ./logger.cpp
        ./simexecutor.cpp
        ./simexchange.cpp
        
)

target_link_libraries(quarkbot_impl
    PRIVATE basic_coro::basic_coro
    PRIVATE quarkbot_utils
    PRIVATE quarkbot_ifc
   
)

add_library(quarkbot::impl ALIAS quarkbot_impl)
