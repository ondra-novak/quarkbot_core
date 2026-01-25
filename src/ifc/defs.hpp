#pragma  once

#include <concepts>
#include <memory>
#include <stdexcept>
#include "coro/src/basic_coro/awaitable.hpp"
#include "coro/src/basic_coro/coroutine.hpp"




namespace quarkbot {

template<typename T> using awaitable = coro::awaitable<T>;

struct StreamTypeItem {
    using Type = std::string_view;
};

template<typename T>
concept StreamType = std::is_base_of_v<StreamTypeItem, T> && requires {
    {T::type}->std::convertible_to<typename T::Type>;
};


template<typename T>
class safe_ref: public std::shared_ptr<T> {
public:
    using std::shared_ptr<T>::shared_ptr;
    using super = std::shared_ptr<T>;
    using element_type = typename super::element_type;

    constexpr safe_ref() = default;
    constexpr safe_ref(const super &other):super(other) {}
    constexpr safe_ref(super &&other):super(std::move(other)) {}

    const super &check_ptr() const {
        if (!*this) throw std::runtime_error("Null pointer reference");
        return *this;
    }

    element_type &operator *() const {return check_ptr().operator*();}
    element_type *operator ->() const {return check_ptr().operator->();}
};


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

using PAccount = safe_ref<IAccount>;
using PExchange = safe_ref<IExchange>;
using PMarketInstrument = safe_ref<IMarketInstrument>;
using PTradableInstrument = safe_ref<ITradableInstrument>;
using POrder = safe_ref<IOrder>;
using PUnderlyingCurrency = safe_ref<IUnderlyingCurrency>;
using PStorage = safe_ref<IStorage>;
template<StreamType T>
using PMarketEventStream = safe_ref<IMarketEventStream<T> >;
using PScheduler = safe_ref<IScheduler>;
using PExecutionWorker = safe_ref<IExecutionWorker>;
using coroutine = coro::coroutine<void>;
using PBacktestDataSource = safe_ref<IBacktestDataSource>;


template <auto Method>
constexpr auto c_link = [](auto* obj, auto... args) {
    return (obj->*Method)(args...);
};

}