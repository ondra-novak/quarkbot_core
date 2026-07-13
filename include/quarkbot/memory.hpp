#pragma once

#include <atomic>
#include <new>
namespace quarkbot {



namespace _details {
    struct PoolItem {
        PoolItem *next;
    };

    struct LocalPoolStack {
        PoolItem *head = {};
        PoolItem *pop() {
            PoolItem *out = head;
            if (out) {
                head = out->next;
            }
            return out;
        }  
        LocalPoolStack() = default;
        LocalPoolStack(const LocalPoolStack &) = delete;
        LocalPoolStack &operator=(const LocalPoolStack &) = delete;
        ~LocalPoolStack() {
            auto x = pop();
            while (x) {
                ::operator delete(x);
                x = pop();
            }
        }
        void trim(std::size_t count) {            
            PoolItem **x = &head;
            while (*x && count) {
                x = &(*x)->next;
                --count;
            }
            while (*x) {
                auto y = *x;
                *x = y->next;
                ::operator delete(y);
            }
        }
    };

    struct GlobalPoolStack {
        std::atomic<PoolItem *> head = {};

        GlobalPoolStack() = default;
        GlobalPoolStack(const GlobalPoolStack &) = delete;
        GlobalPoolStack &operator=(const GlobalPoolStack &) = delete;

        void push(PoolItem *item) {
            item->next = nullptr;
            while (!head.compare_exchange_weak(item->next, item));            
        }
        void swap_to_local(LocalPoolStack &target) {
            target.head = head.exchange(target.head);
        }
        ~GlobalPoolStack() {
            LocalPoolStack local;
            swap_to_local(local);
            //destructor of local performs cleanup
        }
    };
    

}

class LockFreeFramePool {
public:

    static constexpr std::size_t arena_count = 32;
    static constexpr std::size_t alloc_step = 32;    
    static constexpr std::size_t max_local_stack_size = 32;
    static constexpr std::size_t max_pooled_size = arena_count*alloc_step;    
    static constexpr std::size_t size_to_arena_index(std::size_t size){
        return (size+ alloc_step-1)/alloc_step - 1;
    }
    static constexpr std::size_t arena_index_to_size(std::size_t index){
        return (index+1) * alloc_step;
    }

    static void *allocate(std::size_t sz) {
        auto index = size_to_arena_index(sz);
        if (index >= arena_count) [[unlikely]] return ::operator new(sz);
        auto &lst = _local_stack[index];
        auto item = lst.pop();
        if (item == nullptr) [[unlikely]] {
            _global_stack[index].swap_to_local(lst);
            lst.trim(max_local_stack_size);
            item = lst.pop();
            if (item == nullptr) [[unlikely]] return ::operator new(arena_index_to_size(index));            
        }
        return item;
    }
    static void deallocate(void *ptr, std::size_t sz) {
        auto index = size_to_arena_index(sz);
        if (index >= arena_count) [[unlikely]] return ::operator delete(ptr);
        auto &gst = _global_stack[index];
        gst.push(reinterpret_cast<_details::PoolItem *>(ptr));        
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
            return reinterpret_cast<T *>(LockFreeFramePool::allocate(sz));
        }
        void deallocate(void *ptr, size_t n) {
            std::size_t sz = sizeof(T) * n;
            return LockFreeFramePool::deallocate(ptr, sz);
        }
        bool operator==(const Allocator &) const {return true;}
    };


protected:
    static _details::GlobalPoolStack _global_stack[arena_count];
    static thread_local _details::LocalPoolStack _local_stack[arena_count];
};

inline _details::GlobalPoolStack LockFreeFramePool::_global_stack[arena_count];
inline thread_local _details::LocalPoolStack LockFreeFramePool::_local_stack[arena_count];


}