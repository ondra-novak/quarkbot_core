#include "simexchange.hpp"
#include "ifc/underlying.hpp"
#include "impl/simaccount.hpp"
#include "siminstrument.hpp"
#include "ifc/defs.hpp"
#include "ifc/market_events.hpp"
#include "ifc/stream_defs.hpp"
#include "impl/streaming/publisher_manager.hpp"
#include "simtradableinstrument.hpp"
#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <system_error>
#include <unordered_set>

namespace quarkbot {

template<typename T, typename Pub>
std::unique_ptr<IEventStreamBase> SimExchange::connect_to(std::shared_ptr<SimInstrument> instrument, const StreamParams *params) {
    PMarketInstrument gen_inst(instrument);
    auto r =_streams.connect_to(gen_inst, {}, T::type, params);
    if (!r) {
            auto pub = _streams.register_publisher(gen_inst, {}, T::type, params, std::make_shared<Pub>());
            r =  pub->create_subscriber(pub);        
    }
    return r;
}


PAccount SimExchange::create_account(std::string name, std::span<std::pair<std::string, Decimal> > wallet) {
    std::vector<std::pair<UnderlyingCurrency, Decimal> > trn_wallet;
    std::transform(wallet.begin(),wallet.end(),std::back_inserter(trn_wallet), [&](const auto &x){
        return std::pair(create_currency(x.first), x.second);
    });
    auto simacc = std::make_shared<SimAccount>(std::move(name),trn_wallet);
    return simacc;
}


std::unique_ptr<IEventStreamBase> SimExchange::subscribe_stream(
        std::shared_ptr<SimInstrument> instrument,
        std::shared_ptr<SimAccount> /*account*/,
         StreamTypeItem::Type type,
          const StreamParams *params) {

    if (type == Quote::type) {
        return connect_to<Quote, QuotePublisher>(instrument,params);
    } else if (type == Trade::type) {
        return connect_to<Trade, TradePublisher>(instrument, params);
    } else if (type == ClosedBar::type) {
        return connect_to<ClosedBar, TradePublisher>(instrument, params);
    } else if (type == TradeCounter::type) {
        return connect_to<TradeCounter, TradeCounterPublisher>(instrument, params);
    } else {
        return {};
    }
}


std::shared_ptr<SimInstrument> SimExchange::resolve_instrument(const std::string &name) {
    auto iter = _instrument_names.find(name);
    if (iter == _instrument_names.end()) return {};
    auto lk = iter->second.lock();
    if (!lk) return {};
    return lk;

}

void SimExchange::on_event(const std::string &instrument, Quote qt) {
    auto mi = resolve_instrument(instrument);
    if (!mi) return;
    _executor.on_event(mi, qt); 
    _streams.enum_all_publishers(mi, {}, Quote::type, [&](const StreamParams *, PublisherManager::PPublisher pub){
        auto qtpub = std::static_pointer_cast<QuotePublisher>(pub);
        qtpub->write([&](Quote &s) noexcept {s = qt;return true;});        
    });
    for (auto &x: _tradable_instruments) {
        auto trad = x.lock();
        if (trad && trad->get_instrument().get() == mi.get()) {
            auto mid_price = (qt.bid + qt.ask)/2_dec;
            trad->report_price(mid_price);
        }
    }

}
void SimExchange::on_event(const std::string &instrument, Trade tr) {
    auto mi = resolve_instrument(instrument);
    if (!mi) return;
    _executor.on_event(mi, tr); //todo refere instrument by object
    _streams.enum_all_publishers(mi, {}, Trade::type, [&](const StreamParams *, PublisherManager::PPublisher pub){
        auto trpub = std::static_pointer_cast<TradePublisher>(pub);
        trpub->write([&](Trade &s)noexcept {s = tr;return true;});        
    });
    _streams.enum_all_publishers(mi,{},ClosedBar::type, [&](const StreamParams *parm, PublisherManager::PPublisher pub){
        auto cbpub = std::static_pointer_cast<ClosedBarPublisher>(pub);
        auto p =  static_cast<const ClosedBar::ParamType *>(parm);
        auto interval =p->param;
        bool new_bar = false;
        std::size_t new_tp = static_cast<std::size_t>(std::chrono::duration_cast<std::chrono::seconds>(tr.time.time_since_epoch()).count()/interval);
        cbpub->write([&](ClosedBar &s) noexcept {            
            if (s.interval_index != new_tp) {
                new_bar = true;
                return true;
            } else{
                s.close = tr.price;
                s.high = std::max(s.high, tr.price);
                s.low = std::min(s.low, tr.price);
                s.volume += tr.size;                
                return false;
            }
        });
        if (new_bar) {
            cbpub->write([&](ClosedBar &s) noexcept {
                s.open = s.close = s.high = s.low = tr.price;
                s.volume = tr.size;
                s.interval_index = new_tp;            
                return false;
            });
        }
    });
    _streams.enum_all_publishers(mi, {}, TradeCounter::type, [&](const StreamParams *, PublisherManager::PPublisher pub){
        auto tcpub = std::static_pointer_cast<TradeCounterPublisher>(pub);
        TradeCounter cntr = {};
        auto s = tcpub->get_top_seq();
        if (s > 0) [[likely]] {
            --s;
            tcpub->read(cntr,s);
        }
        cntr.last_price = tr.price;
        cntr.volume += tr.size;
        cntr.trades++;
        cntr.time = tr.time;                
        tcpub->write([&](TradeCounter &c) noexcept {c = cntr; return true;});        
    });
    
}

PTradableInstrument SimExchange::create_tradable_instrument(std::shared_ptr<SimInstrument> instrument,std::shared_ptr<SimAccount> account) {
    auto r = std::make_shared<SimTradableInstrument>(instrument, account);
    _tradable_instruments.push_back(r);
    return r;
}

bool SimExchange::cancel_all_orders(PTradableInstrument instrument ) {
    return _executor.cancel_all(instrument);
}
void SimExchange::cancel_order(Order ord) {
    _executor.cancel_order(ord);
}
void SimExchange::place_order(Order ord) {
    auto rep_ord =ord.get_replaced_order();
    if (rep_ord.has_value()) {
        _executor.replace_order(ord, *rep_ord);
    } else {
        _executor.place_order(ord);
    }
}

PMarketInstrument SimExchange::create_instrument(IMarketInstrument::Info def) {
    auto instr =  std::make_shared<SimInstrument>(def, shared_from_this());
    _instrument_names.emplace(def.name, instr);
    return instr;
}
UnderlyingCurrency SimExchange::create_currency(std::string_view name, bool is_unified) {
    return UnderlyingCurrency{std::string(name), is_unified?std::string(name):std::string(), this};
}
UnderlyingCurrency SimExchange::create_currency(std::string_view name) const {
    return UnderlyingCurrency{std::string(name), std::string(name), this};
}
PAccount SimExchange::create_account(const std::string &name, const std::string &) const {
    return std::make_shared<SimAccount>(name, std::span<std::pair<UnderlyingCurrency, Decimal> >{});
}
std::vector<PMarketInstrument> SimExchange::get_market_instruments() const {
    std::vector<PMarketInstrument> out;
    for (auto &[k,v]:_instrument_names) {
        auto lk = v.lock();
        if (lk) out.push_back(std::move(lk));
    }
    return out;
}
std::vector<UnderlyingCurrency> SimExchange::get_all_currencies() const {
    std::unordered_set<UnderlyingCurrency, UnderlyingCurrency::Hash> map;
    for (auto &[k,v]:_instrument_names) {
        auto lk = v.lock();
        if (lk) {
            const auto &info = lk->get_info();
            map.insert(info.pnl_currency);
            map.insert(info.quote_currency);
            if (info.asset_has_wallet()) {
                map.insert(*info.asset_wallet);
            }
        }
    }
    return {map.begin(), map.end()};
}
std::string_view SimExchange::get_name() const {
    return "simulator";
}



}