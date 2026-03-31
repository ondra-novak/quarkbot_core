#pragma once
#include <basic_coro/awaitable.hpp>
#include <basic_coro/exceptions.hpp>
#include <basic_coro/prepared_coro.hpp>
#include "dispatcher.hpp"
#include <concepts>
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>


template<typename T>
class CoroDispatchProxy {
public:
    //construct empty (no action)
    CoroDispatchProxy() = default;
    //construct with awaitable result - must be called from valid dispatcher thread
    CoroDispatchProxy(coro::awaitable_result<T> &&p):_p(std::move(p)) {
        auto dispatcher = Dispatcher::get_instance();
        if (!dispatcher) {
            throw std::runtime_error("Called from invalid thread (must have dispatcher)");            
        } else {
            _dsp = dispatcher;
        }
    }

    template<typename ...Args>
    requires(std::invocable<typename coro::awaitable<T>::result, Args...>)
    coro::prepared_coro operator()(Args && ... args) {
        //don't access dispatcher if empty
        if (!_p) return {};

        auto disp = _dsp.lock();
        if (!disp) return _p.set_exception(std::make_exception_ptr(coro::await_canceled_exception()));

        if constexpr(sizeof...(Args) == 1) {
            if constexpr((std::is_convertible_v<Args, std::nullopt_t> && ...)) {
                handle(disp,_p.set_empty());
            } else if constexpr((std::is_convertible_v<Args, std::exception_ptr> && ...)) {
                handle(disp,_p.set_exception(args...));
            } else {
                handle(disp,_p.set_value(args...));
            }
        } else {
            handle(disp,_p.set_value(args...));
        }
        return {};
        
    }

    operator bool() const {
        return static_cast<bool>(_p);
    }

protected:
    coro::awaitable<T>::result _p;
    std::weak_ptr<Dispatcher> _dsp;

    static void handle(std::shared_ptr<Dispatcher> disp, coro::prepared_coro c) {
        disp->enqueue(std::move(c));
    }

};