#include "instrument.h"
#include "../../trading_ifc/orderbook.h"

BinanceInstrument::BinanceInstrument(std::string_view label,
        const Config &cfg,
        quarkbot::ExchangeInfo x)
:_label(std::move(label))
,_config(cfg)
,_x(std::move(x))
,_last_ticker(std::make_shared<TickerEvent>())
,_last_orderbook(std::make_shared<OrderbookEvent>())
{

}

const BinanceInstrument& BinanceInstrument::from_instrument(const quarkbot::Instrument &i) {
    return dynamic_cast<const BinanceInstrument &>(*i.get_handle());
}

void BinanceInstrument::update_config(RPCClient &client,quarkbot::Function<void()> done) {

}

std::string BinanceInstrument::get_category() const {return {};}


std::string BinanceInstrument::get_label() const {
    return _label;
}

quarkbot::ExchangeInfo BinanceInstrument::get_exchange() const {
    return _x;
}

std::string BinanceInstrument::get_id() const {
    return _config.id;
}

const quarkbot::IInstrument::Config& BinanceInstrument::get_config() const {
    std::lock_guard _(_mx);
    return _config;
}

