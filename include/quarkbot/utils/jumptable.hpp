#pragma once

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

///Passed to the callback when the runtime value is outside [Min, Max]
template<typename T>
struct OutOfRange {
    T value;
    constexpr explicit OutOfRange(T v) noexcept : value(v) {}
};

///Maps a runtime value to a compile-time integral_constant callback dispatch via a jump table.
/**
 * For values in [Min, Max], calls: callback(std::integral_constant<ValueType, N>{}, args...)
 * For values outside the range, calls: callback(OutOfRange<ValueType>{value}, args...)
 *
 * @tparam Min      minimum value (inclusive), deduced as ValueType
 * @tparam Max      maximum value (inclusive), must be >= Min
 * @tparam Callback callable invocable with std::integral_constant<ValueType,N> and Args...
 * @tparam Args     optional extra argument types forwarded to every callback call
 *
 * @note The internal dispatch table is a static constexpr array — zero runtime overhead.
 *       The instance itself can be declared constexpr if Callback is constexpr-constructible.
 *
 * Example:
 * @code
 *   auto cb = [](auto tag, std::string_view label) {
 *       if constexpr (std::is_same_v<decltype(tag), OutOfRange<int>>) {
 *           std::cout << "out of range: " << tag.value << "\n";
 *       } else {
 *           std::cout << "value=" << tag.value << " label=" << label << "\n";
 *       }
 *   };
 *   JumpTable<0, 4, decltype(cb), std::string_view> jt(cb);
 *   jt(2, "hello");  // -> value=2 label=hello
 *   jt(9, "world");  // -> out of range: 9
 * @endcode
 */
template<auto Min, auto Max, typename Callback, typename... Args>
class JumpTable {
    static_assert(Min <= Max, "JumpTable: Min must be <= Max");

    using ValueType = decltype(Min);
    using ReturnType = std::invoke_result_t<Callback, std::integral_constant<ValueType, Min>, Args...>;
    using FuncPtr = ReturnType(*)(Callback&, Args...);

    static constexpr std::size_t kSize = static_cast<std::size_t>(Max - Min + 1);

    template<ValueType N>
    static ReturnType _invoke(Callback& cb, Args... args) {
        return cb(std::integral_constant<ValueType, N>{}, std::forward<Args>(args)...);
    }

    template<std::size_t... Is>
    static constexpr std::array<FuncPtr, kSize> _make_table(std::index_sequence<Is...>) noexcept {
        return { &_invoke<static_cast<ValueType>(Min + static_cast<ValueType>(Is))>... };
    }

    static constexpr std::array<FuncPtr, kSize> _table =
        _make_table(std::make_index_sequence<kSize>{});

    Callback _cb;

public:
    constexpr explicit JumpTable(Callback cb)
        noexcept(std::is_nothrow_move_constructible_v<Callback>)
        : _cb(std::move(cb)) {}

    ///Dispatch value to the callback.
    /**
     * @param value  runtime value; if outside [Min,Max] the callback receives OutOfRange{value}
     * @param args   forwarded to the callback after the tag argument
     */
    ReturnType operator()(ValueType value, Args... args) {
        if (value < static_cast<ValueType>(Min) || value > static_cast<ValueType>(Max)) {
            return _cb(OutOfRange<ValueType>{value}, std::forward<Args>(args)...);
        }
        const auto idx = static_cast<std::size_t>(value - static_cast<ValueType>(Min));
        return _table[idx](_cb, std::forward<Args>(args)...);
    }
};
