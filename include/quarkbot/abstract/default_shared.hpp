#pragma once

#include <memory>
namespace quarkbot {


template<typename T>
std::shared_ptr<T> default_shared(const T &value) {
    T &mutval = const_cast<T &>(value);
    return std::shared_ptr<T>(&mutval, [](T *){});
}

}