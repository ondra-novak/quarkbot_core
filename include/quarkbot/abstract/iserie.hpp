#pragma once

#include <cstddef>
#include <memory>
#include <optional>
namespace quarkbot {

template<typename T>
class ISerie {
public:

    using value_type = T;

    virtual ~ISerie() = default;
    virtual void add(T value) = 0;
    virtual void reserve(std::size_t size) = 0;
    virtual std::optional<T> operator[](std::size_t index) const = 0;;
    virtual std::shared_ptr<ISerie<T> > clone_ptr() const = 0;

    class Null;

};

template<typename T>
class ISerie<T>::Null: public ISerie<T> {
public:
    virtual void add(T) override  {}
    virtual void reserve(std::size_t ) override {}
    virtual std::optional<T> operator[](std::size_t ) const override  {return {};}
    virtual std::shared_ptr<ISerie<T> > clone_ptr() const override {return {const_cast<Null *>(this),[](auto){}};}
};


}