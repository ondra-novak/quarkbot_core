#include "simhistoryadapter.hpp" 
#include "quarkbot/abstract/ieventstream.hpp"
#include "quarkbot/execution_worker.hpp"
#include "quarkbot/stream/history.hpp"

namespace quarkbot {

std::shared_ptr<IEventStreamBase> SimHistoryAdapter::subscribe_stream(std::size_t class_hash, const void *params)  {
    
    return _source(_instrument, class_hash, *static_cast<const HistoryDataRequest *>(params), ExecutionWorker::current().required().now());
}


}