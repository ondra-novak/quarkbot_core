#pragma once

#include "instrument.hpp"
#include "libs/network/rest.hpp"
#include "ifc/market_instrument.hpp"
#include "ifc/types.hpp"
#include "ifc/underlying.hpp"
#include "utils/hashable.hpp"
#include <string>
#include <unordered_map>
namespace quarkbot {
    namespace bitfinex {

        class Exchange;

        class InstrumentMap {
        public:

            InstrumentMap(network::SecureRestClient client);
            
            PMarketInstrument create_instrument(std::string_view id, InstrumentType type, std::shared_ptr<Exchange> exchange);
            UnderlyingCurrency create_currency(std::string_view name, std::shared_ptr<Exchange> exchange);
            std::vector<PMarketInstrument> get_all_instruments(std::shared_ptr<Exchange> exchange);
            std::vector<UnderlyingCurrency> get_all_currencies(std::shared_ptr<Exchange> exchange);

            static std::pair<std::string_view, std::string_view> crack_instrument(std::string_view instrument_name);

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
                std::weak_ptr<BFXInstrument> ref = {}; //TODO: replace with actual instrument type later
                IMarketInstrument::Info infos[4] = {};
                std::atomic<std::uint8_t> cur_info_index = {};
                const auto &get_info() const {return infos[cur_info_index.load() & 3];}
                void set_info(const IMarketInstrument::Info &info) {
                    auto new_idx = cur_info_index.load()+1;
                    infos[new_idx & 3] = info;
                    ++cur_info_index;
                }                
            };

            using Map = std::unordered_map<InstrumentListKey, InstrumentListValue, Hasher<InstrumentListKey> >;

            std::mutex _mx;
            Map _instruments;
            std::unordered_map<std::string, std::string> _currency_map; //map from exchange currency code to unified code
            network::SecureRestClient rest_client;


            std::unordered_map<std::string, std::string> _unified_currencies;

            IMarketInstrument::Info fetch_instrument_info(std::string_view id, InstrumentType type);

            void load_currency_map();
            void load_all_instruments(const IExchange *ex);
            void load_spot_geometry(const IExchange *ex);
            void load_futures_geometry(const IExchange *ex);
            void initialize_steps();

            static Decimal calculate_tick_size(Decimal price);

            UnderlyingCurrency create_currency_from_id(std::string_view id, const IExchange *ex);
        };


    }
}