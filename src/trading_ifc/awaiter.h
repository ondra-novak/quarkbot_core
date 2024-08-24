#pragma once

#include "function.h"

#include <atomic>
#include <coroutine>
#include <exception>
#include <memory>
using std::__exception_ptr::exception_ptr;

namespace trading_api {

///asynchornous operation has been canceled with no futher details
/**
 *  This can happen, everytime the associated callback is dropped
 */
class CanceledException: public std::exception {
public:
    const char *what() const noexcept override {return "Asynchronous operation has been canceled";}
};


///Contains result of asynchronous operation
/**
 * @tparam T type of result, including void
 *
 * This variable can hold result or exception. You need to convert it to T to actually
 * retrieve a result
 */
template<typename T>
class AsyncResult {
public:
    using StoreType = std::conditional_t<std::is_void_v<T>, bool, T>;
    enum ResultType {
        nothing,
        value,
        exception,
    };

    constexpr AsyncResult():_result_type(nothing) {}
    constexpr AsyncResult(const AsyncResult &other):_result_type(other._result_type) {
        switch (_result_type) {
            case value: std::construct_at(&_val, other._val);break;
            case exception: std::construct_at(&_e, other._e);break;
            default: break;
        }
    }
    constexpr AsyncResult(const AsyncResult &&other):_result_type(other._result_type) {
        switch (_result_type) {
            case value: std::construct_at(&_val, std::move(other._val));break;
            case exception: std::construct_at(&_e, std::move(other._e));break;
            default: break;
        }
    }
    constexpr ~AsyncResult() {
        switch (_result_type) {
            case value: std::destroy_at(&_val);break;
            case exception: std::destroy_at(&_e);break;
            default: break;
        }
    }
    constexpr AsyncResult &operator=(const AsyncResult &other) {
        if (this != &other) {
            std::destroy_at(this);
            std::construct_at(this, other);
        }
        return *this;
    }
    constexpr AsyncResult &operator=(AsyncResult &&other) {
        if (this != &other) {
            std::destroy_at(this);
            std::construct_at(this, std::move(other));
        }
        return *this;
    }


    template<typename ... Args> requires(std::is_constructible_v<StoreType, Args...>)
    constexpr AsyncResult(Args && ... args)
        :_result_type(value),_val(std::forward<Args>(args)...) {}

    template<typename ... Args> requires(std::is_constructible_v<StoreType, Args...>)
    constexpr AsyncResult(std::in_place_t, Args && ... args):_result_type(value),_val() {}

    constexpr AsyncResult(std::exception_ptr e)
        :_result_type(exception),_e(std::move(e)) {}

    constexpr void clear() {
        switch(_result_type) {
            case value: std::destroy_at(&_val);_result_type =nothing;break;
            case exception: std::destroy_at(&_e);_result_type =nothing;break;
            default:break;
        }

    }
    template<typename ... Args> requires(std::is_constructible_v<StoreType, Args...>)
    constexpr void set_value(Args && ...args) {
        clear();
        std::construct_at(&_val, std::forward<Args>(args)...);
        _result_type = value;
    }
    constexpr void set_exception(std::exception_ptr e) {
        clear();
        std::construct_at(&_e, std::move(e));
        _result_type = exception;
    }
    constexpr void set_value(const AsyncResult &p) {
        (*this) = p;
    }

    constexpr void set_value(AsyncResult &&p) {
        (*this) = std::move(p);
    }

    constexpr void set_exception() {
        set_exception(std::current_exception());
    }

    template<std::invocable<> Fn>
    constexpr void set_by_fn(Fn &&fn) {
        static_assert(std::is_constructible_v<T, std::invoke_result_t<Fn> >);
        clear();
        new (&_val) T(fn());
        _result_type = value;
    }

    constexpr decltype(auto) get() && {
        handle_exception();
        if constexpr(!std::is_void_v<T>) {return std::move(_val);}
    }

    constexpr decltype(auto) get() & {
        handle_exception();
        if constexpr(!std::is_void_v<T>) {return _val;}
    }

    constexpr decltype(auto) get() const & {
        handle_exception();
        if constexpr(!std::is_void_v<T>) {return _val;}
    }

    constexpr decltype(auto) get() const && {
        handle_exception();
        if constexpr(!std::is_void_v<T>) {return _val;}
    }

    constexpr bool has_value() const {return _result_type == value;}
    constexpr bool has_exception() const {return _result_type == exception;}
    constexpr explicit operator bool() const {return has_value();}
    constexpr std::exception_ptr get_exception() const {
        return _result_type == exception?_e:std::exception_ptr{};
    }



protected:
    ResultType _result_type = nothing;
    union {
        StoreType _val;
        std::exception_ptr _e;
    };
    void handle_exception() {
        if (_result_type != value){
            if (_result_type == exception) {
                std::rethrow_exception(_e);
            } else {
                throw CanceledException();
            }
        }
    }

};

///Generic awaitable, can be accessed both by coroutine or by callback
/**
 * @tparam T type of result (including void)
 * @tparam RegFn type of registration/execution function. This function is
 * responsible to start asynchronous operation once there is an awaiter. The
 * function receives a callback function where the result must be stored
 */
//template<typename T, std::invocable<Function<void(AsyncResult<T>)> > RegFn>
template<typename T, typename RegFn>
class AwaitableResult {
public:

    using Callback = Function<void(AsyncResult<T>)>;
    using Result = AsyncResult<T>;

    class [[nodiscard]] Awaiter {
    public:

        template<std::invocable<Callback> Fn>
        Awaiter(Fn &&fn) {
            charge(std::forward<Fn>(fn));
        }
        Awaiter(const Awaiter &) = delete;
        Awaiter &operator=(const Awaiter &) = delete;


        bool await_ready() const {return _state == resolved;}
        bool await_suspend(std::coroutine_handle<> h) {
            _h = h;
            State p = dormant;
            return _state.compare_exchange(p, awaiting);
        }
        auto await_resume() {
            return std::move(_result).get();
        }

    protected:

        struct Resumer {void operator()(Awaiter *x)const {
            auto s = _state.exchange(resolved);
            if (s == awaiting) {
                x->_h.resume();
            }
        }};
        using MePtr = std::unique_ptr<Awaiter, Resumer>;
        enum State {
            dormant,
            awaiting,
            resolved
        };

        std::coroutine_handle<> _h = {};
        std::atomic<State> _state={false};
        AsyncResult<T> _result;

        void accept(Result &&p) {
            _result = std::move(p);
        }

        struct Acceptor{
            MePtr me;
            template<typename ... Args>
            void operator()(Args && ... args) const {
                me->_result.set_value(std::forward<Args>(args)...);
            }
        };

        template<typename Fn>
        void charge(Fn &&fn) {
            fn(Acceptor{{this}});
        }
    };



    template<typename ... Args> requires(std::is_constructible_v<RegFn, Args...>)
    AwaitableResult(Args &&... args):_regfn(std::forward<Args>(args)...) {}

    ///Attach callback. - start asynchronous operation and attach callback
    /**
     * @param fn callback. It receives AsyncResult<T>, you need to extract the actuall
     * result from it
     */
    template<std::invocable<AsyncResult<T> > Fn>
    void operator>>(Fn &&fn) {
        _reg_fn(std::forward<Fn>(fn));
    }

    ///Handles co_await operator.
    /**
     * @return awaiter which is passed to co_await. The operator returns result
     * directly - or throws exception
     */
    Awaiter operator co_await() {
        return [this](auto &&fn){
            _reg_fn(std::move(fn));
        };
    }
protected:
    RegFn _regfn;
};

}
