#pragma once

// Naive implementation of Bonwick & Adams, "Magazines and Vmem"

#include <algorithm>
#include <cstdlib>
#include <mutex>

namespace _magazine_vmem_details {
    ///cached block
    struct PoolItem {
        PoolItem *next;
    };

    ///also cached block promoted to magazine
    struct Magazine {
        Magazine *next;
        PoolItem *list;
        unsigned int count;
    };

    ///single depot for given block size (block size managed in LocalCaache)
    struct Depot {
        //this is synchronized
        std::mutex mx;
        //list of magazines
        Magazine *full = nullptr;
        //count of full magazines        
        std::size_t n_full = 0;
        //high watermark for caching
        std::size_t high_watermark = 8;
        //size of magazine
        std::size_t magazine_size = 32;
        
        ///configure
        /**
        @param hwm change high watermark
        @param magazine_size change magazine size
         */
        void configure(std::size_t hwm, std::size_t magazine_size = 32) {
            std::scoped_lock _(mx);
            high_watermark = hwm;
            this->magazine_size = magazine_size;


        }

        //try to pop magazine, can return nullptr if none
        Magazine *try_pop() {
            std::scoped_lock _(mx);
            if (full) {
                auto cur = full;
                full = cur->next;
                return cur;
            }
            return full;
        }

        //free magazine to heap,release memory
        static void free_to_heap(Magazine *m) {
            while (m->list) {
                auto p = m->list;
                m->list = p->next;
                ::operator delete(p);
            }
            operator delete(m);
        }

        //push full magazine to depot
        void push(Magazine *m) {
            mx.lock();
            if (n_full >= high_watermark) {
                mx.unlock();
                free_to_heap(m);
            } else {
                m->next = full;
                full = m;
                ++n_full;
                mx.unlock();
            }
        }

        //depot destruction
        ~Depot() {
            std::scoped_lock _(mx);
            while (full) {
                auto p = full;
                full = p->next;
                free_to_heap(p);
            }            
        }

    };


    ///Cache created in thread local
    struct LocalCache {
        ///pointer to depot, must be initialized on creation
        Depot *depot = nullptr;
        ///block size - must be initialized on creation
        std::size_t block_size = 0;
        ///current magazine  (for allocations)
        Magazine *current = nullptr;
        ///previous magazine (for deallocations)
        Magazine *previous = nullptr;
        
        ///allocate from magazine
        void *allocate() {
            //current is empty?
            if (current == nullptr) {
                //previous is empty?
                if (previous == nullptr) {
                    //try pop from depot
                    current = depot->try_pop();
                    //nothing?
                    if (current == nullptr) {
                        //allocate on heap
                        std::size_t needsz = std::max(block_size,sizeof(Magazine));
                        return ::operator new(needsz);
                    }
                    //current = full magazine
                } else {
                    //previous has a magazine
                    std::swap(current, previous);
                    //current has now magazine, previous is empty
                }
            }
            //current has magazine
            //retrive block from list
            auto out = current->list;
            //no block
            if (!out) {
                //the magazine itself is block
                void *ret = current;
                //empty current
                current = nullptr;
                //return block
                return ret;
            }
            //remove block from list
            current->list = out->next;
            //decrease count of blocks
            --current->count;
            //return block
            return out;            
        }

        ///deallocate block
        void deallocate(void *block) {
            //previous is empty?
            if (!previous)  {
                //promote this block to magazine
                previous = reinterpret_cast<Magazine *>(block);
                //initialize magazine
                previous->count = 1;
                previous->list = nullptr;
                previous->next = nullptr;                
                return;
            } else {
                //attempt to put block to previous magazine
                auto item = reinterpret_cast<PoolItem *>(block);
                item->next = previous->list;
                previous->list = item;
                //increase count
                ++previous->count;
                //reached maximum<
                if (previous->count >= depot->magazine_size) {
                    //swap with current, it is possible, that there is space in current
                    std::swap(previous, current);
                    //no space at all
                    if (current->count >= depot->magazine_size) {
                        //return previous to depot
                        depot->push(previous);
                        //previous is now empty
                        previous = nullptr;
                    }
                }
            }
        }
        void clean() {
            if (previous) depot->push(previous);
            if (current) depot->push(current);
        }
    };
}

class MagazineVMemAllocator {
public:

    static constexpr std::size_t arena_count = 32;
    static constexpr std::size_t alloc_step = 32;    
    static constexpr std::size_t max_pooled_size = (arena_count+1)*alloc_step;    

    static constexpr std::size_t size_to_arena_index(std::size_t size){
        return (size+ alloc_step-1)/alloc_step - 1;
    }
    static constexpr std::size_t arena_index_to_size(std::size_t index){
        return (index+1) * alloc_step;
    }

    ///list of depots one per allocation step
    struct DepotList {
        _magazine_vmem_details::Depot depots[arena_count] = {};
    };
    ///list of local caches , one per allocation step
    struct LocalCacheList {
        _magazine_vmem_details::LocalCache cache[arena_count] = {};
        //it has constructor
        LocalCacheList(DepotList &lst) {
            //initialize each item - connect to depot and set block size
            for (std::size_t i = 0; i < arena_count; ++i) {
                cache[i].block_size = (i+1) * alloc_step;
                cache[i].depot = &lst.depots[i];
            }
        }
        ~LocalCacheList() {
            for (std::size_t i = 0; i < arena_count; ++i) {
                cache[i].clean();
            }
        }
    };

    ///static depot list
    static DepotList depot_list;
    ///thread local cache
    static thread_local LocalCacheList local_cache;

    static void *allocate(std::size_t sz) {
        auto index = size_to_arena_index(sz);
        if (index >= arena_count) [[unlikely]] return ::operator new(sz);
        auto &cache = local_cache.cache[index];
        return cache.allocate();
    }
    static void deallocate(void *ptr, std::size_t sz) {
        auto index = size_to_arena_index(sz);
        if (index >= arena_count) [[unlikely]] return ::operator delete(ptr);
        auto &cache = local_cache.cache[index];
        cache.deallocate(ptr);
    }

    template<typename T>
    struct Allocator {

        using value_type = T;

        Allocator() = default;
        Allocator(const Allocator &) = default;
        template<typename Y>
        Allocator(const Allocator<Y> &) {}

        T *allocate(std::size_t n) {
            std::size_t sz = sizeof(T) * n;
            return reinterpret_cast<T *>(MagazineVMemAllocator::allocate(sz));
        }
        void deallocate(void *ptr, size_t n) {
            std::size_t sz = sizeof(T) * n;
            return MagazineVMemAllocator::deallocate(ptr, sz);
        }
        bool operator==(const Allocator &) const {return true;}
    };

};

inline MagazineVMemAllocator::DepotList MagazineVMemAllocator::depot_list;
inline thread_local MagazineVMemAllocator::LocalCacheList MagazineVMemAllocator::local_cache(depot_list);

