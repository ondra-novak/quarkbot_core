#pragma once

#include "arena_allocator.hpp"
#include <mutex>

template<typename X>
class ArenaAllocatorSychronized {
public:
    using value_type = X;        

    ArenaAllocatorSychronized() = default;
    template<typename Y>
    ArenaAllocatorSychronized(ArenaAllocatorSychronized<Y>) {}
    constexpr bool operator==(const ArenaAllocatorSychronized &) const {return true;}

    static ArenaAllocatorInstance<X> instance;
    static std::mutex mx;

    X *allocate([[maybe_unused]]std::size_t n) {
        assert(n == 1);
        std::scoped_lock _(mx);
        return instance.allocate();
    }
    void deallocate(X *p, [[maybe_unused]]std::size_t n) {
        assert(n == 1);
        std::scoped_lock _(mx);
        instance.deallocate(p);
    }
};

template<typename X>
inline ArenaAllocatorInstance<X> ArenaAllocatorSychronized<X>::instance = {};
template<typename X>
inline std::mutex ArenaAllocatorSychronized<X>::mx = {};
