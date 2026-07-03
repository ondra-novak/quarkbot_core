#pragma once

#include <functional>
#include <type_traits>
#include <utility>
#include <variant>
namespace quarkbot {
    

template<typename T>
struct GenericSelector {

    template<typename _Callback, typename ... _OtherCallbacks>
    static constexpr auto select(T &&val, _Callback &&cb, _OtherCallbacks && ... other) {
        if constexpr(std::is_invocable_v<_Callback, T>) {
            return std::invoke(std::forward<_Callback>(cb), std::forward<T>(val));
        } else {
            return select(std::forward<T>(val), std::forward<_OtherCallbacks>(other)...);
        }
    }

    static constexpr auto select(T &&) {}

};



template<typename ... _Types, typename ... _Callbacks>
void selector(const std::variant<_Types ...> &v, _Callbacks &&... callbacks) {
    return std::visit([&]<typename T>(const T &val){
        return GenericSelector<const T &>::select(val, std::forward<_Callbacks>(callbacks)...);
    },v);
}

template<typename ... _Types, typename ... _Callbacks>
void selector(std::variant<_Types ...> &v, _Callbacks &&... callbacks) {
    return std::visit([&]<typename T>(T &val){
        return GenericSelector<T &>::select(val, std::forward<_Callbacks>(callbacks)...);
    },v);
}
template<typename ... _Types, typename ... _Callbacks>
void selector(std::variant<_Types ...> &&v, _Callbacks &&... callbacks) {
    return std::visit([&]<typename T>(T &&val){
        return GenericSelector<T &&>::select(std::move(val), std::forward<_Callbacks>(callbacks)...);
    },std::move(v));
}


}