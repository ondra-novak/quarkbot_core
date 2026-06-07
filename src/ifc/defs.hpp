#pragma  once

#include <cassert>
#include <concepts>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <basic_coro/awaitable.hpp>
#include <basic_coro/coroutine.hpp>



namespace quarkbot {

template<typename T> using awaitable = coro::awaitable<T>;


class IAccount;
class IExchange;
class IMarketInstrument;
class ITradableInstrument;
class IStorage;
class IScheduler;
class IExecutionWorker;
class IBacktestDataSource;
class IMessageBus;



using PAccount = std::shared_ptr<IAccount>;
using PExchange = std::shared_ptr<IExchange>;
using PMarketInstrument = std::shared_ptr<IMarketInstrument>;
using PTradableInstrument = std::shared_ptr<ITradableInstrument>;
using PStorage = std::shared_ptr<IStorage>;
using PExecutionWorker = std::shared_ptr<IExecutionWorker>;
using coroutine = coro::coroutine<void>;
using PBacktestDataSource = std::shared_ptr<IBacktestDataSource>;
using PMessageBus = std::shared_ptr<IMessageBus>;

}