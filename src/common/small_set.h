#pragma once
#include <vector>

namespace trading_api {


template<typename T>
class SmallSet : public std::vector<T> {
public:

    void set(const T &val) {
        auto iter = find(val);
        if (iter != this->end()) *iter = val;
        else push_back(val);
    }
    void set(T &val) {
        auto iter = find(val);
        if (iter != this->end()) *iter = std::move(val);
        else this->push_back(std::move(val));
    }
    template<typename X>
    auto find(const X &val) const {
        return std::find(this->begin(), this->end(), val);
    }
    template<typename X>
    auto find(const X &val) {
        return std::find(this->begin(), this->end(), val);
    }

    template<typename X>
    bool is_set(const X &val) const {
        return find(val) != this->end();
    }
    void unset(const T &val) {
        this->erase(std::remove(this->begin(), this->end(), val), this->end());
    }


};


}
