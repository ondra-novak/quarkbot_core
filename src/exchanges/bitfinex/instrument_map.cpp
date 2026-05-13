#include "instrument_map.hpp"
#include "exchange.hpp"
#include "exchanges/bitfinex/instrument.hpp"
#include "exchanges/bitfinex/network_context.hpp"
#include "libs/network/http_status_exception.hpp"
#include "libs/network/rest.hpp"
#include "libs/network/sslobjects.hpp"
#include "market_instrument.hpp"
#include "types.hpp"
#include "underlying.hpp"
#include "utils/decimal.hpp"
#include "utils/json.hpp"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace quarkbot {
namespace bitfinex {


InstrumentMap::UnifiedCurrenciesMap InstrumentMap::_unified_currencies = {{
        {"USD","USD"},
        {"EUR","EUR"},
        {"JPY","JPY"},
        {"GBP","GBP"},
        {"CNH","CNH"},
        {"MXN","MXN"},
        {"BTC","XTB"},
        {"XRP","XRP"},
        {"XML","XML"},
        {"STR","XML"},
        {"ETH","ETH"},
       {"ETC","ETC"},
       {"DASH","DASH"},
       {"DOGE","DOGE"},
       {"SOL","SOL"},
       {"LINK","LINK"},
       {"AAVE","AAVE"},
       {"AVAX","AVAX"},
       {"PEPE","PEPE"},
       {"ZEC","ZEC"},
       {"DOT","DOT"},
       {"USDt","USDt"},
       {"CNHT","CHNt"},
       {"XAUt","XAUt"},
       {"XLM","XLM"},
       {"LTC","LTC"},
       {"OPX","OP"},
       {"USDF","USDf"},
       {"USDC","USDc"}
    }};

InstrumentMap::InstrumentMap(NetworkContext ctx)
    :_ctx(std::move(ctx))    {}



UnderlyingCurrency InstrumentMap::create_currency(std::string_view name, std::shared_ptr<Exchange> exchange) {
    std::unique_lock lk(_mx);
    if (_currency_map.empty()) {
        CurrencyMap nw_cm;
        auto rest = _ctx.create_rest();
        load_currency_map(nw_cm, rest);        
        _currency_map = std::move(nw_cm);
    }    
    auto iter = _currency_map.find(std::string(name));
    if (iter != _currency_map.end()) {
        return UnderlyingCurrency(std::string(name), iter->second, exchange.get());
    } else {
        return UnderlyingCurrency(std::string(name), {}, exchange.get());
    }
}

std::pair<std::string_view, std::string_view> InstrumentMap::crack_instrument(std::string_view instrument_name) {
  if (instrument_name.empty()) return {};

    size_t colon_pos = instrument_name.find(':');
    if (colon_pos != std::string_view::npos) {
        return {instrument_name.substr(0, colon_pos), instrument_name.substr(colon_pos + 1)};
    }
    auto half = instrument_name.size()/2;
    return {instrument_name.substr(0,half), instrument_name.substr(half)};
}

void InstrumentMap::load_all_instruments(std::unique_lock<std::mutex> &lk, const IExchange *ex) {
    //this part can be unlocked, as we do not interact with internals
    auto client = _ctx.create_rest();
    Map new_instrument_map;
    CurrencyMap new_currency_map;

    load_currency_map(new_currency_map, client);
    load_spot_geometry(new_instrument_map, new_currency_map, client, ex);
    load_futures_geometry(new_instrument_map,new_currency_map, client, ex);
    initialize_steps(new_instrument_map,client);


    //now lock
    if (!lk.owns_lock()) lk.lock();
    _symb_to_type.clear();
    for (auto &[k, v] : new_instrument_map) {
        auto iter = _instruments.find(k);
        if (iter != _instruments.end()) {
            bool changed = iter->second.info != v.info;
            auto ref = iter->second.ref.lock();
            v.ref = ref;
            if (changed && ref) {
                ref->info_updated(InstrumentInfo::from(v.info));
            }
        }
        auto ins = _symb_to_type.emplace(k.id, k.type);
        if (!ins.second && k.type == InstrumentType::margin) {  //shortcut : margin has priority over spot and non-margin instruments are unique
            ins.first->second = k.type;
        }
    }    
    std::swap(_instruments, new_instrument_map);
    std::swap(_currency_map, new_currency_map);

}

void InstrumentMap::load_currency_map(CurrencyMap &new_map, network::SecureRestClient &client) {
    auto response = client.GET("/conf/pub:map:currency:sym");
    if (response.code != 200) throw network::HttpStatusException(response.code, response.message, "Failed to load currency map");
    Json value = Json::parse(response.read_body_as_charstream());
 /*
 [
  [
    [
      "ALG",
      "ALGO"
    ],
    [
      "ATO",
      "ATOM"
    ],
    ...
  ]
]
 */

    auto &arr1 = value.as_array();
    if (arr1.size() != 1) throw std::runtime_error("Bitfinex: unexpected response for currency map");
    auto &arr2 = arr1[0].as_array();
    for (const auto &item: arr2) {
        auto &itemarr = item.as_array();
        if (itemarr.size() != 2) continue;
        const auto &symb =  item[0];
        const auto &cur =  item[1];
        if (symb.is_string() && cur.is_string()) {
            new_map[symb.as<std::string>()] = cur.as<std::string>();
        }   
    }

}

std::vector<UnderlyingCurrency> InstrumentMap::get_all_currencies(std::shared_ptr<Exchange> exchange) {
    std::unique_lock lk(_mx);
    if (_instruments.empty()) load_all_instruments(lk, exchange.get());
    std::unordered_set<UnderlyingCurrency, Hasher<UnderlyingCurrency> > set;

    for (const auto &[k, v] : _instruments) {
        const auto &nfo = v.info;
        if (nfo.asset_wallet) {
            set.insert(*nfo.asset_wallet);
        }
        set.insert(nfo.quote_currency);
    }
    return {set.begin(), set.end()};
}

UnderlyingCurrency InstrumentMap::create_currency_from_id(const CurrencyMap &cmap, std::string_view id, const IExchange *ex) {
        std::string strid(id);
        auto iter = cmap.find(strid);        
        std::string unified;
        auto iter2 = _unified_currencies.find(strid);
        if (iter2 != _unified_currencies.end()) unified = iter2->second;        
        while (unified.empty() && iter != cmap.end()) {
             iter2 = _unified_currencies.find(iter->second);
             if (iter2 != _unified_currencies.end()) unified = iter2->second;        
              iter = cmap.find(iter->second);        
        }
        return {strid, unified, ex};

}

void InstrumentMap::load_spot_geometry(Map &new_map,const CurrencyMap &new_cur_map, network::SecureRestClient &client, const IExchange *ex) {
    auto response = client.GET("/conf/pub:info:pair");
    if (response.code != 200) throw network::HttpStatusException(response.code, response.message, "Failed to load spot geometry");
    Json value = Json::parse(response.read_body_as_charstream());
    Json list = value[0];
    for (Json item: list.as_array()) {
        Json symbol = item[0];
        Json params = item[1];
        Json min_lot = params[3];
        Json max_lot = params[4];
        Json margin = params[8];

        std::string name(symbol.as<std::string>());

        auto [asset,quote] = crack_instrument(name);
        auto asset_currency = create_currency_from_id(new_cur_map, asset, ex);
        auto quote_currency = create_currency_from_id(new_cur_map, quote, ex);

        auto &spot_info = new_map[{name,InstrumentType::spot}];
        IMarketInstrument::Info nfo{
            {InstrumentType::spot},
            Decimal::from_string(min_lot.as_text()),
            Decimal::from_string(max_lot.as_text()),
            1,
            1,
            {},
            {},
            {},
            {},
            quote_currency,
            quote_currency,
            asset_currency,
            name
        };
        spot_info.info = nfo;
        if (margin.is_number()) {
            auto &margin_info = new_map[{name, InstrumentType::margin}];
            nfo.leverage =Decimal(1.0/margin.as_double());
            nfo.type = InstrumentType::margin;
            nfo.asset_wallet = {};
            margin_info.info = nfo;
        }
    }
}
void InstrumentMap::load_futures_geometry(Map &new_map, const CurrencyMap &new_cur_map, network::SecureRestClient &client, const IExchange *ex) {
    auto response = client.GET("/conf/pub:info:pair:futures");
    if (response.code != 200) throw network::HttpStatusException(response.code, response.message, "Failed to load futures geometry");
    Json value = Json::parse(response.read_body_as_charstream());
    Json list = value[0];
    for (Json item: list.as_array()) {
        Json symbol = item[0];
        Json params = item[1];
        Json min_lot = params[3];
        Json max_lot = params[4];
        Json margin = params[8];

        std::string name(symbol.as<std::string>());
        auto [a,q] = crack_instrument(name);
        auto quote_currency = create_currency_from_id(new_cur_map, q, ex);

        auto &futures_info = new_map[{name,InstrumentType::contract}];
        futures_info.info = {
            {InstrumentType::contract},
            Decimal::from_string(min_lot.as_text()),
            Decimal::from_string(max_lot.as_text()),
            1,
            1,
            {},
            Decimal(1.0/margin.as_double()),
            {},
            {},
            quote_currency,
            quote_currency,
            {},
            name
        };         
    }

}

Decimal InstrumentMap::calculate_tick_size(Decimal price) {
    Decimal ref_price = price * 1.05_dec;
    Decimal one(0.1_dec);
    return scaleb10(one, ref_price.exponent()-4);    
}

void InstrumentMap::initialize_steps(Map &new_map, network::SecureRestClient &client) {
     auto response = client.GET("/tickers?symbols=ALL");
    if (response.code != 200) throw network::HttpStatusException(response.code, response.message, "Failed to load initial tickers");
    Json value = Json::parse(response.read_body_as_charstream());
    for (Json item: value.as_array()) {
        Json s = item[0];
        Json b = item[3];
        std::string n = s.as<std::string>();
        if (!n.starts_with('t')) continue;
        n = n.substr(1);
        Decimal tick_size = calculate_tick_size(Decimal::from_string(b.as_text()));
        for (auto type: std::initializer_list<InstrumentType>{InstrumentType::spot, InstrumentType::margin, InstrumentType::contract}) {
            auto iter = new_map.find({n, type});
            if (iter != new_map.end()) {
                iter->second.info.price_increment = tick_size;                
            }
        }
    }
}

PMarketInstrument InstrumentMap::create_instrument(std::string_view id, InstrumentType type, std::shared_ptr<Exchange> exchange) {
    std::unique_lock lk(_mx);
    if (_instruments.empty()) load_all_instruments(lk, exchange.get());
    std::string name(id);
    auto iter = _instruments.find({std::string(id), type});
    if (iter == _instruments.end()) {
        throw std::runtime_error("Bitfinex: unknown instrument: "+ name);
    }
    auto instr = iter->second.ref.lock();
    if (!instr) {
        iter->second.ref = instr = std::make_shared<BFXInstrument>(iter->second.info, exchange);         
    }
    return instr;
}

std::vector<PMarketInstrument> InstrumentMap::get_all_instruments(std::shared_ptr<Exchange> exchange) {
    std::unique_lock lk(_mx);
     if (_instruments.empty()) load_all_instruments(lk, exchange.get());
   std::vector<PMarketInstrument> result;
    for (auto &[k, v]: _instruments) {
        auto p = v.ref.lock();
        if (!p) {
            v.ref = p =  std::make_shared<BFXInstrument>(v.info, exchange);
        }        
        result.push_back(std::move(p));
    }
    return result;
}

void InstrumentMap::check_tick_size(const std::string &id, InstrumentType type, Decimal tick_size) {
    auto iter = _instruments.find({id, type});
    if (iter == _instruments.end()) return;
    if (iter->second.info.price_increment != tick_size) {
        iter->second.info.price_increment = tick_size;
        auto ref = iter->second.ref.lock();
        if (ref) {
            ref->info_updated(InstrumentInfo::from(iter->second.info));            
        }
    }
}

void InstrumentMap::report_price(const std::string &id, Decimal price) {
    Decimal tksz = calculate_tick_size(price);
    std::scoped_lock _(_mx);
    auto i1 = _symb_to_type.find(id);
    if (i1 == _symb_to_type.end()) return;
    check_tick_size(id, i1->second, tksz);
    if (i1->second == InstrumentType::margin) {
        check_tick_size(id, InstrumentType::spot, tksz);
    }
}

}


}