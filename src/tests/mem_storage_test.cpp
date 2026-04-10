#include "impl/mem_storage.hpp"
#include "tests/check.h"
#include <memory>

using namespace quarkbot;

void test_plain_put_get() {
    MemStorage storage;

    // key not present → exists=false, rev=0
    auto v = storage.get({"foo", false});
    CHECK(!v.exists);
    CHECK_EQUAL(v.rev, 0u);

    // write via transaction
    auto tx = storage.write();
    auto r = tx->put({"foo", false}, "hello");
    CHECK_EQUAL(r, 0u);  // non-sequence always returns 0
    tx->commit();

    // now exists
    auto v2 = storage.get({"foo", false});
    CHECK(v2.exists);
    CHECK_EQUAL(v2.rev, 0u);
    CHECK_EQUAL(v2.data, "hello");

    // overwrite
    auto tx2 = storage.write();
    tx2->put({"foo", false}, "world");
    tx2->commit();

    auto v3 = storage.get({"foo", false});
    CHECK(v3.exists);
    CHECK_EQUAL(v3.data, "world");

    // different key still absent
    auto v4 = storage.get({"bar", false});
    CHECK(!v4.exists);

    // non-committed transaction has no effect
    auto tx3 = storage.write();
    tx3->put({"foo", false}, "dropped");
    // no commit — let tx3 go out of scope
    auto v5 = storage.get({"foo", false});
    CHECK_EQUAL(v5.data, "world");
}

int main() {
    test_plain_put_get();
    return 0;
}
