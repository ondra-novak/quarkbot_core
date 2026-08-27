#include "simexchange.hpp"
#include "quarkbot/execution_worker.hpp"
#include "quarkbot/stream/orderbook.hpp"
#include "quarkbot/stream/quote.hpp"
#include "siminstrument.hpp"
#include "simtradableinstrument.hpp"
#include <algorithm>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <unordered_set>

namespace quarkbot {



PAccount SimExchange::create_account(std::string name, std::span<const std::pair<std::string, Decimal> > wallet) {
    std::vector<std::pair<UnderlyingCurrency, Decimal> > trn_wallet;
    std::transform(wallet.begin(),wallet.end(),std::back_inserter(trn_wallet), [&](const auto &x){
        return std::pair(create_currency(x.first), x.second);
    });
    auto simacc = std::make_shared<SimAccount>(std::move(name),trn_wallet);
    return simacc;
}


std::shared_ptr<IEventStreamBase> SimExchange::subscribe_stream(
        std::shared_ptr<SimInstrument> instrument,
        std::size_t hash, const void *param) {
    std::string name = instrument->get_info().name;
    return _streams.subscribe_stream(name, hash, param);
}


std::shared_ptr<SimInstrument> SimExchange::resolve_instrument(const std::string &name) {
    auto iter = _instrument_names.find(name);
    if (iter == _instrument_names.end()) return {};
    return iter->second.get(shared_from_this());
}

SimExchange::InstrumentRef *SimExchange::find_instrument_ref(const std::string &name) {
    auto iter = _instrument_names.find(name);
    return iter == _instrument_names.end()?nullptr:&iter->second;
}



///Get live instance for event dispatch - never creates one
/**
 * A missing instance means nobody holds this instrument, so there can be no
 * SimTradableInstrument for it (it keeps a strong reference) and therefore no
 * orders to match. Creating a throw-away instance per event would only churn
 * allocations - and, because SimExecutor compares instruments by pointer, it
 * could never match anything anyway.
 *
 * Market data streams are keyed by name and live independently of the
 * instance, so they must be fed even when this returns nullptr.
 */
std::shared_ptr<SimInstrument> SimExchange::instrument_for_event(const std::string &name) {
    auto ref = find_instrument_ref(name);
    return ref?ref->_ref.lock():std::shared_ptr<SimInstrument>{};
}

void SimExchange::on_event(const std::string &instrument, Quote qt) {
    if (!find_instrument_ref(instrument)) return;   //unknown symbol
    auto mi = instrument_for_event(instrument);
    if (mi && qt.both_sides()) {
        _executor.on_event(mi, qt);
    }
    _streams.on_event(instrument, qt);
    if (mi) {
        for (auto &x: _tradable_instruments) {
            auto trad = x.lock();
            if (trad && trad->get_instrument().get() == mi.get()) {
                auto mid_price = (qt.bid + qt.ask)/2_dec;
                trad->report_price(mid_price);
            }
        }
    }
}
void SimExchange::on_event(const std::string &instrument, Trade tr) {
    if (!find_instrument_ref(instrument)) return;   //unknown symbol
    auto mi = instrument_for_event(instrument);
    if (mi) _executor.on_event(mi, tr);
    _streams.on_event(instrument, tr);
}

void SimExchange::on_event(const std::string &instrument, Auction au) {
    if (!find_instrument_ref(instrument)) return;   //unknown symbol
    auto mi = instrument_for_event(instrument);
    if (mi) _executor.on_event(mi, au);
    _streams.on_event(instrument, au);
}

void SimExchange::on_event(const std::string &instrument, const OrderBookSnapshot &sn) {
    //orderbook is not matched against orders, it only feeds the streams
    _streams.on_event(instrument, sn.bids, sn.asks, sn.time, true);
}
void SimExchange::on_event(const std::string &instrument, const OrderBookIncrement &inc) {
    if (inc.side == Side::buy) {
        _streams.on_event(instrument, {static_cast<const OrderBookLevel *>(&inc),1}, {}, inc.time, false);
    } else {
        _streams.on_event(instrument, {}, {static_cast<const OrderBookLevel *>(&inc),1},  inc.time, false);
    }
}



PTradableInstrument SimExchange::create_tradable_instrument(std::shared_ptr<SimInstrument> instrument,std::shared_ptr<SimAccount> account) {
    auto r = std::make_shared<SimTradableInstrument>(instrument, account);
    _tradable_instruments.push_back(r);
    return r;
}

bool SimExchange::cancel_all_orders(PTradableInstrument instrument ) {
    return _executor.cancel_all(instrument);
}
void SimExchange::cancel_order(POrder ord) {
    _executor.cancel_order(ord);
}
void SimExchange::cancel_order(IOrder *ord) {
    _executor.cancel_order(ord);
}
void SimExchange::place_order(POrder ord) {
    auto rep_ord =ord->get_replaced_order().lock();
    if (rep_ord) {
        _executor.replace_order(ord, rep_ord);
    } else {
        _executor.place_order(ord);
    }
}

PMarketInstrument SimExchange::add_instrument(std::unique_ptr<IMarketInstrument::Info> def) {
    auto &instr = _instrument_names[def->name];
    instr.info = std::move(def);
    //link exchange to underlying currencies

    if (instr.info->asset_wallet.has_value()) {
        instr.info->asset_wallet->exchange = this;
    }
    instr.info->pnl_currency.exchange = this;
    instr.info->quote_currency.exchange = this;

    auto ref = std::make_shared<SimInstrument>(*instr.info, shared_from_this());;
    instr._ref = ref;
    return ref;;
}
UnderlyingCurrency SimExchange::create_currency(std::string_view name, bool is_unified) {
    return UnderlyingCurrency{std::string(name), is_unified?std::string(name):std::string(), this};
}
UnderlyingCurrency SimExchange::create_currency(std::string_view name) const {
    return UnderlyingCurrency{std::string(name), std::string(name), this};
}
PAccount SimExchange::create_account(const std::string &name, const std::string &)  {
    return std::make_shared<SimAccount>(name, std::span<std::pair<UnderlyingCurrency, Decimal> >{});
}
std::vector<MarketInstrument> SimExchange::get_market_instruments()  {
    std::vector<MarketInstrument> out;
    for (auto &[k,v]:_instrument_names) {
        auto lk = v.get(shared_from_this());
        if (lk) out.push_back(MarketInstrument(std::move(lk)));
    }
    return out;
}

PMarketInstrument SimExchange::create_instrument(std::string_view id, InstrumentType type) {
    std::string idstr(id);
    auto instr = resolve_instrument(idstr);
    if (!instr) throw std::runtime_error("Unknown instrument: SimExchange / " + idstr);
    if (instr->get_info().type != type) throw std::runtime_error("Instrument found, but different type: SimExchange / " + idstr );   
    return instr;
    
}

std::vector<UnderlyingCurrency> SimExchange::get_all_currencies()  {
    std::unordered_set<UnderlyingCurrency, Hasher<UnderlyingCurrency> > map;
    auto me = shared_from_this();
    for (auto &[k,v]:_instrument_names) {
        const auto &info = *v.info;
        map.insert(info.pnl_currency);
        map.insert(info.quote_currency);
        if (info.asset_has_wallet()) {
            map.insert(*info.asset_wallet);
        }
    }
    return {map.begin(), map.end()};
}
std::string_view SimExchange::get_name() const {
    return "simulator";
}


std::shared_ptr<SimInstrument> SimExchange::InstrumentRef::get(const std::shared_ptr<SimExchange> &owner) {
    auto lk = _ref.lock();
    if (!lk) {
        lk = std::make_shared<SimInstrument>(*info, owner);
        //cache the instance - SimExecutor identifies instruments by pointer, so
        //every lookup of the same name must return the very same object
        _ref = lk;
    }
    return lk;
}

void SimExchange::stop_on(std::stop_token tkn) {
    _executor.stop_on(std::move(tkn));
}


}