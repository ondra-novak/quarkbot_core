#include "report_ldjson.h"

namespace quarkbot {

static json::value to_json_number(const Decimal &decimal) {
    return {json::number_string(decimal.to_string())};
}



std::string ReportLDJSON::get_var(std::string_view var_name) const {
    return _storage->get_var(var_name);
}


Fills ReportLDJSON::load_fills(std::size_t limit,std::string_view filter) const {
    return _storage->load_fills(limit, filter);
}


bool ReportLDJSON::is_duplicate_fill(const Fill &fill) const {
    return _storage->is_duplicate_fill(fill);
}

VarSet<std::string_view> ReportLDJSON::get_vars(std::string_view prefix) const {
    return _storage->get_vars(prefix);
}

VarSet<std::string_view> ReportLDJSON::get_vars(std::string_view start, std::string_view end) const {
    return _storage->get_vars(start, end);
}

Fills ReportLDJSON::load_fills(Timestamp limit, std::string_view filter) const {
    return _storage->load_fills(limit, filter);
}
Positions ReportLDJSON::load_positions(std::string_view filter) const {
    return _storage->load_positions(filter);
}
std::vector<SerializedOrder> ReportLDJSON::load_open_orders(const Account &account) const {
    return _storage->load_open_orders(account);
}

Trades ReportLDJSON::load_closed(Timestamp limit,std::string_view filter) const {
    return _storage->load_closed(limit, filter);
}

void ReportLDJSON::put_order(Timestamp tm,const Order &ord) {
    _storage->put_order(tm,ord);
    tx_beg();
    _tx.push_back(new_record(tm, "order", order_to_json(ord)));
    tx_end();
}

void ReportLDJSON::put_fill(Timestamp tm,const Fill &fill) {
    _storage->put_fill(tm,fill);
    tx_beg();
    _tx.push_back(new_record(tm, "fill", {
            {"amount",to_json_number(fill.amount)},
            {"fees",fill.fees},
            {"price",to_json_number(fill.price)},
            {"id",fill.id},
            {"i", {
                {"id",fill.instrument.instrument_id},
                {"m",to_json_number(fill.instrument.multiplier)},
                {"unit", fill.instrument.price_unit},
                {"type", to_string(fill.instrument.type)}
            }},
            {"label", fill.label},
            {"pos_id", fill.pos_id},
            {"side", to_string(fill.side)},
            {"time", fill.time.time_since_epoch().count()},

    }));
    tx_end();
}
void ReportLDJSON::begin_transaction() {
    _storage->begin_transaction();
    tx_beg();
}

void ReportLDJSON::commit() {
    _storage->commit();
    tx_end();
}
void ReportLDJSON::rollback() {
    tx_beg();
    _storage->rollback();
    tx_rollback();

}


void ReportLDJSON::put_var(Timestamp tm,std::string_view name, std::string_view value) {
    tx_beg();
    _storage->put_var(tm, name, value);
    _tx.push_back(new_record(tm, "put_var", {name, value}));
    tx_end();
}
void ReportLDJSON::erase_var(Timestamp tm,std::string_view name) {
    _storage->erase_var(tm, name);
    _tx.push_back(new_record(tm, "erase_var", name));
    tx_end();
}
json::value ReportLDJSON::new_record(Timestamp tm, std::string_view type, json::value payload) {
    return {
        {"time",tm.time_since_epoch().count()},
        {"type", type},
        {"data", std::move(payload)},
    };
}


static json::value build_options(const Order::Options &m) {
    return {
        {"amend",m.amend?json::value(true):json::value()},
        {"amount_is_volume",m.amount_is_volume?json::value(true):json::value()},
        {"behavior",m.behavior != Order::Behavior::standard?json::value(to_string(m.behavior)):json::value()},
        {"leverage",m.leverage != 0?to_json_number(m.leverage):json::value()},
    };
}


static json::value build_json_setup(const IOrder::Undefined &) {
    return {};
}

static json::value build_json_setup(const Order::Market &m) {
    return {
        {"type","MARKET"},
        {"side",to_string(m.side)},
        {"size",to_json_number(m.amount)},
        {"opt",build_options(m.options)}
    };
}

static json::value build_json_setup(const Order::Limit &m) {
    return {
        {"type","LIMIT"},
        {"side",to_string(m.side)},
        {"size",to_json_number(m.amount)},
        {"lim_p",to_json_number(m.limit_price)},
        {"opt",build_options(m.options)}
    };

}

static json::value build_json_setup(const Order::LimitPostOnly &m) {
    return {
        {"type","LIMIT(post)"},
        {"side",to_string(m.side)},
        {"size",to_json_number(m.amount)},
        {"lim_p",to_json_number(m.limit_price)},
        {"opt",build_options(m.options)}
    };

}

static json::value build_json_setup(const Order::Stop &m) {
    return {
        {"type","STOP"},
        {"side",to_string(m.side)},
        {"size",to_json_number(m.amount)},
        {"stp_p",to_json_number(m.stop_price)},
        {"opt",build_options(m.options)}
    };

}

static json::value build_json_setup(const Order::StopLimit &m) {
    return {
        {"type","STOPLIMIT"},
        {"side",to_string(m.side)},
        {"size",to_json_number(m.amount)},
        {"stp_p",to_json_number(m.stop_price)},
        {"lim_p",to_json_number(m.limit_price)},
        {"opt",build_options(m.options)}
    };

}

static json::value build_json_setup(const Order::TpSl &m) {
    return {
        {"type","OCO (TP/SL)"},
        {"side",to_string(m.side)},
        {"size",to_json_number(m.amount)},
        {"tp",to_json_number(m.stop_price)},
        {"sl",to_json_number(m.limit_price)},
        {"opt",build_options(m.options)}
    };

}

static json::value build_json_setup(const Order::TrailingStop &m) {
    return {
        {"type","TR_STOP"},
        {"side",to_string(m.side)},
        {"size",to_json_number(m.amount)},
        {"dist",to_json_number(m.stop_distance)},
        {"opt",build_options(m.options)}
    };
}

static json::value build_json_setup(const Order::ClosePosition &m) {
    return {
        {"type","CLOSE"},
        {"id",m.pos_id},
        {"remain",to_json_number(m.remain)},
    };
}
static json::value build_json_setup(const Order::Transfer &m) {
    return {
        {"type","TRANSFER"},
        {"target",m.target.get_id()},
        {"amount",to_json_number(m.amount)}
    };

}

void ReportLDJSON::tx_beg() {
    _txcnt++;
}

void ReportLDJSON::tx_end() {
    if (--_txcnt <= 0) {
        json::serializer_t srl;
        for (json::value_t &x: _tx) {
            srl.serialize(x, [&](std::string_view str){
                _out << str;
            });
            _out << std::endl;
        }
        _out.flush();
        _tx.clear();
        _txcnt = 0;
    }
}

void ReportLDJSON::tx_rollback() {
    for (auto &x: _tx) {
        x.set("rollback", true);
    }
    tx_end();
}

json::value ReportLDJSON::order_to_json(const Order &ord) {
    Order replaced = ord.get_replaced_order();
    Order::State state = ord.get_state();
    Order::Reason reason = ord.get_reason();
    auto id = ord.get_id();

    json::value_t setup_json = std::visit([&](const auto &item){
        return build_json_setup(item);
    }, ord.get_setup());


    return {
        {"id", ord.get_id()},
        {"i",ord.get_instrument().get_id()},
        {"a",ord.get_account().get_id()},
        {"r",replaced?json::value(replaced.get_id()):json::value(nullptr)},
        {"setup", std::move(setup_json)},
        {"state", to_string(state)},
        {"reason", {reason.get_reason_as_string(), reason.message()}},
        {"filled", to_json_number(ord.get_filled())},
        {"avg_price", to_json_number(ord.get_avg_price())},
        {"label", ord.get_label()}
    };

}
}

