#pragma once

#include "quarkbot/abstract/iserie.hpp"
#include "quarkbot/defs.hpp"
#include "types.hpp"
#include <deque>

namespace quarkbot {


template<typename T>
class MemorySerie final: public ISerie<T> {
public:

    MemorySerie()  = default;
    MemorySerie(std::size_t sz): _size(sz) {};

    virtual void add(T value) override {
        _data.push_front(std::move(value));
        if (_size) while (_data.size() > _size) _data.pop_back();
    }

    virtual void reserve(std::size_t size) override{
        _size = std::max<std::size_t>(size,_size);
        
    }

    virtual std::optional<T> operator[](std::size_t index) const override {
        if (index >= _data.size()) return {};
        else return _data.at(index);
    }

    virtual MemorySerie clone() const {
        return MemorySerie(_size);
    }

    virtual std::shared_ptr<ISerie<T> > clone_ptr() const override {
        return std::make_shared<MemorySerie<T> >(clone());
    }

protected:
    std::deque<T> _data;
    std::size_t _size = 0;
};

static_assert(IsSerie<MemorySerie<int> >);


}