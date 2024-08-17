#pragma once

#include <trading_api/exchange.h>
#include "matching.h"
namespace trading_api {


class SimInstrument: public IInstrument {
public:
    SimInstrument(Exchange &&ex, std::string &&id, Config &&config);
    virtual std::string get_category() const override;
    virtual std::string get_label() const override;
    virtual Exchange get_exchange() const override;
    virtual std::string get_id() const override;
    const virtual Config& get_config() const override;

    static auto create(Exchange ex, std::string id, Config config) {
        return std::make_shared<SimInstrument>(std::move(ex), std::move(id), std::move(config));
    }

    ///retrieve current instrument price relative to its base currency
    /**
     * @return current price
     *
     * @note uses current matching state to calculate current price. You need to set
     * a last price or bid and ask spread on a matching object
     */
    Decimal get_price() const;
    ///retrieve current instrument price relative to its base currency
    /**
     * @param i reference to instrument
     * @return current price, if the instrument is compatible, or nan()
     */
    static Decimal get_price(const Instrument &i);

    ///Retrieves market matching engine object - for simulation
    /**
     * @param i instrument
     * @return lockable market matching object. If the instrument is not compatible,
     * returns nullptr
     *
     */
    static shared_lockable_ptr<simulator::Matching>get_matching(const Instrument &i);


protected:
    Exchange _ex;
    std::string _id;
    Config _config;
    shared_lockable_ptr<simulator::Matching> _matching;


};

} /* namespace trading_api */


