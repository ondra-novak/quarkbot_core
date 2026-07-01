/*
#include <iostream>
#include <cstdlib>

#include "../common/scheduler.h"

template class quarkbot::Scheduler<void (*)() >;
template class quarkbot::SchedulerRealTime<void (*)() >;

*/
#include <quarkbot/exchange.hpp>
#include <quarkbot/underlying.hpp>
#include <quarkbot/account.hpp>
#include <quarkbot/defs.hpp>
#include <quarkbot/execution_worker.hpp>
#include <quarkbot/context.hpp>
#include <quarkbot/order.hpp>
#include <quarkbot/memory.hpp>
#include <quarkbot/tradable_instrument.hpp>
#include <quarkbot/message_bus.hpp>
#include <quarkbot/hub.hpp>
#include <quarkbot/shared_lockable.hpp>
#include <quarkbot/strategy_publisher.hpp>
#include "../quarkbot/streaming/lock_free_publisher.hpp"
#include "../quarkbot/streaming/publisher_manager.hpp"
#include "../quarkbot/streaming/orderbook_state.hpp"
#include <quarkbot/utils/signals.hpp>

template class quarkbot::Hub<int>;

int main() {
    return 0;
}


