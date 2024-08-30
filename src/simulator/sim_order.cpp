
#include "sim_order.h"

#include <memory>


namespace quarkbot {


std::string custom_to_binary(const Order::ClosePosition &c) {
    return c.pos_id;
}

Order::ClosePosition custom_from_binary(std::in_place_type_t<Order::ClosePosition>, std::string n) {
    return {n};
}

std::string custom_to_binary(const Order::Transfer &) {
    return {};
}

Order::Transfer custom_from_binary(std::in_place_type_t<Order::Transfer>,  std::string ) {
    return {Account(),0};
}



static_assert(custom_serialize<Order::ClosePosition>);



using BinOrder = TupleBin<int, //version
            std::string_view , //instrument id
            Order::Setup, //setup
            Decimal, //filled
            Decimal, //avg_price
            Order::State, //state
            Order::Reason::E, //reason code
            std::string_view,   //reason message
            std::string_view //label
            >;

SerializedOrder SimOrder::to_binary() const {

    std::string data = BinOrder::compose(1,
            this->_instrument.get_id(),
            this->_setup,
            this->_status.filled,
            this->_status.avg_price,
            this->_status.state,
            this->_status.reason.code(),
            this->_status.reason.message(),
            this->_label);

    return SerializedOrder{get_id(), std::move(data)};
}

std::pair<std::shared_ptr<SimOrder>,Order::Report> SimOrder::from_binary(const Account &account,
        const SerializedOrder &ord,
        Function<Instrument(std::string_view)> instrument_lookup) {
    {
        auto [ver] =  TupleBin<int>::parse(ord.order_content);
        if (ver != 1) return {nullptr, {}};
    }
    auto [ver, instrument_id, setup, filled, avg_price, state, reason_code, reason_message, label] = BinOrder::parse(ord.order_content);
    Instrument i = instrument_lookup(instrument_id);
    auto o = std::make_shared<SimOrder>(i, account, setup, label, Order::Origin::restored);
    BasicOrder::Status &s = o->get_status();
    s.avg_price = avg_price;
    s.filled = filled;
    s.id = ord.order_id;
    s.state = Order::State::restoring;
    return {o, Order::Report{state, Order::Reason{reason_code, reason_message}, filled, avg_price, {}}};
}

}
