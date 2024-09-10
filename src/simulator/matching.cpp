    #include "matching.h"
#include "sim_instrument.h"

namespace quarkbot {



namespace simulator {



void Matching::set_spread(const Spread &spread) {
    _spread = spread;
    update_spread();
}

Matching::Spread Matching::get_spread() const {
    return _spread;
}

void Matching::update_spread() {
    for (const auto &w: _orders) {
        std::visit([&](const auto &x){
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, Limit> || std::is_same_v<T, TpSl>) {
                switch (x.side) {
                    default: break;
                    case Side::buy: _spread.bid = std::max(_spread.bid, x.limit_price);break;
                    case Side::sell: _spread.ask = std::max(_spread.ask, x.limit_price);break;
                }
            }
        },w);
    }
}



Matching::Execution Matching::place_market_order(Order ord, Side side, Decimal amount) {
    Execution x = {std::move(ord), side};
    switch(side) {
        default: break;
        case Side::buy:
            if (!is_nan(_spread.ask)) {
                x.price = _spread.ask;
                x.size = amount;
            }
            break;
        case Side::sell:
            if (!is_nan(_spread.bid)) {
                x.price = _spread.bid;
                x.size = amount;
            }
            break;
    }
    return x;
}

void Matching::place_waiting_order(WaitingOrder ord) {
    _orders.push_back(std::move(ord));
}

const Matching::WaitingOrder *Matching::find_order(Order ord) const {
    auto iter = std::find_if(_orders.begin(), _orders.end(), [&](const WaitingOrder &w){
       return std::visit([&](const auto &x){return x.order == ord;},w);
    });
    if (iter == _orders.end()) return nullptr;
    return &(*iter);
}

bool Matching::cancel_order(Order order) {
    auto iter = std::find_if(_orders.begin(), _orders.end(), [&](const WaitingOrder &w){
       return std::visit([&](const auto &x){return x.order == order;},w);
    });
    if (iter == _orders.end()) return false;
    _orders.erase(iter);
    return true;
}

std::vector<Matching::Execution> Matching::get_executions() {
    std::vector<Matching::Execution> exx;
    auto iter = std::remove_if(_orders.begin(), _orders.end(), [&](const WaitingOrder &ord){
        return std::visit([&](const auto &item){
            using T = std::decay_t<decltype(item)>;
            if constexpr(std::is_same_v<T, Limit> || std::is_same_v<T, TpSl>) {
                if (_last_size != 0_dec && (
                        (item.side == Side::buy && _last <= item.limit_price)
                        || (item.side == Side::sell && _last >= item.limit_price)
                        )) {
                    auto to_exec = std::min(_last_size, item.amount);
                    exx.push_back({std::move(item.order),item.side,item.limit_price,
                            to_exec, item.amount - to_exec});
                    _last_size -= to_exec;
                    return true;
                }
                if ((item.side == Side::buy && _spread.ask <= item.limit_price)
                        || (item.side == Side::sell && _spread.bid >= item.limit_price)){
                    exx.push_back({std::move(item.order),item.side,item.limit_price,item.amount});
                    return true;
                }
            }
            if constexpr(std::is_same_v<T, Stop> || std::is_same_v<T, TpSl>) {
                if (item.side == Side::buy && _spread.ask > item.stop_price) {
                    exx.push_back({std::move(item.order),item.side,_spread.ask,item.amount});
                } else if (item.side == Side::sell && _spread.bid < item.stop_price) {
                    exx.push_back({std::move(item.order),item.side,_spread.bid,item.amount});
                }
                return true;

            }
            if constexpr(std::is_same_v<T, StopLimit>) {
                if (item.side == Side::buy && _spread.ask > item.stop_price) {
                    if (item.limit_price >= _spread.ask) {
                        exx.push_back({std::move(item.order),item.side,_spread.ask,item.amount});
                    } else {
                        _updates.push_back(Limit{std::move(item.order), item.side, item.amount, item.limit_price});
                    }
                    return true;
                } else if (item.side == Side::sell && _spread.bid < item.stop_price) {
                    if (item.limit_price <= _spread.bid) {
                        exx.push_back({std::move(item.order),item.side,_spread.bid,item.amount});
                        return true;
                    } else {
                        _updates.push_back(Limit{std::move(item.order), item.side, item.amount, item.limit_price});
                    }
                    return true;
                } else {
                    return true;
                }
            }
            if constexpr(std::is_same_v<T, TrailingStop>) {
                if (item.side == Side::buy) {
                    if (is_nan(item.stop_price) || item.stop_price - _spread.ask > item.distance) {
                        _updates.push_back(TrailingStop{
                            std::move(item.order),item.side,item.amount,item.distance,_spread.ask + item.distance,
                        });
                        return true;
                    } else if (_spread.ask > item.stop_price) {
                        exx.push_back({std::move(item.order), item.side,_spread.ask, item.amount});
                    }
                }
                else if (item.side == Side::sell) {
                    if (is_nan(item.stop_price) || _spread.bid - item.stop_price  > item.distance) {
                        _updates.push_back(TrailingStop{
                            std::move(item.order),item.side,item.amount,item.distance,_spread.bid - item.distance
                        });
                        return true;
                    } else if (_spread.bid < item.stop_price) {
                        exx.push_back({std::move(item.order), item.side,_spread.bid, item.amount});
                        return true;
                    }else {
                        return true;
                    }
                }
            }
            return false;

        }, ord);
    });
    _orders.erase(iter, _orders.end());
    for (auto &x: _updates) _orders.push_back(std::move(x));
    _updates.clear();
    update_spread();
    return exx;
}

Decimal Matching::get_effective_price() const {
    if (is_nan(_spread.bid) || is_nan(_spread.ask)) return _last;
    else return (_spread.bid + _spread.ask)/2_dec;
}

MarketEventData Matching::get_ticker(Timestamp curTime) const {
    auto ev = _me_tick.create(TickData{
        curTime,_spread.bid,_spread.ask,_last, _index,
        _spread.bid_size,_spread.ask_size, _cum_volume, _trades
    });
    return MarketEventData(ev);

}

void Matching::accept_ticker(const TickData &tk) {
    set_spread({tk.bid, tk.ask, tk.bid_volume, tk.ask_volume});
    double dff = tk.cum_volume < _prev_volume?tk.cum_volume:tk.cum_volume - _prev_volume;
    _prev_volume = tk.cum_volume;
    _last_size = dff;
    _cum_volume = tk.cum_volume;
    _index = tk.index;
    _trades = tk.cum_trades;
}

}



}
