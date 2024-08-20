    #include "matching.h"
#include "sim_instrument.h"

namespace trading_api {



namespace simulator {

Matching::Matching()
    :_me_tick(std::make_shared<MarketEvent_TickData<> >()) {}




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

void Matching::set_trade(Decimal last, Decimal last_size) {
    _last = last;
    _last_size =last_size;
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
                            to_exec});
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

MarketEvent Matching::get_ticker(Timestamp curTime) const {
    _me_tick->set(TickData{
        curTime,_spread.bid,_spread.bid_size,_spread.ask, _spread.ask_size, _last, _last_size,0
    });
    return MarketEvent(_me_tick);

}


}

}
