#include "simulated_instrument.hpp"
#include "coro/src/basic_coro/prepared_coro.hpp"
#include "ifc/defs.hpp"
#include "ifc/market_events.hpp"
#include "ifc/order.hpp"
#include "instrument_base.hpp"
#include "base_order.hpp"
#include "utils/spin_mutex.hpp"
#include "utils/uuid.hpp"
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>

namespace quarkbot {

    
class QuoteServer: public SimulatedInstrument::MyServer<Quote> {    
public:

    virtual void on_data_received(const StreamTypeItem &data) noexcept override {
        std::unique_lock lk(_mx);
        const Quote &q = static_cast<const Quote &>(data);
        if (order_quote) {
            Quote out;  
            if (q.ask > order_quote->ask && q.bid < order_quote->ask) {
                out.ask = order_quote->ask;
                out.ask_size = order_quote->ask_size;
            }
            if (q.bid < order_quote->bid && q.ask > order_quote->bid) {
                out.bid = order_quote->bid;
                out.bid_size = order_quote->bid_size;
            }
            lk.unlock();
            this->post(out);
        } else {
            lk.unlock();
            this->post(q);
        }        
    }
    void set_order_quote(Quote qt) {
        std::lock_guard _(_mx);
        order_quote = qt;
    }


protected:
    std::optional<Quote> order_quote;
    spin_mutex _mx;

};


SimulatedInstrument::SimulatedInstrument(
        const Info &info, 
        PUnderlyingCurrency quote, 
        PUnderlyingCurrency asset, 
        PUnderlyingCurrency pnl, 
        PExchange exchange
    ) :_info(std::make_shared<Info>(info))
      ,_quote_currency(std::move(quote))
      ,_asset_currency(std::move(asset))
      ,_pnl_currency(std::move(pnl))
      ,_exchange(std::move(exchange)) {}


std::shared_ptr<IMarketEventStreamBase> SimulatedInstrument::subscribe_stream_internal(StreamTypeItem::Type type) const {
    if (type == Quote::type) {
        return this->subscribe<Quote, MyServer<Quote>, QuoteServer>(this->_quote_server);
    } else {
        return InstrumentBase::subscribe_stream_internal(type);
    }
}

void SimulatedInstrument::place_order(std::shared_ptr<BaseOrder> order) {
    auto &params = order->get_parameters();
    if (params.hedge || params.reduce_only) {
        order->reject(OrderRejectionReason::unsupported);
        return;
    }
    const auto &info = *_info;
    if (params.amount < info.min_lot_size) {
        order->reject(OrderRejectionReason::too_small);
        return ;
    }
    if (params.side == Side::undetermined) {
        order->reject(OrderRejectionReason::invalid_params);
        return ;
    }
    if (params.type == OrderType::limit_post_only && _last_quote) {
        if ((params.side == Side::buy && _last_quote->ask <= params.limit_price)
          || (params.side == Side::sell && _last_quote->bid >= params.limit_price)) {
                order->reject(OrderRejectionReason::post_only_taker);
        }
    }
    std::lock_guard _(_mx);
    _orders.push_back(OrderRecord {order,true, {}});
    run_matching(std::nullopt);
}

void SimulatedInstrument::cancel_order(std::shared_ptr<BaseOrder> order) {
    std::unique_lock lk(_mx);
    auto iter = std::find_if(_orders.begin(), _orders.end(), [&](const OrderRecord &rc) {
        return order == rc.order;
        });
    if (iter == _orders.end()) return;
    _orders.erase(iter);
    lk.unlock();
    order->post_update(OrderStatus::canceled);
}

template<typename Param>
void SimulatedInstrument::run_matching(Param trade) {
    static_assert(std::same_as<Param, std::nullopt_t> || std::same_as<Param, const Trade &>);
    if (!_last_quote)return;
    

    Quote new_order_quotes{{},Fixed::min(),{},Fixed::max(), {}, _last_quote->time};
    auto iter = std::remove_if(_orders.begin(), _orders.end(), [&](OrderRecord &rc){
        const auto &params  = rc.order->get_parameters();

        auto full_fill = [&]{
            auto amount = params.amount - rc.fill_amount;
            if (params.side == Side::buy) 
                rc.order->post_update(create_fill(_last_quote->ask, amount, params.side, _last_quote->time));
            else 
                rc.order->post_update(create_fill(_last_quote->bid, amount, params.side, _last_quote->time));
            rc.order->post_update(OrderStatus::filled);
        };

        auto stop_trig = [&]{
            if constexpr(std::is_same_v<Param, const Trade &>) {
                if (params.side == Side::buy && params.stop_price <= trade.price) {
                    return true;
                }
                if (params.side == Side::sell && params.stop_price >= trade.price) {
                    return true;
                }
            }
            if (params.side == Side::buy && params.stop_price <= _last_quote->bid) {
                return true;
            }
            if (params.side == Side::sell && params.stop_price >= _last_quote->ask) {
                return true;
            }
            return false;
        };

        auto crossing_fill = [&] {
            auto amount = params.amount - rc.fill_amount;
            if constexpr(std::is_same_v<Param, const Trade &>) {
                if (params.side == Side::buy) {
                    if (trade.price <= params.limit_price) {
                        amount = std::min(amount, trade.size);
                        rc.order->post_update(create_fill(trade.price, amount, params.side, trade.time));    
                        rc.fill_amount += amount;
                    }
                }  else {
                    if (trade.price >= params.limit_price) {
                        amount = std::min(amount, trade.size);
                        rc.order->post_update(create_fill(trade.price, amount, params.side, trade.time));    
                        rc.fill_amount += amount;
                    }
                }
                amount = params.amount - rc.fill_amount;
            }
            if (params.side == Side::buy) {                
                if (params.limit_price >= _last_quote->ask) {
                    if (params.limit_price == _last_quote->ask) amount = std::min(amount, _last_quote->ask_size);
                    rc.order->post_update(create_fill(_last_quote->ask, amount, params.side, _last_quote->time));
                    rc.fill_amount += amount;
                }
                
            } else {
                if (params.limit_price <= _last_quote->bid) {
                    if (params.limit_price == _last_quote->ask) amount = std::min(amount, _last_quote->bid_size);
                    rc.order->post_update(create_fill(_last_quote->ask, amount, params.side, _last_quote->time));
                    rc.fill_amount += amount;
                }
            }
            bool b = rc.fill_amount >= params.amount;
            if (b) rc.order->post_update(OrderStatus::filled);
            return b;    
        };

        auto update_new_quote = [&]{
            if (params.side == Side::buy && params.limit_price > new_order_quotes.bid)  {
                new_order_quotes.bid = params.limit_price;
                new_order_quotes.bid_size = params.amount;
            }
            if (params.side == Side::sell && params.limit_price < new_order_quotes.ask)  {
                new_order_quotes.ask = params.limit_price;
                new_order_quotes.ask_size = params.amount;
            }
        };

        switch(params.type) {            
            case OrderType::stop: 
                if (stop_trig()) {
                    full_fill();
                    return true;
                }
                break;
            case OrderType::market:
                full_fill();
                return true;
            case OrderType::oco: 
                if (stop_trig()) {
                    full_fill();
                    return true;
                }
                else if (crossing_fill()) return true;
                break;            
            case OrderType::limit_post_only:
            case OrderType::limit:
                if (crossing_fill()) return true;
                update_new_quote();
                break;
            case OrderType::stoplimit:
                if (rc.stopped) {
                    if (stop_trig()) {
                        rc.stopped = false;
                    } else {
                        break;
                    }
                } 
                if (!rc.stopped){
                    if (crossing_fill()) return true;
                    update_new_quote();
                }                    
                break;
            case OrderType::limit_ioc:
                if (!crossing_fill()) {
                    rc.order->post_update(OrderStatus::canceled);                    
                }
                return true;
        }
        return true;

    });
    _orders.erase(iter, _orders.end());
    auto qs = _quote_server.load().lock();
    if (qs) {
        auto qqs = std::static_pointer_cast<QuoteServer>(qs);
        qqs->set_order_quote(new_order_quotes);
    }
}

Fill SimulatedInstrument::create_fill(Fixed price, Fixed amount, Side side, std::chrono::system_clock::time_point tm) {
    Fill f;
    f.id = generate_random_string();
    f.contract = *_info;
    f.amount = amount;
    f.price = price;
    f.side = side;
    f.fee_rate = 1.0 ;//todo
    f.fees = 0.0;   //todo
    f.time = tm;
    return f;
}

void SimulatedInstrument::on_data(const Quote &x){
    std::unique_lock lk(_mx);
    _last_quote = x;
    run_matching(std::nullopt);


}
void SimulatedInstrument::on_data(const Trade &x) {
    std::unique_lock lk(_mx);
    run_matching<const Trade &>(x);

}

void SimulatedInstrument::connect(std::shared_ptr<IDataSource> data_src,  std::string_view stream_topic) {
    InstrumentBase::connect(data_src, stream_topic);
    auto rf = std::static_pointer_cast<SimulatedInstrument>(shared_from_this());
    _quote_stream = std::make_shared<MyDataSource<Quote> >(rf);
    _trade_stream = std::make_shared<MyDataSource<Trade> >(rf);
    _data_src->subscribe(_stream_topic,_quote_stream);
    _data_src->subscribe(_stream_topic,_trade_stream);

}


}