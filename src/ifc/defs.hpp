#pragma  once

#include <memory>
#include "coro/src/basic_coro/awaitable.hpp"
#include "coro/src/basic_coro/coroutine.hpp"



namespace quarkbot {

template<typename T> using awaitable = coro::awaitable<T>;

struct StreamTypeItem {};

template<typename T>
concept StreamType = std::is_base_of_v<StreamTypeItem, T>;


class IAccount;
class IExchange;
class IMarketInstrument;
class ITradableInstrument;
class IOrder;
class IUnderlyingCurrency;
class IStorage;
class IScheduler;
template<StreamType T>
class IMarketEventStream;
class IExecutionWorker;
class IBacktestDataSource;

using PAccount = std::shared_ptr <IAccount>;
using PExchange = std::shared_ptr<IExchange>;
using PMarketInstrument = std::shared_ptr<IMarketInstrument>;
using PTradableInstrument = std::shared_ptr<ITradableInstrument>;
using POrder = std::shared_ptr<IOrder>;
using PUnderlyingCurrency = std::shared_ptr<IUnderlyingCurrency>;
using PStorage = std::shared_ptr<IStorage>;
template<StreamType T>
using PMarketEventStream = std::shared_ptr<IMarketEventStream<T> >;
using PScheduler = std::shared_ptr<IScheduler>;
using PExecutionWorker = std::shared_ptr<IExecutionWorker>;
using coroutine = coro::coroutine<void>;
using PBacktestDataSource = std::shared_ptr<IBacktestDataSource>;



}