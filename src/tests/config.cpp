#include "quarkbot/config.hpp"
#include <algorithm>
#include <stdexcept>
#include <string_view>
#include "check.h"


constexpr std::pair<std::string_view, std::string_view> data[] = {
    {"test_key1", "test_value"},
    {"sect/key1","23.213"},
    {"sect/key2","321"},
    {"bool_key1","1"},
    {"bool_key2","true"},
    {"bool_key3","false"},
    {"bool_key4","yes"},
    {"bool_key5","no"},
    {"enum_key1","sell"},
    {"enum_key2","buy"},  
   {"enum_key3","invalid"},  
};


constexpr auto get_config()  {
    return quarkbot::ConfigT([](std::string_view key)->std::optional<std::string_view> {

        auto iter = std::find_if(std::begin(data), std::end(data), [&](const auto &v){
            return v.first == key;
        });
        if (iter == std::end(data)) return {};
        return iter->second;
    },'/');
}




void test1() {
    auto cfg = get_config();
    std::string_view t1 = cfg["test_key1"];
    CHECK_EQUAL(t1, "test_value");
    auto sect = cfg/"sect";

    Decimal t2 = sect["key1"];
    CHECK_EQUAL(t2, 23.213_dec);

    int t3 = sect["key2"];
    CHECK_EQUAL(t3, 321);

    bool tb;
    tb = cfg["bool_key1"];
    CHECK_EQUAL(tb, true);
    tb = cfg["bool_key2"];
    CHECK_EQUAL(tb, true);
    tb = cfg["bool_key3"];
    CHECK_EQUAL(tb, false);
    tb = cfg["bool_key4"];
    CHECK_EQUAL(tb, true);
    tb = cfg["bool_key5"];
    CHECK_EQUAL(tb, false);

    quarkbot::Side sd;
    sd = cfg["enum_key1"];
    CHECK(sd == quarkbot::Side::sell);
    sd = cfg["enum_key2"];
    CHECK(sd == quarkbot::Side::buy);
    CHECK_EXCEPTION(std::runtime_error, sd = cfg["enum_key3"]);
    CHECK_EXCEPTION(std::runtime_error, t3 = cfg["test_key1"]);

    Decimal t4 = cfg["not_exists"](12.34_dec);
    CHECK_EQUAL(t4, 12.34_dec);
    Decimal t5 = sect["key2"](12.34_dec);
    CHECK_EQUAL(t5, 321_dec);
    Decimal t6 = cfg["not_exists"](0);
    CHECK_EQUAL(t6, 0);

}

int main() {
    test1();
    return 0;
}