#include "sim_exchange.h"
#include "sim_account.h"
#include "sim_instrument.h"

#include "sim_order.h"
namespace quarkbot {

using namespace simulator;

ConfigSchema SimExchange::get_exchange_config_schema() const {
    return {};
}

ConfigSchema SimExchange::get_api_key_config_schema() const {
    return {};
}
void SimExchange::load_credentials(const Config &, std::string_view , Function<void(ExchangeCredentials)> result) {
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
    cfg.tick_size = query["tick_size"] || 0.01_dec;
    cfg.lot_size = query["lot_size"] || 0.00001_dec;
    cfg.lot_multiplier = query["lot_multiplier"] || 1_dec;
    cfg.min_size = query["min_size"] || 0_dec;
    cfg.min_volume = query["min_volume"] || 0_dec;
    cfg.quanto_factor = query["quanto_factor"] || 1_dec;
    cfg.initial_margin = query["initial_margin"] || 1_dec;
    cfg.maintenance_margin = query["maintenance_margin"] || 0_dec;
    cfg.tradable = query["tradable"] || true;
    cfg.can_short = query["can_short"] || true;
    cfg.currency = query["currency"] || "USD";
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


struct SimExchange::OrderExecutor { // @suppress("Miss copy constructor or assignment operator")
    SimExchange *me;
    simulator::Matching &m;
    const Order &ord;
    Decimal filled;
    Order::Report &rpt;

    void operator()(const Order::Market &setup){
        me->process_execution(m,
                m.place_market_order(ord, setup.side, setup.amount - filled));
    }
    void operator()(const Order::Limit &setup) {

        m.place_waiting_order(simulator::Matching::Limit{
            ord,setup.side,setup.amount - filled,setup.limit_price
        });
        rpt.new_state = Order::State::active;

    }
    void operator()(const Order::LimitPostOnly &setup) {
        auto spread= m.get_spread();
        if ((setup.side == Side::buy && setup.limit_price > spread.ask) ||
            (setup.side == Side::sell && setup.limit_price < spread.bid)) {
            rpt.new_state = Order::State::rejected;
            rpt.reason = Order::Reason::crossing;
        } else {
            m.place_waiting_order(simulator::Matching::Limit{
                ord,setup.side,setup.amount - filled,setup.limit_price
            });
            rpt.new_state = Order::State::active;
        }

    }
    void operator()(const Order::ImmediateOrCancel &setup) {
        auto spread= m.get_spread();
        if ((setup.side == Side::buy && setup.limit_price > spread.ask) ||
            (setup.side == Side::sell && setup.limit_price < spread.bid)) {
            me->process_execution(m,
                    m.place_market_order(ord, setup.side, setup.amount-filled));
        } else {
            rpt.new_state = Order::State::canceled;
        }

    }
    void operator()(const Order::Stop &setup) {
        m.place_waiting_order(simulator::Matching::Stop{
            ord, setup.side, setup.amount - filled, setup.stop_price
        });
        rpt.new_state = Order::State::active;
    }
    void operator()(const Order::StopLimit &setup) {
        m.place_waiting_order(simulator::Matching::StopLimit{
            ord, setup.side, setup.amount - filled, setup.stop_price, setup.limit_price
        });
        rpt.new_state = Order::State::active;

    }
    void operator()(const Order::TrailingStop &setup) {
        m.place_waiting_order(simulator::Matching::TrailingStop{
            ord, setup.side, setup.amount - filled, setup.stop_distance
        });
        rpt.new_state = Order::State::active;

    }
    void operator()(const Order::TpSl &setup) {
        m.place_waiting_order(simulator::Matching::TpSl{
            ord, setup.side, setup.amount - filled, setup.stop_price,setup.limit_price
        });
        rpt.new_state = Order::State::active;

    }
    void operator()(const Order::Transfer &) {
        rpt.new_state = Order::State::rejected;
        rpt.reason = {Order::Reason::internal_error, "Order::Transfer cannot execute"};
    }
    void operator()(const Order::ClosePosition &setup) {
        SimAccount &acc = SimAccount::from_account(ord.get_account());
        auto spread = m.get_spread();
        auto fill = acc.close_position(ord.get_instrument(), setup.pos_id,
                spread.bid, spread.ask,
                me->_cur_sim_time, ord.get_label(), setup.remain);
        if (fill) {
            rpt.avg_price = fill->price;
            rpt.filled_amount = fill->amount;
            rpt.new_state = Order::State::filled;
            rpt.fills.push_back(std::move(*fill));
        } else {
            rpt.new_state = Order::State::rejected;
            rpt.reason = Order::Reason::not_found;
        }
    }
    void operator()(const IOrder::Undefined &) {
        rpt.new_state = Order::State::rejected;
        rpt.reason = {Order::Reason::internal_error, "Order::Undefined cannot execute"};

    }

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

    std::visit(OrderExecutor{this, m, ord, filled, rpt}, ord.get_setup());

    order_report(std::move(ord), std::move(rpt));

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
        simulate_market(*m);
    }
}

bool SimExchange::validate_order(const Order::Setup &setup) {
    auto opt = Order::get_options(setup);
    if (opt && opt->amount_is_volume) return false;
    return !(std::holds_alternative<Order::Transfer>(setup)
            || std::holds_alternative<IOrder::Undefined>(setup));
}

void SimExchange::restore_orders(const Account &acc, std::span<SerializedOrder> orders,
        RestoreOrdersCallback callback) {
    std::vector<std::pair<Order, Order::Report> > out_orders;
    for (const SerializedOrder &x: orders) {
        try {
            auto ordpair = SimOrder::from_binary(acc, x, [this](std::string_view id){
                auto instr = _instruments.find(id);
                if (instr) return Instrument(instr);
                throw std::runtime_error("Unknown instrument");
            });
            if (ordpair.first) {
                Order order(ordpair.first);

                Order::Report rpt = ordpair.second;
                Decimal filled = *rpt.filled_amount;
                auto m = SimInstrument::get_matching(order.get_instrument());
                auto mlk = m.lock();

                std::visit(OrderExecutor{this,*mlk, order, filled, rpt}, order.get_setup());

                out_orders.emplace_back(order, rpt);
            }
        } catch (...) {
            //ignore
        }
    }
    callback(RestoredOrders(out_orders.data(), out_orders.size()));
}


void SimExchange::process_execution(simulator::Matching &m, simulator::Matching::Execution ex) {

    Order ord = ex.order;
    SimAccount &acc = SimAccount::from_account(ord.get_account());
    Instrument inst = ord.get_instrument();
    const Order::Options *opt = Order::get_options(ord.get_setup());
    if (opt) {
        if (opt->behavior == Order::Behavior::hedge) {
            acc.open_position(inst, ex.side,ex.price,ex.size,_cur_sim_time, ord.get_label());
            return;
        } else if (opt->behavior == Order::Behavior::reduce) {
            ex.size  = std::max(acc.get_max_reduce(inst, ex.side), ex.size);
        }
    }
    Order::Report rep;
    rep.fills = acc.create_fills(inst, ex.side, ex.side, ex.price, _cur_sim_time, ord.get_label());
    if (ex.remain == 0) rep.new_state = Order::State::filled;
    else {
        Decimal filled = ord.get_total() - ex.remain;
        std::visit(OrderExecutor{this, m, ord, filled, rep}, ord.get_setup());
    }
    order_report(ord, rep);
}


void SimExchange::simulate_market(simulator::Matching &m) {
    auto exec = m.get_executions();
    for (auto x : exec) {
        process_execution(m, std::move(x));
    }
}

}
