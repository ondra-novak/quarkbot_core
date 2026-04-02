#pragma once

#include <array>
#include <atomic>
#include <concepts>
template<typename T, std::size_t count, std::size_t reserved = 1>
class LockFreeQueue {
public:

    using Seq = std::size_t;

    bool read(T &val, Seq &seq) {
        auto cur = _cur_seq.load(std::memory_order_acquire);
        if (seq == cur) return false;
        auto lowest = cur - count;
        while (true) {
            if (seq < lowest) seq = lowest;
            auto index = seq % nitems;
            val = _queue[index];
            cur = _cur_seq.load(std::memory_order_acquire);
            lowest = cur - count;
            if (seq >= lowest) return {
                ++seq;
                return true;
            }
        }
    }

    template<std::invocable<T &> CBWriter>
    void write(CBWriter &&writer) {
        auto seq = _cur_seq.fetch_add(1, std::memory_order_relaxed);
        auto index = seq % nitems;
        writer(_queue[index]);
        _queue[index].store(seq, std::memory_order_release);
    }
    


protected:


    static constexpr auto nitems = count+reserved;

    std::array<T, nitems> _queue;
    std::atomic<Seq> _cur_seq = {0};
}