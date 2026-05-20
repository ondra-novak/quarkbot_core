add_library(quarkbot_ifc INTERFACE)

target_link_libraries(quarkbot_ifc INTERFACE BasicCoro::basic_coro)
target_sources(quarkbot_ifc INTERFACE
         FILE_SET HEADERS  
         BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.."
         FILES account.hpp               exchange.hpp           message_bus.hpp    schema.hpp             streaming.hpp
backtest_data_source.hpp  hub.hpp                order_defs.hpp     storage.hpp            stream_transform.hpp
context.hpp               log.hpp                order.hpp          storage_namespace.hpp  tradable_instrument.hpp
defs.hpp                  market_instrument.hpp  order_storage.hpp  strategy_fragment.hpp  types.hpp
execution_worker.hpp      memory.hpp             reporting.hpp      stream_defs.hpp        underlying.hpp )