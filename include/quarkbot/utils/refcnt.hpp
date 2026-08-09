#pragma once


#include <atomic>
#include <concepts>
#include <utility>
template<typename T>
concept RefCountedInstance = requires(T v) {
    {v.add_ref()};
    {v.release_ref()};
};


template<RefCountedInstance T>
class RefCountPtr {
public:
    RefCountPtr() = default;
    RefCountPtr(T *ptr): _ptr(ptr) {add_ref();}
    RefCountPtr(const RefCountPtr &other):_ptr(other._ptr) {add_ref();}
    RefCountPtr(RefCountPtr &&other):_ptr(other._ptr) {other._ptr = nullptr;}
    RefCountPtr &operator=(const RefCountPtr &other) {
        if (this != &other) {
            release_ref();
            _ptr = other._ptr;
            add_ref();
        }
        return *this;
    }
    RefCountPtr &operator=( RefCountPtr &&other) { 
        if (this != &other) {
            release_ref();
            _ptr = other._ptr;
            other._ptr = nullptr;
        }
        return *this;
    }
    ~RefCountPtr() {
        release_ref();
    }
    template<std::derived_from<T> Y>
    RefCountPtr(const RefCountPtr<Y> &other):_ptr(other.get()) {add_ref();}
    template<std::derived_from<T> Y>
    RefCountPtr(RefCountPtr<Y> &&other):_ptr(other.release()) {}

    
    T *operator->() const {return _ptr;}
    T &operator*() const {return *_ptr;}
    explicit operator bool() {return _ptr != nullptr;}

    ///release pointer - ref counter is unaffected
    T *release() {
        return std::exchange(_ptr, nullptr);
    }
    
    
    ///acquire pointer - ref counter is unaffected
    void acquire(T *val) {
        release_ref();
        _ptr = val;
    }

    ///release current pointer and return pointer to it, so other side can store a pointer there, it assumes acquire (already with reference)
    T **capture() {
        release_ref();
        return &_ptr;
    }
    ///get pointer
    T *get() const {return _ptr;}

    bool operator==(const RefCountPtr &other) const = default;
   

protected:
    T *_ptr = {};

    void add_ref() {
        if (_ptr) _ptr->add_ref();
    }
    void release_ref() {
        if (_ptr) _ptr->release_ref();
        _ptr = nullptr;
    }
};

class RefCountInstanceWithDeleter {
    std::atomic<std::size_t> _references = {};
    void (*_deleter)(RefCountInstanceWithDeleter *) = {};
public:
    RefCountInstanceWithDeleter(void (*deleter)(RefCountInstanceWithDeleter *)):_deleter(deleter) {}

    void add_ref() {
        _references.fetch_add(1, std::memory_order_relaxed);            
    }
    void release_ref() {
        if (_references.fetch_sub(1, std::memory_order_release) <= 1) {
            std::ignore=_references.load(std::memory_order_acquire);
            _deleter(this);
        }
    }
};

