#pragma once

#include "defs.hpp"
#include "exchanges/bitfinex/instrument_map.hpp"
#include "exchanges/bitfinex/stream_manager.hpp"
#include "ifc/exchange.hpp"
#include "ifc/underlying.hpp"
#include "ifc/types.hpp"
#include "libs/network/sslobjects.hpp"
#include "stream_defs.hpp"
#include "streaming.hpp"
#include <memory>
namespace quarkbot {
    namespace bitfinex {

        class Exchange : public IExchange, public std::enable_shared_from_this<Exchange> { 
        public:
            Exchange(network::PSSL_CTX sslctx, PExecutionWorker worker);            

            virtual PAccount create_account(const std::string &name, const std::string &credentials)  override;
            virtual std::vector<PMarketInstrument> get_market_instruments()  override;
            virtual PMarketInstrument create_instrument(std::string_view id, InstrumentType type)  override;
            virtual std::vector<UnderlyingCurrency> get_all_currencies()  override;
            virtual std::string_view get_name() const override;
     

            std::unique_ptr<IEventStreamBase> subscribe_market_stream(std::string symbol, StreamTypeItem::Type type, const StreamParams *params);


        protected:
            
            std::shared_ptr<StreamManager> _stream_manager;
            InstrumentMap _instr_map;
            PExecutionWorker _worker;

        };


    }

}