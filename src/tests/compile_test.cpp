/*
#include <iostream>
#include <cstdlib>

#include "../common/scheduler.h"

template class quarkbot::Scheduler<void (*)() >;
template class quarkbot::SchedulerRealTime<void (*)() >;

*/
#include "../ifc/exchange.hpp"
#include "../ifc/underlying.hpp"
#include "../ifc/account.hpp"
#include "../ifc/defs.hpp"
#include "../ifc/execution_worker.hpp"
#include "../ifc/context.hpp"
#include "../ifc/order.hpp"
#include "../ifc/memory.hpp"
#include "../ifc/scheduler.hpp"
#include "../ifc/storage.hpp"
#include "../ifc/tradable_instrument.hpp"
#include "../impl/streaming/event_stream_publisher.hpp"
#include "../impl/streaming/queue_stream_publisher.hpp"
#include "../impl/streaming/publisher_manager.hpp"
int main(int argc, char **argv) {
    return 0;
}


