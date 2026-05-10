#pragma once
#include "ifc/market_instrument.hpp"
#include "ifc/exchange.hpp"
#include "ifc/underlying.hpp"
#include "ifc/types.hpp"
#include "utils/hashable.hpp"
namespace quarkbot {
    namespace bitfinex {

        class Exchange : public IExchange { 
        public:
            virtual PAccount create_account(const std::string &name, const std::string &credentials)  override;
            virtual std::vector<PMarketInstrument> get_market_instruments()  override;
            virtual PMarketInstrument create_instrument(std::string_view id, InstrumentType type)  override;
            virtual std::vector<UnderlyingCurrency> get_all_currencies()  override;
            virtual std::string_view get_name() const override;
     
        protected:
            




        };


    }

}