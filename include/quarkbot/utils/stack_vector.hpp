#pragma once

#include <cstddef>
#include <new>
#include <type_traits>
#include <vector>

template<std::size_t N>
struct _StackVectorDetails {

    template<typename T>
    struct alignas(T) Allocator {
        char space[N * sizeof(T)];
        bool used = false;

        using value_type = T;

        bool operator==(const Allocator &other) const {return this == &other;}

        Allocator() = default;
        Allocator(const Allocator &) {};
        template<typename X>
        Allocator(const Allocator<X> &) {}

        T *allocate(std::size_t n) {
            if (used || n > N) {
                return reinterpret_cast<T *>(::operator new(sizeof(T) * n));
            } else {
                used = true;
                return reinterpret_cast<T *>(space);
            }
        }

        void deallocate(T *ptr, std::size_t ) {
            if (reinterpret_cast<char *>(ptr) != space) return ::operator delete(ptr);
            used = false;                
        }

    };
};

template<typename T, std::size_t _InitialReserve>
class StackVector: public std::vector<T, typename _StackVectorDetails<_InitialReserve>::template Allocator<T> > {
public:
    using Super  = std::vector<T, typename _StackVectorDetails<_InitialReserve>::template Allocator<T> >;
    
    StackVector() {
        reserve(_InitialReserve);
    }
    StackVector(const StackVector &other) {
        reserve(other.size());
        insert(Super::end(), other.begin(), other.end());
    }
    StackVector &operator=(const StackVector &other) {
        if (this != &other) {
            Super::clear();
            reserve(other.size());
            Super::insert(Super::end(), other.begin(), other.end());
        }
        return *this;
    }
    template<typename ... Args>
    requires(std::is_constructible_v<T, Args...>)
    StackVector(std::size_t n, Args && ... args) {
        reserve(n);
        for (std::size_t i = 0 ; i < n; ++i) Super::emplace_back(std::forward<Args>(args)...);
    }
    template<typename Iter, typename Sentinel = Iter>
    StackVector(Iter from, Sentinel to) {
        reserve(_InitialReserve);;
        insert(Super::end(), from, to);
    }

    void reserve(std::size_t n) {
        Super::reserve(std::max(n, _InitialReserve));;
    }
};

