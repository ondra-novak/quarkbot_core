#pragma once

#include <span>


///Used to store std::span data during asynchronous processing
/**
 * @tparam T type of object in span
 *
 */
template<typename T>
class SavedSpan {
public:
    constexpr SavedSpan(std::span<T> &&s) {

        if (std::is_constant_evaluated()) {
            _ptr = new T[s.size()];
            std::size_t idx = 0;
            for (auto &item: s) {
                _ptr[idx++] = std::move(item);
            }
        } else {
            _ptr = reinterpret_cast<T *>(::operator new(sizeof(T)*s.size()));
            std::size_t idx = 0;
            for (auto &item: s) {
                std::construct_at(_ptr+idx, std::move(item));
            }
        }
        _count = s.size();
    }

    constexpr SavedSpan(const std::span<T> &s) {

        if (std::is_constant_evaluated()) {
            _ptr = new T[s.size()];
            std::size_t idx = 0;
            for (auto &item: s) {
                _ptr[idx++] = item;
            }
        } else {
            _ptr = reinterpret_cast<T *>(::operator new(sizeof(T)*s.size()));
            std::size_t idx = 0;
            for (auto &item: s) {
                std::construct_at(_ptr+idx, item);
            }
        }
        _count = s.size();
    }

    constexpr SavedSpan(SavedSpan &&other):_ptr(other._ptr),_count(other._count) {
        _ptr = nullptr;
        _count = 0;
    }

    constexpr SavedSpan &operator=(SavedSpan &&other) {
        if (this != &other) {
            std::destroy_at(this);
            std::construct_at(this, std::move(other));
        }
    }

    constexpr SavedSpan(const SavedSpan &other) = delete;
    constexpr SavedSpan &operator=(const SavedSpan &other) = delete;

    constexpr operator std::span<T>() {
        return {_ptr, _count};
    }

    constexpr ~SavedSpan() {
        if (std::is_constant_evaluated()) {
            delete [] _ptr;
        } else {
            for (std::size_t i = 0; i < _count; ++i) {
                std::destroy_at(_ptr+i);
            }
            ::operator delete(_ptr);
        }
    }

protected:
    T *_ptr;
    std::size_t _count;
};


template<typename T>
SavedSpan(std::span<T>) -> SavedSpan<T>;

