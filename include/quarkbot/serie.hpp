#pragma once

#include "quarkbot/defs.hpp"
#include "utils/wrapper.hpp"
#include "abstract/iserie.hpp"
#include <optional>
namespace quarkbot {

template<typename T>
class Serie : public Wrapper<ISerie<T> >{
public:
    using Wrapper<ISerie<T> >::Wrapper;

    using value_type = T;


    ///add data point
    void put(T val) {
        this->_ptr->add(std::move(val));
    }
    ///retrieve data point
    /**
    @param index index of datapoint where index 0 is currently added data point, index 1 is previous datapoint and etc
     */
    std::optional<T> operator[](std::size_t index) const {
        return this->_ptr->operator[](index);
    }
    ///reserve series for givem size
    /**
    @param sz new reserved space. You cannot shrink
    */
    void reserve(std::size_t sz) {
        return this->_ptr->reserve(sz);
    }
    ///clone the serie setup (not the values)
    /**
        The serie shares same persistent key, and same type and size, but creates new serie. 
        One serie can be cloned only once, If you need multiple clones, you need
        to use previous clone to create new clone

        @code
        Serie<int> original;
        Serie<int> clone1 = original.clone();
        Serie<int> clone2 = clone1.clone();
        Serie<int> clone3 = clone2.clone();
        @endcode

        @note useful to associate multiple series in single indicator

    */
    Serie clone() const {
        return Serie(this->_ptr->clone_ptr());
    }

};

static_assert(IsSerie<Serie<int> >);

}