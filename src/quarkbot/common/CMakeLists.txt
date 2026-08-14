target_sources(quarkbot_impl PRIVATE 
    logger.cpp 
    logger.hpp 
    mem_storage.hpp 
    order_trigger.cpp 
    order_trigger.hpp 
    thread_executor.cpp 
    thread_executor.hpp
    somodule.cpp
    order_internal_defs.hpp
    account_base.cpp
    tradable_instrument_base.cpp
    storage_common.cpp
    storage_msgbus_replicator.hpp
    storage_msgbus_replicator.cpp
    strategy_config.cpp
    msgbus_stdio.hpp
    msgbus_stdio.cpp

)