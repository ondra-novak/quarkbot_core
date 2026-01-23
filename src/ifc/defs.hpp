#pragma  once

#include <memory>
#include "../coro/src/basic_coro/awaitable.hpp"


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
template<StreamType T>
class IMarketEventStream;


using PAccount = std::shared_ptr <IAccount>;
using PExchange = std::shared_ptr<IExchange>;
using PMarketInstrument = std::shared_ptr<IMarketInstrument>;
using PTradableInstrument = std::shared_ptr<ITradableInstrument>;
using POrder = std::shared_ptr<IOrder>;
using PUnderlyingCurrency = std::shared_ptr<IUnderlyingCurrency>;
using PStorage = std::shared_ptr<IStorage>;
template<StreamType T>
using PMarketEventStream = std::shared_ptr<IMarketEventStream<T> >;






}