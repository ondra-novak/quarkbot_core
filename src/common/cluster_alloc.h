#pragma once

#include <vector>
#include <memory>
#include <array>

template<typename T, unsigned int _cluster_size = 0>
class ClusterAlloc {
public:

    static constexpr auto _cs = _cluster_size?_cluster_size:4096/sizeof(T);

    using value_type = T;

    template< class U >
    struct rebind {
        typedef ClusterAlloc<U, _cluster_size> other;
    };

    ClusterAlloc() = default;
    ClusterAlloc(ClusterAlloc &&other) = default;
    ClusterAlloc &operator=(ClusterAlloc &&other) = default;
    ClusterAlloc(const ClusterAlloc &other) {}

    template<typename X>
    ClusterAlloc(const ClusterAlloc<X,_cluster_size> &) {}




    T *allocate(int n) {
        if (n>1) {
            return reinterpret_cast<T *>(::operator new(sizeof(T)*n));
        }
        if (!_first_free) {
            alloc_cluster();
        }
        Item *p = _first_free;
        _first_free = p->next_free;
        return reinterpret_cast<T *>(p);
    }

    void deallocate(T *ptr, int n) {
        if (n>1) {
            ::operator delete(ptr);
            return;
        }
        Item *x = reinterpret_cast<Item *>(ptr);
        x->next_free = _first_free;
        _first_free = x;
    }

protected:
    union Item {
        Item *next_free;
        char mem[sizeof(T)];
    };

    struct Cluster {
        std::unique_ptr<Cluster> _next;
        std::array<Item, _cs> _data;
    };

    std::unique_ptr<Cluster> _clusters;
    Item *_first_free = nullptr;


    void alloc_cluster() {
        auto c = std::make_unique<Cluster>();
        for (auto &z: c->_data) {
            z.next_free = _first_free;
            _first_free = &z;
        }
        c->_next = std::move(_clusters);
        _clusters = std::move(c);
    }
};
