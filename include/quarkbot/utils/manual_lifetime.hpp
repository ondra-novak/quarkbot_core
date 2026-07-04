#pragma once

#include <memory>
#include <type_traits>


///Manual lifetime wrapper for objects with non-trivial constructor and destructor
/**
 * @note it doesn't track whether the object is constructed or not, so it is up to the caller to ensure that the object is constructed before use and destroyed after use.
 * @tparam T The type of the object to manage.
 **/
template<typename T>
union ManualLifetime {
public:


    ///Constructs the object in place with the given arguments.
    template<typename ... Args>
    requires(std::is_constructible_v<T, Args...>)
    void construct(Args &&... args)  {
        std::construct_at(&_value, std::forward<Args>(args)...);
    }
    ///Destroys the object in place.
    void destroy() {
        std::destroy_at(&_value);
    }

    ///Returns a reference to the managed object.
    T &value() & {return _value;}
    ///Returns a move reference to the managed object.
    T &&value() && {return std::move(_value);}
    ///Returns a const reference to the managed object.
    const T &value() const & {return _value;}
    ///Returns a const move reference to the managed object.
    const T &&value() const && {return std::move(_value);}

    ///Returns a pointer to the managed object.
    T *operator->() {return &_value;}
    ///Returns a const pointer to the managed object.
    const T *operator->() const  {return &_value;}

    ///Constructor - does not construct the managed object.
    ManualLifetime() {}
    ///Destructor - does not destroy the managed object.
    ~ManualLifetime() {}
    ///Not copyable or assignable - the managed object must be constructed and destroyed manually.
    ManualLifetime(const ManualLifetime &) = delete;
    ///Not copyable or assignable - the managed object must be constructed and destroyed manually.
    ManualLifetime &operator=(const ManualLifetime &) = delete;

protected:
    T _value;

};