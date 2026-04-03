#pragma  once

#include <concepts>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include "basic_coro/awaitable.hpp"
#include "basic_coro/coroutine.hpp"



namespace quarkbot {

template<typename T> using awaitable = coro::awaitable<T>;




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
class IStorage;
class IScheduler;
class IExecutionWorker;
class IBacktestDataSource;



using PAccount = safe_ref<IAccount>;
using PExchange = safe_ref<IExchange>;
using PMarketInstrument = safe_ref<IMarketInstrument>;
using PTradableInstrument = safe_ref<ITradableInstrument>;
using PStorage = safe_ref<IStorage>;
using PScheduler = safe_ref<IScheduler>;
using PExecutionWorker = safe_ref<IExecutionWorker>;
using coroutine = coro::coroutine<void>;
using PBacktestDataSource = safe_ref<IBacktestDataSource>;

}