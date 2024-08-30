#pragma once

#include "../../trading_ifc/instrument.h"
#include "../../trading_ifc/micromutex.h"

#include "rpc_client.h"

class BinanceInstrument : public quarkbot::IInstrument{
public:

    struct Config: quarkbot::Instrument::Config  {
        int quantity_precision;
        int base_asset_precision;
        int quote_precision;
        std::string quote_asset;
        std::string base_asset;
        std::string id;
    };


    BinanceInstrument(std::string_view label,
            const Config &cfg,
            quarkbot::ExchangeInfo x);

    static const BinanceInstrument &from_instrument(const quarkbot::Instrument &i);

    void update_config(RPCClient &client, quarkbot::Function<void()> done);


    std::string _label;
    Config _config;
    quarkbot::ExchangeInfo _x;
    mutable uMutex _mx;

    using TickerEvent = quarkbot::MarketEvent_TickData<std::mutex>;
    using OrderbookEvent = quarkbot::MarketEvent_OrderBook<std::mutex>;

    std::shared_ptr<TickerEvent> _last_ticker;
    std::shared_ptr<OrderbookEvent> _last_orderbook;


    virtual std::string get_category() const override;
    virtual std::string get_label() const override;
    virtual quarkbot::ExchangeInfo get_exchange() const override;
    virtual std::string get_id() const override;
    const virtual quarkbot::IInstrument::Config& get_config() const override;

};


