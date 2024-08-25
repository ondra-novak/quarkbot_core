
#include "sim_order.h"



namespace trading_api {


std::string custom_to_binary(const trading_api::Order::ClosePosition &c) {
    return c.pos_id;
}

void custom_from_binary(trading_api::Order::ClosePosition &c, std::string n) {
    c.pos_id = std::move(n);
}

std::string custom_to_binary(const trading_api::Order::Transfer &) {
    return {};
}

void custom_from_binary(trading_api::Order::Transfer &, std::string ) {

}



static_assert(custom_serialize<trading_api::Order::ClosePosition>);



using BinOrder = TupleBin<std::string_view, Order::Setup, Decimal, Decimal, Order::State, Order::Reason::E, std::string_view>;

SerializedOrder SimOrder::to_binary() const {

    BinOrder::compose(this->_instrument.get_id(),
            this->_setup,
            this->_status.filled,
            this->_status.avg_price,
            this->_status.state,
            this->_status.reason.code(),
            this->_status.reason.message());

}



}
