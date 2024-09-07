/*
 * cluster_alloc_reference.h
 *
 *  Created on: 7. 9. 2024
 *      Author: ondra
 */

#ifndef SRC_QUARKBOT_SHARED_CLUSTER_ALLOC_REFERENCE_H_
#define SRC_QUARKBOT_SHARED_CLUSTER_ALLOC_REFERENCE_H_

#include "cluster_alloc.h"
#include <any>

template<typename T, unsigned int _cluster_size = 0, bool atomic_release = false>
class ClusterAllocReference { // @suppress("Miss copy constructor or assignment operator")
public:

    using value_type = T;

    using Allocator = ClusterAlloc<T,_cluster_size, atomic_release>;

    template< class U >
    struct rebind {
        typedef ClusterAllocReference<U, _cluster_size, atomic_release> other;
    };

    ClusterAllocReference() = delete;
    ClusterAllocReference(std::any *instance):_instance(instance) {}

    template<typename Q>
    ClusterAllocReference(const ClusterAllocReference<Q, _cluster_size, atomic_release> &other)
        :_instance(other._instance) {}


    T *allocate(int n) {
        if (!_instance->has_value())  {
            _instance->emplace<Allocator>();
        }
        auto &a = std::any_cast<Allocator &>(*_instance);
        return a.allocate(n);
    }

    void deallocate(T *ptr, int n) {
        auto &a = std::any_cast<Allocator &>(*_instance);
        return a.deallocate(ptr,n);
    }

protected:

    template<typename , unsigned int, bool  >
    friend class ClusterAllocReference ;

    std::any *_instance;
};





#endif /* SRC_QUARKBOT_SHARED_CLUSTER_ALLOC_REFERENCE_H_ */
