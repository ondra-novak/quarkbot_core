#pragma once

#include "exchanges/bitfinex/network_context.hpp"
#include "instrument.hpp"
#include "libs/network/rest.hpp"
#include "ifc/market_instrument.hpp"
#include "ifc/types.hpp"
#include "ifc/underlying.hpp"
#include "libs/network/sslobjects.hpp"
#include "utils/hashable.hpp"
#include <string>
#include <unordered_map>
namespace quarkbot {
    namespace bitfinex {

        class Exchange;

        class InstrumentMap {
        public:

            InstrumentMap(NetworkContext ctx);
            
            PMarketInstrument create_instrument(std::string_view id, InstrumentType type, std::shared_ptr<Exchange> exchange);
            UnderlyingCurrency create_currency(std::string_view name, std::shared_ptr<Exchange> exchange);
            std::vector<PMarketInstrument> get_all_instruments(std::shared_ptr<Exchange> exchange);
            std::vector<UnderlyingCurrency> get_all_currencies(std::shared_ptr<Exchange> exchange);

            static std::pair<std::string_view, std::string_view> crack_instrument(std::string_view instrument_name);

            using UnifiedCurrenciesMap = std::unordered_map<std::string, std::string>;
            void report_price(const std::string &id, Decimal price);

        protected:
            struct InstrumentListKey {
                std::string id;
                InstrumentType type;
                bool operator==(const InstrumentListKey &other) const = default;
                std::size_t get_hash() const {
                    std::size_t h1 = std::hash<std::string>{}(id);
                    std::size_t h2 = std::hash<int>{}(static_cast<int>(type));
                    return h1 ^ (h2 << 1);
                }   
            };

            struct InstrumentListValue {            
                std::weak_ptr<BFXInstrument> ref = {};
                IMarketInstrument::Info info = {};
            };

            using Map = std::unordered_map<InstrumentListKey, InstrumentListValue, Hasher<InstrumentListKey> >;
            using CurrencyMap = std::unordered_map<std::string, std::string>;
            using SymbolToInstrumentType = std::unordered_map<std::string, InstrumentType>;

            std::mutex _mx;
            NetworkContext _ctx;

            Map _instruments;
            CurrencyMap _currency_map; //map from exchange currency code to unified code
            SymbolToInstrumentType _symb_to_type;



            static UnifiedCurrenciesMap _unified_currencies;

            IMarketInstrument::Info fetch_instrument_info(std::string_view id, InstrumentType type);

            ///@note - lk can be unlocked whenn call, but stays locked on exit 
            void load_all_instruments(std::unique_lock<std::mutex> &lk, const IExchange *ex);
            void load_currency_map(CurrencyMap &new_map, network::SecureRestClient &client);
            void load_spot_geometry(Map &new_map, const CurrencyMap &new_cur_map, network::SecureRestClient &client,const IExchange *ex);
            void load_futures_geometry(Map &new_map, const CurrencyMap &new_cur_map, network::SecureRestClient &client,const IExchange *ex);
            void initialize_steps(Map &new_map, network::SecureRestClient &client);

            static Decimal calculate_tick_size(Decimal price);

            static UnderlyingCurrency create_currency_from_id(const CurrencyMap &cmap, std::string_view id, const IExchange *ex);
 
             void check_tick_size(const std::string &id, InstrumentType type, Decimal tick_size);

        };


    }
}