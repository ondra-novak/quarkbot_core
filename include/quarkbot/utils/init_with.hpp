#pragma once

#include <type_traits>
#include <utility>

template <typename F>
struct InitWith {
    F factory;    

    using RetVal = std::invoke_result_t<F>;

    InitWith(F &&f):factory(std::forward<F>(f)) {}

    operator RetVal() const {
        return factory(); 
    }
};