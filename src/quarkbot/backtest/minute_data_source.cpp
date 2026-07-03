#include "minute_data_source.hpp"
#include <stdexcept>
#include <string>

namespace quarkbot {

std::optional<Decimal> MinuteDataSource::load_number() {
    std::optional<Decimal> out;
    std::string ln;
    
    while (!_stream.eof()) {
        std::getline(_stream, ln);
        while (!ln.empty() && std::isspace(ln.back())) ln.pop_back();
        if (ln.empty()) continue;
        try {
            out = Decimal::from_string(ln);
            return out;
        } catch (...) {

        }
    }
    return out;
}

bool MinuteDataSource::operator()(BacktestEvent &ev) {    
    ev.symbol = _instrument;
    
    if (_price) {
        auto t = _tp-std::chrono::seconds(30);        
        ev.data = Trade{
            {}, *_price,{}, t, Side::undetermined
        };
        ev.time = t;
        _price.reset();
        return true;
    }
    auto n = load_number();
    if (!n) return false;
    ev.time = _tp;
    ev.data =  Quote{
            {},std::min(_prev_price, *n),{},std::max(_prev_price, *n),{},_tp
        };    
    _tp = _tp + std::chrono::minutes(1);
    _price = (*n + _prev_price) / 2_dec;
    _prev_price = *n;
    return true;
}
 

MinuteDataSource::MinuteDataSource(std::string instrument, std::filesystem::path path, std::chrono::system_clock::time_point start_time)
    :_ifstream(path, std::ios::in)
    ,_stream(_ifstream) 
    ,_instrument(std::move(instrument))
    ,_tp(start_time) {
        if (!_ifstream) throw std::runtime_error("Can't open input file");
        auto x= load_number();
        if (x) _prev_price = *x;
    }

MinuteDataSource::MinuteDataSource(std::string instrument, std::istream &stream, std::chrono::system_clock::time_point start_time) 
    :_stream(stream)
    ,_instrument(std::move(instrument))
    ,_tp(start_time)  {
        auto x= load_number();
        if (x) _prev_price = *x;
    }

}