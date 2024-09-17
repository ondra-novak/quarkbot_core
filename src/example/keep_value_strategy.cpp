#include <quarkbot/strategy.h>
#include <quarkbot/module.h>
#include <quarkbot/tickdata.h>
#include <quarkbot/ta/bbspread.h>
#include <quarkbot/series.h>


using namespace quarkbot;

class KeepValue: public quarkbot::Strategy {
    Persistent<BBSpread<Decimal> > _spread;


    Order buy; //buy limit order
    Order sell; //sell limit order
    Decimal param_k; //
    Decimal eq;
    Decimal pos;
    Instrument i;
    Account a;

public:
    virtual async main() override {
        auto cfg = get_config();
        configure_spread(cfg["spread"]);
        i = get_instruments().front();
        a = get_accounts().front();
        buy = bind_order(a, i);
        sell = bind_order(a, i);
        param_k = cfg["k"];
        pos = get_position(get_opened_positions());
        eq = param_k/pos;
        return master_cycle();
    }

    async master_cycle() {
        co_await restore_orders();
        auto instr_cfg = i.get_config();
        process_fills(buy);
        process_fills(sell);
        while (true) {
            TickData tk;
            auto cur_time = get_event_time();
            MarketEventData ev = co_await update_market(MarketEventType::tickdata, i);
            if (ev.get(tk)) {
                if (eq == 0) eq = tk.last;
                _spread.update({tk.last, false});
                auto sr = _spread.get_result(eq);
                if (!sr.buy) {
                    cancel_order(buy);
                } else {
                    Decimal p = Instrument::adjust_price(instr_cfg, *sr.buy);
                    if (can_place_order(buy, p)) {
                        Decimal amount = calc_amount(Side::buy, instr_cfg, p);
                        buy = replace_order(buy, IOrder::Limit(Side::buy,amount,p));
                        process_fills(buy);
                    }
                }
                if (!sr.sell) {
                    cancel_order(sell);
                } else {
                    Decimal p = Instrument::adjust_price(instr_cfg, *sr.sell);
                    if (can_place_order(sell, p)) {
                        Decimal amount = calc_amount(Side::sell, instr_cfg, p);
                        if (amount) {
                            buy = replace_order(sell, IOrder::Limit(Side::buy,amount,p));
                            process_fills(sell);
                        }
                    }
                }
            }
            wait_until(cur_time+std::chrono::minutes(1), 0);
        }
    }

    async process_fills(Order ord) {
        while (!ord.done()) {
            auto fills = co_await on_report(ord);
            for (const Fill &f: fills) {
                pos += f.amount;
            }
            eq = param_k/pos;
        }
        co_return;
    }

    bool can_place_order(Order &ord, Decimal price) const {
        if (ord.done()) return true;
        const auto &st = ord.get_setup();
        if (std::holds_alternative<Order::Limit>(st)) {
            const Order::Limit &l = std::get<Order::Limit>(st);
            return  (price != l.limit_price);
        } else {
            return true;
        }
    }

    Decimal calc_amount(Side side, const Instrument::Config &cfg, Decimal price) {
        Decimal px = param_k/price;
        Decimal diff = (px - pos) * static_cast<Decimal>(side);
        if (diff < std::max(diff, cfg.min_lot_size(price))) return 0;
        return diff;
    }

    async restore_orders() {
        auto orders = co_await on_restored_orders();
        for (Order o: orders) {
            if (o.get_side() == Side::buy) buy = o;
            if (o.get_side() == Side::sell) sell = o;
        }
        co_return;
    }

    void configure_spread(Config cfg) {
        unsigned int mean = cfg["mean"] || 900;
        unsigned int stdev = cfg["stdev"] || mean;
        std::vector<Decimal> c;
        Config curves = cfg["curves"];
        for (std::size_t i = 0; curves[i].defined(); ++i) {
            c.push_back(curves[i]);
        }
        bool zero_curve = cfg["zero"] || true;
        _spread.set_params(mean, stdev, c, zero_curve);
        _spread.restore(this, "spread");
    }

};

EXPORT_STRATEGY(KeepValue);
