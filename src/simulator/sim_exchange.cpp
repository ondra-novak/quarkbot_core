#include "sim_exchange.h"
#include "sim_account.h"
#include "sim_instrument.h"

#include "sim_order.h"
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
    if (type != MarketEventType::tickdata) {
        auto me = SimInstrument::get_matching(i).lock_shared()->get_ticker(_cur_sim_time);
        object_updated(i, type, std::move(me));
    } else {
        object_updated(i, type, std::make_exception_ptr(UnsupportedException()));
    }

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

Order SimExchange::create_order(const Instrument &instrument,
        const Account &account, const Order::Setup &setup,
        std::string_view label) {
    if (validate_order(setup)) {
        return Order(std::make_shared<SimOrder>(instrument, account, setup, label, Order::Origin::strategy));
    } else {
        return order_error(instrument, account, setup, label, Order::Reason::unsupported);
    }
}

Order SimExchange::create_order_replace(const Order &replace,
        const Order::Setup &setup, std::string_view label) {
    if (validate_order(setup)) {
        return Order(std::make_shared<SimOrder>(replace, setup, label, Order::Origin::strategy));
    } else {
        return order_error(replace.get_instrument(), replace.get_account(), setup, label, Order::Reason::unsupported);
    }
}

void SimExchange::batch_place(std::span<Order> orders) {
    for (const Order &o: orders) {
        auto m = SimInstrument::get_matching(o.get_instrument());
        auto mlk = m.lock();
        match_order(*mlk, o);
    }
}


struct SimExchange::ExecuteInfo { // @suppress("Miss copy constructor or assignment operator")
    simulator::Matching &m;
    const Order &ord;
    Decimal filled;
    Order::Report &rpt;
};


void SimExchange::match_order(simulator::Matching &m, const Order &ord) {
    Order::Report rpt;
    Decimal filled = 0;

    Order replc = ord.get_replaced_order();
    if (replc) {
        const IOrder::Options *opt = ord.get_options(ord.get_setup());
        if (opt->amend) {
            const IOrder::Setup &new_setup = ord.get_setup();
            const IOrder::Setup &old_setup = replc.get_setup();
            if (replc.get_instrument() != ord.get_instrument()
                    || replc.get_account() != ord.get_account()
                    || old_setup.index() != new_setup.index()
                    || Order::get_side(old_setup) != Order::get_side(new_setup)) {
                rpt.new_state = Order::State::rejected;
                rpt.reason = Order::Reason::invalid_amend;
                order_report(ord, std::move(rpt));
                return;
            } else {
                auto wo = m.find_order(replc);
                if (wo == nullptr) {
                    rpt.new_state = Order::State::rejected;
                    rpt.reason = Order::Reason::not_found;
                    order_report(ord, std::move(rpt));
                    return;
                } else {
                    Decimal remain = std::visit([&](auto &x){return x.amount;},*wo);
                    Decimal old_total = replc.get_total();
                    Decimal new_total = ord.get_total();
                    filled = old_total - remain;
                    m.cancel_order(replc);
                    order_report(replc, Order::Report{Order::State::canceled, Order::Reason::replace});
                    if (filled <= new_total) {
                        rpt.new_state = Order::State::filled;
                        order_report(ord, std::move(rpt));
                        return;
                    }
                }
            }
        } else {
            auto wo = m.find_order(replc);
            if (wo) {
                Decimal remain = std::visit([&](auto &x){return x.amount;},*wo);
                Decimal known_remain = replc.get_remain();
                if (remain != known_remain) {
                    order_report(ord, Order::Report{Order::State::rejected, Order::Reason::unprocessed_fill});
                    return;
                }
                m.cancel_order(replc);
                order_report(replc, Order::Report{Order::State::canceled, Order::Reason::replace});
            }
        }
    }

    std::visit([&](const auto &s){
        execute_order(ExecuteInfo{m, ord, filled, rpt}, s);
    }, ord.get_setup());

}

