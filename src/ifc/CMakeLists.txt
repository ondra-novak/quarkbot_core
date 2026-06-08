add_library(quarkbot_ifc INTERFACE)

target_link_libraries(quarkbot_ifc INTERFACE BasicCoro::basic_coro)
target_sources(quarkbot_ifc INTERFACE
         FILE_SET HEADERS  
         BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.." FILES 
            ./tradable_instrument.hpp
            ./strategy_fragment.hpp
            ./strategy_publisher.hpp
            ./order.hpp
            ./abstract/iexchange.hpp
            ./abstract/imarket_instrument.hpp
            ./abstract/order_internal.hpp
            ./abstract/iexecution_worker.hpp
            ./abstract/itradable_instrument.hpp
            ./abstract/imessage_bus.hpp
            ./abstract/iaccount.hpp
            ./abstract/ipublisher.hpp
            ./abstract/ieventstream.hpp
            ./types.hpp
            ./context.hpp
            ./schema.hpp
            ./stream_transform.hpp
            ./message_bus.hpp
            ./order_defs.hpp
            ./order_storage.hpp
            ./defs.hpp
            ./memory.hpp
            ./execution_worker.hpp
            ./storage_namespace.hpp
            ./minimutex.hpp
            ./storage.hpp
            ./backtest_data_source.hpp
            ./stream/periodic_snapshot.hpp
            ./stream/funding_event.hpp
            ./stream/auction.hpp
            ./stream/rangedbar.hpp
            ./stream/trade.hpp
            ./stream/orderbook.hpp
            ./stream/external_fill.hpp
            ./stream/tradestat.hpp
            ./stream/quote.hpp
            ./stream/ticker.hpp
            ./stream/closedbar.hpp
            ./account.hpp
            ./exchange.hpp
            ./underlying.hpp
            ./shared_lockable.hpp
            ./log.hpp
            ./hub.hpp
            ./stream_defs.hpp
            ./market_instrument.hpp
            ./event_stream.hpp
            ./config.hpp
)