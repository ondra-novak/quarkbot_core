add_library(quarkbot_ifc INTERFACE)

target_link_libraries(quarkbot_ifc INTERFACE BasicCoro::basic_coro)
target_sources(quarkbot_ifc INTERFACE
         FILE_SET HEADERS  
         BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.." FILES 
            tradable_instrument.hpp
            strategy_fragment.hpp
            strategy_publisher.hpp
            order.hpp
            types.hpp
            context.hpp
            schema.hpp
            stream_transform.hpp
            message_bus.hpp
            order_defs.hpp
            order_storage.hpp
            defs.hpp
            memory.hpp
            timer.hpp
            reporting.hpp
            execution_worker.hpp
            storage_namespace.hpp
            minimutex.hpp
            storage.hpp
            backtest_data_source.hpp
            account.hpp
            exchange.hpp
            underlying.hpp
            shared_lockable.hpp
            log.hpp
            hub.hpp
            stream_defs.hpp
            market_instrument.hpp
            streaming.hpp
)