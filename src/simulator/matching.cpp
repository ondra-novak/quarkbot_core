#include "matching.h"
#include "sim_instrument.h"

namespace trading_api {



namespace simulator {


void Matching::set_spread(Decimal bid, Decimal ask) {
    _bid = bid;
    _ask = ask;
}

void Matching::set_trade(Decimal last, Decimal last_size) {
    _last = last;
    _last_size =last_size;
}

Decimal Matching::get_bid() const {
    return _bid;
}

Decimal Matching::get_ask() const {
    return _ask;
}

Matching::Execution Matching::place_market_order(Order ord, Side side, Decimal amount) {
    Execution x = {std::move(ord), side};
    switch(side) {
        default: break;
        case Side::buy:
            if (!is_nan(_ask)) {
                x.price = _ask;
                x.size = amount;
            }
            break;
        case Side::sell:
            if (!is_nan(_bid)) {
                x.price = _bid;
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
                if ((item.side == Side::buy && _ask <= item.limit_price)
                        || (item.side == Side::sell && _bid >= item.limit_price)){
                    exx.push_back({std::move(item.order),item.side,item.limit_price,item.amount});
                    return true;
                }
            }
            if constexpr(std::is_same_v<T, Stop> || std::is_same_v<T, TpSl>) {
                if (item.side == Side::buy && _ask > item.stop_price) {
                    exx.push_back({std::move(item.order),item.side,_ask,item.amount});
                } else if (item.side == Side::sell && _bid < item.stop_price) {
                    exx.push_back({std::move(item.order),item.side,_bid,item.amount});
                }
                return true;

            }
            if constexpr(std::is_same_v<T, StopLimit>) {
                if (item.side == Side::buy && _ask > item.stop_price) {
                    if (item.limit_price >= _ask) {
                        exx.push_back({std::move(item.order),item.side,_ask,item.amount});
                    } else {
                        _updates.push_back(Limit{std::move(item.order), item.side, item.amount, item.limit_price});
                    }
                    return true;
                } else if (item.side == Side::sell && _bid < item.stop_price) {
                    if (item.limit_price <= _bid) {
                        exx.push_back({std::move(item.order),item.side,_bid,item.amount});
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
                    if (is_nan(item.stop_price) || item.stop_price - _ask > item.distance) {
                        _updates.push_back(TrailingStop{
                            std::move(item.order),item.side,item.amount,item.distance,_ask + item.distance,
                        });
                        return true;
                    } else if (_ask > item.stop_price) {
                        exx.push_back({std::move(item.order), item.side,_ask, item.amount});
                    }
                }
                else if (item.side == Side::sell) {
                    if (is_nan(item.stop_price) || _bid - item.stop_price  > item.distance) {
                        _updates.push_back(TrailingStop{
                            std::move(item.order),item.side,item.amount,item.distance,_bid - item.distance
                        });
                        return true;
                    } else if (_bid < item.stop_price) {
                        exx.push_back({std::move(item.order), item.side,_bid, item.amount});
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
    return exx;
}

Decimal Matching::get_effective_price() const {
    if (is_nan(_bid) || is_nan(_ask)) return _last;
    else return (_bid + _ask)/2_dec;
}

}

}