void SimExchange::batch_cancel(std::span<Order> orders) {
    for (auto &o: orders) {
        const BasicOrder &ord = BasicOrder::from_order(o);
        auto m = SimInstrument::get_matching(ord.get_instrument());
        auto mlk = m.lock();
        if (mlk->cancel_order(o)) {
            order_report(o, {Order::State::canceled});
        }
    }
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

void SimExchange::execute_order(ExecuteInfo ctx, const Order::Market &setup) {

    process_execution(
            ctx.m.place_market_order(ctx.ord, setup.side, setup.amount - ctx.filled));
}

void SimExchange::execute_order(ExecuteInfo ctx, const Order::Limit &setup) {
    ctx.m.place_waiting_order(simulator::Matching::Limit{
        ctx.ord,setup.side,setup.amount - ctx.filled,setup.limit_price
    });
    ctx.rpt.new_state = Order::State::active;
}

void SimExchange::execute_order(ExecuteInfo ctx, const Order::LimitPostOnly &setup) {
    auto spread= ctx.m.get_spread();
    if ((setup.side == Side::buy && setup.limit_price > spread.ask) ||
        (setup.side == Side::sell && setup.limit_price < spread.bid)) {
        ctx.rpt.new_state = Order::State::rejected;
        ctx.rpt.reason = Order::Reason::crossing;
    } else {
        ctx.m.place_waiting_order(simulator::Matching::Limit{
            ctx.ord,setup.side,setup.amount - ctx.filled,setup.limit_price
        });
        ctx.rpt.new_state = Order::State::active;
    }
}

void SimExchange::execute_order(ExecuteInfo ctx, const Order::ImmediateOrCancel &setup) {
    auto spread= ctx.m.get_spread();
    if ((setup.side == Side::buy && setup.limit_price > spread.ask) ||
        (setup.side == Side::sell && setup.limit_price < spread.bid)) {
        process_execution(
                ctx.m.place_market_order(ctx.ord, setup.side, setup.amount-ctx.filled));
    } else {
        ctx.rpt.new_state = Order::State::canceled;
    }
}

void SimExchange::execute_order(ExecuteInfo ctx, const Order::Stop &setup) {
    ctx.m.place_waiting_order(simulator::Matching::Stop{
        ctx.ord, setup.side, setup.amount - ctx.filled, setup.stop_price
    });
    ctx.rpt.new_state = Order::State::active;
}

void SimExchange::execute_order(ExecuteInfo ctx, const Order::StopLimit &setup) {
    ctx.m.place_waiting_order(simulator::Matching::StopLimit{
        ctx.ord, setup.side, setup.amount - ctx.filled, setup.stop_price, setup.limit_price
    });
    ctx.rpt.new_state = Order::State::active;
}

void SimExchange::execute_order(ExecuteInfo ctx, const Order::TrailingStop &setup) {
    ctx.m.place_waiting_order(simulator::Matching::TrailingStop{
        ctx.ord, setup.side, setup.amount - ctx.filled, setup.stop_distance
    });
    ctx.rpt.new_state = Order::State::active;

}

void SimExchange::execute_order(ExecuteInfo ctx, const Order::TpSl &setup) {
    ctx.m.place_waiting_order(simulator::Matching::TpSl{
        ctx.ord, setup.side, setup.amount - ctx.filled, setup.stop_price,setup.limit_price
    });
    ctx.rpt.new_state = Order::State::active;
}

void SimExchange::execute_order(ExecuteInfo ctx, const Order::Transfer &setup) {
    throw ;//unreachable
}

void SimExchange::execute_order(ExecuteInfo ctx, const Order::ClosePosition &setup) {
    SimAccount &acc = SimAccount::from_account(ctx.ord.get_account());
    auto spread = ctx.m.get_spread();
    auto fill = acc.close_position(ctx.ord.get_instrument(), setup.pos_id,
            spread.bid, spread.ask,
            _cur_sim_time, ctx.ord.get_label(), setup.remain);
    if (fill) {
        ctx.rpt.avg_price = fill->price;
        ctx.rpt.filled_amount = fill->amount;
        ctx.rpt.new_state = Order::State::filled;
        ctx.rpt.fills.push_back(std::move(*fill));
    } else {
        ctx.rpt.new_state = Order::State::rejected;
        ctx.rpt.reason = Order::Reason::not_found;
    }
}

void SimExchange::execute_order(ExecuteInfo ctx, const IOrder::Undefined &setup) {
    throw ;//unreachable
}

bool SimExchange::validate_order(const Order::Setup &setup) {
    auto opt = Order::get_options(setup);
    if (opt && opt->amount_is_volume) return false;
    return !(std::holds_alternative<Order::Transfer>(setup)
            || std::holds_alternative<IOrder::Undefined>(setup));
}

void SimExchange::process_execution(const simulator::Matching::Execution &ex) {

}

}
