#pragma once

#include <atomic>
namespace quarkbot {


///Very small mutex
/**
    Because it doesn't contain any system resources, it can be used to various synchronziation, not only for locking
    You can unlock mutex in different thread than it was locked.
    You only need to correctly trace locks and unlocks. Use guards is recommended    
*/
class Minimutex {
public:
    Minimutex() =default;
    Minimutex(bool locked) :_locked(locked) {}

    Minimutex(const Minimutex &) = delete;
    Minimutex &operator=(const Minimutex &) = delete;

    void lock() {
        while (_locked.exchange(true, std::memory_order_acquire)) {
            _locked.wait(true);
        }
    }

    void unlock() {
        _locked.store(false, std::memory_order_release);
    }

    bool try_lock() {
        return  !_locked.exchange(true, std::memory_order_acquire);
    }

protected:
    std::atomic<bool> _locked;
};

}