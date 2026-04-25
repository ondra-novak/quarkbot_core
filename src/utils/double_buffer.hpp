#pragma once

#include <atomic>
#include <mutex>
#include <vector>
template<typename T>
class DoubleBufferQueue {
public:

    DoubleBufferQueue(std::mutex &mx):_mx(mx) {}

    void push(const T &item) {
        std::lock_guard lock(_mx);
        _buffer1.push_back(item);
        _has_new_data.store(true, std::memory_order_release);
    }

    void push(T &&item) {
        std::lock_guard lock(_mx);
        _buffer1.push_back(std::move(item));
        _has_new_data.store(true, std::memory_order_release);
    }

    bool empty() const {
        if (is_buffer2_empty() || _has_new_data.load()) return false;
        return  true;
    }

    T &front()
    //pre(condition: !empty())
    {
        if (is_buffer2_empty()) {
            std::lock_guard lock(_mx);
            _buffer1.swap(_buffer2);
            _buffer1.clear();
            _read_index = 0;
            _has_new_data.store(false, std::memory_order_release);
        }
        return _buffer2[_read_index];
    }

    void pop() {
        _read_index++;
    }

protected:
    std::vector<T> _buffer1;
    std::vector<T> _buffer2;
    std::size_t _read_index = 0;
    std::mutex &_mx;
    std::atomic<bool> _has_new_data = false;

    bool is_buffer2_empty() const {
        return _read_index >= _buffer2.size();
    }   
    
};