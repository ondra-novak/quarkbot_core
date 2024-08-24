#pragma once

#include "async.h"
#include "awaiter.h"
#include <coroutine>
#include <optional>

namespace trading_api {


namespace _details {

    template<typename T>
    struct CoroutineResult { // @suppress("Miss copy constructor or assignment operator")
        AsyncResult<T> _result = {};
        template<std::convertible_to<T> X>
        constexpr void return_value(X &&x) {
            _result.set_value(std::forward<X>(x));
        }
        template<std::invocable<> X>
        constexpr void return_value(X &&x) {
            _result.set_by_fn(std::forward<X>(x));
        }
    };

    template<>
    struct CoroutineResult<void> { // @suppress("Miss copy constructor or assignment operator")
        AsyncResult<void> _result = {};
        constexpr void return_void() {
            _result.set_value();
        }
    };



}

class CoroutineBase {
public:
    static thread_local std::exception_ptr stored_exception;

    static void rethrow_stored_exception() {
        auto e = std::move(stored_exception);
        if (e) std::rethrow_exception(e);
    }
};

inline thread_local std::exception_ptr CoroutineBase::stored_exception = {};


template<typename T>
class Coroutine: public CoroutineBase {
public:


    struct promise_type: public _details::CoroutineResult<T> {
        std::coroutine_handle<> _awaiting = {};

        struct finisher { // @suppress("Miss copy constructor or assignment operator")
            promise_type *me;
            static constexpr bool await_ready() {return false;}
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
                auto r = me->_awaiting;
                if (!r) {
                    if (me->_result.has_exception()) {
                        stored_exception = me->_result.get_exception();
                    }
                    r = std::noop_coroutine();
                    h.destroy();
                }
                return r;
            }
            static constexpr void await_resume() noexcept {}
        };

        static constexpr std::suspend_always initial_suspend() noexcept {return {};}
        constexpr finisher final_suspend() noexcept {return {this};}
        constexpr Coroutine get_return_object() {return {this};}
        void unhandled_exception() {
            this->_result.set_exception();
        }
        std::coroutine_handle<promise_type> get_handle() const {
            return std::coroutine_handle<promise_type>::from_promise(*this);
        }
    };

    static constexpr bool await_ready() {return false;}
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        _prom->_awaiting = h;
        _prom.get_deleter()._ran = true;
        return _prom->from_handle();
    }
    decltype(auto) await_resume() {
        return _prom->_result.get();
    }

protected:

    struct Detacher {
        bool _ran = false;
        void operator()(promise_type *p){
            if (!_ran) p->get_handle().destroy();
            else p->get_handle().resume();
        }
    };


    Coroutine(promise_type *p):_prom(p) {}

    std::unique_ptr<promise_type, Detacher> _prom;
};
}
