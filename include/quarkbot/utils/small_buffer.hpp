#pragma once

#include <cstddef>
#include "manual_lifetime.hpp"
#include <memory>
#include <type_traits>



template<typename T, std::size_t stack_size>
class SmallBuffer {
public:
    
    SmallBuffer(): SmallBuffer(0) {};

    SmallBuffer(std::size_t count):_count(count) {
        init();
        auto ptr = get_ptr();
        for (std::size_t i = 0; i < count; ++i) ptr[i].construct();
    }

    SmallBuffer(std::size_t count, const T &defval):_count(count) {
        init();
        auto ptr = get_ptr();
        for (std::size_t i = 0; i < count; ++i) ptr[i].construct(defval);
    }

    SmallBuffer(const SmallBuffer&) = delete;
    SmallBuffer &operator=(const SmallBuffer&) = delete;

    ///sets size - note destructive
    void set_size(std::size_t count) {
        std::destroy_at(this);
        std::construct_at(this, count);
    }

    ///sets size - note destructive
    void set_size(std::size_t count, const T &defval) {
        std::destroy_at(this);
        std::construct_at(this, count, defval);
    }

    ~SmallBuffer() {
        auto ptr = get_ptr();
        std::size_t x = _count;
        while (_count--) {
            ptr[x].destroy();
        }
        if (_count <= stack_size) {
            std::destroy_at(&static_buff);
        } else {
            delete [] heap_buff;
            std::destroy_at(&heap_buff);
        }
    }
    
    T *data() {
        return reinterpret_cast<T *>(get_ptr());
    }
    const T *data() const {
        return reinterpret_cast<const T *>(get_ptr());
    }
    auto begin() {return data();}
    auto begin() const {return data();}
    auto cbegin() const {return data();}
    auto end() {return data()+_count;}
    auto end() const {return data()+_count;}
    auto cend() const {return data()+_count;}
    T &operator[](std::size_t index) {return get_ptr()[index];}
    const T &operator[](std::size_t index) const {return get_ptr()[index];}

protected:
    std::size_t _count;    
    union {
        ManualLifetime<T> static_buff[stack_size];
        ManualLifetime<T> *heap_buff;
    };

    void init() {        
        if (_count <= stack_size) {
            std::construct_at(&static_buff);
        } else {
            std::construct_at(&heap_buff, new ManualLifetime<T>[_count]);
        }
    }

    ManualLifetime<T> *get_ptr() {
        if (_count <= stack_size) return &static_buff;
        else return heap_buff;
    }
    const ManualLifetime<T> *get_ptr() const {
        if (_count <= stack_size) return &static_buff;
        else return heap_buff;
    }

};
