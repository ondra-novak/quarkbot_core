#include "check.h"
#include "quarkbot/json/json.hpp"

#include <cstdint>
#include <string>

// The message bus puts unix millisecond timestamps on the wire as JSON numbers,
// which is only safe if a parsed number keeps its exact digits.
static void test_large_integers_survive_a_round_trip() {
    CHECK_EQUAL(Json::from_string("1784700083313").as<std::int64_t>(), 1784700083313LL);
    CHECK_EQUAL(std::string(Json::from_string("1784700083313").as_text()), "1784700083313");
    // 16 digits, as a microsecond timestamp would be, and past 2^53
    CHECK_EQUAL(Json::from_string("1784700083313456").as<std::int64_t>(), 1784700083313456LL);
    CHECK_EQUAL(Json::from_string("9007199254740993").as<std::int64_t>(), 9007199254740993LL);
    // nested, so the fix holds through object and array parsing too
    CHECK_EQUAL(std::string(Json::from_string(R"({"a":[1784700083313,2]})")["a"][0].as_text()),
                "1784700083313");
    CHECK_EQUAL(Json::from_string("[1784700083313]")[0].as<std::int64_t>(), 1784700083313LL);
    CHECK_EQUAL(Json::from_string(" 1784700083313 ").as<std::int64_t>(), 1784700083313LL);
}

static void test_unsigned_above_int64_max() {
    CHECK_EQUAL(Json::from_string("18446744073709551615").as<std::uint64_t>(), UINT64_MAX);
    CHECK_EQUAL(Json::from_string("\"18446744073709551615\"").as<std::uint64_t>(), UINT64_MAX);
    CHECK_EQUAL(Json::from_string("9223372036854775808").as<std::uint64_t>(),
                9223372036854775808ULL);
}

// Everything that already worked must keep working.
static void test_no_regression_on_existing_behaviour() {
    CHECK_EQUAL(Json::from_string("-42").as<std::int64_t>(), -42);
    CHECK_EQUAL(Json::from_string("0").as<std::int64_t>(), 0);
    CHECK_EQUAL(Json::from_string("1.5").as<double>(), 1.5);
    CHECK_EQUAL(Json::from_string("1e3").as<std::int64_t>(), 1000);
    CHECK_EQUAL(Json::from_string("-1.25e2").as<double>(), -125.0);
    CHECK_EQUAL(Json(static_cast<std::int64_t>(1784700083313LL)).to_string(), "1784700083313");
    CHECK_EQUAL(Json(1.5).to_string(), "1.5");
    CHECK_EQUAL(Json::from_string("true").as_bool(), true);
    CHECK(Json::from_string("null").is_null());
    // a malformed number must still be a parse error, not a silent zero
    CHECK_EXCEPTION(Json::ParseError, Json::from_string("1.2.3"));
}

int main() {
    test_large_integers_survive_a_round_trip();
    test_unsigned_above_int64_max();
    test_no_regression_on_existing_behaviour();
    return 0;
}
