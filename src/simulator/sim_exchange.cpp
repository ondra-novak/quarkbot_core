#include "sim_exchange.h"
#include "sim_account.h"
#include "sim_instrument.h"

namespace trading_api {

ConfigSchema SimExchange::get_exchange_config_schema() const {
    return {};
}

ConfigSchema SimExchange::get_api_key_config_schema() const {
    return {};
}
void SimExchange::load_credentials(const Config &credential_config, std::string_view , Function<void(ExchangeCredentials)> result) {
    ExchangeCredentials cred(std::make_shared<IExchangeCredentials::Null>());
    result(cred);
}

void SimExchange::query_accounts(const ExchangeCredentials &,
        std::string_view label, const Query &query,
        Function<void(std::span<Account>)> result) {
    std::string currency = query["currency"];
    double balance = query["balance"];
    double fees = query["fees"];
    auto acc = Account(std::make_shared<SimAccount>(*this, std::string(label), currency, balance, fees*0.01));
    result(std::span<Account>(&acc, 1));
}

void SimExchange::query_instruments(const ExchangeCredentials &,
        const Query &query, std::string_view label,
        Function<void(std::span<Instrument>)> result) {

    query_instruments(query, label, std::move(result));
}

void SimExchange::query_instruments(const Query &query, std::string_view label,
        Function<void(std::span<Instrument>)> result) {
    Instrument::Config cfg = {};
    std::string symbol = query["symbol"];
    std::string type = query["type"];
    if (type == "spot") cfg.type = Instrument::Type::spot;
    else if (type == "contract") cfg.type = Instrument::Type::contract;
    else if (type == "inverted") cfg.type = Instrument::Type::inverted_contract;
    else if (type == "quanto") cfg.type = Instrument::Type::quanto_contract;
    else if (type == "cfd") cfg.type = Instrument::Type::cfd;
    else throw std::runtime_error("Unknown instrument type("+ symbol+"): "+type);
    cfg.tick_size = query["tick_size"](0.01_dec);
    cfg.lot_size = query["lot_size"](0.00001_dec);
    cfg.lot_multiplier = query["lot_multiplier"](1_dec);
    cfg.min_size = query["min_size"](0_dec);
    cfg.min_volume = query["min_volume"](0_dec);
    cfg.quanto_factor = query["quanto_factor"](1_dec);
    cfg.initial_margin = query["initial_margin"](1_dec);
    cfg.maintenance_margin = query["maintenance_margin"](0_dec);
    cfg.tradable = query["tradable"](true);
    cfg.can_short = query["can_short"](true);
    cfg.currency = query["currency"];
    auto ptr = std::make_shared<SimInstrument>(ExchangeInfo(*this), std::string(label), std::move(cfg));
    auto instr =Instrument(ptr);
    _instruments.insert(symbol, ptr);
    result(std::span<Instrument>(&instr,1));
}

void SimExchange::subscribe(MarketEventType , const Instrument &) {
    //nothing
}

void SimExchange::unsubscribe(MarketEventType , const Instrument &) {
    //nothing
}

void SimExchange::update_account(const Account &a) {
    object_updated(a, {});
}

void SimExchange::update_instrument(const Instrument &i) {
    object_updated(i, {});
}

void SimExchange::update_market(const Instrument &i, MarketEventType type) {
    auto me = SimInstrument::get_matching(i).lock_shared()->get_ticker(_cur_sim_time);
    object_updated(i, {}, std::move(me));

}

std::string SimExchange::get_name() const {
    return "simulator";
}

std::string SimExchange::get_id() const  {
    return get_name();
}

std::optional<IExchangeInfo::Icon> SimExchange::get_icon() const {
    return {};
}

void SimExchange::replay_accept(std::string_view symbol, const TickData &ticker) {
    _cur_sim_time = ticker.tp;
    auto instrument = _instruments.find(symbol);
    if (instrument) {
        auto matching = instrument->get_matching();
        auto m = matching.lock();
        m->accept_ticker(ticker);
        //m->get_executions() //TODO
    }
}

}
