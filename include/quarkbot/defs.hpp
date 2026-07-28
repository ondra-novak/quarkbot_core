#pragma  once

#include "basic_coro/concepts.hpp"
#include <cassert>
#include <chrono>
#include <concepts>
#include <memory>
#include <basic_coro/awaitable.hpp>
#include <basic_coro/coroutine.hpp>
#include <type_traits>



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
class IOrder;
class IHistoryAdapter;
template<typename T> class ISerie;


using PAccount = std::shared_ptr<IAccount>;
using PExchange = std::shared_ptr<IExchange>;
using PMarketInstrument = std::shared_ptr<IMarketInstrument>;
using PTradableInstrument = std::shared_ptr<ITradableInstrument>;
using PStorage = std::shared_ptr<IStorage>;
using PExecutionWorker = std::shared_ptr<IExecutionWorker>;
using coroutine = coro::coroutine<void>;
using PMessageBus = std::shared_ptr<IMessageBus>;
using POrder = std::shared_ptr<IOrder>;
using PHistoryAdapter = std::shared_ptr<IHistoryAdapter>;
template<typename T> using PSerie = std::shared_ptr<ISerie<T> >;



template<typename _Hub, typename _Val>
concept HubProducer = requires(_Hub hub, _Val val) {
    {hub.send(std::move(val))} -> coro::is_awaitable;
    {hub.send(val)} -> coro::is_awaitable;    
};

template<typename _Hub, typename _Val>
concept HubReceiver = requires(_Hub hub, _Val &val) {
    {hub.receive(val)} -> coro::is_awaitable;    
};

template<typename T>
concept IsSerie = requires(T serie, typename T::value_type value, std::size_t index) {
    {serie.add(value)};
    {serie.reserve(index)};
    {serie[index]} -> std::same_as<std::optional<typename T::value_type> >;
    {serie.clone()} -> std::same_as<T>;
};

template<typename T>
concept BasicMathType = std::is_default_constructible_v<T> && std::is_constructible_v<T, double> && std::totally_ordered<T> && requires(T value) {
    {value+value} -> std::same_as<T>;
    {value-value} -> std::same_as<T>;
    {value*value} -> std::same_as<T>;
    {value/value} -> std::same_as<T>;
    {-value} -> std::same_as<T>;
};



}