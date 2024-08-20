/*
 * sim_instrument.cpp
 *
 *  Created on: 17. 8. 2024
 *      Author: ondra
 */

#include "sim_instrument.h"

namespace trading_api {

SimInstrument::SimInstrument(ExchangeInfo &&ex, std::string &&id, Config &&config)
        :_ex(std::move(ex)), _id(std::move(id)), _config(std::move(config))
        ,_matching(make_shared_lockable<simulator::Matching>()) {}

std::string SimInstrument::get_category() const {
    return "sim";
}

std::string SimInstrument::get_label() const {
    return _id;
}

ExchangeInfo SimInstrument::get_exchange() const {
    return _ex;
}


std::string SimInstrument::get_id() const {
    return _id;
}

const IInstrument::Config& SimInstrument::get_config() const {
    return _config;
}


Decimal SimInstrument::get_price() const {
    return _matching.lock_shared()->get_effective_price();
}


Decimal SimInstrument::get_price(const Instrument &i) {
    const SimInstrument *x = dynamic_cast<const SimInstrument *>(i.get_handle().get());
    return x?x->get_price():Decimal::nan();
}

shared_lockable_ptr<simulator::Matching> SimInstrument::get_matching(const Instrument &i) {
    const SimInstrument *x = dynamic_cast<const SimInstrument *>(i.get_handle().get());
    if (x) {
        return x->_matching;
    } else {
        return {};
    }

}

} /* namespace trading_api */
