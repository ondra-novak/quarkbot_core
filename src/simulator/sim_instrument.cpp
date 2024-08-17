/*
 * sim_instrument.cpp
 *
 *  Created on: 17. 8. 2024
 *      Author: ondra
 */

#include "sim_instrument.h"

namespace trading_api {

SimInstrument::SimInstrument(Exchange &&ex, std::string &&id, Config &&config)
        :_ex(std::move(ex)), _id(std::move(id)), config(std::move(config)) {}

std::string SimInstrument::get_category() const {
    return "sim";
}

std::string SimInstrument::get_label() const {
    return _id;
}

Exchange SimInstrument::get_exchange() const {
    return _ex;
}

IInstrument::InstrumentFillInfo SimInstrument::get_fill_info() const {
    return {
        _config.type,_config.lot_multiplier*_config.quantum_factor, _id, {}
    };
}

std::string SimInstrument::get_id() const {
}

const IInstrument::Config& SimInstrument::get_config() const {
}

} /* namespace trading_api */
