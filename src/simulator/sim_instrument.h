#pragma once

#include <trading_api/exchange.h>
namespace trading_api {


class SimInstrument: public IInstrument {
public:
    SimInstrument(Exchange &&ex, std::string &&id, Config &&config);
    virtual std::string get_category() const override;
    virtual std::string get_label() const override;
    virtual Exchange get_exchange() const override;
    virtual InstrumentFillInfo get_fill_info() const override;
    virtual std::string get_id() const override;
    const virtual Config& get_config() const override;

    static auto create(Exchange ex, std::string id, Config config) {
        return std::make_shared<SimInstrument>(std::move(ex), std::move(id), std::move(config));
    }

protected:
    Exchange _ex;
    std::string _id;
    Config _config;
};

} /* namespace trading_api */


