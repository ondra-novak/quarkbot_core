#include "check.h"
#include "quarkbot/common/somodule.hpp"
#include "plugins/plugin_common.hpp"

using namespace quarkbot;

int main() {

    auto list = SoModuleStrategyList::load_module(TEST_PLUGIN_PATH);

    CHECK(list.is_version_compatible());

    auto strategies = list.get_strategies();
    CHECK_EQUAL(strategies.size(), 2u);

    CHECK_EQUAL(strategies[0].name, "empty");
    CHECK(strategies[0].context_type_matches<StrategyContext>());
    CHECK(!strategies[0].context_type_matches<quarkbot_test::FooContext>());

    CHECK_EQUAL(strategies[1].name, "foo");
    CHECK(strategies[1].context_type_matches<quarkbot_test::FooContext>());
    CHECK(!strategies[1].context_type_matches<StrategyContext>());

    CHECK_EXCEPTION(std::runtime_error, SoModuleStrategyList::load_module("/no/such/plugin.so"));

    return 0;
}
