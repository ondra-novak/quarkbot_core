#pragma once

#include "strategy.h"

namespace quarkbot {

///Implements persistent series container
/**
 * The container is attached to a strategy. You should declare a series
 * container as member variable of a strategy
 *
 * @tparam T type of value stored in series
 *
 * The correct usage is
 * - when strategy starts, call load() to restore state from the database
 * - to add point, call push()
 * - to keep only n last points, call crop() for each push().
 */
template<SerializableType T>
class PersistentSeries {
public:

    ///construct series container
    /**
     * @param strategy strategy pointer
     * @param name name of series
     */
    PersistentSeries(Strategy *strategy, std::string name)
        :_strategy(strategy)
        ,_name(name)
    {}

    ///load state from the persistent storage
    void load() {
        _data.clear();
        ValueStream<T> stream = _strategy->load_series(_name);
        for (const T &val: stream) {
            _data.push_back(val);
        }
    }

    ///retrive count of stored points
    std::size_t size() const {return _data.size();}
    ///first (recent) point
    const T &first() const {return _data.back();}
    ///last (old) point
    const T &last() const {return _data.front();}
    ///begin of container (starting by old)
    typename std::deque<T>::const_iterator begin() const {return _data.begin();}
    ///end of container (ends by recent value)
    typename std::deque<T>::const_iterator end() const {return _data.end();}
    ///begin of container (starting by old)
    typename std::deque<T>::const_reverse_iterator rbegin() const {return _data.rbegin();}
    ///end of container (ends by recent value)
    typename std::deque<T>::const_reverse_iterator rend() const {return _data.rend();}
    ///push new value
    /**
     * @param val new value
     */
    void push(T val) {
        _data.push_back(val);
        _idx=_strategy->series_add_point(_name, val);
    }

    ///Crops container to last count values
    /**
     * @param count maximum count of values in series
     * @return last removed value (recent one), if nothing removed, then empty
     */
    std::optional<T> crop(std::size_t count) {
        std::optional<T> out = {};
        while (_data.size() > count) {
            out.emplace(std::move(_data.front()));
            _data.pop_front();
        }
        if (_idx > count) {
            _strategy->series_erase_points(_name, _idx-count);
        }
        return out;
    }

    void pop() {
        _data.pop_back();
        if (_data.size() < _idx) {
            _strategy->series_erase_points(_name, _idx - _data.size());
        }
    }

protected:
    Strategy *_strategy;
    std::string _name;
    std::deque<T> _data;
    std::uint64_t _idx = 0;
};

template<SerializableType T>
class LocalSeries {

    ///retrive count of stored points
    std::size_t size() const {return _data.size();}
    ///first (recent) point
    const T &first() const {return _data.back();}
    ///last (old) point
    const T &last() const {return _data.front();}
    ///begin of container (starting by old)
    typename std::deque<T>::const_iterator begin() const {return _data.begin();}
    ///end of container (ends by recent value)
    typename std::deque<T>::const_iterator end() const {return _data.end();}
    ///begin of container (starting by old)
    typename std::deque<T>::const_reverse_iterator rbegin() const {return _data.rbegin();}
    ///end of container (ends by recent value)
    typename std::deque<T>::const_reverse_iterator rend() const {return _data.rend();}
    ///push new value
    /**
     * @param val new value
     */
    void push(T val) {
        _data.push_back(val);
    }

    ///Crops container to last count values
    /**
     * @param count maximum count of values in series
     * @return last removed value (recent one), if nothing removed, then empty
     */
    std::optional<T> crop(std::size_t count) {
        std::optional<T> out = {};
        while (_data.size() > count) {
            out.emplace(std::move(_data.front()));
            _data.pop_front();
        }
        return out;
    }

    void pop() {
        _data.pop_back();
    }


protected:
    std::deque<T> _data;

};


template<SerializableType T>
class PersistentVar {
public:

    PersistentVar(Strategy *s, std::string name)
        :_s(s), _name(std::move(name)) {}
    PersistentVar(Strategy *s, std::string name, T val)
        :_s(s), _name(std::move(name)),_val(std::move(val)) {}

    void set(T val) {
        _val = val;
        _s->set_var(_name, val);
    }

    operator T() const {return _val;}

protected:
    T _val = {};
    Strategy *_s = {};
    std::string _name = {};
};

template<SerializableType T>
class LocalVar {
public:

    LocalVar() = default;
    LocalVar(T val):_val(std::move(val)) {}

    void set(T val) {
        _val = val;
    }

    operator T() const {return _val;}

protected:
    T _val = {};
};


}
