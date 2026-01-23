#pragma once

#include <atomic>
class spin_mutex {
public:
    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
            flag.wait(true,  std::memory_order_relaxed);
        }
    } 
    void unlock() {
        flag.clear(std::memory_order_release);
        flag.notify_one();
    }   
private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
};