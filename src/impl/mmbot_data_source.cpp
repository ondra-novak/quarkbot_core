#include "mmbot_data_source.hpp"
#include "ifc/market_events.hpp"
#include <stdexcept>
#include <string>

namespace quarkbot {

std::optional<Decimal> MMBOT_backtest_datasource::load_number() {
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

std::optional<MMBOT_backtest_datasource::Event> MMBOT_backtest_datasource::next_event() {    
    std::optional<MMBOT_backtest_datasource::Event> out;
    if (_price) {
        auto t = _tp-std::chrono::seconds(30);
        out.emplace(Event {t, _instrument, Trade{
            {}, *_price,{}, t
        }});
        _price.reset();
        return out;
    }
    auto n = load_number();
    if (!n) return out;
    out.emplace(Event {
        _tp,_instrument, Quote{
            {},std::min(_prev_price, *n),{},std::max(_prev_price, *n),{},_tp
        }
    });
    _tp = _tp + std::chrono::minutes(1);
    _price = (*n + _prev_price) / 2_dec;
    _prev_price = *n;
    return out;
}
 

MMBOT_backtest_datasource::MMBOT_backtest_datasource(std::string instrument, std::filesystem::path path, std::chrono::system_clock::time_point start_time)
    :_ifstream(path, std::ios::in)
    ,_stream(_ifstream) 
    ,_instrument(std::move(instrument))
    ,_tp(start_time) {
        if (!_ifstream) throw std::runtime_error("Can't open input file");
        auto x= load_number();
        if (x) _prev_price = *x;
    }

MMBOT_backtest_datasource::MMBOT_backtest_datasource(std::string instrument, std::istream &stream, std::chrono::system_clock::time_point start_time) 
    :_stream(stream)
    ,_instrument(std::move(instrument))
    ,_tp(start_time)  {
        auto x= load_number();
        if (x) _prev_price = *x;
    }

}