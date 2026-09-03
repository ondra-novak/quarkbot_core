#pragma once

#include "quarkbot/abstract/ieventstream.hpp"
#include "quarkbot/defs.hpp"
#include <type_traits>
#include <utility>
namespace quarkbot {
template<typename T, typename _Callback>
requires(std::is_invocable_r_v<bool, _Callback, T &>)
class CallbackSourceAsStream: public EventStreamStoppable<T>{
public:


    CallbackSourceAsStream(_Callback cb):_cb(std::move(cb)) {}

    CallbackSourceAsStream(const CallbackSourceAsStream &) = delete;
    CallbackSourceAsStream &operator=(const CallbackSourceAsStream &) = delete;

    virtual awaitable<bool> receive(T &ref) override {
        return current(ref);
    }

    virtual awaitable<bool> receive(T &ref, std::size_t &missed) override {
        missed = 0;
        return receive(ref);
    }

    virtual bool current(T &ref) override {
        return _last_val = _cb(ref);
    }

    virtual bool is_open() const override {
        return _last_val;
    }

    virtual void close() override {
        //can't close, there is no async operation
    }

protected:
    _Callback _cb;
    bool _last_val = true;
};

}