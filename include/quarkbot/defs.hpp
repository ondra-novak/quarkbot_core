#pragma  once

#include "basic_coro/concepts.hpp"
#include <cassert>
#include <memory>
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
class IMessageBus;



using PAccount = std::shared_ptr<IAccount>;
using PExchange = std::shared_ptr<IExchange>;
using PMarketInstrument = std::shared_ptr<IMarketInstrument>;
using PTradableInstrument = std::shared_ptr<ITradableInstrument>;
using PStorage = std::shared_ptr<IStorage>;
using PExecutionWorker = std::shared_ptr<IExecutionWorker>;
using coroutine = coro::coroutine<void>;
using PMessageBus = std::shared_ptr<IMessageBus>;


template<typename _Hub, typename _Val>
concept HubProducer = requires(_Hub hub, _Val val) {
    {hub.send(std::move(val))} -> coro::is_awaitable;
    {hub.send(val)} -> coro::is_awaitable;    
};

template<typename _Hub, typename _Val>
concept HubReceiver = requires(_Hub hub, _Val &val) {
    {hub.receiver(val)} -> coro::is_awaitable;    
};



}