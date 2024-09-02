#ifndef SRC_QUARKBOT_SHARED_SHARED_CLUSTER_ALLOC_H_
#define SRC_QUARKBOT_SHARED_SHARED_CLUSTER_ALLOC_H_

#include "cluster_alloc.h"

template<typename T, unsigned int _cluster_size = 0>
class SharedClusterAlloc {
public:

    using value_type = T;

    template< class U >
    struct rebind {
        typedef ClusterAlloc<U, _cluster_size> other;
    };

    ClusterAlloc():


protected:
    std::shared_ptr<ClusterAlloc<T, _cluster_size> > _pool;
};






#endif /* SRC_QUARKBOT_SHARED_SHARED_CLUSTER_ALLOC_H_ */
