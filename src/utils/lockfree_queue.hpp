#pragma once

#include <array>
#include <atomic>
#include <concepts>
#include <type_traits>
template<typename T, std::size_t count, std::size_t reserved = 1>
class LockFreeQueue {
public:

    using Seq = std::size_t;

    template<typename X>
    requires(std::is_nothrow_assignable_v<X,T> )
    bool read(X &val, Seq &seq) {
        auto cur = _cur_seq.load(std::memory_order_acquire);
        if (seq == cur) return false;
        auto lowest = cur - count;
        while (true) {
            if (seq < lowest) seq = lowest;
            auto index = seq % nitems;
            val = _queue[index];
            cur = _cur_seq.load(std::memory_order_acquire);
            lowest = cur - count;
            if (seq >= lowest) {
                ++seq;
                return true;
            }
        }
    }

    template<typename CBWriter>
    requires(std::is_nothrow_invocable_r_v<bool, CBWriter, T &>)
    bool write(CBWriter &&writer) {
        if (writer(_queue[_next_index])) {
            _next_index = (_cur_seq.fetch_add(1, std::memory_order_release)+1) % nitems;
            return true;
        }        
        return false;
    }

    Seq get_top_seq() const {
        return _cur_seq.load(std::memory_order_relaxed);
    }

protected:

    static constexpr auto nitems = count+reserved;

    std::array<T, nitems> _queue;
    std::atomic<Seq> _cur_seq = {0};
    std::size_t _next_index  = 0;
};