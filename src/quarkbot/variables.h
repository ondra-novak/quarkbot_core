#pragma once
#include <memory>
#include <string_view>

#include "serialize.h"

namespace quarkbot {


///Abstract implementation of VarSet
class IVarSet {
public:

    ///initialize reading, seek at begin
    /**
     * @retval true initialized and first item is available
     * @retval false initialized but empty set (no first item)
     */
    virtual bool init() = 0;

    ///move to next item
    /**
     * @retval true next item is available
     * @retval false no next item is available (we are reached end)
     */
    virtual bool next() = 0;

    ///retrieve current value
    /**
     * only valid when previous init() or next() returned true, otherwise UB
     * @return pair: key, value
     * @note strings are valid until next() or init()
     */
    virtual std::pair<std::string_view, std::string_view> get() const = 0;

    ///dtor
    virtual ~IVarSet() = default;

};

///Contains set of variables
/**
 * This object can contain many variables with common prefix, or variables, which
 * names are in alphabetically ordered range. You can use this sets as tables. For
 * example to store all market trades
 *
 * @code
 * key: tickdata_<timestamp_ns>
 * value: TupleBin<Decimal, Decimal> //(price, size)
 * @endcode
 *
 * You can then retrieve whole table or part of the table using get_vars() functions.
 *
 * @code
 * auto s = context.get_vars<TupleBin<Decimal,Decimal> >(tickdata_from, tickdata_to);;
 * for (const auto &[key, value]: s) {
 *      auto tm = parse_time(key);
 *      auto [price, size] = value;
 *      //process result
 * }
 * @endcode
 *
 * @tparam specifies type of stored value, which can be SerializableType or a TupleBin
 * for multicolumn tables. Default value is std::string_view, which is native
 * internal representation
 *
 * @note VarSet is movable only. You can iterate it only once at time - contains
 * input only iterator. However you can iterate it repeatedly,
 * if you obtain new pair begin()-end() on each iteration. In all cases, it
 * should act as a immutable snapshot.
 *
 */
template<typename T = std::string_view>
class VarSet;


template<SerializableType T>
class VarSet<T> {
public:

    ///Inicializes empty set
    VarSet() = default;

    ///Inicializes set by service provider
    VarSet(std::unique_ptr<IVarSet> ptr):_ptr(std::move(ptr)) {}

    ///Converts to set of different type
    template<SerializableType U>
    VarSet(VarSet<U> &&other):_ptr(std::move(other._ptr)) {}

    ///Declares iterator - input iterator
    /** The iterator is copyable, but keep in mind, that it is input iterator, so
     *  there is only one shared state during iteration.
     */
    class iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        ///The iterator returns key a and value. The key is always string
        using value_type = std::pair<std::string_view, T>;

        ///empty iterator is marked as invalid
        iterator() = default;
        ///construct iterator
        iterator(const VarSet *owner, bool valid):_owner(owner),_valid(valid) {fetch();};


        const value_type &operator *() const { return _val;}
        const value_type *operator->() const { return &_val;}
        iterator &operator++() {
            _valid = !_owner->_ptr->next();
            fetch();
            return *this;
        }
        iterator operator++(int) {
            iterator cpy(*this);
            operator++();
            return cpy;
        }
        bool operator==(const iterator &other) const {
            return &_owner == &other._owner && _valid == other._valid;
        }
    protected:
        const VarSet *_owner = {};
        bool _valid = false;
        value_type _val = {};
        void fetch() {
            if (_valid) {
                if constexpr(std::is_same_v<T, std::string_view>) {
                    _val = _owner->_ptr->get();
                } else {
                    auto tmp = _owner->_ptr->get();
                    auto itr = tmp.second.begin();
                    auto end = tmp.second.end();
                    _val = {tmp.first, Serializer::from_binary<T>(itr, end)};
                }
            }
        }

    };

    ///retrieve iterator on first item
    /**
     * @return iterator to begin
     * @note always resets internal state. Do try to retrieve multiple begins by this
     * function. If you really need multiple iterators, make copy, but keep in mind
     * there is actually one shared state
     */
    iterator begin() const {
        return iterator(this, _ptr?_ptr->init():false);
    }
    ///retrieve iterator at the end
    iterator end() const {
        return iterator(this, false);
    }

    ///retrieve internal handle pointer
    IVarSet *get_handle() const {return _ptr.get();}

protected:

    template<typename U> friend class VarSet;

    std::unique_ptr<IVarSet> _ptr;
};

template<is_TupleBin_type T>
class VarSet<T>: public VarSet<typename T::tuple_type> {
    using VarSet<typename T::tuple_type>::VarSet;
};


}
