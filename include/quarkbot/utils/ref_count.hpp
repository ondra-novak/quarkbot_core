#pragma once

#include <atomic>
#include <concepts>
#include <memory>

template<typename T>
concept RefCounted = requires(T v) {
    {v._ref_count}->std::same_as<std::atomic<int> &>;
};


template<RefCounted T, typename Deleter >
struct RefCountedDeleter : public Deleter{

    RefCountedDeleter() = default;
    RefCountedDeleter(Deleter &&other):Deleter(std::move(other)) {}

    void operator()(T *ptr) {
        if (ptr->_ref_count.fetch_sub(1, std::memory_order_release) > 1) return;   
        std::atomic_thread_fence(std::memory_order_acquire);     
        Deleter::operator()(ptr);
    }
};

template<RefCounted T, typename Deleter = std::default_delete<T> >
class refcnt_ptr: public std::unique_ptr<T, RefCountedDeleter<T, Deleter> > {
public:

    using super = std::unique_ptr<T, RefCountedDeleter<T, Deleter> >;
    
    refcnt_ptr() = default;
    refcnt_ptr(T *ptr):super(ptr, {}) {add_ref();}
    refcnt_ptr(T *ptr, Deleter del):super(ptr, {std::move(del)}) {add_ref();}
    refcnt_ptr(refcnt_ptr &&) = default;
    refcnt_ptr &operator=(refcnt_ptr &&) = default;
    refcnt_ptr(const refcnt_ptr &other):super(other.get(), other.get_deleter()) {add_ref();}
    refcnt_ptr &operator=(const refcnt_ptr &other) {        
        if (this != &other) {
            super::operator=(super(other.get(), other.get_deleter()));
            add_ref();
        }
        return *this;
    }

    void reset(T *ptr = nullptr) {        
        std::unique_ptr<T>::reset(ptr);
        add_ref();
    }


protected:
    void add_ref() {
        auto p = this->get();
        if (p) p->_ref_count.fetch_add(1, std::memory_order_relaxed);
    }
};

