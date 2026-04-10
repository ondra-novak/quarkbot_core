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

void test_plain_erase() {
    MemStorage storage;
    auto tx = storage.write();
    tx->put({"key", false}, "value");
    tx->commit();

    CHECK(storage.get({"key", false}).exists);

    auto tx2 = storage.write();
    auto r = tx2->erase({"key", false});
    CHECK_EQUAL(r, 0u);
    tx2->commit();

    // after erase, key is gone — indistinguishable from never having existed
    CHECK(!storage.get({"key", false}).exists);
    CHECK_EQUAL(storage.get({"key", false}).rev, 0u);
}

void test_plain_get_all_keys() {
    MemStorage storage;
    auto tx = storage.write();
    tx->put({"order:1", false}, "a");
    tx->put({"order:2", false}, "b");
    tx->put({"fill:1", false}, "c");
    tx->commit();

    // prefix filter
    auto keys = storage.get_all_keys({"order:", false});
    CHECK_EQUAL(keys.size(), 2u);
    // both order keys present (order not guaranteed)
    bool has1 = false, has2 = false;
    for (auto &k : keys) {
        if (k == "order:1") has1 = true;
        if (k == "order:2") has2 = true;
    }
    CHECK(has1);
    CHECK(has2);

    // empty prefix = all plain keys
    auto all = storage.get_all_keys({"", false});
    CHECK_EQUAL(all.size(), 3u);

    // erased key not returned
    auto tx2 = storage.write();
    tx2->erase({"order:1", false});
    tx2->commit();

    auto keys2 = storage.get_all_keys({"order:", false});
    CHECK_EQUAL(keys2.size(), 1u);
    CHECK_EQUAL(keys2[0], "order:2");
}

void test_seq_put_get_latest() {
    MemStorage storage;

    // key absent
    auto v0 = storage.get({"events", true});
    CHECK(!v0.exists);
    CHECK_EQUAL(v0.rev, 0u);

    // first put → revision 1
    auto tx = storage.write();
    auto r1 = tx->put({"events", true}, "data1");
    CHECK_EQUAL(r1, 1u);
    tx->commit();

    auto v1 = storage.get({"events", true});
    CHECK(v1.exists);
    CHECK_EQUAL(v1.rev, 1u);
    CHECK_EQUAL(v1.data, "data1");

    // second put → revision 2
    auto tx2 = storage.write();
    auto r2 = tx2->put({"events", true}, "data2");
    CHECK_EQUAL(r2, 2u);
    tx2->commit();

    auto v2 = storage.get({"events", true});
    CHECK(v2.exists);
    CHECK_EQUAL(v2.rev, 2u);
    CHECK_EQUAL(v2.data, "data2");

    // third put → revision 3
    auto tx3 = storage.write();
    tx3->put({"events", true}, "data3");
    tx3->commit();

    auto v3 = storage.get({"events", true});
    CHECK(v3.exists);
    CHECK_EQUAL(v3.rev, 3u);
    CHECK_EQUAL(v3.data, "data3");

    // uncommitted transaction must not affect visible state
    auto tx4 = storage.write();
    tx4->put({"events", true}, "dropped");
    // no commit — tx4 goes out of scope
    auto v4 = storage.get({"events", true});
    CHECK_EQUAL(v4.rev, 3u);
    CHECK_EQUAL(v4.data, "data3");
}

int main() {
    test_plain_put_get();
    test_plain_erase();
    test_plain_get_all_keys();
    test_seq_put_get_latest();
    return 0;
}
