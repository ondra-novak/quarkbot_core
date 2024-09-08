#pragma once

#include "../strategy.h"

namespace quarkbot {

template<typename T>
class EMA {
public:

    EMA(Strategy *s, std::string name, unsigned int bars)
        :_s(s), _name(name), _bars(bars+1), _initialized(false) {}

    void load() {
        _ema = _s->get_var(_name, T());
        if (_ema == T()) _initialized = false;
    }

    void update(const T& new_value) {
         if (!_initialized) {
             _ema = new_value;
             _initialized = true;
         } else {
             _ema = (new_value - _ema) * static_cast<T>(2) / _bars + _ema;
         }
         _s->set_var(_name, _ema);
     }

    T value() const {
        return _ema;
    }


 protected:
    Strategy *_s;
    std::string _name;
    unsigned int _bars;
    T _ema;
    bool _initialized;
 };


}
