#pragma once

#include <vector>
#include <memory>
#include <array>

template<typename T, unsigned int _cluster_size = 4096/sizeof(T)>
class ClusterAlloc {
public:

    using value_type = T;

    template< class U >
    struct rebind {
        typedef ClusterAlloc<U, _cluster_size> other;
    };

    ClusterAlloc():_impl(std::make_shared<Impl>()) {}

    T *allocate(int n) {
        if (n>1) {
            return reinterpret_cast<T *>(::operator new(sizeof(T)*n));
        }
        if (!_impl->_first_free) {
            alloc_cluster();
        }
        Item *p = _impl->_first_free;
        _impl->_first_free = p->next_free;
        return reinterpret_cast<T *>(p);     
    }

    void deallocate(T *ptr, int n) {
        if (n>1) {
            ::operator delete(ptr);
            return;
        }
        Item *x = reinterpret_cast<Item *>(ptr);
        x->next_free = _impl->_first_free;
        _impl->_first_free = x;
    }

protected:
    union Item {
        Item *next_free;
        char mem[sizeof(T)];
    };

    struct Cluster {
        std::unique_ptr<Cluster> _next;
        std::array<Item, _cluster_size> _data;
    };

    struct Impl {
        std::unique_ptr<Cluster> _clusters;
        Item *_first_free = nullptr;
    };

    std::shared_ptr<Impl> _impl  = {};
    
    void alloc_cluster() {
        auto c = std::make_unique<Cluster>();
        for (auto &z: c->_data) {
            z.next_free = _impl->_first_free;
            _impl->_first_free = &z;
        }
        c->_next = std::move(_impl->_clusters);
        _impl->_clusters = std::move(c);
    }
};