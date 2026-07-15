#pragma once

#include <compare>
#include <memory>
template<typename _Ifc>
class Wrapper {
public:

    using Null = typename _Ifc::Null;
    static constexpr auto null_instance = Null{};

    Wrapper(): _ptr(const_cast<Null *>(&null_instance), [](auto &){}) {}
    Wrapper(std::shared_ptr<_Ifc> ptr):_ptr(std::move(ptr)) {}

    explicit operator bool() const {return _ptr && _ptr.get() != &null_instance;}

    auto get_handle() const {return _ptr;}

    std::size_t get_hash() const {
        _Ifc *ptr = _ptr.get();
        return static_cast<std::size_t>(*reinterpret_cast<std::uintptr_t *>(&ptr));
    }

    bool operator==(const Wrapper &other) const = default;
    std::strong_ordering operator<=>(const Wrapper &other) const = default;


protected:
    std::shared_ptr<_Ifc> _ptr;
};
