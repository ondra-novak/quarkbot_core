#include "quarkbot/somodule.hpp"
#include "plugin_common.hpp"

namespace {

struct EmptyStrategy {
    explicit EmptyStrategy(quarkbot::StrategyContext &&) {}
    quarkbot::StrategyFragment main() { co_return; }
};

struct FooStrategy {
    explicit FooStrategy(quarkbot_test::FooContext &&) {}
    quarkbot::StrategyFragment main() { co_return; }
};

quarkbot::ExportedStrategy<EmptyStrategy> reg_empty("empty");
quarkbot::ExportedStrategy<FooStrategy, quarkbot_test::FooContext> reg_foo("foo");

}
