#include "check.h"

#include "exchanges/bitfinex/instrument_map.hpp"
#include "ifc/types.hpp"
#include "ifc/underlying.hpp"
#include "libs/network/rest.hpp"
#include "libs/network/sslobjects.hpp"

using namespace quarkbot;
using namespace quarkbot::bitfinex;

network::PSSL_CTX ctx;

InstrumentMap create_map_object() {
    auto client = network::SecureRestClient{ctx,"https://api-pub.bitfinex.com/v2"};
    client.add_header("User-Agent", "quarkbot/1.0 tests");
    return InstrumentMap(std::move(client));
    
    
}

void test_load_currencies() {
    auto map = create_map_object();
    auto list = map.get_all_currencies(nullptr);
    CHECK(!list.empty());
    //list can vary - check for BTC, USDT, and futures USDT (with correct transformation)
    auto iter = std::find(list.begin(), list.end(), UnderlyingCurrency{"BTC","XTB", nullptr});
    CHECK(iter != list.end());
    iter = std::find(list.begin(), list.end(), UnderlyingCurrency{"UST","USDt", nullptr});
    CHECK(iter != list.end());
    iter = std::find(list.begin(), list.end(), UnderlyingCurrency{"USTF0","USDt", nullptr});
    CHECK(iter != list.end());
    iter = std::find(list.begin(), list.end(), UnderlyingCurrency{"USD","USD", nullptr});
    CHECK(iter != list.end());
    iter = std::find(list.begin(), list.end(), UnderlyingCurrency{"WBT","", nullptr});
    CHECK(iter != list.end());
}

void test_market_instrument() {
    auto map = create_map_object();
    auto i1 = map.create_instrument("BTCUSD", InstrumentType::spot, nullptr);    

    auto nfo = i1->get_info();
    CHECK_EQUAL(nfo.name, "BTCUSD");
    std::cout << nfo.min_lot_size.to_string() << std::endl;


}

int main() {
    ctx = network::ssl_init_client();
    test_market_instrument();
    test_load_currencies();
}