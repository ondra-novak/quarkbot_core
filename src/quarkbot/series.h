#pragma once


#include "strategy.h"

namespace quarkbot {

template<typename T> class Persistent;


template<is_indicator _Ind>
class Persistent<_Ind>: public _Ind {
public:
    struct IsPersistentIndicator_Tag{};

    using value_type = typename _Ind::value_type;
    using Super = _Ind;

    using _Ind::_Ind;

    ///restore indicator from persistent storage
    void restore(Strategy *s, std::string_view name) {
        _s = s;
        this->_name.clear();
        this->_name.append(name);
        Super::clear();
        ValueStream<value_type> stream (_s->load_series(_name));
        for (value_type val: stream) {
            Super::update(val);
        }
    }

    void update(value_type v) {
        Super::update(v);
        auto count = Super::max_count();
        if (_s) {
            _ptidx =_s->series_add_point(_name, TupleBin<value_type>::compose(v));
            if (_ptidx >= count) _s->series_erase_points(_name, _ptidx-count);
        }
    }


    void clear() {
        Super::clear();
        if (_s) {
            _s->series_erase_points(_name, _ptidx);
        }
    }


    const std::string &get_name() const {return _name;}
    Strategy *get_strategy() const {return _s;}


protected:
    Strategy *_s = {};
    std::string _name = {};
    std::uint64_t _ptidx = 0;
};

template<typename From, typename To>
class SubIndicator_t {
    using type = To;
};

template<is_persistent_indicator From, is_indicator To>
class SubIndicator_t<From, To> {
    using type = Persistent<To>;
};

template<typename From, typename To>
using SubIndicator = typename SubIndicator_t<From, To>::type;

template<typename From, typename To, typename ... Args>
auto init_subindicator(const From &src, std::string_view name, Args && ... args) {
    if constexpr(is_persistent_indicator<From>) {
        return Persistent<To>(src.get_strategy(), src.get_name()+name, std::forward<Args>(args)...);
    } else {
        return To(std::forward<Args>(args)...);
    }
}



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
//template<SerializableType T>
//class PersistentSeries {
//public:
//
//    ///construct series container
//    /**
//     * @param strategy strategy pointer
//     * @param name name of series
//     */
//    PersistentSeries(Strategy *strategy, std::string name)
//        :_strategy(strategy)
//        ,_name(name)
//    {}
//
//    ///load state from the persistent storage
//    void load() {
//        _data.clear();
//        ValueStream<T> stream = _strategy->load_series(_name);
//        for (const T &val: stream) {
//            _data.push_back(val);
//        }
//    }
//
//    ///retrive count of stored points
//    std::size_t size() const {return _data.size();}
//    ///first (recent) point
//    const T &first() const {return _data.back();}
//    ///last (old) point
//    const T &last() const {return _data.front();}
//    ///begin of container (starting by old)
//    typename std::deque<T>::const_iterator begin() const {return _data.begin();}
//    ///end of container (ends by recent value)
//    typename std::deque<T>::const_iterator end() const {return _data.end();}
//    ///begin of container (starting by old)
//    typename std::deque<T>::const_reverse_iterator rbegin() const {return _data.rbegin();}
//    ///end of container (ends by recent value)
//    typename std::deque<T>::const_reverse_iterator rend() const {return _data.rend();}
//    ///push new value
//    /**
//     * @param val new value
//     */
//    void push(T val) {
//        _data.push_back(val);
//        _idx=_strategy->series_add_point(_name, val);
//    }
//
//    ///Crops container to last count values
//    /**
//     * @param count maximum count of values in series
//     * @return last removed value (recent one), if nothing removed, then empty
//     */
//    std::optional<T> crop(std::size_t count) {
//        std::optional<T> out = {};
//        while (_data.size() > count) {
//            out.emplace(std::move(_data.front()));
//            _data.pop_front();
//        }
//        if (_idx > count) {
//            _strategy->series_erase_points(_name, _idx-count);
//        }
//        return out;
//    }
//
//    void pop() {
//        _data.pop_back();
//        if (_data.size() < _idx) {
//            _strategy->series_erase_points(_name, _idx - _data.size());
//        }
//    }
//
//protected:
//    Strategy *_strategy;
//    std::string _name;
//    std::deque<T> _data;
//    std::uint64_t _idx = 0;
//};

template<typename T>
class LocalSeries {

    bool empty() const {return _data.empty();}
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
    void crop(std::size_t count) {
        while (_data.size() > count) {
            _data.pop_front();
        }
    }

    void pop() {
        _data.pop_back();
    }


protected:
    std::deque<T> _data;

};

template<SerializableType T>
class PersistentSeries: public LocalSeries<T> {
public:

    PersistentSeries(Strategy *strategy, std::string series_name)
         :_strategy(strategy)
         ,_name(series_name)
    {
        _strategy->on_started() >> [this] {
            ValueStream<T> stream = _strategy->load_series(_name);
            this->_data.clear();
            for (T val: stream) {
                this->_data.push_back(val);
            }
        };
    }

    PersistentSeries(const PersistentSeries &) = default;
    PersistentSeries &operator=(const PersistentSeries &) = default;

    void push(T val) {
        LocalSeries<T>::push(val);
        _ptidx =_strategy->series_add_point(_name, TupleBin<T>::compose(val));
    }


    std::optional<T> crop(std::size_t count) {
        LocalSeries<T>::crop(count);
        if (_ptidx >= count) {
            _strategy->series_erase_points(_name, _ptidx-count);
        }
    }

    void pop() {
        LocalSeries<T>::pop();
        auto sz = this->size();
        if (_ptidx >= sz) {
            _strategy->series_erase_points(_name,sz);
        }
    }

protected:
    std::string _name;
    Strategy *_strategy;
    std::uint64_t _ptidx = 0;
};


}
