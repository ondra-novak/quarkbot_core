#pragma once

#include "basic_coro/awaitable.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "quarkbot/abstract/ieventstream.hpp"
#include <mutex>
#include <optional>
#include <queue>
#include <ranges>
namespace quarkbot {

template<std::ranges::forward_range _Range>
class IteratorAsStream: public EventStreamStoppable<std::ranges::range_value_t<_Range> >{
public:
    using Iter = std::ranges::iterator_t<_Range>;
    using value_type = std::ranges::range_value_t<_Range>;


    IteratorAsStream(_Range r):_r(std::move(r)),_iter(_r.begin()) {}

    IteratorAsStream(const IteratorAsStream &) = delete;
    IteratorAsStream &operator=(const IteratorAsStream &) = delete;

    virtual awaitable<bool> receive(value_type &ref) override {
        return current(ref);
    }

    virtual awaitable<bool> receive(value_type &ref, std::size_t &missed) override {
        missed = 0;
        return receive(ref);
    }

    virtual bool current(value_type &ref) override {
        if (_iter == _r.end()) return false;
        ref = *_iter;
        ++_iter;
        return true;        
    }

    virtual bool is_open() const override {
        return _iter != _r.end();
    }

    virtual void close() override {
        //can't close, there is no async operation
    }

protected:
    _Range _r;
    Iter _iter;
};


}